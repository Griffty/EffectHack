#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "HttpClient.h"

namespace effecthack
{
struct ReferenceAudio
{
    juce::String base64, error;
    double durationSeconds = 0, startSeconds = 0;
};
// Decoding, resampling, and encoding run on a worker, never the live audio thread.
ReferenceAudio prepareAudioReference(const juce::File&, double startSeconds, double endSeconds, const Cancellation&);
ReferenceAudio encodeToneExample(const juce::AudioBuffer<float>&, double sampleRate, const Cancellation&);
}
