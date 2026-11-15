#include "PresetManager.h"
#include "FactoryPresets.h"
#include <algorithm>
#include <cmath>

namespace effecthack
{
namespace
{
const auto& fields = rackParameterSpecs;
constexpr const char* ampStages[] { "distortion", "amp", "cabinet", "eq", "chorus", "delay", "reverb" };
constexpr const char* legacyStages[] { "distortion", "cabinet", "eq", "chorus", "delay", "reverb" };
template<class Engine> auto parameters(Engine& e)
{
    return std::array { &e.parameters.inputDb, &e.parameters.driveDb, &e.parameters.mix,
        &e.parameters.outputDb, &e.eqParameters.bassDb, &e.eqParameters.midDb, &e.eqParameters.trebleDb,
        &e.chorusParameters.rateHz, &e.chorusParameters.depth, &e.chorusParameters.mix,
        &e.delayParameters.timeMs, &e.delayParameters.feedback, &e.delayParameters.mix,
        &e.reverbParameters.size, &e.reverbParameters.damping, &e.reverbParameters.mix,
        &e.ampParameters.gainDb, &e.ampParameters.bassDb, &e.ampParameters.midDb,
        &e.ampParameters.trebleDb, &e.ampParameters.presenceDb, &e.ampParameters.levelDb,
        &e.gateParameters.thresholdDb, &e.gateParameters.attackMs, &e.gateParameters.holdMs, &e.gateParameters.releaseMs,
        &e.compressorParameters.thresholdDb, &e.compressorParameters.ratio, &e.compressorParameters.attackMs,
        &e.compressorParameters.releaseMs, &e.compressorParameters.makeupDb, &e.compressorParameters.mix };
}
template<class Engine> auto bypasses(Engine& e)
{
    return std::array { &e.gateParameters.bypass, &e.compressorParameters.bypass, &e.parameters.bypass, &e.ampParameters.bypass, &e.cabinetParameters.bypass, &e.eqParameters.bypass,
        &e.chorusParameters.bypass, &e.delayParameters.bypass, &e.reverbParameters.bypass };
}
bool validId(const juce::String& id)
{
    return id.length() == 32 && id.containsOnly("0123456789abcdef");
}
bool validName(const juce::String& name)
{
    if (name.isEmpty() || name.length() > 64 || name != name.trim()) return false;
    for (auto c : name) if (c < 32 || c == 127) return false;
    return true;
}
juce::Result fail(const juce::String& message) { return juce::Result::fail(message); }
}

bool RackState::matches(const RackState& other) const
{
    for (size_t i = 0; i < values.size(); ++i)
        if (std::abs(values[i] - other.values[i]) > 0.00001f) return false;
    return bypass == other.bypass && cabinetPath == other.cabinetPath && order == other.order && ampVoice == other.ampVoice;
}
RackState captureRack(const AudioEngine& e)
{
    RackState result;
    auto floats = parameters(e);
    auto flags = bypasses(e);
    for (size_t i = 0; i < floats.size(); ++i) result.values[i] = floats[i]->load();
    for (size_t i = 0; i < flags.size(); ++i) result.bypass[i] = flags[i]->load();
    result.cabinetPath = e.cabinetSourcePath();
    result.order = e.stageChain().order();
    result.ampVoice = e.ampParameters.voice.load();
    return result;
}
juce::Result validateRack(const RackState& rack)
{
    for (size_t i = 0; i < rack.values.size(); ++i)
        if (!std::isfinite(rack.values[i]) || rack.values[i] < fields[i].minimum
            || rack.values[i] > fields[i].maximum)
            return fail("Invalid preset value: " + juce::String(fields[i].key));
    if (rack.ampVoice < 0 || rack.ampVoice >= static_cast<int>(ampVoiceIds.size())) return fail("Invalid amp voice.");
    if (!validRackOrder(rack.order)) return fail("Preset order must contain every supported stage exactly once.");
    if (rack.cabinetPath.isNotEmpty() && !juce::File::isAbsolutePath(rack.cabinetPath))
        return fail("The cabinet IR path must be absolute.");
    return juce::Result::ok();
}
PreparedPreset preparePreset(const RackState& rack)
{
    PreparedPreset prepared;
    prepared.rack = rack;
    if (auto result = validateRack(rack); result.failed())
        prepared.error = result.getErrorMessage();
    else if (rack.cabinetPath.isEmpty())
        prepared.impulse = makeBuiltinCabinet();
    else
    {
        auto loaded = loadCabinetImpulse(juce::File(rack.cabinetPath));
        prepared.impulse = std::move(loaded.impulse);
        prepared.error = loaded.error;
    }
    return prepared;
}
juce::Result applyPreset(AudioEngine& e, const PreparedPreset& prepared)
{
    if (prepared.error.isNotEmpty()) return fail(prepared.error);
    if (auto result = validateRack(prepared.rack); result.failed()) return result;
    if (prepared.impulse == nullptr || prepared.impulse->sourcePath != prepared.rack.cabinetPath)
        return fail("The cabinet IR has not been prepared for this preset.");
    if (!e.setStageOrder(prepared.rack.order)) return fail("Could not restore the preset's stage order.");
    e.setCabinetImpulse(prepared.impulse);
    const auto floats = parameters(e);
    const auto flags = bypasses(e);
    for (size_t i = 0; i < floats.size(); ++i) floats[i]->store(prepared.rack.values[i]);
    for (size_t i = 0; i < flags.size(); ++i) flags[i]->store(prepared.rack.bypass[i]);
    e.ampParameters.voice.store(prepared.rack.ampVoice);
    e.clearEffectTails();
    return juce::Result::ok();
}
juce::var encodePreset(const Preset& preset)
{
    auto* root = new juce::DynamicObject;
    root->setProperty("format", "effecthack.preset");
    root->setProperty("version", 4);
    root->setProperty("ampVoice", preset.rack.ampVoice >= 0 && preset.rack.ampVoice < static_cast<int>(ampVoiceIds.size())
        ? ampVoiceIds[static_cast<size_t>(preset.rack.ampVoice)] : "invalid");
    juce::Array<juce::var> order;
    for (const auto& id : preset.rack.order) order.add(id);
    root->setProperty("order", order);
    root->setProperty("id", preset.id);
    root->setProperty("name", preset.name);
    auto* values = new juce::DynamicObject;
    for (size_t i = 0; i < preset.rack.values.size(); ++i)
        values->setProperty(fields[i].key, static_cast<double>(preset.rack.values[i]));
    root->setProperty("parameters", juce::var(values));
    auto* flags = new juce::DynamicObject;
    for (size_t i = 0; i < preset.rack.bypass.size(); ++i)
        flags->setProperty(rackStageIds[i], preset.rack.bypass[i]);
    root->setProperty("bypass", juce::var(flags));
    root->setProperty("cabinetIR", preset.rack.cabinetPath);
    return juce::var(root);
}
juce::Result decodePreset(const juce::var& json, Preset& destination)
{
    if (!json.isObject() || json["format"].toString() != "effecthack.preset"
        || !json["version"].isInt() || static_cast<int>(json["version"]) < 1 || static_cast<int>(json["version"]) > 4)
        return fail("Unsupported preset format or version.");
    const int version = static_cast<int>(json["version"]);
    Preset preset;
    if (!json["id"].isString() || !json["name"].isString() || !json["cabinetIR"].isString())
        return fail("Missing preset identity or cabinet selection.");
    preset.id = json["id"].toString();
    preset.name = json["name"].toString();
    if (!validId(preset.id) || !validName(preset.name)) return fail("Invalid preset ID or name.");
    const auto values = json["parameters"], flags = json["bypass"];
    if (!values.isObject() || !flags.isObject()) return fail("Missing preset settings.");
    const auto valueCount = version < 3 ? size_t(16) : version == 3 ? size_t(22) : preset.rack.values.size();
    for (size_t i = 0; i < valueCount; ++i)
    {
        const auto v = values[fields[i].key];
        if (!v.isInt() && !v.isInt64() && !v.isDouble()) return fail("Missing or invalid preset parameter.");
        const auto number = static_cast<double>(v);
        const auto stored = static_cast<float>(number);
        if (!std::isfinite(number) || !std::isfinite(stored) || stored < fields[i].minimum || stored > fields[i].maximum)
            return fail("Out-of-range preset parameter: " + juce::String(fields[i].key));
        preset.rack.values[i] = stored;
    }
    for (size_t i = 0; i < preset.rack.bypass.size(); ++i)
    {
        const juce::String id(rackStageIds[i]);
        if (version < 4 && (id == "gate" || id == "compressor")) continue;
        if (version < 3 && id == "amp") continue;
        if (!flags[rackStageIds[i]].isBool()) return fail("Missing or invalid bypass switch.");
        preset.rack.bypass[i] = static_cast<bool>(flags[rackStageIds[i]]);
    }
    if (version >= 3)
    {
        if (!json["ampVoice"].isString()) return fail("Missing amp voice.");
        const auto name = json["ampVoice"].toString();
        const auto found = std::find_if(ampVoiceIds.begin(), ampVoiceIds.end(), [&](const char* id) { return name == id; });
        if (found == ampVoiceIds.end()) return fail("Unknown amp voice.");
        preset.rack.ampVoice = static_cast<int>(std::distance(ampVoiceIds.begin(), found));
    }
    // v1 was fixed, v2 stored six IDs, v3 added amp. New dynamics stages are
    // inserted bypassed at the front, preserving every existing relative order.
    if (version >= 2)
    {
        const auto* order = json["order"].getArray();
        const int expected = version == 2 ? 6 : version == 3 ? 7 : static_cast<int>(preset.rack.order.size());
        if (order == nullptr || order->size() != expected) return fail("Preset order must list all supported stages.");
        std::vector<juce::String> ids;
        for (const auto& id : *order)
        {
            if (!id.isString()) return fail("Invalid stage ID in preset order.");
            ids.push_back(id.toString());
        }
        if (version == 2)
            for (const auto* id : legacyStages)
                if (std::count(ids.begin(), ids.end(), id) != 1) return fail("Invalid legacy stage order.");
        if (version == 3)
            for (const auto* id : ampStages)
                if (std::count(ids.begin(), ids.end(), id) != 1) return fail("Invalid legacy stage order.");
        size_t slot = 0;
        if (version < 4)
        {
            preset.rack.order[slot++] = "gate";
            preset.rack.order[slot++] = "compressor";
        }
        for (const auto& id : ids)
        {
            preset.rack.order[slot++] = id;
            if (version == 2 && id == "distortion") preset.rack.order[slot++] = "amp";
        }
    }
    preset.rack.cabinetPath = json["cabinetIR"].toString();
    if (auto result = validateRack(preset.rack); result.failed()) return result;
    destination = std::move(preset);
    return juce::Result::ok();
}
juce::File PresetManager::defaultDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("EffectHack/Presets");
}
juce::Result PresetManager::read(const juce::String& id, Preset& preset) const
{
    if (const auto* factory = findFactoryPreset(id)) { preset = factory->preset; return juce::Result::ok(); }
    if (!validId(id)) return fail("Invalid preset ID.");
    const auto file = folder.getChildFile(id + ".json");
    if (!file.existsAsFile() || file.getSize() > 65536) return fail("Preset is missing or too large.");
    juce::var json;
    if (auto result = juce::JSON::parse(file.loadFileAsString(), json); result.failed())
        return fail("Could not read preset JSON.");
    Preset parsed;
    if (auto result = decodePreset(json, parsed); result.failed()) return result;
    if (parsed.id != id) return fail("Preset ID does not match its filename.");
    preset = std::move(parsed);
    return juce::Result::ok();
}
std::vector<Preset> PresetManager::list(juce::String& warning) const
{
    warning.clear();
    std::vector<Preset> presets;
    int skipped = 0;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        Preset preset;
        if (validId(file.getFileNameWithoutExtension()) && read(file.getFileNameWithoutExtension(), preset).wasOk())
            presets.push_back(std::move(preset));
        else ++skipped;
    }
    std::sort(presets.begin(), presets.end(), [](const auto& a, const auto& b)
        { return a.name.compareIgnoreCase(b.name) < 0; });
    if (skipped > 0) warning = juce::String(skipped) + " unreadable preset(s) skipped.";
    return presets;
}
juce::Result PresetManager::checkName(const juce::String& name, const juce::String& excludingId) const
{
    if (!validName(name)) return fail("Use a name of 1-64 characters without control characters.");
    if (name.equalsIgnoreCase("Default rack")) return fail("Default rack is reserved. Choose another name.");
    juce::String ignored;
    for (const auto& existing : list(ignored))
        if (existing.id != excludingId && existing.name.equalsIgnoreCase(name))
            return fail("A preset with that name already exists.");
    return juce::Result::ok();
}
juce::Result PresetManager::write(const Preset& preset) const
{
    if (!validId(preset.id)) return fail("Invalid preset ID.");
    if (auto result = checkName(preset.name, preset.id); result.failed()) return result;
    if (auto result = validateRack(preset.rack); result.failed()) return result;
    if (auto result = folder.createDirectory(); result.failed()) return result;
    const auto file = folder.getChildFile(preset.id + ".json");
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(encodePreset(preset)))
        || !temporary.overwriteTargetFileWithTemporary())
        return fail("Could not save preset. Check the preset folder permissions.");
    return juce::Result::ok();
}
juce::Result PresetManager::saveAs(const juce::String& name, const RackState& rack, Preset& saved) const
{
    Preset preset { juce::Uuid().toString(), name.trim(), rack };
    if (auto result = write(preset); result.failed()) return result;
    saved = std::move(preset);
    return juce::Result::ok();
}
juce::Result PresetManager::save(const Preset& preset) const
{
    if (isFactoryPreset(preset.id)) return fail("Factory presets are read-only. Use Save as to make a copy.");
    Preset existing;
    if (auto result = read(preset.id, existing); result.failed()) return result;
    return write(preset);
}
juce::Result PresetManager::rename(const Preset& preset, const juce::String& name, Preset& renamed) const
{
    if (isFactoryPreset(preset.id)) return fail("Factory presets are read-only. Use Save as to make a copy.");
    Preset existing;
    if (auto result = read(preset.id, existing); result.failed()) return result;
    existing.name = name.trim(); // Rename preserves the saved tone, including when the UI is dirty.
    if (auto result = write(existing); result.failed()) return result;
    renamed = std::move(existing);
    return juce::Result::ok();
}
juce::Result PresetManager::remove(const juce::String& id) const
{
    if (isFactoryPreset(id)) return fail("Factory presets are read-only.");
    if (!validId(id)) return fail("Invalid preset ID.");
    const auto file = folder.getChildFile(id + ".json");
    return file.existsAsFile() && file.deleteFile() ? juce::Result::ok() : fail("Could not delete preset.");
}
}
