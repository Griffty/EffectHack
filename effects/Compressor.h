#pragma once
#include "StereoRackStage.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct CompressorParameters
{
    std::atomic<float> thresholdDb { -18 }, ratio { 4 }, attackMs { 10 }, releaseMs { 120 }, makeupDb { 0 }, mix { 1 };
    std::atomic<bool> bypass { true };
};

// A common detector and gain for both channels preserves the stereo image.
// All buffers are supplied by the rack; process() does not allocate.
class Compressor final : public StereoRackStage
{
public:
    explicit Compressor(CompressorParameters& controls) : parameters(controls) {}
    const char* id() const noexcept override { return "compressor"; }
    const char* name() const noexcept override { return "Compressor"; }
    void prepare(double sampleRate, int maximumBlockSize) override;
    void reset() noexcept override;
    float gainReductionDb() const noexcept { return reduction.load(std::memory_order_relaxed); }
protected:
    void processStereo(juce::AudioBuffer<float>&, int count) noexcept override;
private:
    CompressorParameters& parameters;
    juce::SmoothedValue<float> threshold, ratio, attack, release, makeup, mix, wet;
    double rate = 48000;
    float reductionState = 0;
    std::atomic<float> reduction { 0 };
    void updateTargets() noexcept;
};
}
