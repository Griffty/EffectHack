#pragma once
#include "InputMonitor.h"

namespace effecthack
{
enum class LevelStatus { idle, low, good, hot, clipped };
struct InputAdvice { LevelStatus status; juce::String message; };
InputAdvice inputAdvice(InputLevels, bool hardware, bool clipped);
class PerformanceTracker
{
public:
    void update(double callbackLoad, int xruns, unsigned deviceGeneration);
    void clear();
    double load() const { return currentLoad; }
    int dropouts() const { return juce::jmax(0, lastCount - baseline); }
    bool overloaded() const { return currentLoad >= .8; }
private:
    double currentLoad = 0;
    unsigned generation = 0;
    int lastCount = 0, baseline = 0;
};
}
