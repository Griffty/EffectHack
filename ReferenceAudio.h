#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "HttpClient.h"

namespace effecthack
{
/// In-memory 16-bit / 16 kHz mono WAV encoded for a Gemini media request.
/// An error means no usable payload; this is separate from live file playback.
struct ReferenceAudio
{
    juce::String base64, error;
    double durationSeconds = 0, startSeconds = 0;
};
// Decoding, resampling, and encoding run on a worker, never the live audio thread.
ReferenceAudio prepareAudioReference(const juce::File&, double startSeconds, double endSeconds, const Cancellation&);
ReferenceAudio encodeToneExample(const juce::AudioBuffer<float>&, double sampleRate, const Cancellation&);
}
