#include "FactoryPresets.h"

namespace effecthack
{
namespace
{
// Indices follow the versioned RackState parameter layout. No DSP lives here.
enum Knob : size_t
{
    output = 3, eqBass = 4, eqMid = 5, eqTreble = 6,
    chorusRate = 7, chorusDepth = 8, chorusMix = 9, delayTime = 10, delayFeedback = 11, delayMix = 12,
    reverbSize = 13, reverbDamping = 14, reverbMix = 15,
    ampGain = 16, ampBass = 17, ampMid = 18, ampTreble = 19, ampPresence = 20, ampLevel = 21,
    gateThreshold = 22, gateAttack = 23, gateHold = 24, gateRelease = 25,
    compressorThreshold = 26, compressorRatio = 27, compressorAttack = 28, compressorRelease = 29,
    compressorMakeup = 30, compressorMix = 31
};
void enable(RackState& rack, const juce::String& id)
{
    for (size_t i = 0; i < rackStageIds.size(); ++i) if (id == rackStageIds[i]) rack.bypass[i] = false;
}
RackState base(AmpVoice voice, float gain)
{
    RackState rack;
    rack.bypass.fill(true);
    rack.ampVoice = static_cast<int>(voice);
    rack.values[output] = -12;
    rack.values[ampGain] = gain;
    rack.values[ampLevel] = 0;
    for (const auto* stage : { "amp", "cabinet", "eq" }) enable(rack, stage);
    return rack;
}
void ambience(RackState& rack, float size, float mix)
{
    enable(rack, "reverb");
    rack.values[reverbSize] = size; rack.values[reverbMix] = mix; rack.values[reverbDamping] = .6f;
}
void compression(RackState& rack, float ratio, float mix)
{
    enable(rack, "compressor");
    rack.values[compressorThreshold] = -24; rack.values[compressorRatio] = ratio;
    rack.values[compressorAttack] = 15; rack.values[compressorRelease] = 180;
    rack.values[compressorMakeup] = 1; rack.values[compressorMix] = mix;
}
std::vector<FactoryPreset> buildPresets()
{
    std::vector<FactoryPreset> result;
    auto clean = base(AmpVoice::clean, 6);
    clean.values[ampBass] = .5f; clean.values[ampTreble] = 1.5f; clean.values[ampPresence] = .5f;
    compression(clean, 2, .6f); ambience(clean, .25f, .08f);
    result.push_back({ { "factory.clean", "Clean", clean }, "Clear amp tone with light compression and a small room." });

    auto crunch = base(AmpVoice::crunch, 10);
    crunch.values[ampLevel] = -18.8f;
    crunch.values[ampBass] = 1; crunch.values[ampMid] = 2; crunch.values[ampTreble] = .5f;
    enable(crunch, "gate"); crunch.values[gateThreshold] = -60; crunch.values[gateHold] = 40; crunch.values[gateRelease] = 140;
    ambience(crunch, .25f, .06f);
    result.push_back({ { "factory.crunch", "Crunch", crunch }, "Mid-forward rhythm crunch with a gentle gate and a small room." });

    auto metal = base(AmpVoice::highGain, 16);
    metal.values[ampLevel] = -21.0f;
    metal.values[ampBass] = 1; metal.values[ampMid] = -2; metal.values[ampTreble] = 1.5f; metal.values[ampPresence] = 1.5f;
    metal.values[eqBass] = -1; metal.values[eqMid] = -1; metal.values[eqTreble] = .5f;
    enable(metal, "gate"); metal.values[gateThreshold] = -55; metal.values[gateAttack] = .5f;
    metal.values[gateHold] = 35; metal.values[gateRelease] = 100;
    result.push_back({ { "factory.metal-rhythm", "Metal Rhythm", metal }, "Tight high gain with a gate and dry rhythm articulation." });

    auto lead = base(AmpVoice::highGain, 12);
    lead.values[ampLevel] = -20.5f;
    lead.values[ampMid] = 3; lead.values[ampTreble] = 1;
    enable(lead, "gate"); lead.values[gateThreshold] = -60; lead.values[gateHold] = 60; lead.values[gateRelease] = 180;
    compression(lead, 2, .5f); lead.values[compressorMakeup] = 1.5f;
    enable(lead, "delay"); lead.values[delayTime] = 350; lead.values[delayFeedback] = .25f; lead.values[delayMix] = .14f;
    ambience(lead, .4f, .12f);
    result.push_back({ { "factory.lead", "Lead", lead }, "Focused lead tone with moderate sustain, delay, and room ambience." });

    auto chorus = base(AmpVoice::clean, 6);
    chorus.values[ampLevel] = 2.8f;
    chorus.values[ampBass] = .5f; chorus.values[ampTreble] = 1;
    compression(chorus, 2.5f, .6f);
    enable(chorus, "chorus"); chorus.values[chorusRate] = .65f; chorus.values[chorusDepth] = .45f; chorus.values[chorusMix] = .3f;
    ambience(chorus, .4f, .12f);
    result.push_back({ { "factory.chorus-clean", "Chorus Clean", chorus }, "Clean amp, slow stereo chorus, and a light reverb." });

    auto ambient = base(AmpVoice::clean, 5);
    ambient.values[ampLevel] = 3.4f;
    ambient.values[ampBass] = -1; ambient.values[ampTreble] = 1.5f;
    compression(ambient, 2, .5f); ambient.values[compressorAttack] = 20; ambient.values[compressorRelease] = 220;
    enable(ambient, "chorus"); ambient.values[chorusRate] = .3f; ambient.values[chorusDepth] = .25f; ambient.values[chorusMix] = .15f;
    enable(ambient, "delay"); ambient.values[delayTime] = 550; ambient.values[delayFeedback] = .45f; ambient.values[delayMix] = .28f;
    ambience(ambient, .78f, .28f);
    result.push_back({ { "factory.ambient-clean", "Ambient Clean", ambient }, "Wide clean texture with slow modulation and longer delay/reverb tails." });
    return result;
}
}
const std::vector<FactoryPreset>& factoryPresets()
{
    static const auto presets = buildPresets();
    return presets;
}
const FactoryPreset* findFactoryPreset(const juce::String& id)
{
    for (const auto& preset : factoryPresets()) if (preset.preset.id == id) return &preset;
    return nullptr;
}
bool isFactoryPreset(const juce::String& id) { return id.isEmpty() || findFactoryPreset(id) != nullptr; }
}
