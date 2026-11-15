#include "Chorus.h"
#include <cmath>
#include <numbers>

namespace effecthack
{
void Chorus::updateTargets() noexcept
{
    rate.setTargetValue(juce::jlimit(0.1f, 5.0f, parameters.rateHz.load(std::memory_order_relaxed)));
    depth.setTargetValue(juce::jlimit(0.0f, 1.0f, parameters.depth.load(std::memory_order_relaxed)));
    mix.setTargetValue(parameters.bypass.load(std::memory_order_relaxed) ? 0.0f
        : juce::jlimit(0.0f, 1.0f, parameters.mix.load(std::memory_order_relaxed)));
}

void Chorus::prepare(double newSampleRate, int maximumBlockSize)
{
    sampleRate = newSampleRate;
    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32>(maximumBlockSize), 2 };
    delayLine.setMaximumDelayInSamples(static_cast<int>(std::ceil(sampleRate * 0.032)) + 2);
    delayLine.prepare(spec);
    rate.reset(sampleRate, 0.03); depth.reset(sampleRate, 0.03); mix.reset(sampleRate, 0.03);
    updateTargets();
    rate.setCurrentAndTargetValue(rate.getTargetValue());
    depth.setCurrentAndTargetValue(depth.getTargetValue());
    mix.setCurrentAndTargetValue(mix.getTargetValue());
    reset();
}

void Chorus::reset() noexcept
{
    delayLine.reset();
    phase = 0.0;
}

void Chorus::processStereo(juce::AudioBuffer<float>& stereo, int count) noexcept
{
    jassert(stereo.getNumChannels() == 2);
    updateTargets();
    auto* left = stereo.getWritePointer(0);
    auto* right = stereo.getWritePointer(1);
    float* channels[] { left, right };
    for (int i = 0; i < count; ++i)
    {
        const auto amount = depth.getNextValue();
        const auto chorusMix = mix.getNextValue();
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto dry = channels[ch][i];
            const auto modulation = std::sin(phase + ch * std::numbers::pi * 0.5);
            const auto delaySamples = static_cast<float>((11.0 + 4.0 * amount * modulation)
                * sampleRate * 0.001);
            delayLine.pushSample(ch, dry);
            channels[ch][i] = dry + chorusMix * (delayLine.popSample(ch, delaySamples) - dry);
        }
        phase += 2.0 * std::numbers::pi * rate.getNextValue() / sampleRate;
        if (phase >= 2.0 * std::numbers::pi)
            phase -= 2.0 * std::numbers::pi;
    }
}
}
