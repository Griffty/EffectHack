#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "../RackDefinition.h"

namespace effecthack
{
struct AmpParameters
{
    std::atomic<int> voice { static_cast<int>(AmpVoice::crunch) };
    std::atomic<float> gainDb { 12 }, bassDb { 0 }, midDb { 0 }, trebleDb { 0 }, presenceDb { 0 }, levelDb { -6 };
    std::atomic<bool> bypass { true };
};

// An original amp-style model: filtered preamp stages, a tone section, and mild
// output saturation. Used twice by ChannelWiseRackStage for independent L/R state.
class Amp final
{
public:
    explicit Amp(AmpParameters&);
    void prepare(double sampleRate, int maximumBlockSize);
    void process(juce::AudioBuffer<float>& mono, int count) noexcept;
    void processRackChannel(juce::AudioBuffer<float>& mono, int count) noexcept { process(mono, count); }
    void reset() noexcept;
    int latencySamples() const noexcept { return latency; }
private:
    struct OnePole
    {
        double coefficient = 0, state = 0;
        void configure(double frequency, double rate) noexcept;
        float low(float value) noexcept;
        float high(float value) noexcept { return value - low(value); }
    };
    struct Biquad
    {
        std::array<double, 5> coefficients { 1, 0, 0, 0, 0 };
        double z1 = 0, z2 = 0;
        void set(const std::array<double, 6>&) noexcept;
        float process(float) noexcept;
        void reset() noexcept { z1 = z2 = 0; }
    };
    struct Voice
    {
        std::array<OnePole, 3> highpass, lowpass;
        std::array<float, 3> drive {}, bias {}, offset {};
        float calibration = 1;
        int stages = 2;
        void prepare(int voice, double rate);
        void reset() noexcept;
        float process(float input, float gain) noexcept;
    };
    AmpParameters& parameters;
    juce::dsp::Oversampling<float> oversampling;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 256 };
    juce::AudioBuffer<float> dryBuffer;
    std::array<Voice, 3> voices;
    std::array<juce::SmoothedValue<float>, 3> voiceWeights;
    juce::SmoothedValue<float> gain, bass, mid, treble, presence, level, wet;
    Biquad bassFilter, midFilter, trebleFilter, presenceFilter;
    OnePole outputDc;
    int capacity = 0, latency = 0, coefficientCountdown = 0;
    double internalRate = 192000;
    void updateTargets() noexcept;
    void updateTone() noexcept;
};
}
