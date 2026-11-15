#include "Compressor.h"
#include <cmath>

namespace effecthack
{
namespace
{
float bounded(float value, float fallback, float minimum, float maximum) noexcept
{
    return std::isfinite(value) ? juce::jlimit(minimum, maximum, value) : fallback;
}
float coefficient(float milliseconds, double rate) noexcept
{
    return static_cast<float>(std::exp(-1.0 / (milliseconds * .001 * rate)));
}
float clean(float value) noexcept { return std::isfinite(value) ? value : 0.0f; }
float reductionDb(float gain) noexcept { return -juce::Decibels::gainToDecibels(gain, -120.0f); }
}
void Compressor::updateTargets() noexcept
{
    threshold.setTargetValue(bounded(parameters.thresholdDb.load(), -18, -60, 0));
    ratio.setTargetValue(bounded(parameters.ratio.load(), 4, 1, 12));
    attack.setTargetValue(coefficient(bounded(parameters.attackMs.load(), 10, .1f, 100), rate));
    release.setTargetValue(coefficient(bounded(parameters.releaseMs.load(), 120, 10, 1000), rate));
    makeup.setTargetValue(juce::Decibels::decibelsToGain(bounded(parameters.makeupDb.load(), 0, 0, 18)));
    mix.setTargetValue(bounded(parameters.mix.load(), 1, 0, 1));
    wet.setTargetValue(parameters.bypass.load() ? 0.0f : 1.0f);
}
void Compressor::prepare(double sampleRate, int)
{
    rate = sampleRate;
    for (auto* smoother : { &threshold, &ratio, &attack, &release, &makeup, &mix, &wet }) smoother->reset(rate, .02);
    updateTargets();
    for (auto* smoother : { &threshold, &ratio, &attack, &release, &makeup, &mix, &wet })
        smoother->setCurrentAndTargetValue(smoother->getTargetValue());
    reset();
}
void Compressor::reset() noexcept
{
    reductionState = 0;
    reduction.store(0, std::memory_order_relaxed);
}
void Compressor::processStereo(juce::AudioBuffer<float>& audio, int count) noexcept
{
    jassert(audio.getNumChannels() == 2);
    updateTargets();
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(1);
    float maximumReduction = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto l = clean(left[i]), r = clean(right[i]);
        const auto peak = juce::jmax(std::abs(l), std::abs(r));
        const auto inputDb = juce::Decibels::gainToDecibels(peak, -120.0f);
        const auto over = inputDb - threshold.getNextValue();
        const auto slope = 1.0f - 1.0f / ratio.getNextValue();
        // A 6 dB soft knee joins unity gain to the selected compression ratio.
        const auto desiredReduction = over <= -3 ? 0.0f : over >= 3 ? over * slope
            : slope * (over + 3) * (over + 3) / 12.0f;
        const auto attackCoefficient = attack.getNextValue(), releaseCoefficient = release.getNextValue();
        const auto smoothing = desiredReduction > reductionState ? attackCoefficient : releaseCoefficient;
        reductionState = desiredReduction + smoothing * (reductionState - desiredReduction);
        const auto compressedGain = juce::Decibels::decibelsToGain(-reductionState);
        const auto makeupGain = makeup.getNextValue();
        const auto amount = mix.getNextValue() * wet.getNextValue();
        const auto applied = 1.0f + amount * (compressedGain * makeupGain - 1.0f);
        left[i] = l * applied; right[i] = r * applied;
        maximumReduction = juce::jmax(maximumReduction, reductionDb(1.0f + amount * (compressedGain - 1.0f)));
    }
    reduction.store(maximumReduction, std::memory_order_relaxed);
}
}
