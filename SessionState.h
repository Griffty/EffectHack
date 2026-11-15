#pragma once
#include "PresetManager.h"
#include "ToneSlots.h"
#include <optional>

namespace effecthack
{
// Startup state is separate from reusable tone presets. Transport always starts stopped.
struct SessionState
{
    RackState rack;
    juce::String presetId, audioFile, deviceXml;
    int sourceId = 2, inputChannel = 0;
    bool loop = true;
    double tunerReference = 440;
    bool tunerMute = true;
    std::optional<Preset> generatedPreset;
    std::optional<ToneSlotState> toneSlots;
};
juce::var encodeSession(const SessionState&);
juce::Result decodeSession(const juce::var&, SessionState&);
/// Validated startup JSON written via a temporary file and atomic replacement.
/// An empty path disables persistence for isolated tests. Save on normal shutdown;
/// in-memory phrases and transient transport/mute/bypass states are not restored.
class SessionStore
{
public:
    explicit SessionStore(juce::File file = defaultFile()) : path(std::move(file)) {}
    static juce::File defaultFile();
    juce::Result read(SessionState&, bool& found) const;
    juce::Result write(const SessionState&) const;
private:
    juce::File path;
};
}
