#pragma once

#include "StereoRackStage.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct ReverbParameters
{
    std::atomic<float> size { 0.45f }, damping { 0.5f }, mix { 0.2f };
    std::atomic<bool> bypass { true };
};

class Reverb final : public StereoRackStage
{
public:
    explicit Reverb(ReverbParameters& parametersToUse) : parameters(parametersToUse) {}
    const char* id() const noexcept override { return "reverb"; }
    const char* name() const noexcept override { return "Reverb"; }
    void prepare(double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override { room.reset(); }

protected:
    void processStereo(juce::AudioBuffer<float>& stereo, int count) noexcept override;

private:
    ReverbParameters& parameters;
    juce::Reverb room;
    juce::AudioBuffer<float> buffer;
    juce::SmoothedValue<float> mix;
    void updateTargets() noexcept;
};
}
