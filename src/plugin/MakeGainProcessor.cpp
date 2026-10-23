#include "plugin/MakeGainProcessor.h"

#include "ui/MakeGainEditor.h"

#include <cmath>

// (Framework-orientation comments live in the header file — lifecycle table,
// entry-point chain; this cpp only repeats them where a call site needs them.)

MakeGainProcessor::MakeGainProcessor()
    // BusesProperties declares our I/O channel "buses". A bus is a group of
    // channels (mono, stereo, 5.1...). The A2 architecture needs TWO input
    // buses: bus 0 = wet (the chain output at this insert point), bus 1 =
    // the dry pre-chain tap. In Reaper terms, bus 1 lands on the track's
    // sidechain channels (3/4 for stereo tracks, pin 2 for mono tracks).
    // The aux bus STARTS disabled (last arg false): a host that has no side
    // chain connected yet simply gives us silence instead of failing to load.
    : AudioProcessor (BusesProperties()
           .withInput  ("Input",         juce::AudioChannelSet::stereo(), true)
           .withInput  ("Aux (dry tap)", juce::AudioChannelSet::stereo(), false)
           .withOutput ("Output",        juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    // In framework terms: APVTS parameters are looked up once here, by ID,
    // and kept as raw atomic<float> pointers — the audio thread reads them
    // every block with zero locking (JUCE guarantees these atomics outlive
    // any parameter change). Editor-side code never touches these raw
    // pointers; it attaches to the parameter objects instead (PR-B3).
    activeParam = apvts.getRawParameterValue ("active");
    metricParam  = apvts.getRawParameterValue ("metric");
    maxcompParam = apvts.getRawParameterValue ("maxcomp");
    ramptcParam  = apvts.getRawParameterValue ("ramptc");
}

// Parameter layout (PR-B3, stage 2). UX-Q4 stands: Active is the ONLY
// automatable parameter; Metric / MaxComp / Ramp are UI-only toggles
// (setIsAutomatable(false)) — they still persist with the session via
// the APVTS (FR-22, A2-narrowed: parameters only). Defaults: LUFS,
// MaxComp 16 dB (A2 default, range 0–48), ramp 300 ms (FR-5).
juce::AudioProcessorValueTreeState::ParameterLayout
MakeGainProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // In framework terms: ParameterID's second value is the "version hint" —
    // how hosts that remember old parameter lists identify this param across
    // plugin versions. Fresh plugin, all hints = 1.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "active", 1 }, "Active", false));

    auto metric = std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "metric", 1 }, "Metric",
        juce::StringArray { "LUFS (K-weighted)", "dB RMS (unweighted)" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false));
    layout.add (std::move (metric));

    auto maxcomp = std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "maxcomp", 1 }, "MaxComp",
        juce::NormalisableRange<float> (0.0f, 48.0f, 0.1f), 16.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false));
    layout.add (std::move (maxcomp));

    auto ramptc = std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ramptc", 1 }, "Ramp tc",
        juce::NormalisableRange<float> (0.05f, 3.0f, 0.01f, 0.7f),
        (float) makegain::dsp::GainComputer::kDefaultRampTcSec,
        juce::AudioParameterFloatAttributes().withAutomatable (false));
    layout.add (std::move (ramptc));

    return layout;
}

void MakeGainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Engagement + the stage-2 parameters cross into the engine here too:
    // a fresh engine must reflect the (possibly persisted) parameter state
    // even before the first processBlock arrives.
    if (machine == nullptr || machineRate != sampleRate)
    {
        machineRate = sampleRate;
        machine = std::make_unique<makegain::dsp::GainComputer> (sampleRate);
    }
    crossParameters();

    // Scratch buffers: 2 mono-sum lanes + 1 gain lane, block-length wide.
    // Allocation happens HERE (non-audio time), never in processBlock.
    workBuf.setSize (3, juce::jmax (1, samplesPerBlock), false, true, true);
    workBuf.clear();
}

void MakeGainProcessor::releaseResources()
{
    // Keep the engine: a host may merely pause playback. The scratch buffer
    // is resized by the next prepareToPlay anyway.
}

void MakeGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // ScopedNoDenormals: near-zero samples can decay into "denormals"
    // (subnormal floats), which some CPUs handle at a massive slowdown — a
    // classic source of mystery CPU spikes in plugins. Cheap and always on.
    juce::ScopedNoDenormals noDenormals;

    // Bus shapes. Per the A2 bus rule the aux mirrors the main's count;
    // isBusesLayoutSupported guarantees it — but a host may present the aux
    // bus DISABLED (no sidechain connect yet): then auxIn == 0 and we feed
    // the engine digital silence on that tap. The hold gate does the rest:
    // no compensation until a real dry tap exists (dry_tap_missing after
    // 5 s), and NO slam either way (this is the structural ear-safety of
    // the A2 model — the old reference regime could boost from silence).
    const int mainIn = getChannelCountOfBus (true, 0);
    const int auxIn  = getChannelCountOfBus (true, 1);
    const int outCh  = getChannelCountOfBus (false, 0);
    const int n = buffer.getNumSamples();
    if (n <= 0 || machine == nullptr)
        return;

    auto* wetSum = workBuf.getWritePointer (0);
    auto* drySum = workBuf.getWritePointer (1);
    auto* gainDb = workBuf.getWritePointer (2);

    // Bus channel → flat buffer index mapping. In framework terms: the
    // processBlock buffer interleaves ALL input buses back to back; JUCE
    // exposes the arithmetic so we never hard-code "stereo = 2 channels".
    const int main0 = getChannelIndexInProcessBlockBuffer (true, 0, 0);
    const int aux0  = (auxIn > 0) ? getChannelIndexInProcessBlockBuffer (true, 1, 0) : -1;

    // Mono sums per sample (both buses fold the same way; a mono bus is
    // its own sum). The engine deliberately sees ONLY the mono sums —
    // channel count never reaches the DSP (UX-Q5).
    for (int s = 0; s < n; ++s)
    {
        double w = 0.0, d = 0.0;
        for (int c = 0; c < mainIn; ++c)
            w += static_cast<double> (buffer.getSample (main0 + c, s));
        wetSum[s] = static_cast<float> (w / static_cast<double> (mainIn));
        if (aux0 >= 0)
        {
            for (int c = 0; c < auxIn; ++c)
                d += static_cast<double> (buffer.getSample (aux0 + c, s));
            drySum[s] = static_cast<float> (d / static_cast<double> (auxIn));
        }
        else
            drySum[s] = 0.0f;  // aux bus present but disabled = silence
    }

    // Parameter crossing (every block, audio thread, atomics only): the
    // setters are allocation-free pointer stores; ramping toward the new
    // target is the engine's own per-sample job (no click — FR §10.3).
    crossParameters();

    // The engine folds BOTH taps' metering + ramp into one call and emits
    // the applied gain in dB after each sample's ramp step.
    machine->process (wetSum, drySum, static_cast<std::size_t> (n), gainDb);

    // Apply: every output channel gets the SAME per-sample gain (the A2
    // gain is stereo-correlated by design — it nulls a chain-induced
    // delta, not channel imbalances). dB → linear once per sample, then
    // multiplied into all channels: the expensive pow() runs n times, not
    // n × channels. Inputs and outputs share buffer indices (in-place);
    // the aux channels live beyond the output range, read-only here.
    for (int s = 0; s < n; ++s)
    {
        const float lin = static_cast<float> (
            std::pow (10.0, static_cast<double> (gainDb[s]) / 20.0));
        for (int c = 0; c < outCh; ++c)
            buffer.setSample (c, s, buffer.getSample (c, s) * lin);
    }

    // Status handoff for the editor (reads of doubles/bools into atomics).
    const auto flags = machine->flags();
    uiClamped.store (flags.clamped);
    uiNoSignalWet.store (flags.no_signal_wet);
    uiDryTapMissing.store (flags.dry_tap_missing);
}

bool MakeGainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The host may propose layouts for each named bus. A2 bus rule:
    //   out   == main input (mono→mono, stereo→stereo)
    //   aux   == main input channel count (mono main ⇒ mono aux)
    // Reject anything else so the host grays bad routings out instead of
    // misbehaving at runtime.
    const auto& main = layouts.getMainInputChannelSet();
    const auto& out  = layouts.getMainOutputChannelSet();
    if (main != out
        || (main != juce::AudioChannelSet::mono()
            && main != juce::AudioChannelSet::stereo()))
        return false;

    const auto aux = layouts.getChannelSet (true, 1);
    // The host may also simply NOT give us an aux (disabled bus = invalid
    // channel set / empty): that is the "dry tap missing" regime, which
    // must load — the hold gate makes it safe (gain stays put).
    if (!aux.isDisabled() && aux != main)
        return false;

    return true;
}

juce::AudioProcessorEditor* MakeGainProcessor::createEditor()
{
    return new MakeGainEditor (*this);
}

// ---------------------------------------------------------------------------
// THE ENTRY POINT (there is no main() in a plugin!)
//
// JUCE's build system (juce_add_plugin in CMakeLists.txt) generates
// format-specific wrapper code: the VST3/CLAP/AU module that actually talks
// to the host. That wrapper knows nothing about our class names — it links
// against this C-style factory and asks it for an AudioProcessor instance.
// After that, the wrapper drives our lifecycle methods (see the contract
// table in MakeGainProcessor.h). If you delete or rename this function,
// the plugin builds fine but the host loads a library that exposes no
// processor — "plugin not found" in Reaper with no compiler error.
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MakeGainProcessor();
}

// ---------------------------------------------------------------------------
// FR-22 session state (A2-narrowed: parameters only — the reference codec
// retired with the FSM).
// ---------------------------------------------------------------------------

void MakeGainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // copyState: thread-safe snapshot of the parameter tree (host may be
    // automating while we serialize).
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MakeGainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // All-or-nothing: a broken/unrecognized blob must leave the CURRENT
    // state untouched, so validate BEFORE replacing.
    auto restored = juce::ValueTree::fromXml (*xml);
    if (restored.isValid())
        apvts.replaceState (restored);
}

// ---------------------------------------------------------------------------
// Editor status (PB2f): one honest line about the current engine state.
// The real readout panel (FR §10.6: chain delta, applied gain, flags as
// labeled states) arrives with the re-derived UI in PR-B3.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Stage-2 parameter crossing (PR-B3): the APVTS atomics are the single
// thread-safe channel between the message thread (APVTS sliders, host
// persistence) and the audio thread (engine setters). Called on prepare
// and every processBlock — no caching, the stores are pointer writes.
// ---------------------------------------------------------------------------
void MakeGainProcessor::crossParameters()
{
    machine->set_engaged (activeParam->load() > 0.5f);
    machine->set_metric (metricParam->load() > 0.5f
                             ? makegain::dsp::GainMetric::db_rms
                             : makegain::dsp::GainMetric::lufs);
    machine->set_max_comp_db ((double) maxcompParam->load());
    machine->set_ramp_tc ((double) ramptcParam->load());
}

const juce::String MakeGainProcessor::statusLine() const
{
    if (machine == nullptr || activeParam->load() <= 0.5f)
        return "standby \u2014 engaged when Active is on";

    const auto wetFlag  = uiNoSignalWet.load();
    const auto dryFlag  = uiDryTapMissing.load();
    const auto clamped  = uiClamped.load();

    if (dryFlag)
        return "AGC engaged \u00b7 dry tap missing (check aux routing)";
    if (wetFlag)
        return "AGC engaged \u00b7 no wet signal";
    return juce::String ("AGC engaged") + (clamped ? " \u00b7 clamped at MaxComp"
                                                   : " \u00b7 tracking");
}
