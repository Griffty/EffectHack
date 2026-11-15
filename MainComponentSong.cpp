#include "MainComponent.h"
#include "SongPresetPanel.h"

namespace effecthack
{
void MainComponent::showSongPicker()
{
    if (loadingPreset || loadingCabinet) return;
    std::vector<SelectionPreset> catalog;
    for (const auto& factory : factoryPresets())
        catalog.push_back({ factory.preset.id, factory.preset.name, factory.description, encodePreset(factory.preset) });
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    auto* panel = new SongPresetPanel(std::move(catalog), [safe](const SongRecommendation& selected, const Cancellation& cancel, SongPresetPanel::Completion done)
    {
        if (safe == nullptr) { done(juce::Result::fail("The rack is no longer available.")); return; }
        safe->applySongRecommendation(selected, cancel, std::move(done));
    }, performHttp, loadApiConfig, [safe]() -> juce::var
    {
        if (safe == nullptr) return {};
        auto* context = new juce::DynamicObject;
        context->setProperty("master_input_db", safe->engine.parameters.inputDb.load());
        context->setProperty("master_output_db", safe->engine.parameters.outputDb.load());
        return juce::var(context);
    });
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(panel);
    options.dialogTitle = "Song tone";
    options.dialogBackgroundColour = juce::Colour(0xff1c242d);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
void MainComponent::applySongRecommendation(const SongRecommendation& recommendation, const Cancellation& cancel, std::function<void(juce::Result)> done)
{
    if (recommendation.error.isNotEmpty()) { done(juce::Result::fail(recommendation.error)); return; }
    if (recommendation.tone.isObject())
    {
        Preset preset;
        const auto valid = decodeGeneratedTone(recommendation.tone, preset);
        if (valid.failed()) { done(valid); return; }
        startToneChange(preset, preset.rack, recommendation.tone, false, cancel, std::move(done), recommendation.preserveMaster);
    }
    else if (const auto* factory = findFactoryPreset(recommendation.presetId))
        startToneChange(factory->preset, factory->preset.rack, {}, false, cancel, std::move(done));
    else done(juce::Result::fail("Unsupported song tone."));
}
void MainComponent::startToneChange(Preset target, RackState rack, juce::var generation, bool reverting, Cancellation cancel, std::function<void(juce::Result)> done, bool preserveMaster)
{
    if (loadingPreset || loadingCabinet) { done(juce::Result::fail("Rack is busy. Try again after loading finishes.")); return; }
    if (cancel->load()) { done(juce::Result::fail("Cancelled.")); return; }
    ToneUndo previous { activePreset, captureRack(engine) };
    loadingPreset = true; refreshPresetControls(); refreshCabinetStatus();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    loader.addJob([safe, target, rack, generation, reverting, cancel, previous, preserveMaster, done = std::move(done)]() mutable
    {
        PreparedPreset prepared;
        float adjustment = 0;
        try
        {
            if (generation.isObject())
            {
                auto generated = prepareGeneratedTone(generation, cancel.get(), preserveMaster);
                target = std::move(generated.preset); prepared = std::move(generated.prepared); adjustment = generated.outputAdjustmentDb;
            }
            else prepared = preparePreset(rack);
        }
        catch (const std::exception&) { prepared.error = "Could not prepare this song tone."; }
        juce::MessageManager::callAsync([safe, target, prepared = std::move(prepared), reverting, cancel, previous, adjustment, preserveMaster, done = std::move(done)]() mutable
        {
            if (safe == nullptr) return;
            if (cancel->load() || prepared.error.isNotEmpty())
            {
                safe->loadingPreset = false;
                const auto error = cancel->load() ? juce::String("Cancelled.") : prepared.error;
                if (!cancel->load()) { safe->presetHasError = true; safe->presetMessage = error; }
                safe->refreshPresetControls(); safe->refreshCabinetStatus();
                done(juce::Result::fail(error)); return;
            }
            safe->pendingToneChange = PendingToneChange { target, std::move(prepared), previous, reverting, adjustment, cancel, std::move(done), preserveMaster };
            {
                const juce::ScopedLock lock(safe->deviceManager.getAudioCallbackLock()); safe->engine.beginRackUpdate();
            }
            safe->serviceToneChange();
        });
    });
}
void MainComponent::serviceToneChange()
{
    if (!pendingToneChange.has_value()) return;
    auto& pending = *pendingToneChange;
    auto result = juce::Result::ok();
    const bool cancelled = pending.cancel->load();
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        if (!cancelled && !engine.rackUpdateIsSilent()) return;
        if (!cancelled && pending.preserveMaster)
        {
            // Master changes made while the network request was running belong to the player.
            pending.prepared.rack.values[0] = engine.parameters.inputDb.load();
            pending.prepared.rack.values[3] = engine.parameters.outputDb.load();
            pending.identity.rack = pending.prepared.rack;
        }
        result = cancelled ? juce::Result::fail("Cancelled.") : applyPreset(engine, pending.prepared);
        engine.endRackUpdate();
    }
    if (result.wasOk())
    {
        if (pending.reverting) previousSongTone.reset(); else previousSongTone = pending.previous;
        activePreset = pending.identity;
        syncRackControls(pending.prepared.rack);
        cabinetName = pending.prepared.impulse->name; cabinetError.clear();
        presetHasError = false;
        presetMessage = pending.reverting ? "Previous tone restored." : "Song tone loaded: " + activePreset.name;
        if (pending.outputAdjustment < 0) presetMessage += " / Reference-headroom Output adjustment " + juce::String(pending.outputAdjustment, 1) + " dB";
        refreshSignalFlow(); resized();
    }
    else if (!cancelled) { presetHasError = true; presetMessage = result.getErrorMessage(); }
    auto completed = std::move(pending.completion);
    pendingToneChange.reset(); loadingPreset = false;
    refreshPresetList(); refreshPresetControls(); refreshCabinetStatus();
    completed(result);
}
void MainComponent::revertSongTone()
{
    if (!previousSongTone.has_value() || loadingPreset || loadingCabinet) return;
    const auto undo = *previousSongTone;
    startToneChange(undo.identity, undo.rack, {}, true, std::make_shared<std::atomic<bool>>(false), [](juce::Result) {});
}
juce::Result MainComponent::selectSongPreset(const juce::String& id)
{
    // Retained for integrations that explicitly request a factory tone.
    if (loadingPreset || loadingCabinet) return juce::Result::fail("Rack is busy. Try again after loading finishes.");
    const auto& factory = factoryPresets();
    for (size_t i = 0; i < factory.size(); ++i)
        if (factory[i].preset.id == id)
        {
            presetSelector.setSelectedId(static_cast<int>(i) + 2, juce::dontSendNotification); loadSelectedPreset(); return juce::Result::ok();
        }
    return juce::Result::fail("Unsupported preset selection.");
}
}
