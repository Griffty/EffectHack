#include "Equalizer.h"

namespace effecthack
{
void Equalizer::Biquad::set(const std::array<double, 6>& values) noexcept
{
    coefficients = { values[0] / values[3], values[1] / values[3], values[2] / values[3],
                     values[4] / values[3], values[5] / values[3] };
}

double Equalizer::Biquad::process(double input) noexcept
{
    const auto output = coefficients[0] * input + z1;
    z1 = coefficients[1] * input - coefficients[3] * output + z2;
    z2 = coefficients[2] * input - coefficients[4] * output;
    return output;
}

void Equalizer::prepare(double sampleRate, int)
{
    prepare(sampleRate);
}

void Equalizer::prepare(double sampleRate)
{
    rate = sampleRate;
    bassFilter = {}; midFilter = {}; trebleFilter = {};
    bass.reset(rate, 0.03); mid.reset(rate, 0.03); treble.reset(rate, 0.03); wet.reset(rate, 0.02);
    bass.setCurrentAndTargetValue(juce::jlimit(-12.0f, 12.0f, parameters.bassDb.load()));
    mid.setCurrentAndTargetValue(juce::jlimit(-12.0f, 12.0f, parameters.midDb.load()));
    treble.setCurrentAndTargetValue(juce::jlimit(-12.0f, 12.0f, parameters.trebleDb.load()));
    wet.setCurrentAndTargetValue(parameters.bypass.load() ? 0.0f : 1.0f);
    updateCountdown = 0;
    updateCoefficients();
}

void Equalizer::updateCoefficients() noexcept
{
    using Coefficients = juce::dsp::IIR::ArrayCoefficients<double>;
    bassFilter.set(Coefficients::makeLowShelf(rate, juce::jmin(120.0, rate * 0.4), 0.7071,
        juce::Decibels::decibelsToGain(static_cast<double>(bass.getCurrentValue()))));
    midFilter.set(Coefficients::makePeakFilter(rate, juce::jmin(750.0, rate * 0.4), 0.8,
        juce::Decibels::decibelsToGain(static_cast<double>(mid.getCurrentValue()))));
    trebleFilter.set(Coefficients::makeHighShelf(rate, juce::jmin(3200.0, rate * 0.4), 0.7071,
        juce::Decibels::decibelsToGain(static_cast<double>(treble.getCurrentValue()))));
}

void Equalizer::process(juce::AudioBuffer<float>& mono, int count) noexcept
{
    bass.setTargetValue(juce::jlimit(-12.0f, 12.0f, parameters.bassDb.load(std::memory_order_relaxed)));
    mid.setTargetValue(juce::jlimit(-12.0f, 12.0f, parameters.midDb.load(std::memory_order_relaxed)));
    treble.setTargetValue(juce::jlimit(-12.0f, 12.0f, parameters.trebleDb.load(std::memory_order_relaxed)));
    wet.setTargetValue(parameters.bypass.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
    auto* samples = mono.getWritePointer(0);
    for (int i = 0; i < count; ++i)
    {
        if (updateCountdown == 0)
        {
            bass.skip(16); mid.skip(16); treble.skip(16);
            updateCoefficients();
            updateCountdown = 16;
        }
        --updateCountdown;
        const auto dry = samples[i];
        const auto filtered = static_cast<float>(trebleFilter.process(midFilter.process(bassFilter.process(dry))));
        samples[i] = dry + wet.getNextValue() * (filtered - dry);
    }
}
}
