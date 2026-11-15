#include "Amp.h"
#include <cmath>
#include <numbers>

namespace effecthack
{
namespace
{
float bounded(float value, float fallback, float minimum, float maximum) noexcept
{
    return std::isfinite(value) ? juce::jlimit(minimum, maximum, value) : fallback;
}
}
void Amp::OnePole::configure(double frequency, double rate) noexcept
{
    coefficient = std::exp(-2.0 * std::numbers::pi * juce::jmin(frequency, rate * .4) / rate);
    state = 0;
}
float Amp::OnePole::low(float value) noexcept
{
    state += (1.0 - coefficient) * (value - state);
    return static_cast<float>(state);
}
void Amp::Biquad::set(const std::array<double, 6>& c) noexcept
{
    coefficients = { c[0] / c[3], c[1] / c[3], c[2] / c[3], c[4] / c[3], c[5] / c[3] };
}
float Amp::Biquad::process(float input) noexcept
{
    const auto output = coefficients[0] * input + z1;
    z1 = coefficients[1] * input - coefficients[3] * output + z2;
    z2 = coefficients[2] * input - coefficients[4] * output;
    return static_cast<float>(output);
}
void Amp::Voice::prepare(int voice, double rate)
{
    // Different headroom, interstage filtering, and asymmetric clipping for each voice.
    const double cuts[] { 45, 85, 120 }, interCuts[] { 20, 65, 130 }, rolls[] { 14000, 9500, 7500 };
    stages = voice == 2 ? 3 : 2;
    drive = voice == 0 ? std::array<float, 3> { .6f, .8f, 1 }
          : voice == 1 ? std::array<float, 3> { 2, 2.5f, 1 }
                       : std::array<float, 3> { 3, 3.5f, 2 };
    bias = voice == 0 ? std::array<float, 3> { .01f, .01f, 0 }
         : voice == 1 ? std::array<float, 3> { .12f, .04f, 0 }
                      : std::array<float, 3> { .2f, -.08f, .04f };
    calibration = voice == 0 ? 1.0f : voice == 1 ? .75f : .65f;
    for (size_t i = 0; i < highpass.size(); ++i)
    {
        highpass[i].configure(i == 0 ? cuts[voice] : interCuts[voice], rate);
        lowpass[i].configure(rolls[voice], rate);
        offset[i] = std::tanh(bias[i]);
    }
}
void Amp::Voice::reset() noexcept
{
    for (auto& filter : highpass) filter.state = 0;
    for (auto& filter : lowpass) filter.state = 0;
}
float Amp::Voice::process(float value, float gainValue) noexcept
{
    for (int stage = 0; stage < stages; ++stage)
    {
        const auto index = static_cast<size_t>(stage);
        value = highpass[index].high(value);
        value = std::tanh(value * drive[index] * (stage == 0 ? gainValue : 1.0f) + bias[index]) - offset[index];
        value = lowpass[index].low(value);
    }
    return value * calibration;
}
Amp::Amp(AmpParameters& controls)
    : parameters(controls), oversampling(1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true) {}

void Amp::prepare(double rate, int maximumBlockSize)
{
    capacity = juce::jmax(1, maximumBlockSize);
    internalRate = rate * 4;
    oversampling.initProcessing(static_cast<size_t>(capacity));
    latency = juce::roundToInt(oversampling.getLatencyInSamples());
    dryDelay.prepare({ rate, static_cast<juce::uint32>(capacity), 1 });
    dryDelay.setDelay(static_cast<float>(latency));
    dryBuffer.setSize(1, capacity);
    for (size_t i = 0; i < voices.size(); ++i)
    {
        voices[i].prepare(static_cast<int>(i), internalRate);
        voiceWeights[i].reset(internalRate, .03);
    }
    gain.reset(internalRate, .03);
    for (auto* value : { &bass, &mid, &treble, &presence }) value->reset(internalRate, .04);
    level.reset(rate, .03); wet.reset(rate, .03);
    outputDc.configure(20, internalRate);
    updateTargets();
    for (auto& value : voiceWeights) value.setCurrentAndTargetValue(value.getTargetValue());
    for (auto* value : { &gain, &bass, &mid, &treble, &presence, &level, &wet })
        value->setCurrentAndTargetValue(value->getTargetValue());
    updateTone();
    reset();
}
void Amp::updateTargets() noexcept
{
    const auto selected = juce::jlimit(0, 2, parameters.voice.load(std::memory_order_relaxed));
    for (size_t i = 0; i < voiceWeights.size(); ++i)
        voiceWeights[i].setTargetValue(static_cast<int>(i) == selected ? 1.0f : 0.0f);
    gain.setTargetValue(juce::Decibels::decibelsToGain(bounded(parameters.gainDb.load(), 12, 0, 36)));
    bass.setTargetValue(bounded(parameters.bassDb.load(), 0, -12, 12));
    mid.setTargetValue(bounded(parameters.midDb.load(), 0, -12, 12));
    treble.setTargetValue(bounded(parameters.trebleDb.load(), 0, -12, 12));
    presence.setTargetValue(bounded(parameters.presenceDb.load(), 0, -12, 12));
    level.setTargetValue(juce::Decibels::decibelsToGain(bounded(parameters.levelDb.load(), -6, -24, 6)));
    wet.setTargetValue(parameters.bypass.load() ? 0.0f : 1.0f);
}
void Amp::updateTone() noexcept
{
    using C = juce::dsp::IIR::ArrayCoefficients<double>;
    bassFilter.set(C::makeLowShelf(internalRate, 120, .7071, juce::Decibels::decibelsToGain(static_cast<double>(bass.getCurrentValue()))));
    midFilter.set(C::makePeakFilter(internalRate, 750, .8, juce::Decibels::decibelsToGain(static_cast<double>(mid.getCurrentValue()))));
    trebleFilter.set(C::makeHighShelf(internalRate, 2800, .7071, juce::Decibels::decibelsToGain(static_cast<double>(treble.getCurrentValue()))));
    presenceFilter.set(C::makeHighShelf(internalRate, 4500, .7071, juce::Decibels::decibelsToGain(static_cast<double>(presence.getCurrentValue()))));
}
void Amp::reset() noexcept
{
    oversampling.reset(); dryDelay.reset();
    for (auto& voice : voices) voice.reset();
    for (auto* filter : { &bassFilter, &midFilter, &trebleFilter, &presenceFilter }) filter->reset();
    outputDc.state = 0;
    coefficientCountdown = 0;
}
void Amp::process(juce::AudioBuffer<float>& buffer, int count) noexcept
{
    jassert(buffer.getNumChannels() >= 1 && count <= capacity);
    if (capacity == 0 || count <= 0) return;
    updateTargets();
    auto* samples = buffer.getWritePointer(0);
    auto* dry = dryBuffer.getWritePointer(0);
    for (int i = 0; i < count; ++i)
    {
        if (!std::isfinite(samples[i])) samples[i] = 0;
        dryDelay.pushSample(0, samples[i]); dry[i] = dryDelay.popSample(0);
    }
    auto block = juce::dsp::AudioBlock<float>(buffer).getSubBlock(0, static_cast<size_t>(count));
    auto upsampled = oversampling.processSamplesUp(block);
    auto* data = upsampled.getChannelPointer(0);
    for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
    {
        if (coefficientCountdown == 0)
        {
            bass.skip(64); mid.skip(64); treble.skip(64); presence.skip(64);
            updateTone(); coefficientCountdown = 64;
        }
        --coefficientCountdown;
        const auto drive = gain.getNextValue();
        float value = 0;
        for (size_t voice = 0; voice < voices.size(); ++voice)
            value += voiceWeights[voice].getNextValue() * voices[voice].process(data[i], drive);
        value = presenceFilter.process(trebleFilter.process(midFilter.process(bassFilter.process(value))));
        // Mild output-stage saturation; DC filtering removes asymmetric-stage offsets.
        value = (std::tanh(1.2f * value + .02f) - std::tanh(.02f)) / 1.2f;
        data[i] = outputDc.high(value);
    }
    oversampling.processSamplesDown(block);
    for (int i = 0; i < count; ++i)
    {
        const auto amount = wet.getNextValue();
        const auto output = samples[i] * level.getNextValue();
        samples[i] = dry[i] + amount * (output - dry[i]);
    }
}
}
