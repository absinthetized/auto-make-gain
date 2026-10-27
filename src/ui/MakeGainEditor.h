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
// PR-C panel (A3): five toggles (Active, Metric switch, Freeze, Meter
// HP + the Response switch), three rotaries (MaxComp, Ramp tc, Meter
// HP freq), readouts (applied gain + chain delta + per-tap loudness)
// and the honest status bar.
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
    void resized() override;   // control layout (buttons + rotaries)

private:
    void timerCallback() override;   // refresh readout strings + repaint

    // Reference (not pointer!): editors are destroyed with their processor
    // in well-behaved hosts; JUCE's leak detector flags dangling UI state.
    MakeGainProcessor& processorRef;

    // Attached buttons (bool parameters: ButtonAttachment does both
    // directions — user click to parameter, persistence to button).
    // Native checkbox look (PR-D owner iteration 2): on/off booleans
    // keep JUCE's standard rendering; the pill look is reserved for
    // the two modality switches below.
    juce::ToggleButton activeButton { "Active" };   // THE automatable param
    juce::ToggleButton freezeButton { "Freeze" };   // FR-24 static lock
    juce::ToggleButton mtrhpButton { "Meter HP" };  // FR-26 enable
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        activeAttach, freezeAttach, mtrhpAttach;

    // Shared custom LookAndFeel (PR-D switch redesign, re-scoped by
    // owner): renders ToggleButtons as sliding modality pills with a
    // caption. One instance for both choice switches — the caption is
    // the live selection text (the timer already maintains it: "LUFS"
    // / "dB RMS", "SHORT" / "FAST") and stays bright in BOTH states
    // (a modality has no off, so no dimming).
    struct ModalitySwitchLnf : juce::LookAndFeel_V4
    {
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool highlighted, bool down) override;
    };
    ModalitySwitchLnf modalitySwitchLnf;

    // Switch buttons over 2-choice parameters (no attachment type maps
    // choice→button, so the glue is manual both ways — see timerCallback
    // and the on-click capture in the constructor).
    juce::ToggleButton metricSwitch;    // LUFS (S) / dB RMS
    juce::ToggleButton responseSwitch;  // Short (3 s) / Fast (0.4 s)

    // The three rotaries: compensation strength, ramp feel, metering HP
    // corner. Range/skew come from the APVTS NormalisableRange.
    juce::Slider maxcompSlider { juce::Slider::RotaryHorizontalVerticalDrag,
                                 juce::Slider::TextBoxBelow };
    juce::Slider ramptcSlider { juce::Slider::RotaryHorizontalVerticalDrag,
                                juce::Slider::TextBoxBelow };
    juce::Slider mtrhphzSlider { juce::Slider::RotaryHorizontalVerticalDrag,
                                 juce::Slider::TextBoxBelow };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        maxcompAttach, ramptcAttach, mtrhphzAttach;

    // Readout strings refreshed by the Timer, drawn in paint().
    juce::String appliedText, deltaText;
    juce::String metricCaption { "CHAIN DELTA" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainEditor)
};
