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
    // Switch redesign, owner-iteration 2 (PR-D): the two modality
    // switches wear the shared sliding-pill LookAndFeel; the three
    // on/off booleans above use the native checkbox rendering.
    metricSwitch.setLookAndFeel (&modalitySwitchLnf);
    responseSwitch.setLookAndFeel (&modalitySwitchLnf);
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

    // Dependent-control greying (PR-D owner rulings):
    //   Meter HP off  -> the MTR HP FREQ rotary does nothing (filtered
    //                    feed is disabled) -> grey.
    //   Freeze on     -> the gain is a static trimmer; MaxComp and Ramp
    //                    tc actuate nothing until release -> grey. ALSO
    //                    (owner ruling): the Meter HP button locks BOTH
    //                    directions while frozen - flipping the metering
    //                    definition mid-hold shifts the readouts under
    //                    the hold (false alarm) and leaves a different
    //                    HP state at release.
    // Component::setEnabled/setAlpha no-op cheaply on unchanged values,
    // so the 10 Hz poll costs nothing at steady state.
    const bool hpOn = processorRef.getApvts()
                          .getRawParameterValue ("mtrhp")->load() > 0.5f;
    const bool frozen = processorRef.uiFrozen();
    mtrhphzSlider.setEnabled (hpOn && !frozen);
    mtrhphzSlider.setAlpha ((hpOn && !frozen) ? 1.0f : 0.35f);
    mtrhpButton.setEnabled (!frozen);
    mtrhpButton.setAlpha (frozen ? 0.35f : 1.0f);
    maxcompSlider.setEnabled (!frozen);
    ramptcSlider.setEnabled (!frozen);
    maxcompSlider.setAlpha (frozen ? 0.35f : 1.0f);
    ramptcSlider.setAlpha (frozen ? 0.35f : 1.0f);

    repaint();
}

void MakeGainEditor::resized()
{
    // Layout order (PR-D owner ruling): the three rotaries are the
    // most-relevant controls, so the dual-state strip renders BELOW
    // them — readouts, rotaries, strip, status bar.
    //
    // Rotary row: MaxComp / Ramp tc / Meter-HP frequency. Widths match
    // the painted captions in paint() (three equal columns).
    maxcompSlider.setBounds (14, 178, 144, 92);
    ramptcSlider.setBounds (162, 178, 144, 92);
    mtrhphzSlider.setBounds (310, 178, 144, 92);

    // Dual-state strip (sliding switches), under the rotaries.
    const int bw = (468 - 28) / 5;
    int x = 14;
    auto placeButton = [&x, bw] (juce::Component& c)
    {
        c.setBounds (x, 300, bw, 54);
        x += bw;
    };
    placeButton (activeButton);
    placeButton (metricSwitch);
    placeButton (responseSwitch);
    placeButton (freezeButton);
    placeButton (mtrhpButton);
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

    // Control captions. Toggle switches carry their own captions; the
    // rotaries need name + unit captions here (positions track the
    // resized() rotary row above the button strip).
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colour (0xff8a9099));
    g.drawText ("MAXCOMP", juce::Rectangle<int> { 14, 276, 144, 16 },
                juce::Justification::centred);
    g.drawText ("RAMP TC", juce::Rectangle<int> { 162, 276, 144, 16 },
                juce::Justification::centred);
    g.drawText ("MTR HP FREQ", juce::Rectangle<int> { 310, 276, 144, 16 },
                juce::Justification::centred);

    // Automatable clue (PR-D owner ruling): no text label on Active.
    // The ONE automatable parameter carries a thin amber border instead
    // — a per-component painted frame (drawn AFTER the component, so it
    // reads as "this control has a role", not as the button's own face).
    g.setColour (juce::Colour (0xffd9a441));   // warm amber accent
    g.drawRoundedRectangle (activeButton.getBounds().toFloat().reduced (2.0f),
                            6.0f, 2.0f);

    // Status bar (PR-D): the band color IS the state light —
    //   gray : nothing is happening (standby, engine idle);
    //   green: rolling (engaged, live delta, actuating compensation);
    //   red  : the aux/sidechain tap is dead (delta unmeasurable);
    //   ice  : gain frozen (deliberate hold — FR-24).
    // Color precedence mirrors statusLine's hierarchy: frozen reports
    // first (the text announces it first), then the sidechain alert,
    // then rolling, else the gray standby band. Values are darker
    // cousins of the palette accents so the 13pt text stays readable.
    auto statusStrip = getLocalBounds().removeFromBottom (32);
    juce::Colour band = juce::Colour (0xff263238);
    juce::Colour ink  = juce::Colour (0xffe8e4dc);
    const bool active = processorRef.getApvts()
                            .getRawParameterValue ("active")->load() > 0.5f;
    if (processorRef.uiFrozen()) {
        band = juce::Colour (0xff274a5a);  // ice blue: deliberate hold
        ink  = juce::Colour (0xffc2e4f2);
    } else if (processorRef.uiSidechainMissing()) {
        band = juce::Colour (0xff5a2a2a);  // red: no sidechain signal
        ink  = juce::Colour (0xfff0c8c8);
    } else if (active && processorRef.uiDeltaValid()) {
        band = juce::Colour (0xff33513a);  // green: compensating
        ink  = juce::Colour (0xffcfe6c8);
    }
    g.setColour (band);
    g.fillRect (statusStrip);
    g.setColour (ink);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (processorRef.statusLine(), statusStrip.reduced (8, 0),
                juce::Justification::centred);
}

// ---------------------------------------------------------------------------
// ModalitySwitchLnf (PR-D switch redesign, owner-iteration 2): custom
// renderer for the two CHOICE switches only. Framework context: a
// LookAndFeel is JUCE's pluggable skin object — overriding
// drawToggleButton here re-styles ONLY the components that carry this
// instance (setLookAndFeel above); the on/off booleans keep the
// native checkbox rendering. We draw the whole control: modality pill
// + caption.
// ---------------------------------------------------------------------------
void MakeGainEditor::ModalitySwitchLnf::drawToggleButton (
    juce::Graphics& g, juce::ToggleButton& b,
    bool highlighted, bool down)
{
    const auto bounds = b.getLocalBounds().toFloat();

    // Geometry: pill on top, caption under it. Pill scans the label
    // width (the "= off, right = on" reading is the switch affordance).
    const float pillW = juce::jmin (bounds.getWidth() * 0.46f, 48.0f);
    const float pillH = juce::jmin (bounds.getHeight() * 0.42f, 26.0f);
    const float pillX = bounds.getCentreX() - pillW / 2.0f;
    const float pillY = bounds.getY() + bounds.getHeight() * 0.12f;
    const auto pill = juce::Rectangle<float> { pillX, pillY, pillW, pillH };
    const float knobD = pillH - 6.0f;
    const bool on = b.getToggleState();

    // Track: soft green when active, dark slate when not; a touched
    // (highlight) track lightens so the pointer target stays evident.
    juce::Colour track = on ? juce::Colour (0xff5e9e6f)   // soft green
                            : (highlighted ? juce::Colour (0xff39424d)
                                           : juce::Colour (0xff2c333c));
    g.setColour (down ? track.darker (0.15f) : track);
    g.fillRoundedRectangle (pill, pillH / 2.0f);

    // Knob: slides left (off) to right (on), no animation — state flips
    // are binary parameter changes, not gestures worth a tween.
    const float knobLeft = pill.getX() + 3.0f;
    const float knobRight = pill.getRight() - 3.0f - knobD;
    const float knobX = on ? knobRight : knobLeft;
    g.setColour (juce::Colour (0xffe8e4dc));   // same warm off-white body
    g.fillEllipse (knobX, pill.getY() + 3.0f, knobD, knobD);

    // Caption row: the live selection text ("LUFS" / "dB RMS",
    // "SHORT" / "FAST"), maintained by the editor timer. ALWAYS the
    // bright ink (PR-D owner iteration 2): a modality has no off
    // state, so the label must not dim when the other side is chosen.
    const auto caption = juce::Rectangle<float> {
        bounds.getX(), bounds.getBottom() - (pillH - 2.0f),
        bounds.getWidth(), pillH - 2.0f };
    g.setColour (juce::Colour (0xffe8e4dc));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), caption,
                juce::Justification::centred);
}
