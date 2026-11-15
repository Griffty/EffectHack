#include "Tuner.h"
#include <cmath>

namespace effecthack
{
float detectPitch(const float* samples, int count, double rate)
{
    if (count < 1024 || count > 2048 || !std::isfinite(rate) || rate <= 0) return 0;
    const auto maximum = juce::jmin(count / 3, static_cast<int>(rate / 40));
    const auto minimum = juce::jmax(2, static_cast<int>(rate / 1400));
    if (minimum >= maximum) return 0;
    double sum = 0, energy = 0;
    for (int i = 0; i < count; ++i)
    {
        if (!std::isfinite(samples[i])) return 0;
        sum += samples[i]; energy += static_cast<double>(samples[i]) * samples[i];
    }
    if (energy / count - std::pow(sum / count, 2) < 1.0e-7) return 0;
    std::array<double, 684> difference {};
    double accumulated = 0;
    difference[0] = 1;
    const auto window = count - maximum;
    // YIN: squared differences followed by cumulative mean normalization.
    // https://www.ee.columbia.edu/~dpwe/papers/deChevK02-yin.pdf
    for (int lag = 1; lag <= maximum; ++lag)
    {
        double value = 0;
        for (int i = 0; i < window; ++i)
        {
            const auto delta = static_cast<double>(samples[i]) - samples[i + lag];
            value += delta * delta;
        }
        accumulated += value;
        difference[static_cast<size_t>(lag)] = accumulated > 0 ? value * lag / accumulated : 1;
    }
    int candidate = 0;
    for (int lag = minimum; lag < maximum; ++lag)
        if (difference[static_cast<size_t>(lag)] < .12)
        {
            while (lag < maximum && difference[static_cast<size_t>(lag + 1)] < difference[static_cast<size_t>(lag)]) ++lag;
            candidate = lag; break;
        }
    if (candidate == 0 || candidate >= maximum) return 0;
    const auto left = difference[static_cast<size_t>(candidate - 1)], middle = difference[static_cast<size_t>(candidate)],
        right = difference[static_cast<size_t>(candidate + 1)];
    const auto denominator = left - 2 * middle + right;
    const auto offset = std::abs(denominator) > 1.0e-12 ? juce::jlimit(-1.0, 1.0, .5 * (left - right) / denominator) : 0;
    const auto hz = rate / (candidate + offset);
    return hz >= 40 && hz <= 1400 ? static_cast<float>(hz) : 0;
}
TuningReading describePitch(float frequency, double referenceA4)
{
    if (!std::isfinite(frequency) || frequency <= 0 || !std::isfinite(referenceA4)
        || referenceA4 < 430 || referenceA4 > 450) return {};
    const auto midi = 69.0 + 12.0 * std::log2(frequency / referenceA4);
    const auto note = juce::roundToInt(midi);
    constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return { juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1),
             static_cast<float>(100 * (midi - note)) };
}
Tuner::Tuner(InputMonitor& monitor) : juce::Thread("Guitar tuner"), input(monitor) { startThread(); }
Tuner::~Tuner() { signalThreadShouldExit(); notify(); stopThread(-1); }
float Tuner::frequency() const noexcept
{
    const auto generation = input.generation();
    return input.active() && detectedGeneration.load() == generation ? detected.load() : 0;
}
void Tuner::run()
{
    std::array<TunerSample, 4096> incoming;
    std::array<float, 2048> window {}, chronological {};
    unsigned generation = 0;
    int position = 0, filled = 0, sinceAnalysis = 0;
    while (!threadShouldExit())
    {
        const auto count = input.read(incoming.data(), static_cast<int>(incoming.size()));
        for (int i = 0; i < count; ++i)
        {
            const auto& sample = incoming[static_cast<size_t>(i)];
            if (sample.generation != generation)
            { generation = sample.generation; position = filled = sinceAnalysis = 0; }
            window[static_cast<size_t>(position)] = sample.value;
            position = (position + 1) % static_cast<int>(window.size());
            filled = juce::jmin(filled + 1, static_cast<int>(window.size())); ++sinceAnalysis;
        }
        if (filled == static_cast<int>(window.size()) && sinceAnalysis >= 768)
        {
            sinceAnalysis = 0;
            for (size_t i = 0; i < window.size(); ++i) chronological[i] = window[(i + static_cast<size_t>(position)) % window.size()];
            const auto hz = detectPitch(chronological.data(), static_cast<int>(chronological.size()), InputMonitor::tunerSampleRate);
            detected = hz; detectedGeneration = generation;
        }
        wait(20);
    }
}
}
