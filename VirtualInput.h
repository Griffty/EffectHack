#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace effecthack
{
struct AudioClip
{
    juce::AudioBuffer<float> samples;
    double sampleRate = 44100.0;
    juce::String name, sourcePath;
    double duration() const { return samples.getNumSamples() / sampleRate; }
};

struct ClipLoadResult
{
    std::shared_ptr<AudioClip> clip;
    juce::String error;
};

// File decoding and riff generation run outside the audio callback.
/// Decode bounded files off-thread; mono downmix and decoded-memory limits are
/// enforced here. Failure is reported in error rather than replacing a live source.
ClipLoadResult loadAudioClip(const juce::File&);
std::shared_ptr<AudioClip> makeTestRiff();

// Playback uses only preloaded memory, with JUCE's sample-rate conversion.
// Serialize control calls with AudioDeviceManager::getAudioCallbackLock().
class VirtualInput
{
public:
    ~VirtualInput();
    void setClip(std::shared_ptr<AudioClip>);
    void prepare(double deviceSampleRate, int blockSize);
    void release();
    void render(juce::AudioBuffer<float>& mono, int count);
    void resume();
    void seek(double seconds);
    void setLooping(bool);
    bool isLooping() const { return looping; }
    bool isFinished() const { return transport.hasStreamFinished(); }
    bool hasClip() const { return clip != nullptr; }
    double position() const;
    double duration() const { return clip != nullptr ? clip->duration() : 0.0; }
    const AudioClip* getClip() const { return clip.get(); }

private:
    std::shared_ptr<AudioClip> clip;
    std::unique_ptr<juce::MemoryAudioSource> memory;
    juce::AudioTransportSource transport;
    bool looping = true;
};
}
