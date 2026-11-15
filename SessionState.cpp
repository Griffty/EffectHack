#include "SessionState.h"
#include "FactoryPresets.h"
#include <cmath>

namespace effecthack
{
namespace
{
constexpr const char* snapshotId = "0123456789abcdef0123456789abcdef";
juce::Result invalid() { return juce::Result::fail("Startup settings are invalid or unsupported."); }
bool validPresetId(const juce::String& id)
{
    return isFactoryPreset(id) || (id.length() == 32 && id.containsOnly("0123456789abcdef"));
}
}
juce::var encodeSession(const SessionState& state)
{
    auto* object = new juce::DynamicObject;
    object->setProperty("format", "effecthack.session");
    object->setProperty("version", 1);
    object->setProperty("rack", encodePreset({ snapshotId, "Last session", state.rack }));
    object->setProperty("selectedPreset", state.presetId);
    object->setProperty("source", state.sourceId);
    object->setProperty("inputChannel", state.inputChannel);
    object->setProperty("loop", state.loop);
    object->setProperty("audioFile", state.audioFile);
    object->setProperty("audioDevice", state.deviceXml);
    object->setProperty("tunerReference", state.tunerReference);
    object->setProperty("tunerMute", state.tunerMute);
    if (state.generatedPreset.has_value())
    {
        auto generated = *state.generatedPreset;
        generated.id = snapshotId;
        object->setProperty("generatedPreset", encodePreset(generated));
    }
    return juce::var(object);
}
juce::Result decodeSession(const juce::var& json, SessionState& destination)
{
    if (!json.isObject() || json["format"].toString() != "effecthack.session"
        || !json["version"].isInt() || static_cast<int>(json["version"]) != 1
        || !json["source"].isInt() || !json["inputChannel"].isInt() || !json["loop"].isBool()
        || !json["selectedPreset"].isString() || !json["audioFile"].isString()
        || !json["audioDevice"].isString()) return invalid();
    SessionState state;
    // Optional additions preserve startup files written before the tuner existed.
    if (json.hasProperty("tunerReference"))
    {
        const auto reference = json["tunerReference"];
        if (!reference.isDouble() && !reference.isInt()) return invalid();
        state.tunerReference = static_cast<double>(reference);
        if (!std::isfinite(state.tunerReference) || state.tunerReference < 430 || state.tunerReference > 450) return invalid();
    }
    if (json.hasProperty("tunerMute"))
    {
        if (!json["tunerMute"].isBool()) return invalid();
        state.tunerMute = json["tunerMute"];
    }
    state.sourceId = json["source"]; state.inputChannel = json["inputChannel"];
    state.loop = json["loop"]; state.presetId = json["selectedPreset"].toString();
    state.audioFile = json["audioFile"].toString(); state.deviceXml = json["audioDevice"].toString();
    if (json.hasProperty("generatedPreset"))
    {
        Preset generated;
        if (state.presetId.isNotEmpty() || decodePreset(json["generatedPreset"], generated).failed()
            || generated.name.equalsIgnoreCase("Default rack")) return invalid();
        generated.id.clear(); state.generatedPreset = std::move(generated);
    }
    if (state.sourceId < 1 || state.sourceId > 3 || state.inputChannel < 0 || state.inputChannel > 1
        || !validPresetId(state.presetId) || state.audioFile.length() > 4096
        || (state.audioFile.isNotEmpty() && !juce::File::isAbsolutePath(state.audioFile))
        || state.deviceXml.length() > 65536) return invalid();
    if (state.deviceXml.isNotEmpty())
    {
        auto xml = juce::parseXML(state.deviceXml);
        if (xml == nullptr || !xml->hasTagName("DEVICESETUP")) return invalid();
    }
    Preset snapshot;
    if (auto result = decodePreset(json["rack"], snapshot); result.failed()) return result;
    state.rack = std::move(snapshot.rack);
    destination = std::move(state);
    return juce::Result::ok();
}
juce::File SessionStore::defaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("EffectHack/session.json");
}
juce::Result SessionStore::read(SessionState& state, bool& found) const
{
    found = false;
    if (path == juce::File() || !path.exists()) return juce::Result::ok();
    found = true;
    if (!path.existsAsFile() || path.getSize() > 131072) return invalid();
    juce::var json;
    if (auto result = juce::JSON::parse(path.loadFileAsString(), json); result.failed()) return invalid();
    return decodeSession(json, state);
}
juce::Result SessionStore::write(const SessionState& state) const
{
    if (path == juce::File()) return juce::Result::ok(); // Isolated smoke checks opt out.
    const auto json = encodeSession(state);
    SessionState checked;
    if (auto result = decodeSession(json, checked); result.failed()) return result;
    if (auto result = path.getParentDirectory().createDirectory(); result.failed()) return result;
    juce::TemporaryFile temporary(path);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(json))
        || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Could not remember startup settings.");
    return juce::Result::ok();
}
}
