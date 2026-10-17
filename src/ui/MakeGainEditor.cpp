#include "ui/MakeGainEditor.h"
#include "plugin/MakeGainProcessor.h"

MakeGainEditor::MakeGainEditor (MakeGainProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    setSize (420, 180);
}

void MakeGainEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101418));

    g.setColour (juce::Colour (0xffe8e4dc));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("MakeGain", getLocalBounds().removeFromTop (60), juce::Justification::centred);

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (juce::Colour (0xff8a9099));
    g.drawText ("M0 stub \u2014 passthrough, no DSP yet",
                getLocalBounds().removeFromBottom (40), juce::Justification::centred);
}
