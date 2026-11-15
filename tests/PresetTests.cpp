#include "../PresetManager.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace effecthack;
void testRackOrderAndStereo(const juce::File&);
void testFactoryPresets(const juce::File&);
void testSessionState(const juce::File&);
void testGeneratedTone(const juce::File&);
void testToneSlots(const juce::File&);
namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void ok(const juce::Result& result)
{
    if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
}
void writeIr(const juce::File& file)
{
    juce::AudioBuffer<float> audio(1, 64);
    audio.clear(); audio.setSample(0, 0, .9f); audio.setSample(0, 12, .2f);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
    require(writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, 64), "IR fixture write failed");
}
}
int main(int argc, char** argv)
{
    juce::MessageManager::getInstance();
    int exitCode = 0;
    try
    {
        require(argc == 2, "Pass a scratch directory");
        auto scratch = juce::File(argv[1]).getChildFile(juce::Uuid().toString());
        ok(scratch.createDirectory());
        const auto folder = scratch.getChildFile("presets");
        PresetManager manager(folder);
        juce::String warning;
        require(manager.list(warning).empty() && !folder.exists(), "Listing must not create the preset folder");
        AudioEngine engine;
        require(captureRack(engine).matches(RackState {}), "Factory rack must match engine defaults");
        RackState tone;
        tone.values = { -4, 31, .45f, -17, 3, -5, 2, 2.1f, .64f, .4f,
                        680, .6f, .37f, .7f, .3f, .22f, 18, 4, -3, 2, 1, -8, -45, 2, 40, 150, -20, 3, 8, 200, 2, .65f };
        tone.ampVoice = static_cast<int>(AmpVoice::highGain);
        tone.bypass = { false, false, true, false, false, true, false, false, false };
        Preset saved;
        ok(manager.saveAs("  My lead  ", tone, saved));
        require(saved.name == "My lead", "Names should be trimmed");
        PresetManager reopened(folder);
        auto entries = reopened.list(warning);
        require(entries.size() == 1 && warning.isEmpty(), "Presets must survive store recreation");
        Preset restored;
        ok(reopened.read(saved.id, restored));
        require(restored.rack.matches(tone), "JSON must round-trip every knob and bypass");
        auto prepared = preparePreset(restored.rack);
        engine.inputSource = AudioEngine::InputSource::hardware;
        engine.monitoring = true; engine.inputChannel = 1;
        ok(applyPreset(engine, prepared));
        require(captureRack(engine).matches(tone), "Applying must restore the complete rack");
        require(engine.inputSource == AudioEngine::InputSource::hardware && engine.monitoring && engine.inputChannel == 1,
                "Tone loads must preserve source, monitoring, and channel");
        auto changed = captureRack(engine); changed.values[1] = 12;
        require(!changed.matches(tone), "Dirty comparison must detect knob changes");
        changed = tone; changed.bypass.front() = !changed.bypass.front();
        require(!changed.matches(tone), "Dirty comparison must detect bypass changes");
        require(manager.saveAs("my LEAD", tone, restored).failed(), "Case-insensitive duplicate names must fail");
        require(manager.saveAs("Default rack", tone, restored).failed(), "Factory name must be reserved");
        require(manager.saveAs("\n", tone, restored).failed(), "Empty names must fail");
        require(manager.saveAs(juce::String::repeatedString("a", 65), tone, restored).failed(), "Long names must fail");
        Preset renamed;
        ok(manager.rename(saved, "Solo", renamed));
        require(renamed.id == saved.id && renamed.rack.matches(tone), "Rename must preserve ID and saved tone");
        saved = renamed; saved.rack.values[1] = 42;
        ok(manager.save(saved)); ok(manager.read(saved.id, restored));
        require(restored.rack.values[1] == 42, "Save must update the existing preset");
        require(manager.list(warning).size() == 1, "Update must not create duplicates");
        require(manager.read("../../outside", restored).failed() && manager.remove("../outside").failed(),
                "IDs must not allow path traversal");
        auto json = encodePreset(saved);
        json.getDynamicObject()->setProperty("version", 999);
        require(decodePreset(json, restored).failed(), "Unknown schema must fail");
        json = encodePreset(saved);
        json["parameters"].getDynamicObject()->setProperty("driveDb", "loud");
        require(decodePreset(json, restored).failed(), "Parameter type must be checked");
        json = encodePreset(saved);
        json["parameters"].getDynamicObject()->setProperty("driveDb", 1000);
        require(decodePreset(json, restored).failed(), "Out-of-range parameters must fail");
        json = encodePreset(saved);
        json["parameters"].getDynamicObject()->removeProperty("chorusMix");
        require(decodePreset(json, restored).failed(), "Missing knobs must fail");
        json = encodePreset(saved);
        json["bypass"].getDynamicObject()->setProperty("delay", 1);
        require(decodePreset(json, restored).failed(), "Bypass must be a boolean");
        auto invalid = tone; invalid.values[1] = std::numeric_limits<float>::infinity();
        require(validateRack(invalid).failed(), "Nonfinite DSP values must fail");
        invalid = tone; invalid.cabinetPath = "relative.wav";
        require(validateRack(invalid).failed(), "Relative IR paths must fail");
        const auto ir = scratch.getChildFile("cabinet.wav");
        writeIr(ir);
        auto custom = tone; custom.cabinetPath = ir.getFullPathName();
        ok(manager.saveAs("Cabinet lead", custom, restored));
        Preset customSaved; ok(manager.read(restored.id, customSaved));
        prepared = preparePreset(customSaved.rack);
        ok(applyPreset(engine, prepared));
        require(captureRack(engine).matches(custom), "Custom cabinet path and rack must restore");
        const auto beforeFailure = captureRack(engine);
        require(ir.deleteFile(), "Could not remove test IR");
        auto missing = custom; missing.values[1] = 1;
        prepared = preparePreset(missing);
        require(prepared.error.isNotEmpty() && applyPreset(engine, prepared).failed(), "Missing IR must fail load");
        require(captureRack(engine).matches(beforeFailure), "A failed load must leave the complete current tone intact");
        require(ir.replaceWithText("not an audio file"), "Could not write invalid fixture");
        require(applyPreset(engine, preparePreset(missing)).failed(), "Invalid IR must fail load");
        require(captureRack(engine).matches(beforeFailure), "Invalid IR must not partially change the rack");
        ok(applyPreset(engine, preparePreset(RackState {})));
        require(captureRack(engine).matches(RackState {}), "Factory reload must restore every default and built-in cabinet");
        auto badFile = folder.getChildFile(juce::Uuid().toString() + ".json");
        require(badFile.replaceWithText("{broken"), "Could not write corrupt fixture");
        require(manager.list(warning).size() == 2 && warning.isNotEmpty(), "Catalog must skip malformed files with a warning");
        auto mismatch = saved; mismatch.id = juce::Uuid().toString();
        require(badFile.replaceWithText(juce::JSON::toString(encodePreset(mismatch))), "Could not write mismatched fixture");
        require(manager.read(badFile.getFileNameWithoutExtension(), restored).failed(), "Filename and ID must agree");
        ok(manager.remove(saved.id));
        require(manager.read(saved.id, restored).failed(), "Deleted preset must be gone");
        require(manager.save(saved).failed(), "Saving a deleted preset must not silently recreate it");
        auto blocker = scratch.getChildFile("blocked");
        require(blocker.replaceWithText("file, not folder"), "Could not write blocked-directory fixture");
        require(PresetManager(blocker).saveAs("Blocked", tone, restored).failed(), "Write failures must be reported");
        testRackOrderAndStereo(scratch);
        testFactoryPresets(scratch);
        testSessionState(scratch);
        testGeneratedTone(scratch);
        testToneSlots(scratch);
        require(scratch.deleteRecursively(), "Scratch cleanup failed");
        std::cout << "Preset persistence, CRUD, validation, IR restoration, and atomic rack load checks passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; exitCode = 1; }
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    return exitCode;
}
