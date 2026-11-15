#include "Reverb.h"

namespace effecthack
{
void Reverb::updateTargets() noexcept
{
    mix.setTargetValue(parameters.bypass.load(std::memory_order_relaxed) ? 0.0f
        : juce::jlimit(0.0f, 1.0f, parameters.mix.load(std::memory_order_relaxed)));
    juce::Reverb::Parameters settings;
    settings.roomSize = juce::jlimit(0.0f, 0.95f, parameters.size.load(std::memory_order_relaxed));
    settings.damping = juce::jlimit(0.0f, 1.0f, parameters.damping.load(std::memory_order_relaxed));
    settings.wetLevel = 1.0f;
    settings.dryLevel = 0.0f;
    settings.width = 1.0f;
    room.setParameters(settings);
}

void Reverb::prepare(double sampleRate, int maximumBlockSize)
{
    buffer.setSize(2, maximumBlockSize);
    room.setSampleRate(sampleRate);
    mix.reset(sampleRate, 0.03);
    updateTargets();
    mix.setCurrentAndTargetValue(mix.getTargetValue());
    reset();
}

void Reverb::processStereo(juce::AudioBuffer<float>& stereo, int count) noexcept
{
    jassert(stereo.getNumChannels() == 2 && count <= buffer.getNumSamples());
    updateTargets();
    buffer.copyFrom(0, 0, stereo, 0, 0, count);
    buffer.copyFrom(1, 0, stereo, 1, 0, count);
    room.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), count);
    for (int i = 0; i < count; ++i)
    {
        const auto amount = mix.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
            stereo.addSample(ch, i, amount * (buffer.getSample(ch, i) - stereo.getSample(ch, i)));
    }
}
}
