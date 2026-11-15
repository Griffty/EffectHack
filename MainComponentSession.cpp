#include "MainComponent.h"

namespace effecthack
{
void MainComponent::restoreSessionTone(const SessionState& session)
{
    restoringSession = loadingPreset = true;
    if (session.toneSlots.has_value()) toneSlots.restore(*session.toneSlots);
    refreshPresetControls();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    loader.addJob([safe, session, manager = presetManager]
    {
        PreparedPreset prepared;
        Preset selected { {}, "Default rack", {} };
        if (session.generatedPreset.has_value()) selected = *session.generatedPreset;
        bool missingPreset = false;
        try
        {
            if (session.presetId.isNotEmpty()) missingPreset = manager.read(session.presetId, selected).failed();
            prepared = preparePreset(session.rack);
        }
        catch (const std::exception&) { prepared.error = "Could not restore the last tone."; }
        juce::MessageManager::callAsync([safe, selected, missingPreset, prepared = std::move(prepared)]
        {
            if (safe == nullptr) return;
            auto result = juce::Result::ok();
            {
                const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock());
                result = applyPreset(safe->engine, prepared);
            }
            safe->restoringSession = safe->loadingPreset = false;
            if (result.wasOk())
            {
                safe->activePreset = selected;
                safe->toneSlots.remember({ selected, prepared.rack });
                safe->slotPrepared[static_cast<size_t>(safe->toneSlots.active())] = prepared;
                safe->syncRackControls(prepared.rack);
                safe->cabinetName = prepared.impulse->name;
                safe->presetMessage = missingPreset ? "Last tone restored; its saved preset is missing. Use Save as."
                    : "Last setup restored; audio is paused.";
                safe->presetHasError = missingPreset;
                safe->refreshSignalFlow();
                safe->resized();
            }
            else { safe->presetMessage = "Could not restore last tone: " + result.getErrorMessage(); safe->presetHasError = true; }
            safe->refreshPresetList();
            safe->refreshPresetControls();
            safe->refreshCabinetStatus();
            safe->refreshDeviceStatus();
        });
    });
}

void MainComponent::saveSession()
{
    // Closing while an asynchronous restoration is still pending must preserve
    // the previous snapshot, rather than replacing it with the temporary defaults.
    if (restoringSession) return;
    SessionState state;
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        state.rack = captureRack(engine);
        state.inputChannel = engine.inputChannel.load();
    }
    state.presetId = activePreset.id;
    toneSlots.remember({ activePreset, state.rack });
    state.toneSlots = toneSlots.state();
    if (isGeneratedPreset()) state.generatedPreset = activePreset;
    state.sourceId = sourceSelector.getSelectedId();
    state.loop = engine.testInput.isLooping(); // Audition always loops; retain the original source preference.
    state.audioFile = rememberedAudioFile;
    state.tunerReference = rigTools.reference();
    state.tunerMute = engine.tunerMute.load();
    if (auto xml = deviceManager.createStateXml()) state.deviceXml = xml->toString();
    else if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        // JUCE keeps explicit settings only after a user change. Remember the
        // actual initial device too, so the first launch can be restored exactly.
        const auto setup = deviceManager.getAudioDeviceSetup();
        juce::XmlElement deviceState("DEVICESETUP");
        deviceState.setAttribute("deviceType", deviceManager.getCurrentAudioDeviceType());
        deviceState.setAttribute("audioInputDeviceName", setup.inputDeviceName);
        deviceState.setAttribute("audioOutputDeviceName", setup.outputDeviceName);
        deviceState.setAttribute("audioDeviceRate", device->getCurrentSampleRate());
        deviceState.setAttribute("audioDeviceBufferSize", device->getCurrentBufferSizeSamples());
        deviceState.setAttribute("audioDeviceInChans", device->getActiveInputChannels().toString(2));
        deviceState.setAttribute("audioDeviceOutChans", device->getActiveOutputChannels().toString(2));
        state.deviceXml = deviceState.toString();
    }
    if (auto result = sessionStore.write(state); result.failed())
        juce::Logger::writeToLog(result.getErrorMessage());
}
}
