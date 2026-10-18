#include "ui/MakeGainEditor.h"
#include "plugin/MakeGainProcessor.h"

MakeGainEditor::MakeGainEditor (MakeGainProcessor& p)
    // AudioProcessorEditor takes a pointer to its processor so the base
    // class can route parameter changes/lifecycle internally; we also keep
    // our own reference for future editor-side meter polling.
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    // Plugin windows are NOT resizable at this stage: resize support
    // (setResizeLimits + component listeners) is an M2 UI item with the
    // real meter layout.
    setSize (420, 180);
}

void MakeGainEditor::paint (juce::Graphics& g)
{
    // Everything below is stock JUCE low-level drawing (no widgets at all):
    // fill the window, then draw two text labels centered in bands of the
    // window rectangle. The palette here sets the dark theme the real UI
    // (M2) will formalize into a juce::LookAndFeel.
    g.fillAll (juce::Colour (0xff101418));   // near-black slate

    g.setColour (juce::Colour (0xffe8e4dc)); // warm off-white
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MakeGain", getLocalBounds().removeFromTop (60), juce::Justification::centred);

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (juce::Colour (0xff8a9099)); // dim grey
    // Status line: states what this build actually contains, so a host
    // load-check is self-verifying (the label must match the branch's
    // promised behavior). Updated manually once per PR — if this text is
    // wrong about the loaded binary, the wrong build is loaded.
    g.drawText ("M1 PR-A \u2014 DSP kernels tested; signal path: passthrough",
                getLocalBounds().removeFromBottom (40), juce::Justification::centred);
}
