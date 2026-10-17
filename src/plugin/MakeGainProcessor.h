// MakeGainProcessor.h — M0 stub: transparency shell.
// No parameters, no DSP: it must not touch a single sample that the host
// feeds it (silence-preserving passthrough), so format/infra commits
// (C1-C4) never carry DSP risk alongside infra risk.
// Real gain computer arrives with the DSP milestones (TS §3.3).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MakeGainProcessor : public juce::AudioProcessor
{
public:
    MakeGainProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Program interface — v0 stub: exactly one (empty) program, no presets
    // system (FR non-goals). Never used in the host until we add params/state.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    // v0 stub: no state yet (state persistence is FR-7; wired with
    // parameters in the DSP milestones).
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainProcessor)
};
