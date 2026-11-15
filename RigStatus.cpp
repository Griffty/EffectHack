#include "RigStatus.h"
#include <cmath>

namespace effecthack
{
InputAdvice inputAdvice(InputLevels levels, bool hardware, bool clipped)
{
    if (clipped) return { LevelStatus::clipped, hardware
        ? "Input clipped. Lower interface gain, then Clear clips."
        : "Source clipped. Use a quieter recording, then Clear clips." };
    if (levels.rawPeak < .001f) return { LevelStatus::idle, "Play strong notes to check your input level." };
    if (levels.rawPeak >= .70795f) return { LevelStatus::hot, hardware
        ? "Little headroom. Lower interface gain."
        : "Source is hot. Keep Input trim low." };
    if (levels.trimmedPeak >= .70795f) return { LevelStatus::hot, "Trimmed input is hot. Lower the Input knob." };
    if (levels.rawPeak < .03162f) return { LevelStatus::low, hardware
        ? "Low signal. Try more interface gain while playing strongly."
        : "Quiet source. Try raising Input trim if needed." };
    return { LevelStatus::good, "Good headroom. Adjust Input trim for the tone you want." };
}
void PerformanceTracker::update(double callbackLoad, int xruns, unsigned deviceGeneration)
{
    const auto next = juce::jlimit(0.0, 10.0, std::isfinite(callbackLoad) ? callbackLoad : 0.0);
    if (generation != deviceGeneration || xruns < lastCount)
    { generation = deviceGeneration; baseline = 0; currentLoad = 0; }
    lastCount = juce::jmax(0, xruns);
    currentLoad = juce::jmax(next, currentLoad * .95);
}
void PerformanceTracker::clear() { baseline = lastCount; }
}
