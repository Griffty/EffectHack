#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace effecthack
{
// Read-only numeric display with a range bar. Values never change DSP controls.
class MetricMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void configure(juce::String caption, float minimum, float maximum, juce::String units);
    void setValue(float next, juce::Colour tint, bool available = true);
    void setTarget(float low, float high) { targetLow = low; targetHigh = high; }
    void setMarker(float next) { marker = next; }
    void paint(juce::Graphics&) override;
private:
    juce::String label, suffix;
    float value = 0, min = 0, max = 1, targetLow = 0, targetHigh = 0, marker = -1000;
    bool valid = false;
    juce::Colour colour { 0xff69d5ba };
};
}
