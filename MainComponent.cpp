#include "MainComponent.h"
#include <cmath>
#include <exception>

namespace effecthack
{
namespace
{
const juce::Colour background { 0xff12171d }, panel { 0xff1c242d }, accent { 0xffffa657 };
const juce::Colour foreground { 0xffedf2f6 }, muted { 0xff9aa9b7 };
const auto& stageIds = rackStageIds;
}

MainComponent::MainComponent(juce::File sessionFile, juce::File presetDirectory)
    : devices(deviceManager, 0, 2, 0, 2, false, false, false, false),
      presetManager(std::move(presetDirectory)), sessionStore(std::move(sessionFile))
{
    lookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, background);
    lookAndFeel.setColour(juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff35404c));
    lookAndFeel.setColour(juce::Slider::textBoxTextColourId, foreground);
    lookAndFeel.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour(juce::Label::textColourId, foreground);
    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, panel);
    lookAndFeel.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff35404c));
    setLookAndFeel(&lookAndFeel);

    for (auto* component : std::initializer_list<juce::Component*> {
         &title, &subtitle, &status, &inputChannelLabel, &rackTitle, &rackSubtitle,
         &inputLabel, &driveLabel, &mixLabel, &outputLabel, &deviceTitle, &deviceHint,
         &input, &drive, &mix, &output, &channelSelector, &monitoringButton, &bypassButton, &deviceViewport,
         &sourceLabel, &sourceInfo, &playheadLabel, &sourceSelector, &loadFileButton, &restartButton, &loopButton,
         &cabinetTitle, &cabinetInfo, &cabinetHint, &eqTitle, &eqHint, &bassLabel, &midLabel, &trebleLabel,
         &bass, &mid, &treble, &loadIrButton, &builtinCabinetButton, &cabinetBypass, &eqBypass })
        addAndMakeVisible(component);
    addAndMakeVisible(signalFlow);
    addAndMakeVisible(settingsButton);
    addAndMakeVisible(contextPlayButton);
    deviceViewport.setViewedComponent(&devices, false);
    deviceViewport.setScrollBarsShown(true, false);
    deviceViewport.setVisible(false);
    deviceTitle.setVisible(false);
    deviceHint.setVisible(false);
    addAndMakeVisible(rackViewport);
    addAndMakeVisible(rackHint);
    addAndMakeVisible(tunerButton);
    addAndMakeVisible(songButton);
    songButton.setTooltip("Generate a complete custom rack or choose a factory tone from a YouTube song link. Uses Gemini with title/channel metadata.");
    songButton.onClick = [this] { showSongPicker(); };
    addAndMakeVisible(revertSongButton);
    revertSongButton.setTooltip("Restore the complete rack from before the last song tone. Replaces current tone edits.");
    revertSongButton.onClick = [this] { revertSongTone(); };
    rackContent.addAndMakeVisible(rigTools);
    tunerButton.onClick = [this] { rigTools.enableTuner(); rackViewport.setViewPosition(0, 0); };
    rackViewport.setViewedComponent(&rackContent, false);
    rackViewport.setScrollBarsShown(true, false);
    rackHint.setText("RACK", juce::dontSendNotification);
    rackHint.setColour(juce::Label::textColourId, muted);
    for (auto* control : std::initializer_list<juce::Component*> {
        &rackTitle, &rackSubtitle, &inputLabel, &driveLabel, &mixLabel, &outputLabel,
        &input, &drive, &mix, &output, &bypassButton, &cabinetTitle, &cabinetInfo, &cabinetHint,
        &eqTitle, &eqHint, &bassLabel, &midLabel, &trebleLabel, &bass, &mid, &treble,
        &loadIrButton, &builtinCabinetButton, &cabinetBypass, &eqBypass })
        rackContent.addAndMakeVisible(control);
    for (size_t i = 0; i < moveUpButtons.size(); ++i)
    {
        moveUpButtons[i].setButtonText("Up");
        moveDownButtons[i].setButtonText("Down");
        rackContent.addAndMakeVisible(moveUpButtons[i]);
        rackContent.addAndMakeVisible(moveDownButtons[i]);
        moveUpButtons[i].setTooltip("Move this effect earlier in the signal chain.");
        moveDownButtons[i].setTooltip("Move this effect later in the signal chain.");
        moveUpButtons[i].onClick = [this, i] { reorderStage(stageIds[i], -1); };
        moveDownButtons[i].onClick = [this, i] { reorderStage(stageIds[i], 1); };
    }

    struct KnobSettings
    {
        const char* name; double minimum, maximum, value; const char* suffix;
        std::atomic<float>* target; float scale;
    };
    for (size_t i = 0; i < dynamicsTitles.size(); ++i)
    {
        rackContent.addAndMakeVisible(dynamicsTitles[i]);
        rackContent.addAndMakeVisible(reductionMeters[i]);
        rackContent.addAndMakeVisible(dynamicsBypasses[i]);
        dynamicsTitles[i].setText(i == 0 ? "NOISE GATE" : "COMPRESSOR", juce::dontSendNotification);
        dynamicsTitles[i].setFont(juce::FontOptions(18.0f, juce::Font::bold));
        dynamicsTitles[i].setColour(juce::Label::textColourId, accent);
        reductionMeters[i].configure("GR", 0, i == 0 ? 90.0f : 36.0f, "dB");
        reductionMeters[i].setTooltip(i == 0 ? "Gate attenuation. Stereo linked, with 6 dB hysteresis."
            : "Compressor gain reduction before makeup. Stereo linked, with a 6 dB soft knee.");
        dynamicsBypasses[i].setButtonText(i == 0 ? "Bypass gate" : "Bypass compressor");
        dynamicsBypasses[i].setToggleState(true, juce::dontSendNotification);
    }
    dynamicsBypasses[0].onClick = [this] { engine.gateParameters.bypass.store(dynamicsBypasses[0].getToggleState()); };
    dynamicsBypasses[1].onClick = [this] { engine.compressorParameters.bypass.store(dynamicsBypasses[1].getToggleState()); };
    const KnobSettings gateSettings[] {
        { "THRESHOLD", -90, 0, -50, " dB", &engine.gateParameters.thresholdDb, 1 },
        { "ATTACK", .1, 20, 1, " ms", &engine.gateParameters.attackMs, 1 },
        { "HOLD", 0, 250, 30, " ms", &engine.gateParameters.holdMs, 1 },
        { "RELEASE", 10, 1000, 120, " ms", &engine.gateParameters.releaseMs, 1 }
    };
    const KnobSettings compressorSettings[] {
        { "THRESHOLD", -60, 0, -18, " dB", &engine.compressorParameters.thresholdDb, 1 },
        { "RATIO", 1, 12, 4, ":1", &engine.compressorParameters.ratio, 1 },
        { "ATTACK", .1, 100, 10, " ms", &engine.compressorParameters.attackMs, 1 },
        { "RELEASE", 10, 1000, 120, " ms", &engine.compressorParameters.releaseMs, 1 },
        { "MAKEUP", 0, 18, 0, " dB", &engine.compressorParameters.makeupDb, 1 },
        { "MIX", 0, 100, 100, " %", &engine.compressorParameters.mix, .01f }
    };
    const auto setupDynamics = [this](auto& knobs, auto& labels, const auto& settings)
    {
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            rackContent.addAndMakeVisible(knobs[i]); rackContent.addAndMakeVisible(labels[i]);
            const auto& setting = settings[i];
            setupKnob(knobs[i], labels[i], setting.name, setting.minimum, setting.maximum, setting.value, setting.suffix);
            auto* knob = &knobs[i];
            knobs[i].onValueChange = [knob, target = setting.target, scale = setting.scale]
            { target->store(static_cast<float>(knob->getValue()) * scale); };
        }
    };
    setupDynamics(gateKnobs, gateLabels, gateSettings);
    setupDynamics(compressorKnobs, compressorLabels, compressorSettings);

    for (auto* control : std::initializer_list<juce::Component*> { &ampTitle, &ampHint, &ampVoiceSelector, &ampBypass })
        rackContent.addAndMakeVisible(control);
    ampTitle.setText("02  /  AMP", juce::dontSendNotification);
    ampTitle.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    ampTitle.setColour(juce::Label::textColourId, accent);
    ampHint.setText("Generic voices  /  Try with distortion bypassed; keep the cabinet enabled.", juce::dontSendNotification);
    ampHint.setColour(juce::Label::textColourId, muted);
    ampBypass.setToggleState(true, juce::dontSendNotification);
    ampBypass.onClick = [this] { engine.ampParameters.bypass.store(ampBypass.getToggleState()); };
    ampVoiceSelector.addItem("Clean", 1);
    ampVoiceSelector.addItem("Crunch", 2);
    ampVoiceSelector.addItem("High Gain", 3);
    ampVoiceSelector.setSelectedId(2, juce::dontSendNotification);
    ampVoiceSelector.onChange = [this] { engine.ampParameters.voice.store(ampVoiceSelector.getSelectedId() - 1); };
    const KnobSettings ampSettings[] {
        { "GAIN", 0, 36, 12, " dB", &engine.ampParameters.gainDb, 1 },
        { "BASS", -12, 12, 0, " dB", &engine.ampParameters.bassDb, 1 },
        { "MID", -12, 12, 0, " dB", &engine.ampParameters.midDb, 1 },
        { "TREBLE", -12, 12, 0, " dB", &engine.ampParameters.trebleDb, 1 },
        { "PRESENCE", -12, 12, 0, " dB", &engine.ampParameters.presenceDb, 1 },
        { "LEVEL", -24, 6, -6, " dB", &engine.ampParameters.levelDb, 1 }
    };
    for (size_t i = 0; i < ampKnobs.size(); ++i)
    {
        rackContent.addAndMakeVisible(ampKnobs[i]); rackContent.addAndMakeVisible(ampLabels[i]);
        const auto& setting = ampSettings[i];
        setupKnob(ampKnobs[i], ampLabels[i], setting.name, setting.minimum, setting.maximum, setting.value, setting.suffix);
        ampKnobs[i].onValueChange = [this, i, target = setting.target]
        { target->store(static_cast<float>(ampKnobs[i].getValue())); };
    }

    const KnobSettings settings[] {
        { "RATE", 0.1, 5, 0.8, " Hz", &engine.chorusParameters.rateHz, 1.0f },
        { "DEPTH", 0, 100, 35, " %", &engine.chorusParameters.depth, 0.01f },
        { "MIX", 0, 100, 25, " %", &engine.chorusParameters.mix, 0.01f },
        { "TIME", 20, 1500, 350, " ms", &engine.delayParameters.timeMs, 1.0f },
        { "FEEDBACK", 0, 80, 30, " %", &engine.delayParameters.feedback, 0.01f },
        { "MIX", 0, 100, 20, " %", &engine.delayParameters.mix, 0.01f },
        { "SIZE", 0, 95, 45, " %", &engine.reverbParameters.size, 0.01f },
        { "DAMPING", 0, 100, 50, " %", &engine.reverbParameters.damping, 0.01f },
        { "MIX", 0, 100, 20, " %", &engine.reverbParameters.mix, 0.01f }
    };
    for (size_t i = 0; i < spatialKnobs.size(); ++i)
    {
        rackContent.addAndMakeVisible(spatialKnobs[i]);
        rackContent.addAndMakeVisible(spatialLabels[i]);
        const auto& s = settings[i];
        setupKnob(spatialKnobs[i], spatialLabels[i], s.name, s.minimum, s.maximum, s.value, s.suffix);
        spatialKnobs[i].onValueChange = [this, i, target = s.target, scale = s.scale]
        { target->store(static_cast<float>(spatialKnobs[i].getValue()) * scale); };
    }

    const char* stageNames[] { "04  /  CHORUS", "05  /  DELAY", "06  /  REVERB" };
    const char* descriptions[] { "Stereo modulation", "Smoothed time / feedback", "Stereo room ambience" };
    std::atomic<bool>* bypassTargets[] { &engine.chorusParameters.bypass, &engine.delayParameters.bypass, &engine.reverbParameters.bypass };
    for (size_t i = 0; i < spatialTitles.size(); ++i)
    {
        rackContent.addAndMakeVisible(spatialTitles[i]);
        rackContent.addAndMakeVisible(spatialHints[i]);
        rackContent.addAndMakeVisible(spatialBypasses[i]);
        spatialTitles[i].setText(stageNames[i], juce::dontSendNotification);
        spatialTitles[i].setFont(juce::FontOptions(16.0f, juce::Font::bold));
        spatialTitles[i].setColour(juce::Label::textColourId, accent);
        spatialHints[i].setText(descriptions[i], juce::dontSendNotification);
        spatialHints[i].setColour(juce::Label::textColourId, muted);
        spatialBypasses[i].setButtonText("Bypass");
        spatialBypasses[i].setToggleState(true, juce::dontSendNotification);
        spatialBypasses[i].onClick = [this, i, target = bypassTargets[i]]
        { target->store(spatialBypasses[i].getToggleState()); };
    }

    title.setText("EffectHack", juce::dontSendNotification);
    title.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    subtitle.setText("Choose a source. Shape your sound.", juce::dontSendNotification);
    subtitle.setColour(juce::Label::textColourId, muted);
    status.setColour(juce::Label::textColourId, muted);
    sourceInfo.setColour(juce::Label::textColourId, muted);
    playheadLabel.setColour(juce::Label::textColourId, muted);
    playheadLabel.setJustificationType(juce::Justification::centredRight);
    sourceLabel.setText("Input source", juce::dontSendNotification);
    sourceSelector.addItem("Audio file", 1);
    sourceSelector.addItem("Test riff", 2);
    sourceSelector.addItem("Guitar input", 3);
    sourceSelector.setSelectedId(2, juce::dontSendNotification);
    sourceSelector.onChange = [this] { changeInputSource(); };
    sourceLabel.setVisible(false);
    sourceSelector.setVisible(false);
    loadFileButton.onClick = [this] { chooseAudioFile(); };
    loopButton.setToggleState(true, juce::dontSendNotification);
    loopButton.onClick = [this]
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        engine.fileInput.setLooping(loopButton.getToggleState());
        engine.testInput.setLooping(loopButton.getToggleState());
    };
    restartButton.onClick = [this]
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        engine.seekPlayback(0.0);
    };
    stopButton.onClick = [this]
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        engine.stopPlayback();
        refreshSourceStatus();
        refreshDeviceStatus();
    };
    settingsButton.onClick = [this] { showSettings(); };
    settingsButton.setTooltip("Choose the input source and configure the audio device.");
    contextPlayButton.onClick = [this] { togglePlayback(); };
    inputChannelLabel.setText("Guitar input", juce::dontSendNotification);
    rackTitle.setText("01  /  DISTORTION", juce::dontSendNotification);
    rackTitle.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    rackTitle.setColour(juce::Label::textColourId, accent);
    rackSubtitle.setText("Soft clipping  /  4x oversampling", juce::dontSendNotification);
    rackSubtitle.setColour(juce::Label::textColourId, muted);
    cabinetTitle.setText("02  /  CABINET", juce::dontSendNotification);
    eqTitle.setText("03  /  EQ", juce::dontSendNotification);
    for (auto* label : { &cabinetTitle, &eqTitle })
    {
        label->setFont(juce::FontOptions(18.0f, juce::Font::bold));
        label->setColour(juce::Label::textColourId, accent);
    }
    for (auto* label : { &cabinetInfo, &cabinetHint, &eqHint })
        label->setColour(juce::Label::textColourId, muted);
    cabinetHint.setText("Use a cabinet impulse response up to 500 ms.", juce::dontSendNotification);
    eqHint.setText("120 Hz  /  750 Hz  /  3.2 kHz", juce::dontSendNotification);
    setupKnob(bass, bassLabel, "BASS", -12, 12, 0, " dB");
    setupKnob(mid, midLabel, "MID", -12, 12, 0, " dB");
    setupKnob(treble, trebleLabel, "TREBLE", -12, 12, 0, " dB");
    bass.onValueChange = [this] { engine.eqParameters.bassDb.store(static_cast<float>(bass.getValue())); };
    mid.onValueChange = [this] { engine.eqParameters.midDb.store(static_cast<float>(mid.getValue())); };
    treble.onValueChange = [this] { engine.eqParameters.trebleDb.store(static_cast<float>(treble.getValue())); };
    cabinetBypass.onClick = [this] { engine.cabinetParameters.bypass.store(cabinetBypass.getToggleState()); };
    eqBypass.onClick = [this] { engine.eqParameters.bypass.store(eqBypass.getToggleState()); };
    loadIrButton.onClick = [this] { chooseCabinetImpulse(); };
    builtinCabinetButton.onClick = [this]
    {
        auto impulse = makeBuiltinCabinet();
        {
            const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
            engine.setCabinetImpulse(impulse);
        }
        cabinetName = impulse->name;
        cabinetError.clear();
        refreshCabinetStatus();
    };
    refreshCabinetStatus();
    deviceTitle.setText("Audio setup", juce::dontSendNotification);
    deviceTitle.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    deviceHint.setText("Files and the test riff need only an output device. Scroll for channel and buffer settings.", juce::dontSendNotification);
    deviceHint.setColour(juce::Label::textColourId, muted);

    setupKnob(input, inputLabel, "INPUT", -24, 24, 0, " dB");
    setupKnob(drive, driveLabel, "DRIVE", 0, 48, 18, " dB");
    setupKnob(mix, mixLabel, "MIX", 0, 100, 100, " %");
    setupKnob(output, outputLabel, "OUTPUT", -60, 0, -12, " dB");
    output.setTooltip("Output volume after the complete effects rack.");
    input.onValueChange = [this] { engine.parameters.inputDb.store(static_cast<float>(input.getValue())); };
    drive.onValueChange = [this] { engine.parameters.driveDb.store(static_cast<float>(drive.getValue())); };
    mix.onValueChange = [this] { engine.parameters.mix.store(static_cast<float>(mix.getValue() / 100.0)); };
    output.onValueChange = [this] { engine.parameters.outputDb.store(static_cast<float>(output.getValue())); };
    bypassButton.onClick = [this] { engine.parameters.bypass.store(bypassButton.getToggleState()); };
    channelSelector.onChange = [this] { engine.inputChannel.store(channelSelector.getSelectedId() - 1); };
    monitoringButton.onClick = [this] { togglePlayback(); };
    monitoringButton.setTooltip("Turn audio monitoring on or off.");
    for (auto* component : std::initializer_list<juce::Component*> {
        &input, &inputLabel, &output, &outputLabel, &masterTitle,
        &masterMuteButton, &rackBypassButton, &clearClipsButton, &timeline, &stopButton })
        addAndMakeVisible(component);
    masterTitle.setText("MASTER", juce::dontSendNotification);
    masterTitle.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    masterTitle.setColour(juce::Label::textColourId, accent);
    masterMuteButton.setTooltip("Silence the output while playback continues. Independent of presets.");
    rackBypassButton.setTooltip("Compare the delay-aligned dry signal. Independent of each effect's bypass and presets.");
    masterMuteButton.onClick = [this] { engine.masterMute.store(masterMuteButton.getToggleState()); };
    rackBypassButton.onClick = [this] { engine.rackBypass.store(rackBypassButton.getToggleState()); };
    clearClipsButton.setTooltip("Clear the latched input/output clipping indicators.");
    clearClipsButton.onClick = [this]
    {
        engine.inputClipped.store(false); engine.outputClipped.store(false); repaint(masterBounds);
    };
    timeline.setSliderStyle(juce::Slider::LinearHorizontal);
    timeline.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    timeline.setRange(0.0, 1.0, .001);
    timeline.setScrollWheelEnabled(false);
    timeline.setName("Playback position");
    timeline.setTooltip("Seek within the recording. Drag to preview the time; release to seek.");
    timeline.onDragStart = [this] { draggingTimeline = true; };
    timeline.onDragEnd = [this] { draggingTimeline = false; seekFromTimeline(); };
    timeline.onValueChange = [this]
    {
        if (!draggingTimeline) seekFromTimeline();
    };
    monitoringButton.setTooltip("Play / pause (Space). Typing in a text box keeps its normal behavior.");
    restartButton.setTooltip("Restart at the beginning, keeping play/pause state (Home).");
    stopButton.setTooltip("Pause and rewind to the beginning.");
    setWantsKeyboardFocus(true);

    for (auto* component : std::initializer_list<juce::Component*> {
        &presetSelector, &savePresetButton, &saveAsPresetButton, &renamePresetButton,
        &deletePresetButton, &reloadPresetButton, &presetInfo })
        addAndMakeVisible(component);
    presetInfo.setColour(juce::Label::textColourId, muted);
    presetSelector.setTooltip("Select a preset to load its tone. Unsaved knob changes are replaced.");
    savePresetButton.setTooltip("Update the selected user preset with the current tone.");
    reloadPresetButton.setTooltip("Restore the selected preset and discard unsaved tone changes.");
    presetSelector.onChange = [this] { loadSelectedPreset(); };
    reloadPresetButton.onClick = [this] { loadSelectedPreset(); };
    savePresetButton.onClick = [this] { savePreset(); };
    saveAsPresetButton.onClick = [this] { namePreset(false); };
    renamePresetButton.onClick = [this] { namePreset(true); };
    deletePresetButton.onClick = [this] { deletePreset(); };
    ampTitle.setTooltip(ampHint.getText()); ampHint.setVisible(false);
    rackTitle.setTooltip(rackSubtitle.getText()); rackSubtitle.setVisible(false);
    cabinetTitle.setTooltip(cabinetHint.getText()); cabinetHint.setVisible(false);
    eqTitle.setTooltip(eqHint.getText()); eqHint.setVisible(false);
    for (size_t i = 0; i < spatialHints.size(); ++i)
    {
        spatialTitles[i].setTooltip(spatialHints[i].getText()); spatialHints[i].setVisible(false);
    }
    refreshPresetList();
    refreshPresetControls();
    refreshSignalFlow();

    SessionState session;
    bool found = false;
    const auto restored = sessionStore.read(session, found);
    const bool resumeSetup = found && restored.wasOk();
    std::unique_ptr<juce::XmlElement> deviceState;
    if (resumeSetup)
    {
        sourceSelector.setSelectedId(session.sourceId, juce::dontSendNotification);
        engine.inputSource = session.sourceId == 1 ? AudioEngine::InputSource::file
            : session.sourceId == 2 ? AudioEngine::InputSource::testRiff : AudioEngine::InputSource::hardware;
        engine.inputChannel = session.inputChannel;
        loopButton.setToggleState(session.loop, juce::dontSendNotification);
        engine.fileInput.setLooping(session.loop); engine.testInput.setLooping(session.loop);
        rememberedAudioFile = session.audioFile;
        rigTools.setReference(session.tunerReference);
        rigTools.setMuteToTune(session.tunerMute);
        if (session.deviceXml.isNotEmpty()) deviceState = juce::parseXML(session.deviceXml);
    }
    else if (restored.failed()) { presetMessage = restored.getErrorMessage() + " Using defaults."; presetHasError = true; }
    // JUCE restores device type/names, active channels, sample rate, and buffer size,
    // falling back to an available default device when the saved one is unavailable.
    deviceError = deviceManager.initialise(resumeSetup && session.sourceId == 3 ? 2 : 0, 2, deviceState.get(), true);
    deviceManager.addAudioCallback(&engine);
    if (resumeSetup)
    {
        restoreSessionTone(session);
        if (session.sourceId == 1 && session.audioFile.isNotEmpty()) loadAudioFile(juce::File(session.audioFile));
    }
    refreshSourceStatus();
    refreshDeviceStatus();
    setSize(1120, 1120);
    startTimerHz(30);
}

void MainComponent::reorderStage(const juce::String& stageId, int delta)
{
    const auto& stages = engine.stageChain().stages();
    const auto current = std::find_if(stages.begin(), stages.end(),
        [&stageId](const auto* stage) { return stageId == stage->id(); });
    if (current == stages.end()) return;
    const auto index = static_cast<int>(std::distance(stages.begin(), current));
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        if (!engine.moveStage(stageId, index + delta)) return;
    }
    refreshSignalFlow();
    refreshPresetControls();
    resized();
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSession();
    fileChooser.reset();
    cabinetFileChooser.reset();
    loader.removeAllJobs(true, -1);
    deviceManager.removeAudioCallback(&engine);
    deviceManager.closeAudioDevice();
    setLookAndFeel(nullptr);
}

void MainComponent::changeInputSource()
{
    engine.monitoring.store(false);
    engine.clearEffectTails();
    sourceError.clear();
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        const auto mode = sourceSelector.getSelectedId();
        engine.cancelPlaybackSeek();
        engine.inputSource.store(mode == 1 ? AudioEngine::InputSource::file
            : mode == 2 ? AudioEngine::InputSource::testRiff : AudioEngine::InputSource::hardware);
    }
    // Reopen the device only when moving between virtual and physical inputs.
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        juce::BigInteger desired;
        if (engine.inputSource.load() == AudioEngine::InputSource::hardware)
            desired.setRange(0, juce::jmin(2, device->getInputChannelNames().size()), true);
        if (setup.inputChannels != desired)
        {
            setup.inputChannels = desired;
            setup.useDefaultInputChannels = false;
            deviceError = deviceManager.setAudioDeviceSetup(setup, true);
        }
    }
    refreshSourceStatus();
    refreshDeviceStatus();
    resized();
}

void MainComponent::showSettings()
{
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new SettingsPanel(deviceManager, sourceSelector.getSelectedId(),
        [safe](int selected)
        {
            if (safe != nullptr)
                safe->sourceSelector.setSelectedId(selected, juce::sendNotificationSync);
        }));
    options.content->setSize(760, 560);
    options.dialogTitle = "Input & audio settings";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void MainComponent::chooseAudioFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Choose a guitar recording", juce::File(),
        "*.wav;*.flac;*.mp3;*.aiff;*.aif;*.ogg");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
                return;
            safe->loadAudioFile(file);
        });
}

void MainComponent::loadAudioFile(const juce::File& file)
{
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    engine.monitoring.store(false);
    loadingFile = true;
    sourceError.clear();
    loadFileButton.setEnabled(false);
    refreshSourceStatus();
    loader.addJob([safe, file]
    {
        ClipLoadResult loaded;
        try { loaded = loadAudioClip(file); }
        catch (const std::exception&) { loaded.error = "Could not load this recording into memory."; }
        juce::MessageManager::callAsync([safe, loaded = std::move(loaded)]
        {
            if (safe == nullptr)
                return;
            safe->loadingFile = false;
            safe->loadFileButton.setEnabled(true);
            if (loaded.clip != nullptr)
            {
                {
                    const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock());
                    safe->engine.cancelPlaybackSeek();
                    safe->engine.fileInput.setClip(loaded.clip);
                    safe->engine.clearEffectTails();
                    safe->engine.fileInput.setLooping(safe->loopButton.getToggleState());
                }
                safe->rememberedAudioFile = loaded.clip->sourcePath;
                safe->sourceSelector.setSelectedId(1, juce::sendNotificationSync);
                safe->sourceError.clear();
            }
            else
                safe->sourceError = loaded.error;
            safe->refreshSourceStatus();
        });
    });
}

void MainComponent::refreshSourceStatus()
{
    auto* source = engine.currentVirtualInput();
    const bool hardware = source == nullptr;
    inputChannelLabel.setVisible(hardware);
    channelSelector.setVisible(hardware);
    sourceInfo.setVisible(true);
    playheadLabel.setVisible(!hardware);
    loadFileButton.setVisible(!hardware && sourceSelector.getSelectedId() == 1);
    contextPlayButton.setVisible(!hardware);
    restartButton.setVisible(!hardware);
    stopButton.setVisible(!hardware);
    loopButton.setVisible(!hardware);
    const bool canSeek = source != nullptr && source->hasClip() && !loadingFile;
    restartButton.setEnabled(canSeek);
    stopButton.setEnabled(canSeek);
    timeline.setEnabled(canSeek);
    timeline.setVisible(!hardware);
    if (canSeek)
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        if (!draggingTimeline)
        {
            timeline.setRange(0.0, source->duration(), .001);
            timeline.setValue(engine.playbackPosition(), juce::dontSendNotification);
        }
    }
    else if (!draggingTimeline) timeline.setValue(0.0, juce::dontSendNotification);
    loopButton.setEnabled(!hardware);
    if (loadingFile)
        sourceInfo.setText("Loading recording...", juce::dontSendNotification);
    else if (sourceError.isNotEmpty())
        sourceInfo.setText(sourceError, juce::dontSendNotification);
    else if (source != nullptr && source->getClip() != nullptr)
        sourceInfo.setText(source->getClip()->name + "  |  "
            + juce::String(source->getClip()->sampleRate / 1000.0, 1) + " kHz", juce::dontSendNotification);
    else
        sourceInfo.setText(hardware ? "Choose your guitar interface and enable its input channels below."
                                   : "Load an audio file to use as your virtual guitar input.", juce::dontSendNotification);
    playheadLabel.setText(source != nullptr && source->hasClip()
        ? formatTime(draggingTimeline ? timeline.getValue() : engine.playbackPosition())
            + " / " + formatTime(source->duration()) : hardware ? "LIVE INPUT" : "--:-- / --:--", juce::dontSendNotification);
}

void MainComponent::chooseCabinetImpulse()
{
    loadingCabinet = true;
    refreshCabinetStatus();
    refreshPresetControls();
    cabinetFileChooser = std::make_unique<juce::FileChooser>("Choose a cabinet impulse response", juce::File(),
        "*.wav;*.aif;*.aiff;*.flac");
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    cabinetFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            if (!chooser.getResult().existsAsFile())
            {
                safe->loadingCabinet = false;
                safe->refreshCabinetStatus();
                safe->refreshPresetControls();
                return;
            }
            const auto file = chooser.getResult();
            safe->loadingCabinet = true;
            safe->cabinetError.clear();
            safe->refreshCabinetStatus();
            safe->loader.addJob([safe, file]
            {
                CabinetLoadResult loaded;
                try { loaded = loadCabinetImpulse(file); }
                catch (const std::exception&) { loaded.error = "Could not load this cabinet IR."; }
                juce::MessageManager::callAsync([safe, loaded = std::move(loaded)]
                {
                    if (safe == nullptr)
                        return;
                    safe->loadingCabinet = false;
                    if (loaded.impulse != nullptr)
                    {
                        {
                            const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock());
                            safe->engine.setCabinetImpulse(loaded.impulse);
                        }
                        safe->cabinetName = loaded.impulse->name;
                        safe->cabinetError.clear();
                    }
                    else
                        safe->cabinetError = loaded.error;
                    safe->refreshCabinetStatus();
                });
            });
        });
}

void MainComponent::refreshCabinetStatus()
{
    cabinetInfo.setText(loadingCabinet ? "Loading cabinet IR..."
        : cabinetError.isNotEmpty() ? cabinetError : cabinetName, juce::dontSendNotification);
    cabinetInfo.setTooltip(cabinetInfo.getText());
    loadIrButton.setEnabled(!loadingCabinet && !loadingPreset);
    builtinCabinetButton.setEnabled(!loadingCabinet && !loadingPreset);
}

void MainComponent::setupKnob(juce::Slider& knob, juce::Label& label, const juce::String& name,
                              double minimum, double maximum, double value, const juce::String& suffix)
{
    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, muted);
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 26);
    knob.setRange(minimum, maximum, 0.1);
    knob.setValue(value, juce::dontSendNotification);
    knob.setDoubleClickReturnValue(true, value);
    knob.setTextValueSuffix(suffix);
    knob.setName(name);
    knob.setScrollWheelEnabled(false);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.setColour(panel);
    g.fillRoundedRectangle(deviceBounds.toFloat(), 14.0f);
    g.fillRoundedRectangle(masterBounds.toFloat(), 14.0f);
    g.fillRoundedRectangle(contextBounds.toFloat(), 14.0f);
    auto meters = meterBounds;
    drawMeter(g, meters.removeFromLeft(meters.getWidth() / 2).reduced(12, 0), displayedInput, "INPUT", engine.inputClipped.load());
    drawMeter(g, meters.reduced(12, 0), displayedOutput, "OUTPUT", engine.outputClipped.load());
}

void MainComponent::drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, float level, const juce::String& name, bool clipped)
{
    const auto db = juce::Decibels::gainToDecibels(level, -60.0f);
    const auto text = (db <= -60.0f ? juce::String("-inf") : juce::String(db, 1)) + " dBFS" + (clipped ? "  CLIP" : "");
    auto heading = bounds.removeFromTop(22);
    g.setColour(muted);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(name, heading, juce::Justification::centredLeft);
    const auto tint = clipped ? juce::Colours::salmon : name == "INPUT" ? rigTools.inputColour() : juce::Colour(0xff69d5ba);
    g.setColour(tint); g.drawText(text, heading, juce::Justification::centredRight);
    auto bar = bounds.removeFromTop(10).toFloat();
    g.setColour(juce::Colour(0xff35404c));
    g.fillRoundedRectangle(bar, 3.0f);
    g.setColour(tint);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f)), 3.0f);
    if (name == "INPUT")
    {
        g.setColour(juce::Colour(0xff69d5ba));
        for (float target : { -18.0f, -6.0f })
        {
            const auto x = bar.getX() + bar.getWidth() * (target + 60) / 60;
            g.drawLine(x, bar.getBottom() + 1, x, bar.getBottom() + 5, 1);
        }
    }
    g.setFont(juce::FontOptions(10)); g.setColour(muted);
    auto ticks = bounds.removeFromTop(20);
    g.drawText("-60", ticks, juce::Justification::centredLeft);
    g.drawText("0", ticks, juce::Justification::centredRight);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(28);
    auto header = area.removeFromTop(64);
    monitoringButton.setBounds(header.removeFromRight(52).withSizeKeepingCentre(46, 42));
    settingsButton.setBounds(header.removeFromRight(155).withSizeKeepingCentre(155, 42));
    tunerButton.setBounds(header.removeFromRight(92).reduced(4, 11));
    songButton.setBounds(header.removeFromRight(86).reduced(4, 11));
    title.setBounds(header.removeFromTop(38));
    subtitle.setBounds(header.removeFromTop(26));
    status.setBounds(area.removeFromTop(26));
    auto presetRow = area.removeFromTop(38);
    for (auto* button : { &revertSongButton, &deletePresetButton, &renamePresetButton, &saveAsPresetButton,
                          &savePresetButton, &reloadPresetButton })
        button->setBounds(presetRow.removeFromRight(94).reduced(3, 3));
    presetSelector.setBounds(presetRow.reduced(0, 3));
    presetInfo.setBounds(area.removeFromTop(24));
    masterBounds = area.removeFromTop(130);
    auto master = masterBounds.reduced(14);
    masterTitle.setBounds(master.removeFromLeft(88).removeFromTop(28));
    for (auto pair : { std::pair { &input, &inputLabel }, std::pair { &output, &outputLabel } })
    {
        auto column = master.removeFromLeft(125);
        pair.second->setBounds(column.removeFromTop(20));
        pair.first->setBounds(column);
    }
    master.removeFromLeft(10);
    auto actions = master.removeFromTop(30);
    clearClipsButton.setBounds(actions.removeFromRight(94));
    masterMuteButton.setBounds(actions.removeFromLeft(75));
    rackBypassButton.setBounds(actions);
    master.removeFromTop(8);
    meterBounds = master;
    area.removeFromTop(8);
    signalFlow.setBounds(area.removeFromTop(38));
    area.removeFromTop(8);
    rackHint.setBounds(area.removeFromTop(24));
    contextBounds = area.removeFromBottom(156);
    area.removeFromBottom(12);
    rackViewport.setBounds(area);
    const auto contentWidth = juce::jmax(1, rackViewport.getWidth() - rackViewport.getScrollBarThickness());
    rackContent.setSize(contentWidth, 172 + static_cast<int>(rackStageIds.size()) * 220 + static_cast<int>(rackStageIds.size() - 1) * 12);
    auto rackArea = rackContent.getLocalBounds();
    rigTools.setBounds(rackArea.removeFromTop(160));
    rackArea.removeFromTop(12);
    for (size_t i = 0; i < dynamicsBounds.size(); ++i)
    {
        dynamicsBounds[i] = rackArea.removeFromTop(220);
        rackArea.removeFromTop(12);
        rackContent.panels[i] = dynamicsBounds[i];
    }
    const auto layoutDynamics = [this](auto& knobs, auto& labels, size_t index)
    {
        auto inside = dynamicsBounds[index].reduced(16);
        auto stageHeader = inside.removeFromTop(30);
        moveUpButtons[index].setBounds(stageHeader.removeFromRight(42).reduced(2));
        moveDownButtons[index].setBounds(stageHeader.removeFromRight(48).reduced(2));
        dynamicsBypasses[index].setBounds(stageHeader.removeFromRight(185));
        dynamicsTitles[index].setBounds(stageHeader);
        reductionMeters[index].setBounds(inside.removeFromTop(32).removeFromLeft(220));
        const auto columnWidth = inside.getWidth() / static_cast<int>(knobs.size());
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            auto column = inside.removeFromLeft(columnWidth);
            labels[i].setBounds(column.removeFromTop(22));
            knobs[i].setBounds(column);
        }
    };
    layoutDynamics(gateKnobs, gateLabels, 0);
    layoutDynamics(compressorKnobs, compressorLabels, 1);
    rackBounds = rackArea.removeFromTop(220);
    auto rack = rackBounds.reduced(20);
    auto rackHeader = rack.removeFromTop(30);
    moveUpButtons[2].setBounds(rackHeader.removeFromRight(42).reduced(2));
    moveDownButtons[2].setBounds(rackHeader.removeFromRight(48).reduced(2));
    bypassButton.setBounds(rackHeader.removeFromRight(175));
    rackTitle.setBounds(rackHeader);
    auto description = rack.removeFromLeft(juce::roundToInt(rack.getWidth() * .4));
    rackSubtitle.setBounds(description.removeFromTop(24));
    const auto width = rack.getWidth() / 2;
    juce::Slider* knobs[] { &drive, &mix };
    juce::Label* labels[] { &driveLabel, &mixLabel };
    for (int i = 0; i < 2; ++i)
    {
        auto column = rack.removeFromLeft(width);
        labels[i]->setBounds(column.removeFromTop(22));
        knobs[i]->setBounds(column);
    }
    rackArea.removeFromTop(12);
    ampBounds = rackArea.removeFromTop(220);
    rackArea.removeFromTop(12);
    cabinetBounds = rackArea.removeFromTop(220);
    rackArea.removeFromTop(12);
    eqBounds = rackArea.removeFromTop(220);
    auto ampArea = ampBounds.reduced(16);
    auto ampHeader = ampArea.removeFromTop(30);
    moveUpButtons[3].setBounds(ampHeader.removeFromRight(42).reduced(2));
    moveDownButtons[3].setBounds(ampHeader.removeFromRight(48).reduced(2));
    ampBypass.setBounds(ampHeader.removeFromRight(125));
    ampVoiceSelector.setBounds(ampHeader.removeFromRight(170).reduced(0, 1));
    ampTitle.setBounds(ampHeader);
    ampHint.setBounds(ampArea.removeFromTop(24));
    const auto ampWidth = ampArea.getWidth() / static_cast<int>(ampKnobs.size());
    for (size_t i = 0; i < ampKnobs.size(); ++i)
    {
        auto column = ampArea.removeFromLeft(ampWidth);
        ampLabels[i].setBounds(column.removeFromTop(22));
        ampKnobs[i].setBounds(column);
    }
    auto cab = cabinetBounds.reduced(16);
    auto cabHeader = cab.removeFromTop(28);
    moveUpButtons[4].setBounds(cabHeader.removeFromRight(42).reduced(2));
    moveDownButtons[4].setBounds(cabHeader.removeFromRight(48).reduced(2));
    cabinetTitle.setBounds(cabHeader);
    cabinetBypass.setBounds(cab.removeFromTop(28));
    cabinetInfo.setBounds(cab.removeFromTop(30));
    cabinetHint.setBounds(cab.removeFromTop(30));
    auto buttons = cab.removeFromTop(34);
    loadIrButton.setBounds(buttons.removeFromLeft(112));
    buttons.removeFromLeft(8);
    builtinCabinetButton.setBounds(buttons.removeFromLeft(120));
    auto eq = eqBounds.reduced(16);
    auto eqHeader = eq.removeFromTop(28);
    moveUpButtons[5].setBounds(eqHeader.removeFromRight(42).reduced(2));
    moveDownButtons[5].setBounds(eqHeader.removeFromRight(48).reduced(2));
    eqBypass.setBounds(eqHeader.removeFromRight(130));
    eqTitle.setBounds(eqHeader);
    eqHint.setBounds(eq.removeFromTop(24));
    const auto eqWidth = eq.getWidth() / 3;
    juce::Slider* eqKnobs[] { &bass, &mid, &treble };
    juce::Label* eqLabels[] { &bassLabel, &midLabel, &trebleLabel };
    for (int i = 0; i < 3; ++i)
    {
        auto column = eq.removeFromLeft(eqWidth);
        eqLabels[i]->setBounds(column.removeFromTop(22));
        eqKnobs[i]->setBounds(column);
    }
    rackArea.removeFromTop(12);
    const auto spatialHeight = 220;
    rackContent.panels[2] = rackBounds;
    rackContent.panels[3] = ampBounds;
    rackContent.panels[4] = cabinetBounds;
    rackContent.panels[5] = eqBounds;
    for (size_t stage = 0; stage < 3; ++stage)
    {
        auto panelArea = rackArea.removeFromTop(spatialHeight);
        rackContent.panels[stage + 6] = panelArea;
        auto inside = panelArea.reduced(12);
        auto heading = inside.removeFromTop(28);
        moveUpButtons[stage + 6].setBounds(heading.removeFromRight(42).reduced(2));
        moveDownButtons[stage + 6].setBounds(heading.removeFromRight(48).reduced(2));
        spatialBypasses[stage].setBounds(heading.removeFromRight(84));
        spatialTitles[stage].setBounds(heading);
        spatialHints[stage].setBounds(inside.removeFromTop(24));
        const auto widthPerKnob = inside.getWidth() / 3;
        for (size_t knob = 0; knob < 3; ++knob)
        {
            auto column = inside.removeFromLeft(widthPerKnob);
            const auto index = stage * 3 + knob;
            spatialLabels[index].setBounds(column.removeFromTop(22));
            spatialKnobs[index].setBounds(column);
        }
        rackArea.removeFromTop(12);
    }
    const std::array<std::vector<juce::Component*>, rackStageIds.size()> stageControls {
        std::vector<juce::Component*> { &dynamicsTitles[0], &reductionMeters[0], &dynamicsBypasses[0],
            &gateKnobs[0], &gateKnobs[1], &gateKnobs[2], &gateKnobs[3],
            &gateLabels[0], &gateLabels[1], &gateLabels[2], &gateLabels[3], &moveUpButtons[0], &moveDownButtons[0] },
        std::vector<juce::Component*> { &dynamicsTitles[1], &reductionMeters[1], &dynamicsBypasses[1],
            &compressorKnobs[0], &compressorKnobs[1], &compressorKnobs[2], &compressorKnobs[3], &compressorKnobs[4], &compressorKnobs[5],
            &compressorLabels[0], &compressorLabels[1], &compressorLabels[2], &compressorLabels[3], &compressorLabels[4], &compressorLabels[5],
            &moveUpButtons[1], &moveDownButtons[1] },
        std::vector<juce::Component*> { &rackTitle, &rackSubtitle, &drive, &driveLabel,
            &mix, &mixLabel, &bypassButton, &moveUpButtons[2], &moveDownButtons[2] },
        std::vector<juce::Component*> { &ampTitle, &ampHint, &ampVoiceSelector, &ampBypass,
            &ampKnobs[0], &ampKnobs[1], &ampKnobs[2], &ampKnobs[3], &ampKnobs[4], &ampKnobs[5],
            &ampLabels[0], &ampLabels[1], &ampLabels[2], &ampLabels[3], &ampLabels[4], &ampLabels[5],
            &moveUpButtons[3], &moveDownButtons[3] },
        std::vector<juce::Component*> { &cabinetTitle, &cabinetBypass, &cabinetInfo, &cabinetHint,
            &loadIrButton, &builtinCabinetButton, &moveUpButtons[4], &moveDownButtons[4] },
        std::vector<juce::Component*> { &eqTitle, &eqHint, &eqBypass, &bass, &mid, &treble,
            &bassLabel, &midLabel, &trebleLabel, &moveUpButtons[5], &moveDownButtons[5] },
        std::vector<juce::Component*> { &spatialTitles[0], &spatialHints[0], &spatialBypasses[0],
            &spatialKnobs[0], &spatialKnobs[1], &spatialKnobs[2], &spatialLabels[0], &spatialLabels[1],
            &spatialLabels[2], &moveUpButtons[6], &moveDownButtons[6] },
        std::vector<juce::Component*> { &spatialTitles[1], &spatialHints[1], &spatialBypasses[1],
            &spatialKnobs[3], &spatialKnobs[4], &spatialKnobs[5], &spatialLabels[3], &spatialLabels[4],
            &spatialLabels[5], &moveUpButtons[7], &moveDownButtons[7] },
        std::vector<juce::Component*> { &spatialTitles[2], &spatialHints[2], &spatialBypasses[2],
            &spatialKnobs[6], &spatialKnobs[7], &spatialKnobs[8], &spatialLabels[6], &spatialLabels[7],
            &spatialLabels[8], &moveUpButtons[8], &moveDownButtons[8] }
    };
    const auto& stages = engine.stageChain().stages();
    for (size_t source = 0; source < stageControls.size(); ++source)
    {
        const auto found = std::find_if(stages.begin(), stages.end(),
            [source](const auto& stage) { return juce::String(stageIds[source]) == stage->id(); });
        if (found == stages.end())
            continue;
        const auto target = static_cast<size_t>(std::distance(stages.begin(), found));
        moveUpButtons[source].setEnabled(target > 0);
        moveDownButtons[source].setEnabled(target + 1 < stages.size());
        juce::Label* headings[] { &dynamicsTitles[0], &dynamicsTitles[1], &rackTitle, &ampTitle, &cabinetTitle, &eqTitle,
            &spatialTitles[0], &spatialTitles[1], &spatialTitles[2] };
        headings[source]->setText(juce::String(static_cast<int>(target) + 1).paddedLeft('0', 2)
            + "  /  " + juce::String((*found)->name()).toUpperCase(), juce::dontSendNotification);
        const auto delta = rackContent.panels[target].getPosition() - rackContent.panels[source].getPosition();
        for (auto* component : stageControls[source])
            component->setBounds(component->getBounds().translated(delta.x, delta.y));
    }
    rackContent.repaint();
    area.removeFromTop(12);
    deviceBounds = area;
    auto setup = deviceBounds.reduced(16);
    deviceTitle.setBounds(setup.removeFromTop(28));
    deviceHint.setBounds(setup.removeFromTop(26));
    deviceViewport.setBounds(setup);
    // The device selector sizes itself to its controls, which can exceed this panel.
    devices.setSize(juce::jmax(1, setup.getWidth() - deviceViewport.getScrollBarThickness()),
                    juce::jmax(200, devices.getHeight()));

    auto context = contextBounds.reduced(14);
    sourceInfo.setBounds(context.removeFromTop(24));
    if (engine.currentVirtualInput() != nullptr)
    {
        auto controls = context.removeFromTop(38);
        contextPlayButton.setBounds(controls.removeFromLeft(80).reduced(0, 3));
        loadFileButton.setBounds(controls.removeFromLeft(145).reduced(0, 3));
        restartButton.setBounds(controls.removeFromLeft(90).reduced(0, 3));
        stopButton.setBounds(controls.removeFromLeft(70).reduced(0, 3));
        loopButton.setBounds(controls.removeFromLeft(80));
        playheadLabel.setBounds(controls.removeFromRight(155));
        timeline.setBounds(context.removeFromTop(28).reduced(0, 2));
    }
    else
    {
        auto routing = context.removeFromTop(38);
        inputChannelLabel.setBounds(routing.removeFromLeft(110));
        channelSelector.setBounds(routing.removeFromLeft(320).reduced(0, 3));
        sourceInfo.setBounds(context.removeFromTop(24));
    }
}

void MainComponent::timerCallback()
{
    serviceToneChange();
    rigTools.refresh();
    subtitle.setText(rigTools.performanceSummary(), juce::dontSendNotification);
    subtitle.setColour(juce::Label::textColourId, rigTools.performanceColour());
    subtitle.setTooltip("Audio callback load and reported/estimated dropouts since device restart or Reset in the tools panel.");
    input.setTooltip(rigTools.inputMessage() + "\nInput trim changes rack drive; source level is set at the interface. See SOURCE/TRIM meters above the rack.");
    inputLabel.setTooltip(input.getTooltip());
    displayedInput = juce::jmax(displayedInput * 0.88f, engine.inputPeak.load(std::memory_order_relaxed));
    displayedOutput = juce::jmax(displayedOutput * 0.88f, engine.outputPeak.load(std::memory_order_relaxed));
    refreshDeviceStatus();
    refreshSourceStatus();
    refreshPresetControls();
    refreshSignalFlow();
    const auto gateReduction = engine.gateReductionDb();
    const auto compressionReduction = engine.compressionReductionDb();
    reductionMeters[0].setValue(gateReduction, gateReduction > .1f ? accent : muted, !engine.gateParameters.bypass.load());
    reductionMeters[1].setValue(compressionReduction, compressionReduction > .1f ? accent : muted, !engine.compressorParameters.bypass.load());
    repaint(meterBounds);
}

void MainComponent::refreshSignalFlow()
{
    signalFlow.setState(engine.stageChain().order(), {
        engine.gateParameters.bypass.load(std::memory_order_relaxed),
        engine.compressorParameters.bypass.load(std::memory_order_relaxed),
        engine.parameters.bypass.load(std::memory_order_relaxed),
        engine.ampParameters.bypass.load(std::memory_order_relaxed),
        engine.cabinetParameters.bypass.load(std::memory_order_relaxed),
        engine.eqParameters.bypass.load(std::memory_order_relaxed),
        engine.chorusParameters.bypass.load(std::memory_order_relaxed),
        engine.delayParameters.bypass.load(std::memory_order_relaxed),
        engine.reverbParameters.bypass.load(std::memory_order_relaxed)
    });
}

void MainComponent::refreshDeviceStatus()
{
    const auto enabled = engine.monitoring.load();
    auto* virtualInput = engine.currentVirtualInput();
    monitoringButton.setButtonText(juce::String::fromUTF8("\xe2\x8f\xbb"));
    contextPlayButton.setButtonText(enabled ? "Pause" : "Play");
    monitoringButton.setColour(juce::TextButton::buttonColourId,
                               enabled ? juce::Colour(0xffa34f20) : juce::Colour(0xff35404c));
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        status.setText("Select an audio device below." + (deviceError.isNotEmpty() ? " " + deviceError : ""), juce::dontSendNotification);
        monitoringButton.setEnabled(false);
        channelSelector.setEnabled(false);
        return;
    }
    const auto active = device->getActiveInputChannels();
    const auto names = device->getInputChannelNames();
    juce::StringArray available;
    for (int i = 0; i < names.size(); ++i)
        if (active[i])
            available.add(names[i]);
    const auto signature = available.joinIntoString("|");
    if (lastInputNames != signature || channelSelector.getNumItems() != available.size())
    {
        lastInputNames = signature;
        channelSelector.clear(juce::dontSendNotification);
        for (int i = 0; i < available.size(); ++i)
            channelSelector.addItem(available[i], i + 1);
        const auto selection = juce::jlimit(0, juce::jmax(0, available.size() - 1), engine.inputChannel.load());
        if (!available.isEmpty()) engine.inputChannel.store(selection);
        channelSelector.setSelectedId(available.isEmpty() ? 0 : selection + 1, juce::dontSendNotification);
        channelSelector.setTextWhenNothingSelected("Enable an input channel below");
    }
    const bool canMonitor = (virtualInput != nullptr ? virtualInput->hasClip() : !available.isEmpty())
        && !device->getActiveOutputChannels().isZero();
    monitoringButton.setEnabled(canMonitor && !restoringSession && !loadingFile);
    contextPlayButton.setEnabled(canMonitor && !restoringSession && !loadingFile);
    channelSelector.setEnabled(!available.isEmpty());
    const auto sampleRate = device->getCurrentSampleRate();
    const auto latencyMs = sampleRate > 0.0 ? 1000.0 * ((virtualInput == nullptr ? device->getInputLatencyInSamples() : 0)
        + device->getOutputLatencyInSamples() + engine.effectLatency.load()) / sampleRate : 0.0;
    const auto transportStatus = enabled ? (engine.tailPlaying.load() ? "TAIL  |  " : virtualInput != nullptr ? "PLAYING  |  " : "LIVE  |  ")
        : virtualInput != nullptr ? "PAUSED  |  " : "STOPPED  |  ";
    status.setText(juce::String(transportStatus) + (engine.masterMute.load() ? "MUTED  |  " : "")
        + (engine.tunerEnabled.load() && engine.tunerMute.load() ? "TUNING MUTE  |  " : "")
        + (engine.rackBypass.load() ? "RACK BYPASSED  |  " : "") + device->getName(), juce::dontSendNotification);
    status.setTooltip(juce::String(sampleRate / 1000.0, 1) + " kHz / "
        + juce::String(device->getCurrentBufferSizeSamples()) + " samples / ~"
        + juce::String(latencyMs, 1) + " ms driver-reported and rack latency.");
}
}

namespace effecthack
{
void MainComponent::refreshPresetList()
{
    juce::String warning;
    presets = presetManager.list(warning);
    presetSelector.clear(juce::dontSendNotification);
    presetSelector.addSectionHeading("Factory presets");
    presetSelector.addItem("Default rack", 1);
    int selection = 1;
    const auto& factory = factoryPresets();
    for (size_t i = 0; i < factory.size(); ++i)
    {
        const auto item = static_cast<int>(i) + 2;
        presetSelector.addItem(factory[i].preset.name, item);
        if (factory[i].preset.id == activePreset.id) selection = item;
    }
    if (isGeneratedPreset())
    {
        presetSelector.addSeparator(); presetSelector.addSectionHeading("Generated");
        presetSelector.addItem(activePreset.name, 900); selection = 900;
    }
    if (!presets.empty()) { presetSelector.addSeparator(); presetSelector.addSectionHeading("Your presets"); }
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto item = static_cast<int>(i) + 1000;
        presetSelector.addItem(presets[i].name, item);
        if (presets[i].id == activePreset.id) selection = item;
    }
    presetSelector.setSelectedId(selection, juce::dontSendNotification);
    if (warning.isNotEmpty()) { presetMessage += "  /  " + warning; presetHasError = true; }
}
void MainComponent::refreshPresetControls()
{
    const bool busy = loadingPreset || loadingCabinet;
    songButton.setEnabled(!busy);
    revertSongButton.setEnabled(previousSongTone.has_value() && !busy);
    const bool userPreset = !isFactoryPreset(activePreset.id);
    savePresetButton.setTooltip(isGeneratedPreset() ? "Save this generated tone as a user preset." : userPreset ? "Update the selected user preset with the current tone."
        : "Make a custom copy of this factory preset.");
    presetSelector.setEnabled(!busy);
    for (auto* button : { &savePresetButton, &saveAsPresetButton, &reloadPresetButton })
        button->setEnabled(!busy);
    renamePresetButton.setEnabled(!busy && userPreset);
    deletePresetButton.setEnabled(!busy && userPreset);
    rackContent.setEnabled(!loadingPreset);
    input.setEnabled(!loadingPreset);
    output.setEnabled(!loadingPreset);
    const bool dirty = !captureRack(engine).matches(activePreset.rack);
    const auto text = loadingPreset ? juce::String("...") : presetHasError ? presetMessage
        : dirty ? juce::String("Modified") : isGeneratedPreset() ? juce::String("Generated") : userPreset ? juce::String("Saved") : activePreset.id.isEmpty() ? juce::String("Default") : juce::String("Factory");
    if (presetInfo.getText() != text) presetInfo.setText(text, juce::dontSendNotification);
    presetInfo.setColour(juce::Label::textColourId, presetHasError ? juce::Colours::salmon : dirty ? accent : muted);
    presetInfo.setTooltip(text + "\n" + presetMessage + "\nPreset folder: " + presetManager.directory().getFullPathName());
}
void MainComponent::loadSelectedPreset()
{
    if (loadingPreset || loadingCabinet) return;
    const auto selection = presetSelector.getSelectedId();
    Preset requested;
    if (selection == 1) requested = { {}, "Default rack", {} };
    else if (selection == 900 && isGeneratedPreset()) requested = activePreset;
    else if (selection >= 2 && selection < static_cast<int>(factoryPresets().size()) + 2)
        requested = factoryPresets()[static_cast<size_t>(selection - 2)].preset;
    else if (selection >= 1000 && selection - 1000 < static_cast<int>(presets.size()))
        requested = presets[static_cast<size_t>(selection - 1000)];
    else return;
    loadingPreset = true;
    refreshPresetControls();
    refreshCabinetStatus();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    loader.addJob([safe, requested, manager = presetManager]
    {
        auto preset = requested;
        auto prepared = PreparedPreset {};
        try
        {
            auto result = preset.id.isEmpty() ? juce::Result::ok() : manager.read(preset.id, preset);
            if (result.failed()) prepared.error = result.getErrorMessage();
            else prepared = preparePreset(preset.rack);
        }
        catch (const std::exception&) { prepared.error = "Could not prepare this preset."; }
        juce::MessageManager::callAsync([safe, preset, prepared = std::move(prepared)]
        {
            if (safe == nullptr) return;
            auto result = juce::Result::ok();
            {
                const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock());
                result = applyPreset(safe->engine, prepared);
            }
            safe->loadingPreset = false;
            if (result.wasOk())
            {
                safe->activePreset = preset;
                safe->syncRackControls(preset.rack);
                safe->refreshSignalFlow();
                safe->resized();
                safe->cabinetName = prepared.impulse->name;
                safe->cabinetError.clear();
                safe->presetMessage = "Loaded " + preset.name;
                safe->presetHasError = false;
                if (const auto* factory = findFactoryPreset(preset.id)) safe->presetMessage += "  /  " + factory->description;
            }
            else { safe->presetMessage = "Load failed: " + result.getErrorMessage(); safe->presetHasError = true; }
            safe->refreshPresetList();
            safe->refreshPresetControls();
            safe->refreshCabinetStatus();
        });
    });
}
void MainComponent::savePreset()
{
    if (isFactoryPreset(activePreset.id)) { namePreset(false); return; }
    auto updated = activePreset;
    updated.rack = captureRack(engine);
    const auto result = presetManager.save(updated);
    if (result.wasOk())
    {
        activePreset = updated;
        presetMessage = "Updated " + updated.name;
        presetHasError = false;
        refreshPresetList();
    }
    else { presetMessage = "Save failed: " + result.getErrorMessage(); presetHasError = true; }
    refreshPresetControls();
}
void MainComponent::namePreset(bool rename)
{
    if (rename && isFactoryPreset(activePreset.id)) return;
    auto* dialog = new juce::AlertWindow(rename ? "Rename preset" : "Save preset as",
        "Choose a unique name (up to 64 characters).", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", rename || isGeneratedPreset() ? activePreset.name : juce::String(), "Preset name:");
    dialog->addButton(rename ? "Rename" : "Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    auto safeDialog = juce::Component::SafePointer<juce::AlertWindow>(dialog);
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, safeDialog, rename](int choice)
    {
        if (choice != 1 || safe == nullptr || safeDialog == nullptr) return;
        Preset saved;
        const auto name = safeDialog->getTextEditorContents("name");
        const auto result = rename
            ? safe->presetManager.rename(safe->activePreset, name, saved)
            : safe->presetManager.saveAs(name, captureRack(safe->engine), saved);
        if (result.wasOk())
        {
            safe->activePreset = saved;
            safe->presetMessage = (rename ? "Renamed to " : "Saved ") + saved.name;
            safe->presetHasError = false;
            safe->refreshPresetList();
        }
        else { safe->presetMessage = "Could not save: " + result.getErrorMessage(); safe->presetHasError = true; }
        safe->refreshPresetControls();
    }), true);
}
void MainComponent::deletePreset()
{
    if (isFactoryPreset(activePreset.id)) return;
    const auto selected = activePreset;
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Delete preset?")
        .withMessage("Delete \"" + selected.name + "\"? The current tone will stay in the rack.")
        .withButton("Delete").withButton("Cancel"), [safe, selected](int choice)
        {
            if (choice != 1 || safe == nullptr) return;
            const auto result = safe->presetManager.remove(selected.id);
            if (result.wasOk())
            {
                safe->activePreset = { {}, "Default rack", {} };
                safe->presetMessage = "Deleted " + selected.name + "; current tone kept";
                safe->presetHasError = false;
                safe->refreshPresetList();
            }
            else { safe->presetMessage = "Delete failed: " + result.getErrorMessage(); safe->presetHasError = true; }
            safe->refreshPresetControls();
        });
}
void MainComponent::syncRackControls(const RackState& rack)
{
    juce::Slider* knobs[] { &input, &drive, &mix, &output, &bass, &mid, &treble };
    for (size_t i = 0; i < std::size(knobs); ++i)
        knobs[i]->setValue(rack.values[i] * (i == 2 ? 100.0 : 1.0), juce::dontSendNotification);
    for (size_t i = 0; i < spatialKnobs.size(); ++i)
        spatialKnobs[i].setValue(rack.values[i + 7] * (i == 0 || i == 3 ? 1.0 : 100.0), juce::dontSendNotification);
    for (size_t i = 0; i < ampKnobs.size(); ++i) ampKnobs[i].setValue(rack.values[i + 16], juce::dontSendNotification);
    ampVoiceSelector.setSelectedId(rack.ampVoice + 1, juce::dontSendNotification);
    for (size_t i = 0; i < gateKnobs.size(); ++i) gateKnobs[i].setValue(rack.values[i + 22], juce::dontSendNotification);
    for (size_t i = 0; i < compressorKnobs.size(); ++i)
        compressorKnobs[i].setValue(rack.values[i + 26] * (i == 5 ? 100.0 : 1.0), juce::dontSendNotification);
    juce::ToggleButton* bypassControls[] { &dynamicsBypasses[0], &dynamicsBypasses[1], &bypassButton, &ampBypass, &cabinetBypass, &eqBypass,
        &spatialBypasses[0], &spatialBypasses[1], &spatialBypasses[2] };
    for (size_t i = 0; i < std::size(bypassControls); ++i)
        bypassControls[i]->setToggleState(rack.bypass[i], juce::dontSendNotification);
}
}

namespace effecthack
{
juce::String MainComponent::formatTime(double seconds)
{
    const auto total = juce::jmax(0, static_cast<int>(std::floor(seconds)));
    return juce::String(total / 60).paddedLeft('0', 2) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}
void MainComponent::togglePlayback()
{
    if (loadingFile || restoringSession) return;
    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        if (engine.monitoring.load()) engine.monitoring.store(false);
        else if (!device->getActiveOutputChannels().isZero())
        {
            if (engine.currentVirtualInput() != nullptr) engine.resumePlayback();
            else engine.monitoring.store(!device->getActiveInputChannels().isZero());
        }
    }
    refreshDeviceStatus();
}
void MainComponent::seekFromTimeline()
{
    if (!timeline.isEnabled() || draggingTimeline) return;
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    engine.seekPlayback(timeline.getValue());
}
bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown() || isCurrentlyBlockedByAnotherModalComponent()) return false;
    for (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused != nullptr; focused = focused->getParentComponent())
        if (dynamic_cast<juce::TextEditor*>(focused) != nullptr) return false;
    if (key.isKeyCode(juce::KeyPress::spaceKey))
    {
        if (!spaceHeld && monitoringButton.isEnabled()) togglePlayback();
        spaceHeld = true;
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::homeKey) && restartButton.isEnabled())
    {
        if (!homeHeld) restartButton.onClick();
        homeHeld = true;
        return true;
    }
    return false;
}
bool MainComponent::keyStateChanged(bool)
{
    if (!juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey)) spaceHeld = false;
    if (!juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::homeKey)) homeHeld = false;
    return false;
}
}
