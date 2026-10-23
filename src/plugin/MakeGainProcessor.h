// MakeGainProcessor.h — the plugin's host-facing shell.
//
// PB2f state: the A2 architecture is wired — ONE plugin, TWO input
// buses (FR §10.1):
//
//   bus 0 "Input"       — the wet signal (post-chain, at this plugin's
//                         position in the chain)
//   bus 1 "Aux (dry tap)" — the pre-chain dry tap, routed by the host
//                         (Reaper channel 3/4 feed or the shipped TAP)
//
// The bus rule (A2, UX-Q5): the aux bus mirrors the MAIN's channel
// count — a mono track gets a mono main + a mono aux (pin 2 in
// Reaper), a stereo track gets stereo main + stereo aux (pins 3/4).
// The engine itself is channel-count agnostic: both buses are folded
// to mono sums here before feeding the GainComputer.
//
// JUCE orientation (what this file is, in framework terms):
//
// A DAW plugin is NOT an application: there is no main(). The host (Reaper,
// Ardour...) loads a binary library and drives it through the format's ABI
// (VST3, CLAP, AU). JUCE hides that protocol behind one class:
// juce::AudioProcessor — we subclass it, and JUCE's generated glue
// ("wrapper") for each format translates the host's calls into our
// virtual methods. This ONE class therefore serves every format we build.
//
// Lifecycle contract (who calls what, when, on which thread):
//   prepareToPlay()    — host (before audio starts): pass sample rate +
//                        max block size; allocate everything memory-heavy here.
//   processBlock()     — host, on its REAL-TIME audio thread, in smaller
//                        blocks: must be allocation-free and lock-free, or
//                        the host's audio stalls (audible dropouts/clicks).
//   releaseResources() — host (after/before config change): free things.
//   getStateInformation()/setStateInformation() — host, saving/loading the
//                        session (FR-22, narrowed by A2: parameters only).
//
// Parameters (A2): Active is the only automatable parameter (UX-Q4);
// Metric/MaxComp/Ramp follow as UI-only parameters in PR-B3.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

#include "dsp/gain_computer.h"

class MakeGainProcessor : public juce::AudioProcessor
{
public:
    MakeGainProcessor();

    // --- host-called audio lifecycle (see the file-header contract) ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // --- editor (UI) glue: the window peer lives in src/ui/ ---
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- small facts the host likes to ask ---
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Program interface — JUCE's preset-slot API. Even a no-parameter plugin
    // must implement it (pure virtuals), hence the trivial "one empty
    // program" stub. Real presets are a non-goal for v1.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // --- host configuration negotiation ---
    // Before the host fixes our channel layout it offers candidates; we
    // accept exactly what the A2 bus rule allows:
    //   output == main input (mono→mono or stereo→stereo), and the aux
    //   input mirrors the MAIN's channel count (mono main ⇒ mono aux).
    // Anything else (asymmetric pairs, 3+ channel sets) is rejected so the
    // host grays the bad routings out instead of misbehaving at runtime.
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    // FR-22 (A2-narrowed): parameter state only.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Status snapshot for the editor (PB2f: one status line; the real
    // readout panel arrives in PR-B3). Reads ONLY atomics — legal from
    // the message thread while audio runs.
    const juce::String statusLine() const;

    double uiAppliedDb() const { return uiAppliedDb_.load (std::memory_order_relaxed); }
    double uiDeltaDb()  const { return uiDeltaDb_.load (std::memory_order_relaxed); }
    bool   uiDeltaValid() const { return uiDeltaValid_.load (std::memory_order_relaxed); }

    // Editor-only access to the parameter tree (attachments bind here).
    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }

    // Stage-2 parameter crossing: APVTS atomics → engine setters; called
    // on prepareToPlay and every processBlock (audio thread).
    void crossParameters();

private:
    // Parameter layout — the single source of parameter IDs for the whole
    // plugin (APVTS owns the params; the editor attaches to them).
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // AudioProcessorValueTreeState (APVTS): JUCE's standard parameter +
    // persistence machinery. "In framework terms": the host asks us for a
    // list of parameters; each is exposed as an atomic<float> the audio
    // thread reads lock-free, while the UI talks to juce::RangedAudioParameter
    // objects on the message thread. The APVTS serializes both sides into
    // one ValueTree — which is what a DAW actually stores in the session.
    juce::AudioProcessorValueTreeState apvts
        { *this, nullptr, "STATE", createParameterLayout() };

    std::atomic<float>* activeParam = nullptr;
    std::atomic<float>* metricParam  = nullptr;   // 0 = LUFS, 1 = dB RMS
    std::atomic<float>* maxcompParam = nullptr;   // 0..48 dB
    std::atomic<float>* ramptcParam  = nullptr;   // seconds

    // The A2 delta engine. Unique pointer (not member) because the sample
    // rate is only known at prepareToPlay: the engine owns two meters that
    // are rate-fixed, so a rate change means a fresh engine (FR-6's
    // per-rate K-weighting, now inside the meters).
    std::unique_ptr<makegain::dsp::GainComputer> machine;
    double machineRate = 0.0;

    // Scratch: mono sums of each bus + the applied-gain dB trajectory.
    // Allocated once in prepareToPlay; processBlock never allocates.
    juce::AudioBuffer<float> workBuf;

    // UI-facing atomics, written on the audio thread each block.
    std::atomic<bool> uiClamped { false };
    std::atomic<bool> uiNoSignalWet { false };
    std::atomic<bool> uiDryTapMissing { false };
    std::atomic<bool>  uiDeltaValid_ { false };
    std::atomic<double> uiAppliedDb_ { 0.0 };   // applied gain readout (FR §10.6)
    std::atomic<double> uiDeltaDb_ { 0.0 };     // chain delta readout

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainProcessor)
};
