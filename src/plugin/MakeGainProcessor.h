// MakeGainProcessor.h — the plugin's "brain".
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
//
// Our subclass starts as an M0 stub: it passes audio through untouched so
// format/infra commits never carry DSP risk. The gain computer (TS §3.3)
// arrives with the DSP milestones.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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

    // v0 stub: no state yet (state persistence is FR-7; wired with
    // parameters in the DSP milestones).
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainProcessor)
};
