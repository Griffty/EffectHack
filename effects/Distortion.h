#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct DistortionParameters
{
    std::atomic<float> inputDb { 0.0f };
    std::atomic<float> driveDb { 18.0f };
    std::atomic<float> mix { 1.0f };
    std::atomic<float> outputDb { 0.0f };
    std::atomic<bool> bypass { false };
};

// Mono soft clipping, with four-times oversampling and a latency-aligned dry path.
// prepare() owns all allocations; process() is called only by the audio thread.
class Distortion final
{
public:
    explicit Distortion(DistortionParameters& parametersToUse);
    void prepare(double sampleRate, int maximumBlockSize);
    // The rack applies its output level after the cabinet and EQ. Standalone
    // callers can keep the original output gain/clipping behavior (default).
    void process(juce::AudioBuffer<float>& monoBuffer, int numberOfSamples, bool applyOutputStage = true, bool applyInputStage = true) noexcept;
    void reset() noexcept { oversampling.reset(); dryDelay.reset(); }
    [[nodiscard]] int latencySamples() const noexcept { return latency; }

    void processRackChannel(juce::AudioBuffer<float>& buffer, int count) noexcept { process(buffer, count, false, false); }

private:
    DistortionParameters& parameters;
    juce::dsp::Oversampling<float> oversampling;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 256 };
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> inputGain, driveGain, wetAmount, outputGain;
    int maximumSamples = 0;
    int latency = 0;
    void updateTargets() noexcept;
};
}
