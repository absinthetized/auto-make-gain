#include "plugin/MakeGainProcessor.h"
#include "ui/MakeGainEditor.h"

// (Framework-orientation comments live in the header file — lifecycle table,
// entry-point chain; this cpp only repeats them where a call site needs them.)

MakeGainProcessor::MakeGainProcessor()
    // BusesProperties declares our I/O channel "buses". A bus is a group of
    // channels (mono, stereo, 5.1...). We want exactly one stereo input +
    // one stereo output, and NO sidechain bus anywhere: the MakeGain design
    // learns its reference in place (FR §1/§2.1: one instance, one insert
    // point) — which is also why GarageBand support needs no degraded mode.
    : AudioProcessor (BusesProperties()
           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // In framework terms: APVTS parameters are looked up once here, by ID,
    // and kept as raw atomic<float> pointers — the audio thread reads them
    // every block with zero locking (JUCE guarantees these atomics outlive
    // any parameter change). Editor-side code never touches these raw
    // pointers; it attaches to the parameter objects instead (PB3).
    activeParam = apvts.getRawParameterValue ("active");
    readParam   = apvts.getRawParameterValue ("read");
}

// Parameter layout — stage 1 of the staged rollout (owner, 2026-10-22):
// Active (the only automatable param, UX-Q4) plus Read. Read is exposed as
// a normal parameter so Reaper's generic panel can click it, but is marked
// non-automatable: it is a momentary ACTION (a press), not a state to
// automate (UX-Q4). Metric/MaxComp/Ramp land in stage 2 (PB2c-2), the
// graphical Read Anyway control in PB3.
juce::AudioProcessorValueTreeState::ParameterLayout
MakeGainProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // In framework terms: ParameterID's second value is the "version hint" —
    // how hosts that remember old parameter lists identify this param across
    // plugin versions. Fresh plugin, all hints = 1.
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "active", 1 }, "Active", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "read", 1 }, "Read", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    return layout;
}

void MakeGainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // FR-7 across rate changes: the machine is sample-rate-bound (its 3.0 s
    // window is in samples), so a rate change means a new machine — but the
    // TRAINED REFERENCE must survive the swap. Carry the codec bytes over.
    std::vector<std::uint8_t> carried;
    if (machine != nullptr)
        carried = machine->encode_reference();

    machine = std::make_unique<makegain::dsp::GainComputer> (sampleRate);

    // RT-scratch, sized once here (prepareToPlay may be called again on a
    // configuration change — resizing is fine outside the audio thread).
    monoSum.assign (static_cast<std::size_t> (samplesPerBlock), 0.0f);
    gainDb.assign  (static_cast<std::size_t> (samplesPerBlock), 0.0f);

    // Reference restoration order matters to hosts: some load a session by
    // calling setStateInformation BEFORE prepareToPlay — hence the deferred
    // stash (FR-7: all-or-nothing; a failed decode leaves nothing changed).
    auto applyRef = [this] (const std::vector<std::uint8_t>& bytes)
    {
        if (! bytes.empty())
            machine->decode_reference (bytes.data(), bytes.size());
    };
    if (! deferredReferenceBytes.empty()) { applyRef (deferredReferenceBytes); deferredReferenceBytes.clear(); }
    else                                  applyRef (carried);
}

void MakeGainProcessor::releaseResources()
{
    // The machine and scratch buffers are freed by reassignment in
    // prepareToPlay; nothing here needs explicit teardown (no RT thread
    // will be running when the host calls this).
}

void MakeGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // ScopedNoDenormals: after a gain stage near-zero samples can decay into
    // "denormals" (subnormal floats), which some CPUs handle at a massive
    // slowdown — a classic source of mystery CPU spikes in plugins. This
    // guard clamps that for the block. Cheap and always on in real DSP code.
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();
    const auto n      = buffer.getNumSamples();

    // Output channels we receive no data for are OUR responsibility (may hold
    // uninitialised memory) — always cleared, DB engaged or not.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    // When the machine is not (yet) constructed, the build remains a
    // pass-through — the RT contract holds even in half-initialised states.
    if (machine == nullptr || n == 0 || numIn == 0 || n > monoSum.size())
        return;

    // --- parameter edges -------------------------------------------------
    // In framework terms: parameter values arrive as raw atomic floats the
    // host writes (automation / UI) and we read here, on the audio thread,
    // lock-free. The machine's MODE changes on TRANSITIONS (see header):
    // an Active left ON must not re-engage after a training completes
    // (FR-2), and a Read click is a rising edge (a press), not a level.
    const float activeRaw = activeParam->load (std::memory_order_relaxed);
    const float readRaw   = readParam->load (std::memory_order_relaxed);
    const float lastActive = lastActiveRaw.exchange (activeRaw,
                                                     std::memory_order_relaxed);
    const float lastRead   = lastReadRaw.exchange (readRaw,
                                                   std::memory_order_relaxed);

    if (readRaw > 0.5f && lastRead <= 0.5f)
        machine->set_mode (makegain::dsp::GainMode::reading);
    if (activeRaw > 0.5f && lastActive <= 0.5f)
        machine->set_mode (makegain::dsp::GainMode::adjusting);
    else if (activeRaw <= 0.5f && lastActive > 0.5f)
        machine->set_mode (makegain::dsp::GainMode::idle);

    // --- mono-sum feed (TS/UX-Q5) -----------------------------------------
    // The meter measures ONE target: the mono sum (L+R)/2 — loudness
    // perception of the composite program, not of either channel alone.
    // A mono bus is its own sum trivially.
    const float* l = buffer.getReadPointer (0);
    for (int i = 0; i < n; ++i) monoSum[(std::size_t) i] = l[i];
    if (numIn > 1)
    {
        const float* r = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i)
            monoSum[(std::size_t) i] = 0.5f * (l[i] + r[i]);
    }

    // --- machine advance + per-sample gain emission (PB2a contract) -------
    machine->process (monoSum.data(), static_cast<std::size_t> (n),
                      gainDb.data());

    // --- apply: each sample multiplied by 10^(dB/20) (FR-11: no lookahead,
    // no delay line — sample i's gain is sample i's factor, both channels).
    for (int i = 0; i < n; ++i)
        gainDb[(std::size_t) i] = std::pow (10.0f,
                                            gainDb[(std::size_t) i] * 0.05f);
    for (int ch = 0; ch < numIn; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            out[i] *= gainDb[(std::size_t) i];
    }
}

MakeGainProcessor::EngineReadout MakeGainProcessor::getReadout() const
{
    // Called on the message thread (editor paint, PB3). The machine's
    // read accessors are cheap, lock-free value copies — no pointers out.
    EngineReadout ro;
    if (machine == nullptr)
        return ro;
    ro.appliedGainDb = machine->applied_gain_db();
    ro.reference     = machine->reference();
    ro.flags         = machine->flags();
    return ro;
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
// FR-7 session state (save/load)
//
// In framework terms: JUCE's standard persistence story is
// AudioProcessorValueTreeState (APVTS). The host calls getStateInformation
// / setStateInformation on a NON-realtime thread whenever it saves/loads a
// session or a preset. We serialize the APVTS ValueTree as XML via the
// helpers juce ships for exactly this (copyXmlToMemory / getXmlFromBinary),
// and ride ONE extra property inside the same tree: the trained reference
// (its 18-byte codec record, base64-wrapped). One blob = one session truth.
// ---------------------------------------------------------------------------

void MakeGainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // copyState: thread-safe snapshot of the parameter tree (host may be
    // automating while we serialize).
    auto state = apvts.copyState();

    const std::vector<std::uint8_t> refBytes =
        (machine != nullptr) ? machine->encode_reference()
                             : std::vector<std::uint8_t> {};
    if (! refBytes.empty())
        state.setProperty ("makegainReference",
                           juce::Base64::toBase64 (refBytes.data(),
                                                   refBytes.size()),
                           nullptr);
    else
        state.removeProperty ("makegainReference", nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MakeGainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // All-or-nothing (FR-7): a broken/unrecognized blob must leave the
    // CURRENT state untouched, so decode everything BEFORE replacing.
    auto restored = juce::ValueTree::fromXml (*xml);
    if (! restored.isValid())
        return;

    const auto refBase64 = restored.getProperty ("makegainReference").toString();

    // Reference bytes first, into the stash — the machine may not exist yet
    // (session-load order, see prepareToPlay). A failed decode sets nothing
    // (FR-7 guarantee inside decode_reference itself). In framework terms:
    // Base64 decoding writes into an OutputStream — MemoryOutputStream over
    // a MemoryBlock is the standard way to land it in raw bytes.
    juce::MemoryBlock raw;
    if (refBase64.length() > 0)
    {
        juce::MemoryOutputStream rawStream (raw, false);
        if (juce::Base64::convertFromBase64 (rawStream, refBase64))
        {
            deferredReferenceBytes.assign (
                static_cast<const std::uint8_t*> (raw.getData()),
                static_cast<const std::uint8_t*> (raw.getData()) + raw.getSize());
        }
        else
        {
            deferredReferenceBytes.clear();
        }
    }
    else
    {
        deferredReferenceBytes.clear();
    }

    apvts.replaceState (restored);

    // If the machine IS alive, hand the reference over right away.
    if (machine != nullptr && ! deferredReferenceBytes.empty())
    {
        machine->decode_reference (deferredReferenceBytes.data(),
                                   deferredReferenceBytes.size());
        deferredReferenceBytes.clear();
    }
}
