#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct EqualizerParameters
{
    std::atomic<float> bassDb { 0.0f }, midDb { 0.0f }, trebleDb { 0.0f };
    std::atomic<bool> bypass { false };
};

class Equalizer final
{
public:
    explicit Equalizer(EqualizerParameters& controls) : parameters(controls) {}
    void prepare(double sampleRate, int maximumBlockSize);
    void prepare(double sampleRate);
    void process(juce::AudioBuffer<float>& mono, int count) noexcept;
    int latencySamples() const noexcept { return 0; }
    void reset() noexcept
    {
        bassFilter.z1 = bassFilter.z2 = midFilter.z1 = midFilter.z2 = trebleFilter.z1 = trebleFilter.z2 = 0.0;
    }

    void processRackChannel(juce::AudioBuffer<float>& buffer, int count) noexcept { process(buffer, count); }

private:
    struct Biquad
    {
        std::array<double, 5> coefficients { 1.0, 0.0, 0.0, 0.0, 0.0 };
        double z1 = 0.0, z2 = 0.0;
        void set(const std::array<double, 6>& values) noexcept;
        double process(double input) noexcept;
    };
    EqualizerParameters& parameters;
    Biquad bassFilter, midFilter, trebleFilter;
    juce::SmoothedValue<float> bass, mid, treble, wet;
    double rate = 48000.0;
    int updateCountdown = 0;
    void updateCoefficients() noexcept;
};
}
