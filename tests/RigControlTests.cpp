#include "../MainComponent.h"
#include <iostream>
#include <stdexcept>
void testPhraseControls(const juce::File&);

namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void ok(const juce::Result& result) { if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString()); }
}
namespace effecthack
{
struct RigControlsTestAccess
{
    static void wait(MainComponent& component)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        juce::AudioBuffer<float> output(2, 128);
        while (component.loadingPreset && juce::Time::getMillisecondCounter() < deadline)
        {
            component.engine.audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(), 2, 128, {});
            juce::MessageManager::getInstance()->runDispatchLoopUntil(3);
            component.serviceToneChange();
        }
        require(!component.loadingPreset, "Asynchronous slot load must finish without an audio device");
    }
    static void press(MainComponent& component, int key, juce::ModifierKeys modifiers = {})
    {
        component.heldShortcutKeys.clear();
        require(component.keyPressed(juce::KeyPress(key, modifiers, 0)), "Shortcut should be handled");
    }
    static void live(MainComponent& component, const ToneSnapshot& snapshot)
    {
        ok(applyPreset(component.engine, preparePreset(snapshot.rack)));
        component.activePreset = snapshot.identity;
        component.syncRackControls(snapshot.rack);
        component.refreshPresetList(); component.refreshPresetControls();
    }
    static void writeIr(const juce::File& path)
    {
        juce::AudioBuffer<float> audio(1, 64); audio.clear(); audio.setSample(0, 0, .9f);
        std::unique_ptr<juce::OutputStream> stream(path.createOutputStream());
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
        require(writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, 64), "Write slot IR fixture");
    }
    static void run(const juce::File& folder)
    {
        const auto session = folder.getChildFile("session.json"), presets = folder.getChildFile("presets");
        ToneSnapshot a { factoryPresets()[0].preset, factoryPresets()[0].preset.rack };
        ToneSnapshot b { { {}, "Generated B", factoryPresets()[3].preset.rack }, factoryPresets()[3].preset.rack };
        b.rack.values[5] = 2; std::swap(b.rack.order[2], b.rack.order[3]);
        {
            MainComponent component(session, presets, false); component.stopTimer();
            require(component.deviceManager.getCurrentAudioDevice() == nullptr, "UI tests must never open real hardware");
            live(component, a);
            press(component, '2');
            require(component.toneSlots.active() == 1 && captureRack(component.engine).matches(a.rack), "First B visit copies the live tone");
            live(component, b);
            component.engine.parameters.inputDb = -4; component.engine.parameters.outputDb = -17;
            component.engine.masterMute = true; component.engine.rackBypass = true;
            component.engine.prepare(48000, 128); component.engine.monitoring = true;
            component.engine.testInput.resume();
            juce::AudioBuffer<float> output(2, 128);
            for (int i = 0; i < 30; ++i) component.engine.audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(), 2, 128, {});
            const auto position = component.engine.playbackPosition();
            const auto actualImpulseB = component.engine.cabinetImpulse();
            component.slotPrepared[1] = preparePreset(captureRack(component.engine)); // Stale cache, same source path.
            press(component, '1');
            require(component.loadingPreset && component.toneSlots.active() == 1, "Selection stays on B until A commits");
            component.selectToneSlot(1); // An overlapping request cannot overwrite the pending transition.
            wait(component);
            auto expectedA = a.rack; expectedA.values[0] = -4; expectedA.values[3] = -17;
            require(component.toneSlots.active() == 0 && captureRack(component.engine).matches(expectedA), "A restores the full rack while keeping shared Master gains");
            require(component.engine.masterMute && component.engine.rackBypass && component.engine.monitoring
                && component.engine.playbackPosition() > position, "Slot switching preserves mute, bypass and uninterrupted transport position");
            require(!component.previousSongTone.has_value(), "A/B switching must not create song-Revert history");
            require(component.slotPrepared[1]->impulse == actualImpulseB, "Slots retain the actual response even after an IR reload at the same path");
            component.engine.parameters.outputDb = -20;
            press(component, 'X'); wait(component);
            auto expectedB = b.rack; expectedB.values[0] = -4; expectedB.values[3] = -20;
            require(component.toneSlots.active() == 1 && captureRack(component.engine).matches(expectedB)
                && component.activePreset.rack.matches(b.identity.rack) && component.presetInfo.getText() == "Modified",
                "B restores unsaved edits and generated baseline independently; Master stays shared");
            const auto savedSlots = component.toneSlots.state();
            component.heldShortcutKeys.clear();
            const juce::KeyPress mute('M', {}, 0);
            require(component.keyPressed(mute) && !component.engine.masterMute, "M toggles Master mute");
            require(component.keyPressed(mute) && !component.engine.masterMute, "Held key repeat must not toggle again");
            component.keyStateChanged(false);
            require(component.keyPressed(mute) && component.engine.masterMute, "Release permits the next shortcut");
            press(component, 'B'); require(!component.engine.rackBypass, "B toggles whole-rack bypass");
            press(component, 'T'); require(component.engine.tunerEnabled && component.engine.masterMute, "T enables tuner while preserving Master mute");
            press(component, 'T'); require(!component.engine.tunerEnabled && component.engine.masterMute, "T disables tuner without unmuting Master");
            juce::TextEditor editor;
            auto& listener = static_cast<juce::KeyListener&>(component);
            require(!listener.keyPressed(juce::KeyPress('M', {}, 0), &editor) && component.engine.masterMute, "Typing in an editor must not mute or switch tones");
            require(!listener.keyPressed(juce::KeyPress('1', {}, 0), &component.presetSelector), "Preset-menu typeahead keeps normal keys");
            require(!component.keyPressed(juce::KeyPress('1', juce::ModifierKeys::ctrlModifier, 0)), "Control shortcuts must not select slots");
            press(component, 'X', juce::ModifierKeys::shiftModifier);
            require(component.toneSlots.active() == 1 && component.toneSlots.stored(0)->rack.matches(expectedB), "Shift+X copies without selecting or changing the live rack");
            component.toneSlots.restore(savedSlots);

            // Missing custom cabinets fail before commit; existing audio stays intact.
            press(component, '1'); wait(component);
            const auto beforeFailure = captureRack(component.engine);
            auto broken = component.toneSlots.state();
            broken.slots[1]->rack.cabinetPath = folder.getChildFile("missing-ir.wav").getFullPathName();
            component.toneSlots.restore(broken); component.slotPrepared[1].reset();
            press(component, '2'); wait(component);
            require(component.presetHasError && component.toneSlots.active() == 0 && captureRack(component.engine).matches(beforeFailure), "Missing IR must leave active slot and live audio unchanged");

            // Warm slots use their prepared immutable IR even if its file disappears.
            const auto ir = folder.getChildFile("cached-ir.wav"); writeIr(ir);
            auto cached = savedSlots; cached.active = 0; cached.slots[1]->rack.cabinetPath = ir.getFullPathName();
            component.toneSlots.restore(cached); component.slotPrepared[1] = preparePreset(cached.slots[1]->rack);
            require(ir.deleteFile(), "Remove cached IR fixture");
            press(component, '2'); wait(component);
            require(!component.presetHasError && component.toneSlots.active() == 1 && component.engine.cabinetSourcePath() == ir.getFullPathName(), "Prepared slot switches must not reread cabinet files");
            const auto impulseB = component.engine.cabinetImpulse();
            auto impulseA = std::make_shared<CabinetImpulse>(*impulseB);
            impulseA->samples.applyGain(.5f);
            auto samePath = component.toneSlots.state();
            samePath.slots[0] = samePath.slots[1]; // Same controls/path, two loaded versions of the file.
            component.toneSlots.restore(samePath);
            component.slotPrepared[0] = PreparedPreset { samePath.slots[0]->rack, impulseA, {} };
            press(component, '1');
            require(component.loadingPreset, "Different cached cabinet responses need a transition even with identical paths and knobs");
            wait(component);
            require(component.engine.cabinetImpulse() == impulseA, "Slot A retains its own loaded cabinet response");
            press(component, '2'); wait(component);
            require(component.engine.cabinetImpulse() == impulseB, "Slot B retains its own loaded cabinet response");
            component.toneSlots.restore(savedSlots); component.slotPrepared[1].reset();
            live(component, { b.identity, expectedB });
            component.saveSession();
            for (int width : {940, 1120})
            {
                component.setSize(width, 900);
                require(!component.slotButtons[0].getBounds().intersects(component.presetInfo.getBounds())
                    && component.copySlotButton.getRight() <= component.getWidth(), "Slot controls must fit at minimum width");
                auto stream = folder.getParentDirectory().getChildFile("rig-slots-" + juce::String(width) + ".png").createOutputStream();
                require(stream != nullptr && juce::PNGImageFormat().writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()), *stream), "Render slot UI for inspection");
            }
        }
        {
            MainComponent restored(session, presets, false); restored.stopTimer(); wait(restored);
            require(restored.toneSlots.active() == 1 && restored.toneSlots.stored(0) != nullptr
                && restored.toneSlots.stored(1) != nullptr && restored.activePreset.name == "Generated B"
                && restored.engine.parameters.outputDb.load() == -20 && !restored.engine.monitoring,
                "Restart restores both slots, selected B, shared gains and generated identity, with audio paused");
            press(restored, '1'); wait(restored);
            require(restored.toneSlots.active() == 0 && restored.activePreset.id == a.identity.id, "Inactive slot remains usable after restart");
        }
    }
};
}
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    try
    {
        require(argc == 2, "Pass scratch folder");
        const auto folder = juce::File(argv[1]).getChildFile(juce::Uuid().toString()); ok(folder.createDirectory());
        effecthack::RigControlsTestAccess::run(folder);
        testPhraseControls(folder);
        require(folder.deleteRecursively(), "Remove control test fixtures");
        std::cout << "A/B slots, keyboard guards, fades, IR caching and startup integration passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
