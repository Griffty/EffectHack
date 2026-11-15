#include "MainComponent.h"

namespace effecthack
{
void MainComponent::initialiseToneSlots()
{
    for (size_t index = 0; index < slotButtons.size(); ++index)
    {
        auto& button = slotButtons[index];
        addAndMakeVisible(button);
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a3825));
        button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffa657));
        button.onClick = [this, index] { selectToneSlot(static_cast<int>(index)); grabKeyboardFocus(); };
    }
    addAndMakeVisible(copySlotButton); addAndMakeVisible(shortcutHelpButton);
    copySlotButton.onClick = [this] { copyToneSlot(); grabKeyboardFocus(); };
    shortcutHelpButton.onClick = []
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Keyboard controls",
            "1     Slot A\n2     Slot B\nX     Switch A / B\nShift + X     Copy current tone to the other slot\n\n"
            "B     Whole-rack bypass\nM     Master mute\nT     Tuner on / off\nR     Record / finish dry phrase\nL     Audition phrase / return to live input\nSpace     Play / pause\nHome     Restart\n\n"
            "Each slot keeps its edits automatically. Master Input / Output are shared.\n"
            "Shortcuts apply inside the rack window. Text fields and selection menus keep their normal keys.");
    };
    shortcutHelpButton.setTooltip("Keyboard controls. 1/2: A/B, X: switch, Shift+X: copy, B: bypass, M: mute, T: tuner.");
    toneSlots.remember({ activePreset, captureRack(engine) });
    refreshToneSlots();
}
void MainComponent::refreshToneSlots()
{
    const bool busy = loadingPreset || loadingCabinet;
    for (int index = 0; index < 2; ++index)
    {
        auto& button = slotButtons[static_cast<size_t>(index)];
        button.setEnabled(!busy);
        button.setToggleState(toneSlots.active() == index, juce::dontSendNotification);
        const auto* stored = toneSlots.stored(index);
        const auto name = toneSlots.active() == index ? activePreset.name : stored != nullptr ? stored->identity.name : "Copy of current tone on first use";
        button.setTooltip(juce::String(index == 0 ? "A (1)" : "B (2)") + " / " + name
            + "\nEdits are retained when switching. Master Input/Output, playback, Mute and Bypass stay unchanged.");
    }
    copySlotButton.setEnabled(!busy);
    copySlotButton.setButtonText(juce::String::fromUTF8(toneSlots.active() == 0 ? "A → B" : "B → A"));
    copySlotButton.setTooltip(juce::String("Copy the current tone and edits to ") + (toneSlots.active() == 0 ? "B" : "A")
        + " (Shift+X). Replaces the other slot without switching or changing saved presets.");
    tunerButton.setToggleState(engine.tunerEnabled.load(), juce::dontSendNotification);
}
void MainComponent::selectToneSlot(int index)
{
    if (index < 0 || index >= 2 || index == toneSlots.active() || loadingPreset || loadingCabinet) return;
    ToneSnapshot current { activePreset, {} };
    PreparedPreset currentPrepared;
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        current.rack = captureRack(engine);
        currentPrepared = { current.rack, engine.cabinetImpulse(), {} };
    }
    const auto destination = toneSlots.recall(index, current);
    if (!destination.has_value()) return;
    auto& outgoing = slotPrepared[static_cast<size_t>(toneSlots.active())];
    outgoing = std::move(currentPrepared);
    auto rack = destination->rack;
    rack.values[0] = current.rack.values[0]; rack.values[3] = current.rack.values[3];
    const auto& targetCache = slotPrepared[static_cast<size_t>(index)];
    const bool sameCabinet = rack.cabinetPath.isEmpty() || toneSlots.stored(index) == nullptr
        || (targetCache.has_value() && targetCache->impulse == outgoing->impulse);
    if (sameCabinet && rack.matches(current.rack))
    {
        // First-use cloning and identical tones require no DSP reset or fade.
        toneSlots.commit(index, { destination->identity, rack });
        slotPrepared[static_cast<size_t>(index)] = outgoing;
        activePreset = destination->identity;
        presetHasError = false;
        refreshPresetList(); refreshPresetControls(); return;
    }
    startToneChange(destination->identity, rack, {}, ToneChangeOrigin::slot,
        std::make_shared<std::atomic<bool>>(false), [](juce::Result) {}, true, index);
}
void MainComponent::copyToneSlot()
{
    if (loadingPreset || loadingCabinet) return;
    ToneSnapshot current { activePreset, {} };
    PreparedPreset prepared;
    {
        const juce::ScopedLock lock(deviceManager.getAudioCallbackLock());
        current.rack = captureRack(engine);
        prepared = { current.rack, engine.cabinetImpulse(), {} };
    }
    const auto active = static_cast<size_t>(toneSlots.active());
    toneSlots.copyToOther(current);
    slotPrepared[active] = prepared; slotPrepared[1 - active] = std::move(prepared);
    refreshToneSlots();
}
}
