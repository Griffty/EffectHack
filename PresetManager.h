#pragma once
#include "AudioEngine.h"

namespace effecthack
{
// Tone only: sources, transport, devices, and monitoring are deliberately excluded.
struct RackState
{
    std::array<float, 32> values { 0, 18, 1, -12, 0, 0, 0, .8f, .35f, .25f,
                                 350, .3f, .2f, .45f, .5f, .2f, 12, 0, 0, 0, 0, -6, -50, 1, 30, 120, -18, 4, 10, 120, 0, 1 };
    std::array<bool, rackStageIds.size()> bypass { true, true, false, true, false, false, true, true, true };
    int ampVoice = static_cast<int>(AmpVoice::crunch);
    RackOrder order = defaultRackOrder();
    juce::String cabinetPath; // Empty means the built-in cabinet.
    bool matches(const RackState&) const;
};
struct Preset
{
    juce::String id, name;
    RackState rack;
};
struct PreparedPreset
{
    RackState rack;
    std::shared_ptr<const CabinetImpulse> impulse;
    juce::String error;
};
RackState captureRack(const AudioEngine&);
juce::Result validateRack(const RackState&);
PreparedPreset preparePreset(const RackState&); // File decoding off the audio thread.
// Caller must hold the audio device manager's callback lock.
juce::Result applyPreset(AudioEngine&, const PreparedPreset&);
juce::var encodePreset(const Preset&);
juce::Result decodePreset(const juce::var&, Preset&);

class PresetManager
{
public:
    explicit PresetManager(juce::File directory = defaultDirectory()) : folder(std::move(directory)) {}
    static juce::File defaultDirectory();
    const juce::File& directory() const { return folder; }
    std::vector<Preset> list(juce::String& warning) const;
    juce::Result read(const juce::String& id, Preset&) const;
    juce::Result saveAs(const juce::String& name, const RackState&, Preset& saved) const;
    juce::Result save(const Preset&) const;
    juce::Result rename(const Preset&, const juce::String& name, Preset& renamed) const;
    juce::Result remove(const juce::String& id) const;
private:
    juce::File folder;
    juce::Result write(const Preset&) const;
    juce::Result checkName(const juce::String&, const juce::String& excludingId) const;
};
}
