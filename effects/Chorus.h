#pragma once

#include "StereoRackStage.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct ChorusParameters
{
    std::atomic<float> rateHz { 0.8f }, depth { 0.35f }, mix { 0.25f };
    std::atomic<bool> bypass { true };
};

/// Stereo modulated delay with a quarter-cycle L/R phase offset. Parameters use
/// Hz and normalized depth/mix. Bypass ramps mix to zero while history continues.
class Chorus final : public StereoRackStage
{
public:
    explicit Chorus(ChorusParameters& parametersToUse) : parameters(parametersToUse) {}
    const char* id() const noexcept override { return "chorus"; }
    const char* name() const noexcept override { return "Chorus"; }
    void prepare(double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;

protected:
    void processStereo(juce::AudioBuffer<float>& stereo, int count) noexcept override;

private:
    ChorusParameters& parameters;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine;
    juce::SmoothedValue<float> rate, depth, mix;
    double sampleRate = 48000.0;
    double phase = 0.0;
    void updateTargets() noexcept;
};
}
