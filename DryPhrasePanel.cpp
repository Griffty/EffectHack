#include "DryPhrasePanel.h"
#include <cmath>

namespace effecthack
{
DryPhrasePanel::DryPhrasePanel()
{
    for (auto* child : std::initializer_list<juce::Component*> { &record, &audition, &live, &clear, &caption, &duration }) addAndMakeVisible(child);
    caption.setText("PHRASE", juce::dontSendNotification); caption.setFont(juce::FontOptions(11));
    caption.setColour(juce::Label::textColourId, juce::Colour(0xff9aa9b7));
    duration.setFont(juce::FontOptions(12)); duration.setJustificationType(juce::Justification::centredRight);
    record.setTooltip("Record the selected dry input before Input trim and effects (R). Finish to audition it through the rack. Maximum 30 seconds; session only.");
    audition.setTooltip("Loop the recorded phrase through the current rack (L). Guitar monitoring is replaced by the phrase while auditioning.");
    live.setTooltip("Return to the original input without changing the tone. The phrase stays available.");
    clear.setTooltip("Discard the dry phrase and return to the original input. Recordings are kept in memory and are never uploaded.");
    audition.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff304b43));
    audition.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff69d5ba));
}
void DryPhrasePanel::update(bool capturing, bool busy, bool hasClip, bool looping, bool canRecord,
    bool canPlay, double seconds, double position, const juce::String& error)
{
    recording = capturing; playing = looping; elapsed = seconds; cursor = position;
    record.setButtonText(busy ? "..." : capturing ? "Finish" : "Record");
    record.setEnabled(!busy && (capturing || canRecord));
    record.setColour(juce::TextButton::buttonColourId, capturing ? juce::Colour(0xff733536) : juce::Colour(0xff35404c));
    audition.setEnabled(!busy && !capturing && hasClip && canPlay);
    audition.setToggleState(looping, juce::dontSendNotification);
    live.setEnabled(looping);
    clear.setEnabled(capturing || hasClip || busy);
    duration.setText(error.isNotEmpty() ? "Check input" : busy ? "..." : capturing ? juce::String(seconds, 1) + " / 30 s"
        : hasClip ? juce::String(seconds, 1) + " s" : "--", juce::dontSendNotification);
    duration.setColour(juce::Label::textColourId, error.isNotEmpty() ? juce::Colour(0xffffa657)
        : capturing ? juce::Colour(0xfffa8072) : juce::Colour(0xff9aa9b7));
    duration.setTooltip(error);
    repaint(waveform);
}
void DryPhrasePanel::resized()
{
    auto row = getLocalBounds();
    caption.setBounds(row.removeFromLeft(66));
    record.setBounds(row.removeFromLeft(76).reduced(2, 3));
    audition.setBounds(row.removeFromLeft(86).reduced(2, 3));
    live.setBounds(row.removeFromLeft(60).reduced(2, 3));
    clear.setBounds(row.removeFromLeft(60).reduced(2, 3));
    duration.setBounds(row.removeFromRight(106));
    waveform = row.reduced(10, 7);
}
void DryPhrasePanel::paint(juce::Graphics& g)
{
    if (waveform.isEmpty()) return;
    const auto bounds = waveform.toFloat();
    g.setColour(juce::Colour(0xff35404c)); g.fillRect(bounds.withSizeKeepingCentre(bounds.getWidth(), 1));
    const auto peak = *std::max_element(peaks.begin(), peaks.end());
    g.setColour(recording ? juce::Colour(0xfffa8072) : juce::Colour(0xff69d5ba));
    for (size_t index = 0; index < peaks.size(); ++index)
    {
        const auto height = peak > 0 ? juce::jmax(1.0f, peaks[index] / peak * bounds.getHeight()) : 1.0f;
        const auto width = bounds.getWidth() / static_cast<float>(peaks.size());
        g.fillRect(bounds.getX() + static_cast<float>(index) * width, bounds.getCentreY() - height / 2, juce::jmax(1.0f, width * .65f), height);
    }
    const auto fraction = recording ? elapsed / 30 : playing && elapsed > 0 ? cursor / elapsed : -1;
    if (fraction >= 0)
    {
        g.setColour(juce::Colour(0xffedf2f6));
        const auto x = bounds.getX() + static_cast<float>(juce::jlimit(0.0, 1.0, fraction)) * bounds.getWidth();
        g.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getBottom());
    }
}
}
