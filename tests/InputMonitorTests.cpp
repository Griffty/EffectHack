#include "../Tuner.h"
#include "../RigStatus.h"
#include <cmath>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
void testInputMonitor()
{
    using namespace effecthack;
    for (double rate : { 8000.0, 32000.0, 44100.0, 48000.0, 96000.0 })
    {
        InputMonitor monitor; monitor.prepare(rate); monitor.beginBlock(true, 0, true);
        for (int i = 0; i < static_cast<int>(rate * .3); ++i)
        {
            const auto sample = static_cast<float>(.2 * std::sin(juce::MathConstants<double>::twoPi * 82.4069 * i / rate));
            monitor.push(sample, sample * 2);
        }
        monitor.publish();
        const auto levels = monitor.levels();
        require(levels.rawPeak > .195f && levels.rawPeak <= .2f && levels.trimmedPeak > .39f && levels.trimmedPeak <= .4f
            && levels.rms > .135f && levels.rms < .145f, "Raw peak/RMS and trimmed peak must be independent and stable across sample rates");
        std::array<TunerSample, 4096> incoming; const auto count = monitor.read(incoming.data(), 4096);
        require(count >= 2048 && std::abs(count - 3600) <= 1, "Tuner stream must be resampled to 12 kHz");
        std::array<float, 2048> window;
        for (size_t i = 0; i < window.size(); ++i) window[i] = incoming[static_cast<size_t>(count - 2048) + i].value;
        const auto pitch = detectPitch(window.data(), 2048, 12000);
        require(pitch > 0 && std::abs(1200 * std::log2(pitch / 82.4069)) < 3, "Resampling must preserve tuning accuracy");
        const auto generation = monitor.generation(); monitor.beginBlock(true, 1, true);
        require(monitor.generation() != generation && monitor.levels().rawPeak == 0, "New sources must clear stale analysis history");
        monitor.beginBlock(true, 1, false);
        for (int i = 0; i < 5000; ++i) monitor.push(.1f, .2f);
        require(monitor.read(incoming.data(), 4096) == 0, "Disabled tuner must not enqueue analysis samples");
        monitor.stop(); require(!monitor.active() && monitor.levels().rawPeak == 0, "Stopped input must clear levels");
    }
    InputMonitor overflow; overflow.prepare(48000); overflow.beginBlock(true, 1, true);
    for (int i = 0; i < 100000; ++i) overflow.push(.1f, .1f);
    std::array<TunerSample, 4096> samples;
    require(overflow.read(samples.data(), 4096) == 4095, "An absent consumer must drop data without growing or blocking");
    require(inputAdvice({ 1, .1f, .1f }, true, true).status == LevelStatus::clipped, "Raw overload must take priority over software trim");
    require(inputAdvice({ .01f, .02f, .01f }, true, false).status == LevelStatus::low, "Weak raw input must suggest interface gain");
    require(inputAdvice({ .2f, .8f, .1f }, true, false).message.contains("Input knob"), "Hot post-trim input must suggest software trim");
    require(inputAdvice({ .2f, .3f, .1f }, true, false).status == LevelStatus::good, "Moderate peaks must show useful headroom");
    require(inputAdvice({}, true, false).status == LevelStatus::idle, "Silence must request notes rather than more gain");
    PerformanceTracker tracker; tracker.update(.2, 0, 1);
    require(!tracker.overloaded() && tracker.dropouts() == 0, "Light callback load must be healthy");
    tracker.update(.9, 3, 1); require(tracker.overloaded() && tracker.dropouts() == 3, "High load and dropouts must be visible");
    tracker.clear(); require(tracker.dropouts() == 0, "Reset must rebase the displayed counter");
    tracker.update(.2, 4, 1); require(tracker.dropouts() == 1, "Later dropouts must count after reset");
    tracker.update(.1, 0, 2); require(tracker.dropouts() == 0 && !tracker.overloaded(), "New device sessions must clear stale performance warnings");
}
