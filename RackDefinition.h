#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <algorithm>
namespace effecthack
{
enum class AmpVoice { clean, crunch, highGain };
inline constexpr std::array<const char*, 3> ampVoiceIds { "clean", "crunch", "highGain" };
inline constexpr std::array<const char*, 9> rackStageIds { "gate", "compressor", "distortion", "amp", "cabinet", "eq", "chorus", "delay", "reverb" };
using RackOrder = std::array<juce::String, rackStageIds.size()>;
inline RackOrder defaultRackOrder()
{
    RackOrder result;
    for (size_t i = 0; i < result.size(); ++i) result[i] = rackStageIds[i];
    return result;
}
inline bool validRackOrder(const RackOrder& order) noexcept
{
    for (const auto* id : rackStageIds) if (std::count(order.begin(), order.end(), id) != 1) return false;
    return true;
}
struct RackParameterSpec { const char* key; float minimum, maximum; };
inline constexpr RackParameterSpec rackParameterSpecs[] {
    { "inputDb", -24, 24 }, { "driveDb", 0, 48 }, { "distortionMix", 0, 1 },
    { "outputDb", -60, 0 }, { "bassDb", -12, 12 }, { "midDb", -12, 12 },
    { "trebleDb", -12, 12 }, { "chorusRateHz", .1f, 5 }, { "chorusDepth", 0, 1 },
    { "chorusMix", 0, 1 }, { "delayTimeMs", 20, 1500 }, { "delayFeedback", 0, .8f },
    { "delayMix", 0, 1 }, { "reverbSize", 0, .95f }, { "reverbDamping", 0, 1 },
    { "reverbMix", 0, 1 },
    { "ampGainDb", 0, 36 }, { "ampBassDb", -12, 12 }, { "ampMidDb", -12, 12 },
    { "ampTrebleDb", -12, 12 }, { "ampPresenceDb", -12, 12 }, { "ampLevelDb", -24, 6 },
    { "gateThresholdDb", -90, 0 }, { "gateAttackMs", .1f, 20 }, { "gateHoldMs", 0, 250 }, { "gateReleaseMs", 10, 1000 },
    { "compressorThresholdDb", -60, 0 }, { "compressorRatio", 1, 12 }, { "compressorAttackMs", .1f, 100 },
    { "compressorReleaseMs", 10, 1000 }, { "compressorMakeupDb", 0, 18 }, { "compressorMix", 0, 1 }
};
}
