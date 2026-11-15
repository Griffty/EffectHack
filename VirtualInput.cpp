#include "VirtualInput.h"
#include <cmath>
#include <random>
#include <vector>

namespace effecthack
{
ClipLoadResult loadAudioClip(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return { {}, "Could not read that audio file. Try WAV, FLAC, MP3, AIFF, or Ogg." };
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return { {}, "This file does not contain playable audio." };
    if (reader->numChannels > 2)
        return { {}, "Choose a mono or stereo audio file." };
    const auto channelCount = static_cast<int>(reader->numChannels);
    constexpr juce::int64 maximumBytes = 128 * 1024 * 1024;
    constexpr int decodeBlockSize = 8192;
    if (reader->lengthInSamples / reader->sampleRate > 600.0
        || reader->lengthInSamples > maximumBytes / static_cast<int>(sizeof(float)) - decodeBlockSize * channelCount)
        return { {}, "Choose a shorter clip (up to 10 minutes and 128 MB of decoded audio)." };

    const auto count = static_cast<int>(reader->lengthInSamples);
    // Keep only the mono clip plus a small decode window in memory. Holding the
    // entire stereo recording as well used to reject ordinary four-minute songs.
    juce::AudioBuffer<float> decoded(channelCount, decodeBlockSize);
    auto result = std::make_shared<AudioClip>();
    result->name = file.getFileName();
    result->sourcePath = file.getFullPathName();
    result->sampleRate = reader->sampleRate;
    result->samples.setSize(1, count);
    auto* out = result->samples.getWritePointer(0);
    for (int offset = 0; offset < count; offset += decodeBlockSize)
    {
        const auto blockSize = juce::jmin(decodeBlockSize, count - offset);
        if (!reader->read(&decoded, 0, blockSize, offset, true, true))
            return { {}, "The audio file could not be fully decoded." };
        for (int i = 0; i < blockSize; ++i)
        {
            const auto value = channelCount == 2 ? 0.5f * (decoded.getSample(0, i) + decoded.getSample(1, i))
                                                 : decoded.getSample(0, i);
            out[offset + i] = std::isfinite(value) ? value : 0.0f;
        }
    }
    return { std::move(result), {} };
}

std::shared_ptr<AudioClip> makeTestRiff()
{
    auto result = std::make_shared<AudioClip>();
    result->name = "Test riff - generated plucked strings";
    result->sampleRate = 44100.0;
    result->samples.setSize(1, 8 * 44100);
    result->samples.clear();
    std::mt19937 random(42);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    auto pluck = [&](int midiNote, int start, float level)
    {
        const auto frequency = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
        const auto period = juce::jmax(2, juce::roundToInt(result->sampleRate / frequency));
        std::vector<float> string(static_cast<size_t>(period));
        for (auto& sample : string)
            sample = noise(random);
        const int length = juce::roundToInt(result->sampleRate * 0.46);
        for (int i = 0; i < length; ++i)
        {
            const auto index = static_cast<size_t>(i % period);
            const auto next = static_cast<size_t>((i + 1) % period);
            const auto value = string[index];
            string[index] = 0.498f * (string[index] + string[next]);
            const auto attack = juce::jmin(1.0f, static_cast<float>(i) / 100.0f);
            const auto release = juce::jmin(1.0f, static_cast<float>(length - i - 1) / 300.0f);
            result->samples.addSample(0, start + i, value * level * attack * release);
        }
    };
    const int notes[] { 40, 40, 43, 45, 40, 47, 45, 43, 40, 43, 45, 47, 40, 43, 45, 40 };
    for (int step = 0; step < 16; ++step)
    {
        pluck(notes[step], step * 22050, 1.0f);
        if (step >= 8)
        {
            pluck(notes[step] + 7, step * 22050, 0.55f);
            pluck(notes[step] + 12, step * 22050, 0.35f);
        }
    }
    const auto peak = result->samples.getMagnitude(0, result->samples.getNumSamples());
    if (peak > 0.0f)
        result->samples.applyGain(0.35f / peak);
    return result;
}

VirtualInput::~VirtualInput() { transport.setSource(nullptr); }

void VirtualInput::setClip(std::shared_ptr<AudioClip> newClip)
{
    transport.setSource(nullptr);
    memory.reset();
    clip = std::move(newClip);
    if (clip != nullptr)
    {
        memory = std::make_unique<juce::MemoryAudioSource>(clip->samples, false, looping);
        transport.setSource(memory.get(), 0, nullptr, clip->sampleRate, 1);
        transport.start();
    }
}

void VirtualInput::prepare(double deviceSampleRate, int blockSize)
{
    transport.prepareToPlay(blockSize, deviceSampleRate);
    if (memory != nullptr)
    {
        // Recreate the resampler at the new device rate, allocating its full read
        // buffer now rather than allowing it to grow on the first audio callback.
        transport.setSource(memory.get(), 0, nullptr, clip->sampleRate, 1);
        transport.start();
    }
    transport.setPosition(0.0);
}

void VirtualInput::release() { transport.releaseResources(); }

void VirtualInput::render(juce::AudioBuffer<float>& mono, int count)
{
    transport.getNextAudioBlock({ &mono, 0, count });
}

void VirtualInput::resume()
{
    if (isFinished())
        seek(0.0);
    transport.start();
}

void VirtualInput::seek(double seconds)
{
    transport.setPosition(juce::jlimit(0.0, duration(), seconds));
    transport.start();
}

void VirtualInput::setLooping(bool shouldLoop)
{
    looping = shouldLoop;
    if (memory != nullptr)
        memory->setLooping(looping);
}

double VirtualInput::position() const
{
    if (duration() <= 0.0)
        return 0.0;
    const auto seconds = transport.getCurrentPosition();
    return looping ? std::fmod(seconds, duration()) : juce::jmin(seconds, duration());
}
}
