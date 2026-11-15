#include "InputMonitor.h"
#include <cmath>

namespace effecthack
{
void InputMonitor::newStream()
{
    streamGeneration.fetch_add(1);
    phase = 0; low1 = low2 = previous = 0;
    rawPeak = trimmedPeak = energy = 0;
    publish();
}
void InputMonitor::prepare(double sampleRate)
{
    const auto rate = std::isfinite(sampleRate) && sampleRate >= 8000 ? sampleRate : 48000;
    step = tunerSampleRate / rate;
    decay = static_cast<float>(std::exp(-1.0 / (.35 * rate)));
    rmsDecay = static_cast<float>(std::exp(-1.0 / (.1 * rate)));
    filterCoefficient = static_cast<float>(1.0 - std::exp(-juce::MathConstants<double>::twoPi * 1800.0 / rate));
    stop();
}
void InputMonitor::stop()
{
    streamActive = false; identity = -1; tuning = false;
    newStream();
}
void InputMonitor::beginBlock(bool sourceActive, int sourceIdentity, bool tune)
{
    if (sourceActive != streamActive.load() || sourceIdentity != identity || tune != tuning)
    {
        identity = sourceIdentity; tuning = tune;
        newStream();
        streamActive = sourceActive;
    }
}
void InputMonitor::push(float raw, float trimmed) noexcept
{
    if (!streamActive.load(std::memory_order_relaxed)) return;
    raw = std::isfinite(raw) ? juce::jlimit(-8.0f, 8.0f, raw) : 0;
    trimmed = std::isfinite(trimmed) ? juce::jlimit(-128.0f, 128.0f, trimmed) : 0;
    rawPeak = juce::jmax(std::abs(raw), rawPeak * decay);
    trimmedPeak = juce::jmax(std::abs(trimmed), trimmedPeak * decay);
    energy = rmsDecay * energy + (1 - rmsDecay) * raw * raw;
    if (!tuning) return;
    low1 += filterCoefficient * (raw - low1);
    low2 += filterCoefficient * (low1 - low2);
    phase += step;
    while (phase >= 1)
    {
        const auto value = low2 + static_cast<float>((phase - 1) / step) * (previous - low2);
        phase -= 1;
        int first = 0, firstSize = 0, second = 0, secondSize = 0;
        fifo.prepareToWrite(1, first, firstSize, second, secondSize);
        if (firstSize > 0) { buffer[static_cast<size_t>(first)] = { value, streamGeneration.load() }; fifo.finishedWrite(1); }
    }
    previous = low2;
}
void InputMonitor::publish() noexcept
{
    publishedRaw = rawPeak; publishedTrim = trimmedPeak; publishedRms = std::sqrt(energy);
}
InputLevels InputMonitor::levels() const noexcept
{
    return { publishedRaw.load(), publishedTrim.load(), publishedRms.load() };
}
int InputMonitor::read(TunerSample* destination, int maximum) noexcept
{
    int first = 0, firstSize = 0, second = 0, secondSize = 0;
    fifo.prepareToRead(maximum, first, firstSize, second, secondSize);
    std::copy_n(buffer.data() + first, firstSize, destination);
    std::copy_n(buffer.data() + second, secondSize, destination + firstSize);
    fifo.finishedRead(firstSize + secondSize);
    return firstSize + secondSize;
}
}
