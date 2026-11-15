#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace effecthack
{
struct CabinetParameters { std::atomic<bool> bypass { false }; };

/// Prepared response shared read-only by the two channel processors. sourcePath
/// is empty for the built-in response; custom preset files retain only its path.
struct CabinetImpulse
{
    juce::AudioBuffer<float> samples;
    double sampleRate = 48000.0;
    juce::String name, sourcePath;
    bool normalise = false;
};

struct CabinetLoadResult
{
    std::shared_ptr<CabinetImpulse> impulse;
    juce::String error;
};

std::shared_ptr<CabinetImpulse> makeBuiltinCabinet();
/// Decode and validate an IR off-thread before attempting to replace live DSP.
CabinetLoadResult loadCabinetImpulse(const juce::File&);

class Cabinet final
{
public:
    explicit Cabinet(CabinetParameters& controls);
    // Call outside the audio callback, serialized with the manager's callback lock.
    void setImpulse(std::shared_ptr<const CabinetImpulse>);
    void prepare(double sampleRate, int maximumBlockSize);
    void process(juce::AudioBuffer<float>& mono, int count) noexcept;
    void reset() noexcept { convolution.reset(); }
    juce::String sourcePath() const { return impulse != nullptr ? impulse->sourcePath : juce::String(); }
    std::shared_ptr<const CabinetImpulse> currentImpulse() const { return impulse; }
    int latencySamples() const noexcept { return convolution.getLatency(); }

    void processRackChannel(juce::AudioBuffer<float>& buffer, int count) noexcept { process(buffer, count); }

private:
    CabinetParameters& parameters;
    std::shared_ptr<const CabinetImpulse> impulse;
    juce::dsp::Convolution convolution;
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float> wet;
    void queueImpulse();
};
}
