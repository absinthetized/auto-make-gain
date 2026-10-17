// MakeGainEditor.h — M0 stub editor: an empty dark window with a title.
// Stock JUCE components only (TS §1: no OpenGL, no graph library — the
// meters/graph arrive with the DSP milestones).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MakeGainProcessor;   // forward declaration — include cycle avoidance

class MakeGainEditor : public juce::AudioProcessorEditor
{
public:
    explicit MakeGainEditor (MakeGainProcessor& p);

    void paint (juce::Graphics&) override;

private:
    MakeGainProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainEditor)
};
