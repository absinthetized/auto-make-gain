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
//     thread-safe channels (atomics, ValueTree state — wired when real
//     parameters arrive).
//   * The editor is disposable: hosts destroy/recreate it freely (window
//     close/reopen, project reload). The processor survives.
//
// M0 stub: a fixed-size dark window with a title label. Stock JUCE
// components only (TS §1: no OpenGL); meters/graphs arrive with DSP.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MakeGainProcessor;   // forward declaration — include cycle avoidance

class MakeGainEditor : public juce::AudioProcessorEditor
{
public:
    explicit MakeGainEditor (MakeGainProcessor& p);

    // AudioProcessorEditor's minimal required customization: we draw the
    // whole window ourselves in paint() (custom-component style). The
    // constructor's setSize() defines the plugin window's default size.
    void paint (juce::Graphics&) override;

private:
    // Reference (not pointer!): editors are destroyed with their processor
    // in well-behaved hosts; JUCE's leak detector flags dangling UI state.
    MakeGainProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MakeGainEditor)
};
