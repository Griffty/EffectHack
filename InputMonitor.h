#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

namespace effecthack
{
struct InputLevels { float rawPeak = 0, trimmedPeak = 0, rms = 0; };
struct TunerSample { float value = 0; unsigned generation = 0; };

// One audio-thread producer, one tuner-thread consumer. Storage is fixed and
// neither preparation nor source changes reset the FIFO while it is in use.
class InputMonitor
{
public:
    static constexpr double tunerSampleRate = 12000;
    void prepare(double sampleRate);
    void beginBlock(bool sourceActive, int sourceIdentity, bool tune);
    void push(float raw, float trimmed) noexcept;
    void publish() noexcept;
    void stop();
    InputLevels levels() const noexcept;
    unsigned generation() const noexcept { return streamGeneration.load(); }
    bool active() const noexcept { return streamActive.load(); }
    /// Tuner-thread consumer only. Generation tags let the worker reject samples
    /// from previous sources without resetting the FIFO concurrently.
    int read(TunerSample* destination, int capacity) noexcept;
private:
    static constexpr int capacity = 4096;
    juce::AbstractFifo fifo { capacity };
    std::array<TunerSample, capacity> buffer {};
    std::atomic<unsigned> streamGeneration { 1 };
    std::atomic<bool> streamActive { false };
    std::atomic<float> publishedRaw { 0 }, publishedTrim { 0 }, publishedRms { 0 };
    double step = .25, phase = 0;
    float decay = 0, rmsDecay = 0, filterCoefficient = 0;
    float rawPeak = 0, trimmedPeak = 0, energy = 0, low1 = 0, low2 = 0, previous = 0;
    int identity = -1;
    bool tuning = false;
    void newStream();
};
}
