#include "ui/MakeGainEditor.h"
#include "plugin/MakeGainProcessor.h"

MakeGainEditor::MakeGainEditor (MakeGainProcessor& p)
    // AudioProcessorEditor takes a pointer to its processor so the base
    // class can route parameter changes/lifecycle internally; we also keep
    // our own reference for readout polling + attachment targets.
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    setSize (468, 400);
    setResizable (false, false);

    auto& apvts = processorRef.getApvts();

    // Attached toggles (bool parameters: ButtonAttachment does both
    // directions — user click to parameter, host persistence to button).
    addAndMakeVisible (activeButton);
    addAndMakeVisible (freezeButton);
    addAndMakeVisible (mtrhpButton);
    activeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "active", activeButton);
    freezeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "freeze", freezeButton);
    mtrhpAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "mtrhp", mtrhpButton);

    // Manual glue for the two 2-choice parameters (Metric, Response).
    // A 2-choice APVTS parameter stores its selection as a normalized
    // 0/1; this switch forwards clicks 0/1 and the timer reflects the
    // storage's latest value back onto the button (both directions see
    // host automation/persistence without a dedicated attachment type).
    metricSwitch.onClick = [this]
    {
        processorRef.getApvts().getParameter ("metric")
             ->setValueNotifyingHost (metricSwitch.getToggleState() ? 1.0f
                                                                    : 0.0f);
    };
    responseSwitch.onClick = [this]
    {
        processorRef.getApvts().getParameter ("response")
             ->setValueNotifyingHost (responseSwitch.getToggleState() ? 1.0f
                                                                      : 0.0f);
    };
    addAndMakeVisible (metricSwitch);
    addAndMakeVisible (responseSwitch);

    // The three rotaries (SliderAttachment = parameter <-> control in
    // both directions; range/skew come from the NormalisableRange).
    maxcompSlider.setTextValueSuffix (" dB");
    ramptcSlider.setTextValueSuffix (" s");
    mtrhphzSlider.setTextValueSuffix (" Hz");
    addAndMakeVisible (maxcompSlider);
    addAndMakeVisible (ramptcSlider);
    addAndMakeVisible (mtrhphzSlider);
    maxcompAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "maxcomp", maxcompSlider);
    ramptcAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "ramptc", ramptcSlider);
    mtrhphzAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "mtrhphz", mtrhphzSlider);

    // 10 Hz readout refresh: atomics only, no locks, no audio-thread work.
    startTimerHz (10);
}

void MakeGainEditor::timerCallback()
{
    const bool valid = processorRef.uiDeltaValid();
    const bool rms = processorRef.getApvts()
                         .getRawParameterValue ("metric")->load() > 0.5f;
    const bool fast = processorRef.getApvts()
                          .getRawParameterValue ("response")->load() > 0.5f;

    appliedText = juce::String (processorRef.uiAppliedDb(), 1) + " dB";
    deltaText   = valid
        ? "chain delta " + juce::String (processorRef.uiDeltaDb(), 1)
              + (rms ? " dB" : " LU")
        : "chain delta - no full window yet";
    metricCaption = rms ? "CHAIN DELTA (dB RMS)" : "CHAIN DELTA (LUFS)";

    // Reflect the two choice parameters onto their switches (activity
    // made on the parameter side — session reload, our own click —
    // both land here; toggles without a notify callback stay inert).
    if (rms != metricSwitch.getToggleState())
        metricSwitch.setToggleState (rms, juce::dontSendNotification);
    metricSwitch.setButtonText (rms ? "dB RMS" : "LUFS");
    if (fast != responseSwitch.getToggleState())
        responseSwitch.setToggleState (fast, juce::dontSendNotification);
    responseSwitch.setButtonText (fast ? "FAST" : "SHORT");

    repaint();
}

void MakeGainEditor::resized()
{
    // Button row: the five engagement/mode toggles share one strip.
    const int bw = (468 - 28) / 5;
    int x = 14;
    auto placeButton = [&x, bw] (juce::Component& c)
    {
        c.setBounds (x, 134, bw, 54);
        x += bw;
    };
    placeButton (activeButton);
    placeButton (metricSwitch);
    placeButton (responseSwitch);
    placeButton (freezeButton);
    placeButton (mtrhpButton);

    // Rotary row: MaxComp / Ramp tc / Meter-HP frequency. Widths match
    // the painted captions in paint() (three equal columns).
    maxcompSlider.setBounds (14, 196, 144, 92);
    ramptcSlider.setBounds (162, 196, 144, 92);
    mtrhphzSlider.setBounds (310, 196, 144, 92);
}

void MakeGainEditor::paint (juce::Graphics& g)
{
    // All text is plain ASCII (owner-visible mojibake came from UTF-8
    // punctuation being mis-decoded in the host's text stack).
    g.fillAll (juce::Colour (0xff101418));   // near-black slate

    g.setColour (juce::Colour (0xffe8e4dc)); // warm off-white
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MakeGain", getLocalBounds().removeFromTop (44),
                juce::Justification::centred);

    // Readout band: caption + big number for applied gain, then delta.
    auto readouts = getLocalBounds().removeFromTop (170).removeFromBottom (126);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (0xff8a9099));
    g.drawText ("APPLIED GAIN (dB)", readouts.removeFromTop (16),
                juce::Justification::centred);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.setColour (juce::Colour (0xffcfe6c8));   // soft green
    g.drawText (appliedText, readouts.removeFromTop (44),
                juce::Justification::centred);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (0xff8a9099));
    g.drawText (metricCaption, readouts.removeFromTop (16),
                juce::Justification::centred);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colour (0xffc8d2e6));
    g.drawText (deltaText, readouts, juce::Justification::centred);

    // Control captions. Toggle buttons carry their own labels; the
    // rotaries need name + unit captions here.
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (0xff8a9099));
    g.drawText ("MAXCOMP", juce::Rectangle<int> { 14, 290, 144, 16 },
                juce::Justification::centred);
    g.drawText ("RAMP TC", juce::Rectangle<int> { 162, 290, 144, 16 },
                juce::Justification::centred);
    g.drawText ("MTR HP FREQ", juce::Rectangle<int> { 310, 290, 144, 16 },
                juce::Justification::centred);
    g.drawText ("AUTOMATABLE", juce::Rectangle<int> { 14, 120, 84, 12 },
                juce::Justification::centred);

    // Status bar: a dedicated, differently-colored strip at the bottom
    // (owner feedback: status is not a control). The processor composes
    // the string from engine state — freeze reports FIRST (FR-24).
    auto statusStrip = getLocalBounds().removeFromBottom (32);
    g.setColour (juce::Colour (0xff263238));   // distinct blue-grey band
    g.fillRect (statusStrip);
    g.setColour (juce::Colour (0xffe8e4dc));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (processorRef.statusLine(), statusStrip.reduced (8, 0),
                juce::Justification::centred);
}
