#include "MetricMeter.h"
#include <cmath>

namespace effecthack
{
void MetricMeter::configure(juce::String caption, float minimum, float maximum, juce::String units)
{
    label = std::move(caption); min = minimum; max = maximum; suffix = std::move(units);
    setTitle(label); setDescription(suffix);
}
void MetricMeter::setValue(float next, juce::Colour tint, bool available)
{
    value = std::isfinite(next) ? next : min; colour = tint; valid = available;
    setDescription(valid ? juce::String(value, 1) + " " + suffix : "Unavailable");
    repaint();
}
void MetricMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto heading = area.removeFromTop(18);
    g.setFont(juce::FontOptions(11)); g.setColour(juce::Colour(0xff9aa9b7));
    g.drawText(label, heading, juce::Justification::centredLeft);
    g.setColour(valid ? colour : juce::Colour(0xff697681));
    const auto text = !valid ? juce::String("--") : value <= min && min <= -60 ? juce::String("-inf") : juce::String(value, 1);
    g.drawText(text + " " + suffix, heading, juce::Justification::centredRight);
    auto bar = area.withHeight(juce::jlimit(4, 10, area.getHeight())).toFloat();
    const auto position = [this, &bar](float v) { return bar.getX() + bar.getWidth() * juce::jlimit(0.0f, 1.0f, (v - min) / (max - min)); };
    g.setColour(juce::Colour(0xff35404c)); g.fillRoundedRectangle(bar, 3);
    if (targetHigh > targetLow)
    {
        g.setColour(juce::Colour(0xff69d5ba).withAlpha(.23f));
        g.fillRect(juce::Rectangle<float>(position(targetLow), bar.getY(), position(targetHigh) - position(targetLow), bar.getHeight()));
    }
    if (valid)
    {
        g.setColour(colour); g.fillRoundedRectangle(bar.withRight(position(value)), 3);
        if (marker >= min && marker <= max)
        {
            g.setColour(juce::Colour(0xffedf2f6));
            g.drawLine(position(marker), bar.getY() - 2, position(marker), bar.getBottom() + 2, 2);
        }
    }
    // Short marks keep the recommended range readable even when the bar covers it.
    if (targetHigh > targetLow)
    {
        g.setColour(juce::Colour(0xff69d5ba));
        for (float tick : { targetLow, targetHigh }) g.drawLine(position(tick), bar.getBottom() + 1, position(tick), bar.getBottom() + 4, 1);
    }
    if (getHeight() >= 42)
    {
        g.setColour(juce::Colour(0xff697681)); g.setFont(juce::FontOptions(9));
        const auto ticks = juce::Rectangle<int>(0, static_cast<int>(bar.getBottom()) + 6, getWidth(), 12);
        g.drawText(juce::String(min, 0), ticks, juce::Justification::centredLeft);
        g.drawText(juce::String(max, 0), ticks, juce::Justification::centredRight);
        if (targetHigh > targetLow)
        {
            g.setColour(juce::Colour(0xff69d5ba));
            for (float tick : { targetLow, targetHigh })
                g.drawText(juce::String(tick, 0), ticks.withWidth(26).withCentre({ static_cast<int>(position(tick)), ticks.getCentreY() }), juce::Justification::centred);
        }
    }
}
}
