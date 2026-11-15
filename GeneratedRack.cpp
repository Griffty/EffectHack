#include "GeneratedRack.h"
#include <cmath>

namespace effecthack
{
namespace
{
juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* result = new juce::DynamicObject;
    for (const auto& property : properties) result->setProperty(property.first, property.second);
    return juce::var(result);
}
bool exactKeys(const juce::var& value, const juce::StringArray& keys)
{
    if (!value.isObject() || value.getDynamicObject()->getProperties().size() != keys.size()) return false;
    for (const auto& key : keys) if (!value.hasProperty(key)) return false;
    return true;
}
}
juce::var generatedRackSchema()
{
    auto* parameters = new juce::DynamicObject;
    auto* bypass = new juce::DynamicObject;
    juce::Array<juce::var> names, stages, voices;
    for (const auto& field : rackParameterSpecs)
    {
        parameters->setProperty(field.key, object({ { "type", "NUMBER" }, { "minimum", field.minimum }, { "maximum", field.maximum } }));
        names.add(field.key);
    }
    for (const auto* id : rackStageIds) { bypass->setProperty(id, object({ { "type", "BOOLEAN" } })); stages.add(id); }
    for (const auto* voice : ampVoiceIds) voices.add(voice);
    return object({ { "type", "OBJECT" }, { "nullable", true }, { "properties", object({
        { "name", object({ { "type", "STRING" }, { "description", "Short song tone name, 1-64 characters. Never 'Default rack'." } }) },
        { "parameters", object({ { "type", "OBJECT" }, { "properties", juce::var(parameters) }, { "required", names } }) },
        { "bypass", object({ { "type", "OBJECT" }, { "properties", juce::var(bypass) }, { "required", stages } }) },
        { "ampVoice", object({ { "type", "STRING" }, { "enum", voices } }) },
        { "order", object({ { "type", "ARRAY" }, { "minItems", 9 }, { "maxItems", 9 },
            { "items", object({ { "type", "STRING" }, { "enum", stages } }) } }) },
        { "cabinet", object({ { "type", "STRING" }, { "enum", juce::Array<juce::var> { "builtin" } } }) } }) },
        { "required", juce::Array<juce::var> { "name", "parameters", "bypass", "ampVoice", "order", "cabinet" } } });
}
juce::var generatedRackCapabilities()
{
    return object({ { "schema", generatedRackSchema() }, { "processor_notes", juce::String(
        "All dB parameters are actual decibels. Mix/depth/size/feedback values are fractions 0-1, not percentages. "
        "inputDb is pre-rack trim; outputDb is final volume after all stages. Both may be changed. "
        "driveDb/distortionMix control a 4x-oversampled tanh pedal. Amp has independent clean/crunch/highGain voices, "
        "ampGainDb is preamp drive and ampLevelDb is post-amp volume. Use factory examples to judge gain/level relationships. "
        "Amp bass/mid/treble/presence are tone controls; EQ bass/mid/treble are a separate stage. "
        "The cabinet is a built-in synthetic speaker IR; no other files/effects/amp brands are available. "
        "Noise gate is stereo-linked, with threshold/attack/hold/release. Keep it gentle enough for quiet notes and decay. "
        "Compressor is stereo-linked with threshold, ratio, attack, release, makeup and parallel mix; no automatic makeup. "
        "Chorus is stereo modulation; delay time is milliseconds with feedback and mix; reverb is a generic room algorithm. "
        "true bypass means DISABLED. Every stage occurs exactly once in order; disable unused stages rather than omitting them. "
        "Use one overall tone per song. Default order is gate, compressor, distortion, amp, cabinet, eq, chorus, delay, reverb. "
        "Reorder only for a musical reason. Default to Input 0 dB if live hardware levels are unavailable. "
        "If hardware peak/RMS are supplied, they describe current playing, not a guarantee of future peaks. "
        "Keep final Output conservative (usually -12 to -6 dB), amp level balanced, spatial feedback and makeup moderate.") } });
}
juce::Result validateGeneratedRack(const juce::var& tone)
{
    const auto fail = [](const juce::String& detail) { return juce::Result::fail("Invalid generated rack: " + detail); };
    if (!exactKeys(tone, { "name", "parameters", "bypass", "ampVoice", "order", "cabinet" })) return fail("missing or unknown fields.");
    const auto name = tone["name"].toString();
    if (!tone["name"].isString() || name.isEmpty() || name.length() > 64 || name != name.trim() || name.equalsIgnoreCase("Default rack")) return fail("tone name.");
    for (const auto c : name) if (c < 32 || c == 127) return fail("tone name.");
    juce::StringArray parameterNames, bypassNames;
    for (const auto& field : rackParameterSpecs) parameterNames.add(field.key);
    for (const auto* stage : rackStageIds) bypassNames.add(stage);
    if (!exactKeys(tone["parameters"], parameterNames) || !exactKeys(tone["bypass"], bypassNames)) return fail("all controls are required.");
    for (const auto& field : rackParameterSpecs)
    {
        const auto value = tone["parameters"][field.key];
        if (!value.isInt() && !value.isInt64() && !value.isDouble()) return fail(field.key);
        const auto number = static_cast<double>(value);
        const auto stored = static_cast<float>(number);
        if (!std::isfinite(number) || !std::isfinite(stored) || stored < field.minimum || stored > field.maximum) return fail(field.key);
    }
    for (const auto* stage : rackStageIds) if (!tone["bypass"][stage].isBool()) return fail("bypass must be boolean.");
    const auto voice = tone["ampVoice"].toString();
    if (!tone["ampVoice"].isString() || std::none_of(ampVoiceIds.begin(), ampVoiceIds.end(), [&voice](const char* id) { return voice == id; })) return fail("amp voice.");
    const auto orderValue = tone["order"];
    const auto* order = orderValue.getArray();
    if (order == nullptr || order->size() != static_cast<int>(rackStageIds.size())) return fail("stage order.");
    RackOrder stageOrder;
    for (size_t i = 0; i < stageOrder.size(); ++i)
    {
        if (!(*order)[static_cast<int>(i)].isString()) return fail("stage order.");
        stageOrder[i] = (*order)[static_cast<int>(i)].toString();
    }
    if (!validRackOrder(stageOrder)) return fail("stage order must contain every stage exactly once.");
    if (!tone["cabinet"].isString() || tone["cabinet"].toString() != "builtin") return fail("unavailable cabinet.");
    return juce::Result::ok();
}
}
