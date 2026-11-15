#pragma once
#include "StereoRackStage.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct NoiseGateParameters
{
    std::atomic<float> thresholdDb { -50 }, attackMs { 1 }, holdMs { 30 }, releaseMs { 120 };
    std::atomic<bool> bypass { true };
};

// A common detector and gain for both channels preserves the stereo image.
// All buffers are supplied by the rack; process() does not allocate.
class NoiseGate final : public StereoRackStage
{
public:
    explicit NoiseGate(NoiseGateParameters& controls) : parameters(controls) {}
    const char* id() const noexcept override { return "gate"; }
    const char* name() const noexcept override { return "Noise Gate"; }
    void prepare(double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    float gainReductionDb() const noexcept { return reduction.load(std::memory_order_relaxed); }
protected:
    void processStereo(juce::AudioBuffer<float>&, int count) noexcept override;
private:
    NoiseGateParameters& parameters;
    juce::SmoothedValue<float> threshold, attack, release, wet;
    double rate = 48000;
    float detector = 0, gain = 0, detectorAttack = 0, detectorRelease = 0;
    int holdRemaining = 0;
    bool opened = false;
    std::atomic<float> reduction { 0 };
    void updateTargets() noexcept;
};
}
