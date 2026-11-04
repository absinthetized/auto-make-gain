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
// ZOOM architecture (FR-27): the editor's OWN transform is host-reserved
// machinery (JUCE: "applying your own transform to the editor will
// obliterate it") — so everything visual lives on the MainPanel CHILD,
// and zoom = transform the child + resize the (untransformed) editor
// around it. The panel always works in logical 468x400 coordinates;
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MakeGainProcessor;   // forward declaration — include cycle avoidance

class MakeGainEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit MakeGainEditor (MakeGainProcessor& p);
    ~MakeGainEditor() override;   // needed: MainPanel is an incomplete
                                  // type until the cpp defines it (rule:
                                  // unique_ptr member owns the child)

private:
    class MainPanel;              // everything visual, logical 468x400
    std::unique_ptr<MainPanel> panel;

    MakeGainProcessor& processorRef;   // the shell's own handle (zoom poll)

    // The shell's 10 Hz poll: zoom re-apply + panel.refresh() (readout
    // strings, switch reflection, dependent greying).
    void timerCallback() override;

    // FR-27 zoom state: the transform factor actually active (avoids
    // re-transforming on every poll tick of applyZoom's guard).
    double currentZoom = 1.0;
    void applyZoom (double rawIndex);   // 0..4 -> the 5 step factors

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainEditor)
};

// ---------------------------------------------------------------------------
// MainPanel — everything visual (the PR-D grammar: grid, pills, status
// light). A plain Component, NOT the editor: its transform is OURS to
// use (the editor's is the host's). Runs in logical 468x400 space.
// ---------------------------------------------------------------------------
class MakeGainEditor::MainPanel : public juce::Component
{
public:
    explicit MainPanel (MakeGainProcessor& p);

    void paint (juce::Graphics&) override;   // full-window custom paint
    void resized() override;                 // control layout (grid)

    // The editor's 10 Hz poll lands here: refresh readout strings,
    // reflect choice params onto switches, grey dependents, repaint.
    void refresh();

private:
    // Reference (not pointer!): the panel dies with its editor.
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

    // Boolean skin (PR-D owner visual feedback): the native checkbox
    // draws its label BESIDE the tick, which broke the row grammar
    // (pills read caption-below). This keeps the checkbox look — tick
    // box, on/off dimming — but centers the box and puts the caption
    // below, matching the pill geometry exactly.
    struct BooleanCheckboxLnf : juce::LookAndFeel_V4
    {
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool highlighted, bool down) override;
    };
    BooleanCheckboxLnf booleanCheckboxLnf;

    // Switch buttons over 2-choice parameters (no attachment type maps
    // choice→button, so the glue is manual both ways — see refresh()
    // and the on-capture in the constructor).
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

    // FR-27 zoom selector: a plain dropdown over the 5-step choice
    // parameter (ComboBoxAttachment = native param plumbing, the same
    // pattern the rotary sliders use). The EDITOR owns the transform
    // logic; the param merely carries the persisted selection.
    juce::ComboBox zoomBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        zoomAttach;

    // Readout strings refreshed by refresh(), drawn in paint().
    juce::String appliedText, deltaText;
    juce::String metricCaption { "CHAIN DELTA" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};
