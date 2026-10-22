#include "ui/MakeGainEditor.h"
#include "plugin/MakeGainProcessor.h"

MakeGainEditor::MakeGainEditor (MakeGainProcessor& p)
    // AudioProcessorEditor takes a pointer to its processor so the base
    // class can route parameter changes/lifecycle internally; we also keep
    // our own reference for the status polling.
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    // Plugin windows are NOT resizable at this stage: resize support
    // (setResizeLimits + component listeners) is an M2 UI item with the
    // real meter layout.
    setSize (420, 180);

    // 4 Hz status refresh (see the header note: atomics only, no locks).
    startTimerHz (4);
}

void MakeGainEditor::timerCallback()
{
    // Only the label region changes — but with no separate label component
    // yet, a plain full repaint is cheaper than bookkeeping a component
    // tree (PR-B3 replaces this whole window anyway).
    repaint();
}

void MakeGainEditor::paint (juce::Graphics& g)
{
    // Everything below is stock JUCE low-level drawing (no widgets at all):
    // fill the window, then draw text centered in bands of the window
    // rectangle. The palette here sets the dark theme the real UI (M2)
    // will formalize into a juce::LookAndFeel.
    g.fillAll (juce::Colour (0xff101418));   // near-black slate

    g.setColour (juce::Colour (0xffe8e4dc)); // warm off-white
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MakeGain", getLocalBounds().removeFromTop (60), juce::Justification::centred);

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (juce::Colour (0xff8a9099)); // dim grey
    // Status line: composed by the processor from its status atomics, so
    // what this text claims is ALWAYS about the loaded binary. Interim
    // states tell the smoke-tester the engine is alive without a meter.
    g.drawText (processorRef.statusLine(),
                getLocalBounds().removeFromBottom (40), juce::Justification::centred);
}
