#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "Tuner.h"
#include "RigStatus.h"
#include "MetricMeter.h"

namespace effecthack
{
class RigTools final : public juce::Component
{
public:
    RigTools(AudioEngine&, juce::AudioDeviceManager&);
    void refresh();
    void enableTuner();
    void setTunerEnabled(bool);
    void paint(juce::Graphics&) override;
    void resized() override;
    juce::String performanceSummary() const;
    juce::String inputMessage() const { return advice.message; }
    juce::Colour inputColour() const;
    juce::Colour performanceColour() const;
    double reference() const { return referenceA4.getValue(); }
    void setReference(double value) { referenceA4.setValue(value, juce::dontSendNotification); }
    void setMuteToTune(bool value) { mute.setToggleState(value, juce::dontSendNotification); engine.tunerMute = value; }
private:
    AudioEngine& engine;
    juce::AudioDeviceManager& devices;
    Tuner tuner;
    PerformanceTracker performance;
    InputAdvice advice { LevelStatus::idle, {} };
    juce::ToggleButton enabled { "Tuner" }, mute { "Mute to tune" };
    juce::Slider referenceA4;
    juce::TextButton clearDropouts { "Reset" };
    juce::Label note, pitch, referenceLabel, centsValue, inputTitle,
        performanceTitle, load, dropouts, deviceDetails, loadCaption, dropoutsCaption;
    MetricMeter rawLevel, trimmedLevel;
    std::array<float, 150> loadHistory {};
    int historyPosition = 0;
    unsigned historyGeneration = 0;
    std::array<juce::Rectangle<int>, 3> panels;
    juce::Rectangle<int> needle, loadGraph;
    float cents = 0;
    bool validPitch = false, hasDevice = false;
};
}
