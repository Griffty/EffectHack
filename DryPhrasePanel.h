#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace effecthack
{
class DryPhrasePanel final : public juce::Component
{
public:
    DryPhrasePanel();
    juce::TextButton record { "Record" }, audition { "Audition" }, live { "Live" }, clear { "Clear" };
    void update(bool capturing, bool busy, bool hasClip, bool looping, bool canRecord,
                bool canPlay, double seconds, double position, const juce::String& error);
    void setPeaks(const std::array<float, 128>& values) { peaks = values; repaint(); }
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    juce::Label caption, duration;
    std::array<float, 128> peaks {};
    juce::Rectangle<int> waveform;
    bool recording = false, playing = false;
    double elapsed = 0, cursor = 0;
};
}
