#include "MainComponent.h"

namespace effecthack
{
void MainComponent::initialiseDryPhrase()
{
    addAndMakeVisible(phrasePanel);
    phrasePanel.record.onClick = [this] { togglePhraseRecording(); grabKeyboardFocus(); };
    phrasePanel.audition.onClick = [this] { togglePhraseAudition(); grabKeyboardFocus(); };
    phrasePanel.live.onClick = [this] { returnFromPhrase(); grabKeyboardFocus(); };
    phrasePanel.clear.onClick = [this] { clearPhrase(); grabKeyboardFocus(); };
    refreshDryPhrase();
}
void MainComponent::abortPhraseCapture()
{
    ++phraseRequest; preparingPhrase = false;
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    engine.phraseRecorder.cancel();
    phrasePanel.setPeaks(phrasePeaks);
}
void MainComponent::togglePhraseRecording()
{
    if (preparingPhrase || clearingPhrase) return;
    if (engine.phraseRecorder.state() == DryPhraseRecorder::State::ready)
    { finishPhraseRecording(); refreshDryPhrase(); return; }
    if (engine.phraseRecorder.capturing())
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        engine.phraseRecorder.stop();
    }
    else
    {
        auto* device = deviceManager.getCurrentAudioDevice();
        if (device == nullptr || !phrasePanel.record.isEnabled()) return;
        phraseError.clear();
        phraseCaptureRate = device->getCurrentSampleRate();
        std::shared_ptr<AudioClip> buffer;
        try { buffer = DryPhraseRecorder::allocate(phraseCaptureRate); }
        catch (const std::exception&) { phraseError = "Could not allocate recording memory."; }
        if (buffer == nullptr) { if (phraseError.isEmpty()) phraseError = "Unsupported recording sample rate."; return; }
        ++phraseRequest; phraseGeneration = engine.deviceGeneration.load();
        {
            const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
            engine.requestPhraseAudition(false);
            if (engine.inputSource.load() == AudioEngine::InputSource::file) engine.fileInput.resume();
            if (engine.inputSource.load() == AudioEngine::InputSource::testRiff) engine.testInput.resume();
            engine.phraseRecorder.arm(std::move(buffer));
            engine.monitoring.store(true);
        }
        phrasePanel.setPeaks({});
    }
    refreshDryPhrase();
}
void MainComponent::finishPhraseRecording()
{
    std::shared_ptr<AudioClip> buffer;
    int samples = 0;
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        samples = engine.phraseRecorder.length(); buffer = engine.phraseRecorder.take();
    }
    if (buffer == nullptr) return;
    preparingPhrase = true;
    const auto serial = ++phraseRequest;
    const auto generation = phraseGeneration;
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    phraseWorker.addJob([safe, buffer = std::move(buffer), samples, serial, generation]() mutable
    {
        DryPhrase phrase;
        try { phrase = DryPhraseRecorder::finish(std::move(buffer), samples); }
        catch (const std::exception&) { phrase.error = "Could not prepare the recorded phrase."; }
        juce::MessageManager::callAsync([safe, serial, generation, phrase = std::move(phrase)]
        {
            if (safe == nullptr || serial != safe->phraseRequest) return;
            safe->preparingPhrase = false; safe->phraseError = phrase.error;
            if (phrase.clip != nullptr)
            {
                const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock());
                safe->engine.phraseInput.setClip(phrase.clip);
                safe->engine.phraseInput.setLooping(true);
                if (safe->engine.monitoring.load() && safe->engine.deviceGeneration.load() == generation)
                    safe->engine.requestPhraseAudition(true);
                safe->phrasePeaks = phrase.peaks;
            }
            safe->phrasePanel.setPeaks(safe->phrasePeaks);
            safe->refreshSourceStatus(); safe->refreshDryPhrase(); safe->resized();
        });
    });
}
void MainComponent::togglePhraseAudition()
{
    if (preparingPhrase || clearingPhrase || engine.phraseRecorder.capturing() || !engine.phraseInput.hasClip()) return;
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        const auto audition = !engine.isPhraseAuditioning();
        engine.requestPhraseAudition(audition);
        if (audition) engine.monitoring.store(true);
    }
    refreshDryPhrase(); refreshSourceStatus(); resized();
}
void MainComponent::returnFromPhrase()
{
    const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
    engine.requestPhraseAudition(false);
}
void MainComponent::clearPhrase()
{
    abortPhraseCapture(); phraseError.clear(); clearingPhrase = true;
    returnFromPhrase(); refreshDryPhrase();
}
void MainComponent::refreshDryPhrase()
{
    if (!preparingPhrase && !clearingPhrase && engine.phraseRecorder.state() == DryPhraseRecorder::State::ready)
        finishPhraseRecording();
    if (clearingPhrase && !engine.isPhraseAuditioning() && !engine.phraseSwitchPending())
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        engine.phraseInput.setClip({}); clearingPhrase = false; phrasePeaks.fill(0); phrasePanel.setPeaks({});
    }
    const bool audition = engine.isPhraseAuditioning();
    if (audition != lastPhraseLayout) { lastPhraseLayout = audition; resized(); }
    auto* device = deviceManager.getCurrentAudioDevice();
    const bool outputReady = device != nullptr && !device->getActiveOutputChannels().isZero();
    const auto original = engine.inputSource.load();
    const bool inputReady = original == AudioEngine::InputSource::file ? engine.fileInput.hasClip()
        : original == AudioEngine::InputSource::testRiff ? engine.testInput.hasClip()
        : device != nullptr && device->getActiveInputChannels().countNumberOfSetBits() > engine.inputChannel.load()
            && engine.inputChannel.load() >= 0;
    const bool capturing = engine.phraseRecorder.capturing();
    phrasePanel.update(capturing, preparingPhrase || clearingPhrase, engine.phraseInput.hasClip(), audition,
        outputReady && inputReady && !loadingFile && !restoringSession, outputReady,
        capturing ? engine.phraseRecorder.duration(phraseCaptureRate) : engine.phraseInput.duration(),
        audition ? engine.playbackPosition() : 0, phraseError);
}
}
