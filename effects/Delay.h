#pragma once

#include "StereoRackStage.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct DelayParameters
{
    std::atomic<float> timeMs { 350.0f }, feedback { 0.3f }, mix { 0.2f };
    std::atomic<bool> bypass { true };
};

class Delay final : public StereoRackStage
{
public:
    explicit Delay(DelayParameters& parametersToUse) : parameters(parametersToUse) {}
    const char* id() const noexcept override { return "delay"; }
    const char* name() const noexcept override { return "Delay"; }
    void prepare(double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override { delayLine.reset(); }

protected:
    void processStereo(juce::AudioBuffer<float>& stereo, int count) noexcept override;

private:
    DelayParameters& parameters;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::SmoothedValue<float> time, feedback, mix;
    double sampleRate = 48000.0;
    void updateTargets() noexcept;
};
}
