#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioEngine.h"
#include "PresetManager.h"
#include "FactoryPresets.h"
#include "SessionState.h"
#include "RigTools.h"
#include "DryPhrasePanel.h"
#include "GeneratedTonePreparation.h"
#include "GeminiRecommender.h"
#include <optional>
#include <functional>
#include <set>

namespace effecthack
{
class RackContent final : public juce::Component
{
public:
    std::array<juce::Rectangle<int>, rackStageIds.size()> panels;
    void paint(juce::Graphics& g) override
    {
        for (size_t i = 0; i < panels.size(); ++i)
        {
            const auto& panel = panels[i];
            g.setColour(juce::Colour(i % 2 == 0 ? 0xff202a34 : 0xff1d2730));
            g.fillRoundedRectangle(panel.toFloat(), 14.0f);
            g.setColour(juce::Colour(0xff3a4652));
            g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), 14.0f, 1.0f);
        }
    }
};

class SignalFlowComponent final : public juce::Component
{
public:
    void setState(const RackOrder& order, const std::array<bool, rackStageIds.size()>& states)
    {
        if (stageOrder == order && bypassStates == states) return;
        stageOrder = order;
        bypassStates = states;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().reduced(2, 4);
        const auto nodeWidth = juce::jmax(1, (bounds.getWidth() - static_cast<int>(rackStageIds.size() + 1) * 12) / static_cast<int>(rackStageIds.size() + 2));
        auto area = bounds;

        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        for (int i = 0; i < static_cast<int>(rackStageIds.size() + 2); ++i)
        {
            const auto node = area.removeFromLeft(nodeWidth);
            const bool effect = i >= 1 && i <= static_cast<int>(rackStageIds.size());
            int source = -1;
            if (effect)
                for (size_t index = 0; index < rackStageIds.size(); ++index)
                    if (stageOrder[static_cast<size_t>(i - 1)] == rackStageIds[index]) source = static_cast<int>(index);
            const auto bypassed = source >= 0 && bypassStates[static_cast<size_t>(source)];
            const auto name = i == 0 ? juce::String("INPUT") : i == static_cast<int>(rackStageIds.size() + 1) ? juce::String("OUTPUT")
                : stageOrder[static_cast<size_t>(i - 1)].toUpperCase();
            g.setColour(bypassed ? juce::Colour(0xff2b333b) : juce::Colour(0xff35404c));
            g.fillRoundedRectangle(node.toFloat(), 6.0f);
            g.setColour(bypassed ? juce::Colour(0xff697681) : juce::Colour(0xffffa657));
            g.drawRoundedRectangle(node.toFloat().reduced(0.5f), 6.0f, 1.0f);
            g.setColour(bypassed ? juce::Colour(0xff89949d) : juce::Colour(0xffedf2f6));
            g.drawFittedText(name, node.reduced(4, 0), juce::Justification::centred, 1);
            if (i < static_cast<int>(rackStageIds.size() + 1))
            {
                const auto arrow = area.removeFromLeft(12);
                g.setColour(juce::Colour(0xff697681));
                g.drawLine(static_cast<float>(arrow.getX()), static_cast<float>(arrow.getCentreY()),
                           static_cast<float>(arrow.getRight() - 3), static_cast<float>(arrow.getCentreY()), 1.0f);
                g.drawLine(static_cast<float>(arrow.getRight() - 6), static_cast<float>(arrow.getCentreY() - 3),
                           static_cast<float>(arrow.getRight() - 3), static_cast<float>(arrow.getCentreY()), 1.0f);
                g.drawLine(static_cast<float>(arrow.getRight() - 6), static_cast<float>(arrow.getCentreY() + 3),
                           static_cast<float>(arrow.getRight() - 3), static_cast<float>(arrow.getCentreY()), 1.0f);
            }
        }
    }

private:
    RackOrder stageOrder = defaultRackOrder();
    std::array<bool, rackStageIds.size()> bypassStates {};
};

class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel(juce::AudioDeviceManager& manager, int selectedSource,
                  std::function<void(int)> sourceChanged)
        : devices(manager, 0, 2, 0, 2, false, false, false, false),
          onSourceChanged(std::move(sourceChanged))
    {
        addAndMakeVisible(sourceLabel);
        addAndMakeVisible(sourceSelector);
        addAndMakeVisible(devices);
        sourceLabel.setText("Input source", juce::dontSendNotification);
        sourceSelector.addItem("Audio file", 1);
        sourceSelector.addItem("Test riff", 2);
        sourceSelector.addItem("Guitar input", 3);
        sourceSelector.setSelectedId(selectedSource, juce::dontSendNotification);
        sourceSelector.onChange = [this]
        {
            if (onSourceChanged)
                onSourceChanged(sourceSelector.getSelectedId());
        };
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(18);
        auto source = area.removeFromTop(34);
        sourceLabel.setBounds(source.removeFromLeft(110));
        sourceSelector.setBounds(source.removeFromLeft(220).reduced(0, 3));
        area.removeFromTop(10);
        devices.setBounds(area);
    }

private:
    juce::Label sourceLabel;
    juce::ComboBox sourceSelector;
    juce::AudioDeviceSelectorComponent devices;
    std::function<void(int)> onSourceChanged;
};

/** Message-thread coordinator for devices, controls, and worker completions.
 * Owns the live engine; companion MainComponent*.cpp files separate tone commits,
 * A/B state, phrase capture, keyboard handling, and startup persistence.
 */
class MainComponent final : public juce::Component, private juce::Timer, private juce::KeyListener
{
public:
    explicit MainComponent(juce::File sessionFile = SessionStore::defaultFile(),
                           juce::File presetDirectory = PresetManager::defaultDirectory(), bool openAudioDevice = true);
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool) override;

private:
    friend struct RigControlsTestAccess;
    friend struct PhraseControlsTestAccess;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::AudioDeviceManager deviceManager;
    AudioEngine engine;
    RigTools rigTools { engine, deviceManager };
    juce::TextButton tunerButton { "Tuner..." };
    juce::TextButton songButton { "Song..." };
    void showSongPicker();
    juce::Result selectSongPreset(const juce::String&);
    void applySongRecommendation(const SongRecommendation&, const Cancellation&, std::function<void(juce::Result)>);
    struct ToneUndo { Preset identity; RackState rack; };
    enum class ToneChangeOrigin { song, revert, slot };
    struct PendingToneChange
    {
        Preset identity;
        PreparedPreset prepared;
        ToneUndo previous;
        ToneChangeOrigin origin = ToneChangeOrigin::song;
        float outputAdjustment = 0;
        Cancellation cancel;
        std::function<void(juce::Result)> completion;
        bool preserveMaster = false;
        int slotIndex = -1;
    };
    std::optional<ToneUndo> previousSongTone;
    std::optional<PendingToneChange> pendingToneChange;
    juce::TextButton revertSongButton { "Revert" };
    bool isGeneratedPreset() const { return activePreset.id.isEmpty() && activePreset.name != "Default rack"; }
    void startToneChange(Preset, RackState, juce::var, ToneChangeOrigin, Cancellation, std::function<void(juce::Result)>, bool preserveMaster = false, int slotIndex = -1);
    void serviceToneChange();
    void revertSongTone();
    ToneSlots toneSlots;
    std::array<std::optional<PreparedPreset>, 2> slotPrepared;
    std::array<juce::TextButton, 2> slotButtons { juce::TextButton("A"), juce::TextButton("B") };
    juce::TextButton copySlotButton { juce::String::fromUTF8("A → B") }, shortcutHelpButton { "Keys" };
    void initialiseToneSlots();
    void refreshToneSlots();
    void selectToneSlot(int);
    void copyToneSlot();
    void installKeyboardControls(juce::Component&);
    void releaseShortcutKeys();
    std::set<int> heldShortcutKeys;
    DryPhrasePanel phrasePanel;
    juce::ThreadPool phraseWorker { 1, 0, juce::Thread::Priority::low };
    unsigned phraseRequest = 0, phraseGeneration = 0;
    bool preparingPhrase = false, clearingPhrase = false, lastPhraseLayout = false;
    double phraseCaptureRate = 48000;
    std::array<float, 128> phrasePeaks {};
    juce::String phraseError;
    void initialiseDryPhrase();
    void togglePhraseRecording();
    void togglePhraseAudition();
    void returnFromPhrase();
    void clearPhrase();
    void abortPhraseCapture();
    void refreshDryPhrase();
    void finishPhraseRecording();
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;
    bool keyStateChanged(bool down, juce::Component*) override { return keyStateChanged(down); }
    juce::AudioDeviceSelectorComponent devices;
    juce::Viewport deviceViewport;
    RackContent rackContent;
    juce::Viewport rackViewport;
    SignalFlowComponent signalFlow;
    juce::Label rackHint;
    juce::TextButton settingsButton { "Input & audio..." };
    std::array<juce::Slider, 9> spatialKnobs;
    std::array<juce::Label, 9> spatialLabels;
    std::array<juce::Label, 3> spatialTitles, spatialHints;
    std::array<juce::ToggleButton, 3> spatialBypasses;
    std::array<juce::TextButton, rackStageIds.size()> moveUpButtons, moveDownButtons;
    juce::Label ampTitle, ampHint;
    juce::ComboBox ampVoiceSelector;
    juce::ToggleButton ampBypass { "Bypass amp" };
    std::array<juce::Slider, 6> ampKnobs;
    std::array<juce::Label, 6> ampLabels;
    juce::Rectangle<int> ampBounds;
    std::array<juce::Label, 2> dynamicsTitles;
    std::array<MetricMeter, 2> reductionMeters;
    std::array<juce::ToggleButton, 2> dynamicsBypasses;
    std::array<juce::Rectangle<int>, 2> dynamicsBounds;
    std::array<juce::Slider, 4> gateKnobs;
    std::array<juce::Label, 4> gateLabels;
    std::array<juce::Slider, 6> compressorKnobs;
    std::array<juce::Label, 6> compressorLabels;
    juce::Label title, subtitle, status, inputChannelLabel, rackTitle, rackSubtitle;
    juce::Label inputLabel, driveLabel, mixLabel, outputLabel;
    juce::Label deviceTitle, deviceHint;
    juce::Slider input, drive, mix, output;
    juce::ComboBox channelSelector;
    juce::Label sourceLabel, sourceInfo, playheadLabel;
    juce::Slider timeline;
    juce::TextButton stopButton { "Stop" };
    juce::TextButton contextPlayButton { "Play" };
    juce::Label masterTitle;
    juce::ToggleButton masterMuteButton { "Mute" }, rackBypassButton { "Bypass whole rack" };
    juce::TextButton clearClipsButton { "Clear clips" };
    juce::Rectangle<int> masterBounds;
    bool draggingTimeline = false;
    void togglePlayback();
    void seekFromTimeline();
    static juce::String formatTime(double);
    juce::ComboBox sourceSelector;
    juce::TextButton loadFileButton { "Load audio file..." }, restartButton { "Restart" };
    juce::ToggleButton loopButton { "Loop" };
    juce::TextButton monitoringButton { "Power" };
    juce::ToggleButton bypassButton { "Bypass distortion" };
    juce::Label cabinetTitle, cabinetInfo, cabinetHint, eqTitle, eqHint;
    juce::Label bassLabel, midLabel, trebleLabel;
    juce::Slider bass, mid, treble;
    juce::TextButton loadIrButton { "Load IR..." }, builtinCabinetButton { "Use built-in" };
    juce::ToggleButton cabinetBypass { "Bypass cabinet" }, eqBypass { "Bypass EQ" };
    juce::Rectangle<int> cabinetBounds, eqBounds;
    juce::String cabinetName { "Built-in speaker approximation" }, cabinetError;
    bool loadingCabinet = false;
    std::unique_ptr<juce::FileChooser> cabinetFileChooser;
    juce::Rectangle<int> rackBounds, meterBounds, deviceBounds;
    juce::Rectangle<int> contextBounds;
    juce::String lastInputNames, deviceError;
    float displayedInput = 0.0f, displayedOutput = 0.0f;
    juce::ThreadPool loader { 1 };
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool loadingFile = false;
    juce::String sourceError;
    PresetManager presetManager;
    SessionStore sessionStore;
    juce::String rememberedAudioFile;
    bool restoringSession = false;
    void restoreSessionTone(const SessionState&);
    void saveSession();
    Preset activePreset { {}, "Default rack", {} };
    std::vector<Preset> presets;
    juce::ComboBox presetSelector;
    juce::TextButton savePresetButton { "Save" }, saveAsPresetButton { "Save as..." },
        renamePresetButton { "Rename..." }, deletePresetButton { "Delete..." }, reloadPresetButton { "Reload" };
    juce::Label presetInfo;
    juce::String presetMessage { "All nine stages and the cabinet selection" };
    bool loadingPreset = false;
    bool presetHasError = false;
    void refreshPresetList();
    void refreshPresetControls();
    void loadSelectedPreset();
    void savePreset();
    void namePreset(bool rename);
    void deletePreset();
    void syncRackControls(const RackState&);

    void changeInputSource();
    void showSettings();
    void chooseAudioFile();
    void loadAudioFile(const juce::File&);
    void refreshSourceStatus();
    void chooseCabinetImpulse();
    void refreshCabinetStatus();
    void timerCallback() override;
    void refreshDeviceStatus();
    void refreshSignalFlow();
    void reorderStage(const juce::String&, int);
    void setupKnob(juce::Slider&, juce::Label&, const juce::String&, double, double, double,
                   const juce::String&);
    void drawMeter(juce::Graphics&, juce::Rectangle<int>, float, const juce::String&, bool clipped);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
}
