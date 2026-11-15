#include "Distortion.h"
#include <cmath>

namespace effecthack
{
namespace
{
float gain(float db) noexcept { return juce::Decibels::decibelsToGain(db); }
}

Distortion::Distortion(DistortionParameters& parametersToUse)
    : parameters(parametersToUse),
      oversampling(1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true)
{
}

void Distortion::prepare(double sampleRate, int maximumBlockSize)
{
    maximumSamples = juce::jmax(1, maximumBlockSize);
    oversampling.initProcessing(static_cast<size_t>(maximumSamples));
    oversampling.reset();
    latency = juce::roundToInt(oversampling.getLatencyInSamples());
    dryDelay.prepare({ sampleRate, static_cast<juce::uint32>(maximumSamples), 1 });
    dryDelay.setDelay(static_cast<float>(latency));
    dryDelay.reset();
    dryBuffer.setSize(1, maximumSamples);
    inputGain.reset(sampleRate, 0.02);
    driveGain.reset(sampleRate * 4.0, 0.02);
    wetAmount.reset(sampleRate, 0.02);
    outputGain.reset(sampleRate, 0.02);
    updateTargets();
    inputGain.setCurrentAndTargetValue(inputGain.getTargetValue());
    driveGain.setCurrentAndTargetValue(driveGain.getTargetValue());
    wetAmount.setCurrentAndTargetValue(wetAmount.getTargetValue());
    outputGain.setCurrentAndTargetValue(outputGain.getTargetValue());
}

void Distortion::updateTargets() noexcept
{
    inputGain.setTargetValue(gain(juce::jlimit(-24.0f, 24.0f, parameters.inputDb.load(std::memory_order_relaxed))));
    driveGain.setTargetValue(gain(juce::jlimit(0.0f, 48.0f, parameters.driveDb.load(std::memory_order_relaxed))));
    outputGain.setTargetValue(gain(juce::jlimit(-60.0f, 0.0f, parameters.outputDb.load(std::memory_order_relaxed))));
    wetAmount.setTargetValue(parameters.bypass.load(std::memory_order_relaxed) ? 0.0f
        : juce::jlimit(0.0f, 1.0f, parameters.mix.load(std::memory_order_relaxed)));
}

void Distortion::process(juce::AudioBuffer<float>& buffer, int numberOfSamples, bool applyOutputStage, bool applyInputStage) noexcept
{
    jassert(numberOfSamples <= maximumSamples && buffer.getNumChannels() >= 1);
    if (numberOfSamples <= 0 || maximumSamples == 0)
        return;

    updateTargets();
    auto* samples = buffer.getWritePointer(0);
    auto* dry = dryBuffer.getWritePointer(0);
    for (int i = 0; i < numberOfSamples; ++i)
    {
        const auto input = std::isfinite(samples[i]) ? samples[i] : 0.0f;
        const auto inputLevel = inputGain.getNextValue();
        samples[i] = input * (applyInputStage ? inputLevel : 1.0f);
        dryDelay.pushSample(0, samples[i]);
        dry[i] = dryDelay.popSample(0);
    }

    auto block = juce::dsp::AudioBlock<float>(buffer).getSubBlock(0, static_cast<size_t>(numberOfSamples));
    auto upsampled = oversampling.processSamplesUp(block);
    auto* wet = upsampled.getChannelPointer(0);
    for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
        wet[i] = std::tanh(wet[i] * driveGain.getNextValue());
    oversampling.processSamplesDown(block);

    for (int i = 0; i < numberOfSamples; ++i)
    {
        const auto mix = wetAmount.getNextValue();
        const auto level = outputGain.getNextValue();
        const auto output = dry[i] + mix * (samples[i] - dry[i]);
        samples[i] = applyOutputStage ? juce::jlimit(-1.0f, 1.0f, output * level) : output;
    }
}
}
