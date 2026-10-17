#include "plugin/MakeGainProcessor.h"
#include "ui/MakeGainEditor.h"

MakeGainProcessor::MakeGainProcessor()
    // BusesProperties declares our I/O channel "buses". A bus is a group of
    // channels (mono, stereo, 5.1...). We want exactly one stereo input +
    // one stereo output, and NO sidechain bus anywhere: the MakeGain design
    // learns its reference in place (FR §1/§2.1: one instance, one insert
    // point) — which is also why GarageBand support needs no degraded mode.
    : AudioProcessor (BusesProperties()
           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void MakeGainProcessor::prepareToPlay (double, int) {}
void MakeGainProcessor::releaseResources() {}

void MakeGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // ScopedNoDenormals: after a gain stage near-zero samples can decay into
    // "denormals" (subnormal floats), which some CPUs handle at a massive
    // slowdown — a classic source of mystery CPU spikes in plugins. This
    // guard clamps that for the block. Cheap and always on in real DSP code.
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    // Passthrough semantics (M0 stub): when the host's bus layout matches
    // what we declared in the constructor, JUCE hands us ONE buffer whose
    // channels carry the input data — leaving them untouched IS passing
    // the audio through. We must only silence channels we receive no data
    // for (numIn < numOut cases), because output channels are our
    // responsibility and may hold uninitialised memory otherwise.
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

bool MakeGainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The host may propose: stereo→stereo, mono→mono, or weird asymmetric
    // pairs (mono-in→stereo-out etc). We accept only symmetric, ≤stereo
    // pairs; anything else must show as "not supported" so the host grays
    // the bad routings out instead of misbehaving at runtime.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo()
                      || in == juce::AudioChannelSet::mono());
}

juce::AudioProcessorEditor* MakeGainProcessor::createEditor()
{
    return new MakeGainEditor (*this);
}

// ---------------------------------------------------------------------------
// THE ENTRY POINT (there is no main() in a plugin!)
//
// JUCE's build system (juce_add_plugin in CMakeLists.txt) generates
// format-specific wrapper code: the VST3/CLAP/AU module that actually talks
// to the host. That wrapper knows nothing about our class names — it links
// against this C-style factory and asks it for an AudioProcessor instance.
// After that, the wrapper drives our lifecycle methods (see the contract
// table in MakeGainProcessor.h). If you delete or rename this function,
// the plugin builds fine but the host loads a library that exposes no
// processor — "plugin not found" in Reaper with no compiler error.
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MakeGainProcessor();
}
