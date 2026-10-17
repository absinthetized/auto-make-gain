#include "MakeGainProcessor.h"
#include "ui/MakeGainEditor.h"

MakeGainProcessor::MakeGainProcessor()
    : AudioProcessor (BusesProperties()
           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // No sidechain bus anywhere in MakeGain — the reference is learned in
    // place at M0 (FR-1/§2.1 of the FRs: one instance, one insert point).
}

void MakeGainProcessor::prepareToPlay (double, int) {}
void MakeGainProcessor::releaseResources() {}

void MakeGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    // JUCE hands the input channels inside the same buffer when the bus
    // layouts match: leaving them untouched IS passthrough. Only clear the
    // channels we don't receive data for, so we never emit uninitialised
    // memory (silence-preserving guarantee of the M0 stub).
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

bool MakeGainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // v0 accepts exactly the default stereo/stereo (and mono/mono) pairs;
    // the channel-config matrix is an M3 decision (FR §5, UX-Q5).
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo()
                      || in == juce::AudioChannelSet::mono());
}

juce::AudioProcessorEditor* MakeGainProcessor::createEditor()
{
    return new MakeGainEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MakeGainProcessor();
}
