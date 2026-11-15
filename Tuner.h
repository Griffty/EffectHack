#pragma once
#include "InputMonitor.h"

namespace effecthack
{
// Monophonic YIN difference/normalization estimator. Analysis runs off the audio thread.
float detectPitch(const float* samples, int count, double rate);
struct TuningReading { juce::String note; float cents = 0; };
TuningReading describePitch(float frequency, double referenceA4);

/// Background consumer of InputMonitor. Published frequency is valid only for
/// the current input generation; estimation latency does not delay guitar audio.
class Tuner final : private juce::Thread
{
public:
    explicit Tuner(InputMonitor&);
    ~Tuner() override;
    float frequency() const noexcept;
private:
    InputMonitor& input;
    std::atomic<float> detected { 0 };
    std::atomic<unsigned> detectedGeneration { 0 };
    void run() override;
};
}
