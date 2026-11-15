#include "Delay.h"

namespace effecthack
{
void Delay::updateTargets() noexcept
{
    time.setTargetValue(juce::jlimit(20.0f, 1500.0f, parameters.timeMs.load(std::memory_order_relaxed)));
    feedback.setTargetValue(juce::jlimit(0.0f, 0.8f, parameters.feedback.load(std::memory_order_relaxed)));
    mix.setTargetValue(parameters.bypass.load(std::memory_order_relaxed) ? 0.0f
        : juce::jlimit(0.0f, 1.0f, parameters.mix.load(std::memory_order_relaxed)));
}

void Delay::prepare(double newSampleRate, int maximumBlockSize)
{
    sampleRate = newSampleRate;
    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(maximumBlockSize), 2 };
    delayLine.setMaximumDelayInSamples(static_cast<int>(std::ceil(sampleRate * 1.5)) + 2);
    delayLine.prepare(spec);
    time.reset(sampleRate, 0.05); feedback.reset(sampleRate, 0.03); mix.reset(sampleRate, 0.03);
    updateTargets();
    time.setCurrentAndTargetValue(time.getTargetValue());
    feedback.setCurrentAndTargetValue(feedback.getTargetValue());
    mix.setCurrentAndTargetValue(mix.getTargetValue());
    reset();
}

void Delay::processStereo(juce::AudioBuffer<float>& stereo, int count) noexcept
{
    jassert(stereo.getNumChannels() == 2);
    updateTargets();
    for (int i = 0; i < count; ++i)
    {
        const auto delaySamples = time.getNextValue() * static_cast<float>(sampleRate * 0.001);
        const auto regeneration = feedback.getNextValue();
        const auto delayMix = mix.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* channel = stereo.getWritePointer(ch);
            const auto input = channel[i];
            const auto echo = delayLine.popSample(ch, delaySamples);
            delayLine.pushSample(ch, input + echo * regeneration);
            channel[i] = input + delayMix * (echo - input);
        }
    }
}
}
