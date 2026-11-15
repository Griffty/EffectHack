#include "RigTools.h"
#include <cmath>

namespace effecthack
{
namespace
{
const juce::Colour muted { 0xff9aa9b7 }, good { 0xff69d5ba }, warning { 0xffffa657 }, bad { 0xfffa8072 };
float db(float value) { return juce::Decibels::gainToDecibels(value, -60.0f); }
}
RigTools::RigTools(AudioEngine& audio, juce::AudioDeviceManager& manager)
    : engine(audio), devices(manager), tuner(audio.inputMonitor)
{
    for (auto* label : { &note, &pitch, &referenceLabel, &centsValue, &inputTitle,
                        &performanceTitle, &load, &dropouts, &deviceDetails, &loadCaption, &dropoutsCaption })
    {
        addAndMakeVisible(label); label->setColour(juce::Label::textColourId, muted);
        label->setFont(juce::FontOptions(12.0f));
    }
    for (auto* control : std::initializer_list<juce::Component*> { &enabled, &mute, &referenceA4, &clearDropouts, &rawLevel, &trimmedLevel })
        addAndMakeVisible(control);
    note.setFont(juce::FontOptions(25.0f, juce::Font::bold));
    load.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    dropouts.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    centsValue.setJustificationType(juce::Justification::centredRight);
    inputTitle.setText("INPUT", juce::dontSendNotification);
    performanceTitle.setText("PERFORMANCE", juce::dontSendNotification);
    inputTitle.setColour(juce::Label::textColourId, warning); performanceTitle.setColour(juce::Label::textColourId, warning);
    loadCaption.setText("LOAD", juce::dontSendNotification); dropoutsCaption.setText("DROPOUTS", juce::dontSendNotification);
    dropoutsCaption.setFont(juce::FontOptions(10));
    enabled.setTooltip("Listen to the dry source before Input trim, gate, and effects. Pluck one string at a time.");
    mute.setToggleState(true, juce::dontSendNotification);
    mute.setTooltip("Silence the output while the tuner is enabled. Master Mute is preserved separately.");
    enabled.onClick = [this] { engine.tunerEnabled = enabled.getToggleState(); refresh(); };
    mute.onClick = [this] { engine.tunerMute = mute.getToggleState(); };
    referenceLabel.setText("A4", juce::dontSendNotification);
    referenceA4.setSliderStyle(juce::Slider::IncDecButtons);
    referenceA4.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 65, 24);
    referenceA4.setRange(430, 450, 1); referenceA4.setValue(440, juce::dontSendNotification);
    referenceA4.setTextValueSuffix(" Hz"); referenceA4.setScrollWheelEnabled(false);
    referenceA4.setName("Tuner A4 reference");
    clearDropouts.setTooltip("Reset the displayed dropout count. Counters also reset on device restart.");
    clearDropouts.onClick = [this] { performance.clear(); refresh(); };
    rawLevel.configure("SOURCE", -60, 0, "dBFS"); trimmedLevel.configure("TRIM", -60, 0, "dBFS");
    for (auto* meter : { &rawLevel, &trimmedLevel }) meter->setTarget(-18, -6);
    refresh();
}
void RigTools::enableTuner()
{
    enabled.setToggleState(true, juce::dontSendNotification); enabled.onClick();
}
juce::Colour RigTools::inputColour() const
{
    if (advice.status == LevelStatus::clipped) return bad;
    if (advice.status == LevelStatus::hot || advice.status == LevelStatus::low) return warning;
    return advice.status == LevelStatus::good ? good : muted;
}
juce::Colour RigTools::performanceColour() const
{
    return !hasDevice ? muted : performance.overloaded() ? bad : performance.dropouts() > 0 ? warning : good;
}
juce::String RigTools::performanceSummary() const
{
    return hasDevice ? "LOAD  " + juce::String(performance.load() * 100, 1) + "%     DROPOUTS  " + juce::String(performance.dropouts())
                     : "LOAD  --     DROPOUTS  --";
}
void RigTools::refresh()
{
    const auto hz = engine.tunerEnabled.load() ? tuner.frequency() : 0;
    const auto reading = describePitch(hz, referenceA4.getValue());
    validPitch = reading.note.isNotEmpty(); cents = reading.cents;
    const auto pitchColour = validPitch ? std::abs(cents) <= 3 ? good : warning : muted;
    note.setText(validPitch ? reading.note : "--", juce::dontSendNotification);
    note.setColour(juce::Label::textColourId, pitchColour);
    pitch.setText(validPitch ? juce::String(hz, 2) + " Hz" : "-- Hz", juce::dontSendNotification);
    centsValue.setText(validPitch ? (cents >= 0 ? "+" : "") + juce::String(cents, 1) + " ct" : "-- ct", juce::dontSendNotification);
    centsValue.setColour(juce::Label::textColourId, pitchColour);
    const auto tunerHelp = !engine.tunerEnabled.load() ? juce::String("Enable the tuner and pluck one string at a time.")
        : engine.inputSource.load() != AudioEngine::InputSource::hardware && !engine.monitoring.load() ? juce::String("Press Play to tune a file or the test riff.")
        : validPitch ? juce::String("Cents from the nearest note. Center is in tune (within +/-3 cents). Negative is flat; positive is sharp.")
        : juce::String("Waiting for a steady single note. Chords and mixed songs can be ambiguous.");
    note.setTooltip(tunerHelp); pitch.setTooltip(tunerHelp); centsValue.setTooltip(tunerHelp);
    const auto levels = engine.inputMonitor.levels();
    advice = inputAdvice(levels, engine.inputSource.load() == AudioEngine::InputSource::hardware, engine.inputClipped.load());
    if (!engine.inputMonitor.active() && !engine.inputClipped.load())
        advice = { LevelStatus::idle, engine.inputSource.load() == AudioEngine::InputSource::hardware
            ? "Enable a guitar input in Input & audio."
            : "Press Play, then check a strong passage." };
    const auto levelColour = [](float value)
    {
        return value >= .70795f ? bad : value >= .50119f ? warning : value >= .03162f ? good : value >= .001f ? warning : muted;
    };
    const auto active = engine.inputMonitor.active();
    rawLevel.setValue(db(levels.rawPeak), engine.inputClipped.load() ? bad : levelColour(levels.rawPeak), active);
    rawLevel.setMarker(db(levels.rms));
    trimmedLevel.setValue(db(levels.trimmedPeak), levelColour(levels.trimmedPeak), active);
    rawLevel.setTooltip(advice.message + "\nSource peak: " + juce::String(db(levels.rawPeak), 1) + " dBFS. White marker: RMS "
        + juce::String(db(levels.rms), 1) + " dBFS. Green marks: -18 to -6 dBFS. Interface clipping cannot be repaired by Input trim.");
    trimmedLevel.setTooltip("Peak after the Input knob, before the rack. Green marks: -18 to -6 dBFS as a starting guide. " + advice.message);
    auto* device = devices.getCurrentAudioDevice(); hasDevice = device != nullptr;
    const auto generation = engine.deviceGeneration.load();
    if (generation != historyGeneration) { loadHistory.fill(0); historyPosition = 0; historyGeneration = generation; }
    const auto currentLoad = hasDevice ? devices.getCpuUsage() : 0;
    performance.update(currentLoad, hasDevice ? devices.getXRunCount() : 0, generation);
    loadHistory[static_cast<size_t>(historyPosition)] = static_cast<float>(juce::jlimit(0.0, 1.0, currentLoad));
    historyPosition = (historyPosition + 1) % static_cast<int>(loadHistory.size());
    load.setText(hasDevice ? juce::String(performance.load() * 100, 1) + "%" : "--", juce::dontSendNotification);
    load.setColour(juce::Label::textColourId, !hasDevice ? muted : performance.overloaded() ? bad : good);
    dropouts.setText(hasDevice ? juce::String(performance.dropouts()) : "--", juce::dontSendNotification);
    dropouts.setColour(juce::Label::textColourId, !hasDevice ? muted : performance.dropouts() > 0 ? warning : good);
    clearDropouts.setEnabled(hasDevice);
    if (device != nullptr)
    {
        const auto rate = device->getCurrentSampleRate();
        const auto latency = rate > 0 ? 1000.0 * ((engine.currentVirtualInput() == nullptr ? device->getInputLatencyInSamples() : 0)
            + device->getOutputLatencyInSamples() + engine.effectLatency.load()) / rate : 0;
        deviceDetails.setText(juce::String(rate / 1000, 1) + " kHz / " + juce::String(device->getCurrentBufferSizeSamples())
            + " smp / ~" + juce::String(latency, 1) + " ms", juce::dontSendNotification);
    }
    else deviceDetails.setText("-- kHz / -- smp / -- ms", juce::dontSendNotification);
    load.setTooltip("Audio callback workload, not total CPU use. Graph: approximately 5 seconds, 0-100%, amber line at 80%. High load: try a larger audio buffer.");
    dropouts.setTooltip("Driver-reported dropouts plus JUCE callback overrun estimates, since restart / Reset. If this rises, try a larger audio buffer.");
    deviceDetails.setTooltip("Sample rate / buffer size / driver-reported latency plus rack processing delay. Not a measured round trip.");
    repaint();
}
void RigTools::resized()
{
    auto area = getLocalBounds();
    const auto width = (area.getWidth() - 16) / 3;
    for (auto& panel : panels) { panel = area.removeFromLeft(width); area.removeFromLeft(8); }
    auto left = panels[0].reduced(10);
    auto buttons = left.removeFromTop(24);
    enabled.setBounds(buttons.removeFromLeft(75)); mute.setBounds(buttons);
    auto reading = left.removeFromTop(32); note.setBounds(reading.removeFromLeft(56));
    centsValue.setBounds(reading.removeFromRight(68)); pitch.setBounds(reading);
    needle = left.removeFromTop(48).reduced(8, 0);
    auto referenceRow = left.removeFromTop(26);
    referenceLabel.setBounds(referenceRow.removeFromLeft(28)); referenceA4.setBounds(referenceRow.removeFromLeft(108));
    auto middle = panels[1].reduced(12, 10);
    inputTitle.setBounds(middle.removeFromTop(24));
    rawLevel.setBounds(middle.removeFromTop(48)); trimmedLevel.setBounds(middle.removeFromTop(48));
    auto right = panels[2].reduced(10); auto heading = right.removeFromTop(24);
    clearDropouts.setBounds(heading.removeFromRight(56)); performanceTitle.setBounds(heading);
    auto values = right.removeFromTop(32); auto loadArea = values.removeFromLeft(values.getWidth() / 2);
    loadCaption.setBounds(loadArea.removeFromLeft(40)); load.setBounds(loadArea);
    dropoutsCaption.setBounds(values.removeFromLeft(68)); dropouts.setBounds(values);
    loadGraph = right.removeFromTop(52).reduced(4, 4);
    deviceDetails.setBounds(right.removeFromTop(26));
}
void RigTools::paint(juce::Graphics& g)
{
    for (const auto& panel : panels)
    {
        g.setColour(juce::Colour(0xff202a34)); g.fillRoundedRectangle(panel.toFloat(), 10);
        g.setColour(juce::Colour(0xff3a4652)); g.drawRoundedRectangle(panel.toFloat().reduced(.5f), 10, 1);
    }
    auto track = needle.withHeight(16).toFloat();
    const auto center = track.getCentreX();
    g.setColour(juce::Colour(0xff35404c)); g.fillRoundedRectangle(track, 3);
    g.setColour(good.withAlpha(.25f)); g.fillRect(track.withWidth(track.getWidth() * .06f).withCentre(track.getCentre()));
    g.setColour(muted); g.drawHorizontalLine(static_cast<int>(track.getCentreY()), track.getX(), track.getRight());
    g.setColour(good); g.drawLine(center, track.getY(), center, track.getBottom(), 1);
    if (validPitch)
    {
        const auto x = track.getX() + track.getWidth() * (.5f + juce::jlimit(-50.0f, 50.0f, cents) / 100);
        g.setColour(std::abs(cents) <= 3 ? good : warning);
        g.fillEllipse(x - 4, track.getCentreY() - 4, 8, 8);
    }
    g.setColour(muted); g.setFont(juce::FontOptions(10));
    auto ticks = needle.withTrimmedTop(18).withHeight(16);
    g.drawText("-50", ticks, juce::Justification::centredLeft); g.drawText("0", ticks, juce::Justification::centred);
    g.drawText("+50", ticks, juce::Justification::centredRight);
    const auto graph = loadGraph.toFloat();
    g.setColour(juce::Colour(0xff151e27)); g.fillRoundedRectangle(graph, 3);
    g.setColour(juce::Colour(0xff35404c)); g.drawLine(graph.getX(), graph.getCentreY(), graph.getRight(), graph.getCentreY(), 1);
    g.setColour(warning.withAlpha(.5f)); g.drawLine(graph.getX(), graph.getY() + graph.getHeight() * .2f, graph.getRight(), graph.getY() + graph.getHeight() * .2f, 1);
    juce::Path path;
    for (size_t i = 0; i < loadHistory.size(); ++i)
    {
        const auto x = graph.getX() + graph.getWidth() * static_cast<float>(i) / static_cast<float>(loadHistory.size() - 1);
        const auto y = graph.getBottom() - graph.getHeight() * loadHistory[(i + static_cast<size_t>(historyPosition)) % loadHistory.size()];
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(hasDevice ? performance.overloaded() ? bad : good : muted);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}
}
