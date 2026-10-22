// MakeGainProcessor.h — the plugin's host-facing shell.
//
// PB2e-red state: the A2 amendment retired the training FSM, so the old
// GainComputer wiring is removed HERE as well — this build is an honest
// passthrough while the delta engine is only a tested stub. The aux-bus
// declaration and the full AGC wiring return in PB2f.
//
// JUCE orientation (what this file is, in framework terms):
//
// A DAW plugin is NOT an application: there is no main(). The host (Reaper,
// Ardour...) loads a binary library and drives it through the format's ABI
// (VST3, CLAP, AU). JUCE hides that protocol behind one class:
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
//   getStateInformation()/setStateInformation() — host, saving/loading the
//                        session (FR-22, narrowed by A2: parameters only).
//
// Parameters (A2): Active is the only automatable parameter (UX-Q4);
// Metric/MaxComp/Ramp follow as UI-only parameters in PR-B3.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

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
    // program" stub. Real presets are a non-goal for v1.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // --- host configuration negotiation ---
    // Before the host fixes our channel layout it offers candidates; we
    // accept exactly stereo-in→stereo-out (and the mono pair). The two
    // stereo input pairs of the A2 architecture arrive in PB2f.
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    // FR-22 (A2-narrowed): parameter state only.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

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

    std::atomic<float>* activeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainProcessor)
};
