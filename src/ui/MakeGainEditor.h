// MakeGainEditor.h — the plugin's WINDOW (the part you see in the DAW).
//
// JUCE orientation: a plugin has two long-lived "sides":
//
//   MakeGainProcessor  — the DSP brain, owned by the host's audio machinery,
//                        fed on the real-time audio thread.
//   MakeGainEditor     — the UI peer, created on demand (createEditor()),
//                        owned by the host's UI machinery, running on the
//                        MESSAGE (UI) thread.
//
// Rules of the separation (JUCE shows no mercy):
//   * The UI thread must NEVER touch audio internals synchronously, and
//     the audio thread must never wait on the UI. They talk through
//     thread-safe channels: the processor's status atomics and the
//     APVTS parameter objects (attachments).
//   * The editor is disposable: hosts destroy/recreate it freely (window
//     close/reopen, project reload). The processor survives.
//
// PR-B3 UI (the re-derived A2 panel, FR §10.6): three real controls
// (Metric combo, MaxComp + Ramp rotaries, APVTS attachments — no manual
// parameter plumbing), readouts painted from processor atomics on a
// Timer (chain delta + applied gain), and the honest status line.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MakeGainProcessor;   // forward declaration — include cycle avoidance

class MakeGainEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit MakeGainEditor (MakeGainProcessor& p);

    // Full-window custom paint: dark theme + the text readouts (their
    // strings refresh from the processor's atomics in timerCallback).
    void paint (juce::Graphics&) override;
    void resized() override;   // control layout (sliders/combo boxes)

private:
    void timerCallback() override;   // refresh readout strings + repaint

    // Reference (not pointer!): editors are destroyed with their processor
    // in well-behaved hosts; JUCE's leak detector flags dangling UI state.
    MakeGainProcessor& processorRef;

    // Controls (message thread; APVTS attachments do the plumbing).
    // Active: the ONE automatable parameter (UX-Q4) — a big toggle so the
    // plugin is fully drivable from its own panel, no "generic UI" detour.
    juce::ToggleButton activeButton { "Active" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        activeAttach;

    // Metric: a two-position switch (owner feedback: "not a dropdown").
    // The APVTS parameter stays a choice (index 0 = LUFS, 1 = dB RMS) for
    // host compatibility; the button maps its on/off state to that index.
    juce::ToggleButton metricSwitch;
    juce::Label metricCaption { {}, "metric" };

    juce::Slider maxcompSlider { juce::Slider::RotaryHorizontalVerticalDrag,
                                 juce::Slider::TextBoxBelow };
    juce::Slider ramptcSlider { juce::Slider::RotaryHorizontalVerticalDrag,
                                juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        maxcompAttach, ramptcAttach;

    // Readout strings refreshed by the Timer, drawn in paint(). The gauge
    // captions are constants (owner feedback: unlabeled numbers are
    // meaningless) and stay in paint().
    juce::String appliedText, deltaText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainEditor)
};
