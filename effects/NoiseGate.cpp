#include "NoiseGate.h"
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
void NoiseGate::updateTargets() noexcept
{
    threshold.setTargetValue(juce::Decibels::decibelsToGain(bounded(parameters.thresholdDb.load(), -50, -90, 0)));
    attack.setTargetValue(coefficient(bounded(parameters.attackMs.load(), 1, .1f, 20), rate));
    release.setTargetValue(coefficient(bounded(parameters.releaseMs.load(), 120, 10, 1000), rate));
    wet.setTargetValue(parameters.bypass.load() ? 0.0f : 1.0f);
}
void NoiseGate::prepare(double sampleRate, int)
{
    rate = sampleRate;
    for (auto* smoother : { &threshold, &attack, &release, &wet }) smoother->reset(rate, .02);
    detectorAttack = coefficient(.1f, rate);
    detectorRelease = coefficient(20, rate);
    updateTargets();
    for (auto* smoother : { &threshold, &attack, &release, &wet })
        smoother->setCurrentAndTargetValue(smoother->getTargetValue());
    reset();
}
void NoiseGate::reset() noexcept
{
    detector = 0; gain = 0; holdRemaining = 0; opened = false;
    reduction.store(0, std::memory_order_relaxed);
}
void NoiseGate::processStereo(juce::AudioBuffer<float>& audio, int count) noexcept
{
    jassert(audio.getNumChannels() == 2);
    updateTargets();
    const auto holdSamples = juce::roundToInt(bounded(parameters.holdMs.load(), 30, 0, 250) * .001 * rate);
    auto* left = audio.getWritePointer(0);
    auto* right = audio.getWritePointer(1);
    float minimumGain = 1;
    for (int i = 0; i < count; ++i)
    {
        const auto l = clean(left[i]), r = clean(right[i]);
        const auto peak = juce::jmax(std::abs(l), std::abs(r));
        const auto detectorCoefficient = peak > detector ? detectorAttack : detectorRelease;
        detector = peak + detectorCoefficient * (detector - peak);
        const auto openingThreshold = threshold.getNextValue();
        if (!opened && detector >= openingThreshold) opened = true;
        if (opened)
        {
            // The closing threshold is 6 dB below the opening threshold.
            if (detector >= openingThreshold * .50118723f) holdRemaining = holdSamples;
            else if (holdRemaining > 0) --holdRemaining;
            else opened = false;
        }
        const auto attackCoefficient = attack.getNextValue(), releaseCoefficient = release.getNextValue();
        const auto target = opened ? 1.0f : 0.0f;
        gain = target + (opened ? attackCoefficient : releaseCoefficient) * (gain - target);
        if (gain < 1.e-7f) gain = 0;
        else if (gain > .999999f) gain = 1;
        const auto applied = 1.0f + wet.getNextValue() * (gain - 1.0f);
        left[i] = l * applied; right[i] = r * applied;
        minimumGain = juce::jmin(minimumGain, applied);
    }
    reduction.store(reductionDb(minimumGain), std::memory_order_relaxed);
}
}
