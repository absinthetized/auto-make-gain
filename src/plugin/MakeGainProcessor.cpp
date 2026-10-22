#include "plugin/MakeGainProcessor.h"
#include "ui/MakeGainEditor.h"

// (Framework-orientation comments live in the header file — lifecycle table,
// entry-point chain; this cpp only repeats them where a call site needs them.)

MakeGainProcessor::MakeGainProcessor()
    // BusesProperties declares our I/O channel "buses". A bus is a group of
    // channels (mono, stereo, 5.1...). We want exactly one stereo input +
    // one stereo output, and NO sidechain bus anywhere YET: the A2 aux
    // input bus is declared in PB2f together with the delta-engine wiring.
    : AudioProcessor (BusesProperties()
           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    // In framework terms: APVTS parameters are looked up once here, by ID,
    // and kept as raw atomic<float> pointers — the audio thread reads them
    // every block with zero locking (JUCE guarantees these atomics outlive
    // any parameter change). Editor-side code never touches these raw
    // pointers; it attaches to the parameter objects instead (PR-B3).
    activeParam = apvts.getRawParameterValue ("active");
}

// Parameter layout — A2: Active only for now. The UX-Q4 rule stands: the
// engage/bypass toggle is the one AUTOMATABLE parameter; Metric/MaxComp/
// ramp-speed are UI-only parameters that arrive in PR-B3. (The old "read"
// parameter retired with the training FSM — Amendment A2.)
juce::AudioProcessorValueTreeState::ParameterLayout
MakeGainProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // In framework terms: ParameterID's second value is the "version hint" —
    // how hosts that remember old parameter lists identify this param across
    // plugin versions. Fresh plugin, all hints = 1.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "active", 1 }, "Active", false));

    return layout;
}

void MakeGainProcessor::prepareToPlay (double, int) {}
void MakeGainProcessor::releaseResources() {}

void MakeGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // ScopedNoDenormals: near-zero samples can decay into "denormals"
    // (subnormal floats), which some CPUs handle at a massive slowdown — a
    // classic source of mystery CPU spikes in plugins. Cheap and always on.
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    // Passthrough semantics (PB2e-red build): when the host's bus layout
    // matches what we declared in the constructor, JUCE hands us ONE buffer
    // whose channels carry the input data — leaving them untouched IS
    // passing the audio through. We must only silence channels we receive
    // no data for, because output channels are our responsibility.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

bool MakeGainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The host may propose: stereo→stereo, mono→mono, or weird asymmetric
    // pairs (mono-in→stereo-out etc). We accept only symmetric, ≤stereo
    // pairs; anything else must show as "not supported" so the host grays
    // the bad routings out instead of misbehaving at runtime.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo()
                      || in == juce::AudioChannelSet::mono());
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
