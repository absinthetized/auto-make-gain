// MakeGainProcessor.h — the plugin's "brain", now with the gain machine
// wired in (PR-B2 stage 1).
//
// JUCE orientation (what this file is, in framework terms):
//
// A DAW plugin is NOT an application: there is no main(). The host (Reaper,
// Ardour, GarageBand...) loads a binary library and drives it through the
// format's ABI (VST3, CLAP, AU). JUCE hides that protocol behind one class:
// juce::AudioProcessor — we subclass it, and JUCE's generated glue
// ("wrapper") for each format translates the host's calls into our
// virtual methods. This ONE class therefore serves every format we build.
//
// Lifecycle contract (who calls what, when, on which thread):
//   prepareToPlay()    — host (before audio starts): pass sample rate +
//                        max block size; allocate everything memory-heavy here.
//   processBlock()     — host, on its REAL-TIME audio thread, in smaller
//                        blocks: must be allocation-free and lock-free, or
//                        the host's audio stalls (audible dropouts/clicks).
//   releaseResources() — host (after/before config change): free things.
//   createEditor()     — host, on the UI thread, when the user opens the
//                        plugin window: only UI work may happen there.
//   getStateInformation()/setStateInformation() — host, saving/loading the
//                        session: "serialize the mode the user left behind"
//                        (FR-7). NOT realtime; allocations are fine here.
//
// Parameters (UX-Q4, frozen contract): the Active toggle is the only
// AUTOMATABLE parameter. Stage 1 exposes Active + Read; Metric, MaxComp
// and Ramp speed follow in stage 2 (PB2c-2). All params persist via the
// APVTS state; the trained reference rides in the same blob (FR-7).
//
// The DSP itself (measurement, gain decisions) lives in src/dsp/GainComputer
// — this class is deliberately thin glue: it feeds, reads attribute atomics
// and copies per-sample gains onto the bus. Nothing here does math.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

#include "dsp/gain_computer.h"

class MakeGainProcessor : public juce::AudioProcessor
{
public:
    MakeGainProcessor();

    // --- host-called audio lifecycle (see the file-header contract) ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- editor (UI) glue: the window peer lives in src/ui/ ---
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- small facts the host likes to ask ---
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Program interface — JUCE's preset-slot API. Even a no-parameter plugin
    // must implement it (pure virtuals), hence the trivial "one empty
    // program" stub. Real presets are a non-goal for v1 (FR §8).
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // --- host configuration negotiation ---
    // Before the host fixes our channel layout it offers candidates; we
    // accept exactly stereo-in→stereo-out (and the mono pair). The wider
    // channel-config matrix is an M3 decision (FR §5, UX-Q5).
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    // FR-7 state: APVTS parameters + the trained reference (codec bytes').
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Snapshot readouts for the editor (FR-12; PB3 consumes them fully).
    // Returns POD copies: no locks, no interior pointers escape.
    struct EngineReadout
    {
        double appliedGainDb = 0.0;
        makegain::dsp::ReferenceRecord reference;
        makegain::dsp::GainFlags flags;
    };
    EngineReadout getReadout() const;

private:
    // Parameter layout — the single source of parameter IDs for the whole
    // plugin (APVTS owns the params; the editor attaches to them).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // AudioProcessorValueTreeState (APVTS): JUCE's standard parameter +
    // persistence machinery. "In framework terms": the host asks us for a
    // list of parameters; each is exposed as an atomic<float> the audio
    // thread reads lock-free, while the UI talks to juce::RangedAudioParameter
    // objects on the message thread. The APVTS serializes both sides into
    // one ValueTree — which is what a DAW actually stores in the session.
    juce::AudioProcessorValueTreeState apvts
        { *this, nullptr, "STATE", createParameterLayout() };

    // Atomic float views (fast, lock-free reads on the audio thread);
    // present only if the param exists (they always do by construction).
    std::atomic<float>* activeParam = nullptr;
    std::atomic<float>* readParam   = nullptr;

    // Edge detection: the machine's mode changes on TRANSITIONS, not
    // steady values (an Active left ON must not re-engage after a
    // training completes — FR-2: the plugin stays un-engaged until a
    // new user action). These hold the PREVIOUS block's raw values.
    std::atomic<float> lastActiveRaw { 0.0f };
    std::atomic<float> lastReadRaw   { 0.0f };

    // The compensate machine. Deferred to prepareToPlay because it is
    // sample-rate-bound (FR-2's 3.0 s window is in samples); constructed
    // ONLY there (never on the audio thread — no allocation at RT time).
    std::unique_ptr<makegain::dsp::GainComputer> machine;

    // RT-scratch, sized once in prepareToPlay: mono-sum feed + the
    // per-sample gain dB the machine emits (the texture the channels
    // get multiplied by).
    std::vector<float> monoSum;
    std::vector<float> gainDb;

    // FR-7: if state arrives before prepareToPlay (hosts vary in session
    // load order!), the reference bytes wait here for the machine's birth.
    std::vector<std::uint8_t> deferredReferenceBytes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainProcessor)
};
