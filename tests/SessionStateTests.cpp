#include "../SessionState.h"
#include "../FactoryPresets.h"
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
void testSessionState(const juce::File& scratch)
{
    using namespace effecthack;
    SessionStore store(scratch.getChildFile("session/settings.json"));
    SessionState restored;
    bool found = true;
    require(store.read(restored, found).wasOk() && !found, "First launch must use defaults without a startup file");
    SessionState state;
    state.rack = factoryPresets()[3].preset.rack;
    state.presetId = "factory.lead";
    std::swap(state.rack.order[0], state.rack.order[1]); state.rack.values[0] = -7;
    state.rack.cabinetPath = scratch.getChildFile("cabinet.wav").getFullPathName();
    state.sourceId = 1; state.inputChannel = 1; state.loop = false;
    state.tunerReference = 442; state.tunerMute = false;
    state.audioFile = scratch.getChildFile("dry-guitar.wav").getFullPathName();
    state.deviceXml = "<DEVICESETUP deviceType=\"ALSA\" audioOutputDeviceName=\"Test interface\" audioDeviceRate=\"48000\" audioDeviceBufferSize=\"128\" audioDeviceInChans=\"10\" audioDeviceOutChans=\"11\"/>";
    require(store.write(state).wasOk() && store.read(restored, found).wasOk() && found, "Startup snapshot must persist");
    require(restored.rack.matches(state.rack) && restored.presetId == state.presetId
        && restored.sourceId == 1 && restored.inputChannel == 1 && !restored.loop
        && restored.audioFile == state.audioFile && restored.deviceXml == state.deviceXml
        && restored.tunerReference == 442 && !restored.tunerMute,
        "Tone edits/order, identity, sources, and complete device state must round trip");
    auto invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("source", 4);
    const auto before = restored;
    require(decodeSession(invalid, restored).failed() && restored.rack.matches(before.rack), "Invalid source must fail atomically");
    auto older = encodeSession(state);
    older.getDynamicObject()->removeProperty("tunerReference"); older.getDynamicObject()->removeProperty("tunerMute");
    require(decodeSession(older, restored).wasOk() && restored.tunerReference == 440 && restored.tunerMute,
        "Startup files predating the tuner must remain compatible");
    invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("tunerReference", 900);
    require(decodeSession(invalid, restored).failed(), "Invalid tuning reference must be rejected");
    invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("inputChannel", -1);
    require(decodeSession(invalid, restored).failed(), "Invalid input channel must fail");
    invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("version", 99);
    require(decodeSession(invalid, restored).failed(), "Unknown startup versions must fail");
    invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("loop", "true");
    require(decodeSession(invalid, restored).failed(), "Startup types must be validated");
    invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("audioDevice", "<wrong/>");
    require(decodeSession(invalid, restored).failed(), "Device XML must have the right root");
    invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("audioFile", "relative.wav");
    require(decodeSession(invalid, restored).failed(), "Source paths must be absolute");
    invalid = encodeSession(state); invalid.getDynamicObject()->setProperty("selectedPreset", "../../escape");
    require(decodeSession(invalid, restored).failed(), "Selected identity must be safe");
    auto broken = state; broken.rack.values[0] = 1000;
    require(store.write(broken).failed() && store.read(restored, found).wasOk()
        && restored.rack.matches(state.rack), "Invalid replacement must preserve the previous settings");
    const auto file = scratch.getChildFile("session/settings.json");
    require(file.replaceWithText("{broken") && store.read(restored, found).failed(), "Corrupt startup JSON must be reported");
    const auto blocker = scratch.getChildFile("session-blocker");
    require(blocker.replaceWithText("file") && SessionStore(blocker.getChildFile("settings.json")).write(state).failed(),
        "Startup write errors must be reported");
    require(SessionStore(juce::File()).write(state).wasOk(), "Smoke checks must be able to disable persistence");
}
