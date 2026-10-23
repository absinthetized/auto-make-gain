#include "ui/MakeGainEditor.h"
#include "plugin/MakeGainProcessor.h"

MakeGainEditor::MakeGainEditor (MakeGainProcessor& p)
    // AudioProcessorEditor takes a pointer to its processor so the base
    // class can route parameter changes/lifecycle internally; we also keep
    // our own reference for readout polling + attachment targets.
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    setSize (440, 360);
    setResizable (false, false);

    auto& apvts = processorRef.getApvts();

    // Active: a ButtonAttachment binds a toggle button to a bool parameter
    // in framework terms — user clicks land on the parameter, host-side
    // automation/persistence lands back on the button. Fully automatic.
    addAndMakeVisible (activeButton);
    activeButton.setButtonText ("Active");
    activeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "active", activeButton);

    // Metric switch: no stock attachment type maps a 2-choice parameter to
    // a toggle button (attachments are bool-only), so this glue is manual:
    //   click   -> write the choice index (0/1) into the parameter tree,
    //   timer   -> reflect the parameter's value back onto the button.
    metricSwitch.setToggleState (false, juce::dontSendNotification);
    metricSwitch.onClick = [this]
    {
        auto* metricParam = processorRef.getApvts().getParameter ("metric");
        // setValueNotifyingHost in framework terms: change the value AND
        // tell the host, so session persistence + gesture recording agree.
        metricParam->setValueNotifyingHost (metricSwitch.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible (metricSwitch);
    addAndMakeVisible (metricCaption);
    metricCaption.setJustificationType (juce::Justification::centred);
    metricCaption.setColour (juce::Label::textColourId, juce::Colour (0xff8a9099));
    metricCaption.setFont (juce::FontOptions (13.0f));

    // MaxComp + Ramp tc rotaries. SliderAttachment is the same deal as the
    // Active button: parameter <-> control, both directions, no manual
    // plumbing. Range/skew come from the parameter's NormalisableRange.
    maxcompSlider.setTextValueSuffix (" dB");
    ramptcSlider.setTextValueSuffix (" s");
    addAndMakeVisible (maxcompSlider);
    addAndMakeVisible (ramptcSlider);
    maxcompAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "maxcomp", maxcompSlider);
    ramptcAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, "ramptc", ramptcSlider);

    // 10 Hz readout refresh: atomics only, no locks, no audio-thread work.
    startTimerHz (10);
}

void MakeGainEditor::timerCallback()
{
    const bool valid = processorRef.uiDeltaValid();

    appliedText = juce::String (processorRef.uiAppliedDb(), 1) + " dB";
    deltaText   = valid
        ? "chain delta " + juce::String (processorRef.uiDeltaDb(), 1) + " dB"
        : "chain delta - no full window yet";

    // Reflect the metric parameter onto the switch (activity made on the
    // parameter side — session reload, our own click — both land here).
    const bool rms = processorRef.getApvts()
                         .getRawParameterValue ("metric")->load() > 0.5f;
    if (rms != metricSwitch.getToggleState())
        metricSwitch.setToggleState (rms, juce::dontSendNotification);
    metricSwitch.setButtonText (rms ? "dB RMS" : "LUFS");

    repaint();
}

void MakeGainEditor::resized()
{
    // Control row, below the painted readout band. The two toggle buttons
    // get generous rectangles; the rotaries get the rest.
    activeButton.setBounds    (14, 186, 80, 60);
    metricSwitch.setBounds    (104, 186, 80, 60);
    maxcompSlider.setBounds   (192, 180, 112, 88);
    ramptcSlider.setBounds    (312, 180, 112, 88);
    metricCaption.setBounds   (100, 268, 88, 18);
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
    g.drawText ("CHAIN DELTA", readouts.removeFromTop (16),
                juce::Justification::centred);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colour (0xffc8d2e6));
    g.drawText (deltaText, readouts, juce::Justification::centred);

    // Knob captions (rotaries carry their own value in the text box, but
    // the NAME lives here).
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (0xff8a9099));
    g.drawText ("MAXCOMP", juce::Rectangle<int> { 192, 268, 112, 18 },
                juce::Justification::centred);
    g.drawText ("RAMP TC", juce::Rectangle<int> { 312, 268, 112, 18 },
                juce::Justification::centred);
    g.drawText ("AUTOMATABLE", juce::Rectangle<int> { 14, 246, 80, 18 },
                juce::Justification::centred);

    // Status bar: a dedicated, differently-colored strip at the bottom
    // (owner feedback: status is not a control, it should not look like
    // one). The processor composes the string from engine state.
    auto statusStrip = getLocalBounds().removeFromBottom (32);
    g.setColour (juce::Colour (0xff263238));   // distinct blue-grey band
    g.fillRect (statusStrip);
    g.setColour (juce::Colour (0xffe8e4dc));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (processorRef.statusLine(), statusStrip.reduced (8, 0),
                juce::Justification::centred);
}
