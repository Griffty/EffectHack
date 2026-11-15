#include "ToneSlots.h"
#include "FactoryPresets.h"

namespace effecthack
{
namespace
{
constexpr const char* snapshotId = "0123456789abcdef0123456789abcdef";
bool validIndex(int index) { return index == 0 || index == 1; }
}
const ToneSnapshot* ToneSlots::stored(int index) const
{
    if (!validIndex(index)) return nullptr;
    const auto& slot = saved.slots[static_cast<size_t>(index)];
    return slot.has_value() ? &*slot : nullptr;
}
std::optional<ToneSnapshot> ToneSlots::recall(int index, const ToneSnapshot& current)
{
    if (!validIndex(index)) return std::nullopt;
    remember(current);
    if (const auto* slot = stored(index)) return *slot;
    return current; // The first visit starts a variation of the current tone.
}
bool ToneSlots::commit(int index, const ToneSnapshot& loaded)
{
    if (!validIndex(index)) return false;
    saved.slots[static_cast<size_t>(index)] = loaded;
    saved.active = index;
    return true;
}
void ToneSlots::copyToOther(const ToneSnapshot& current)
{
    remember(current);
    saved.slots[static_cast<size_t>(1 - saved.active)] = current;
}
juce::var encodeToneSlots(const ToneSlotState& state)
{
    auto* root = new juce::DynamicObject;
    root->setProperty("active", state.active);
    juce::Array<juce::var> slots;
    for (const auto& slot : state.slots)
    {
        if (!slot.has_value()) { slots.add(juce::var()); continue; }
        auto* entry = new juce::DynamicObject;
        auto identity = slot->identity;
        entry->setProperty("presetId", identity.id);
        identity.id = snapshotId;
        entry->setProperty("identity", encodePreset(identity));
        entry->setProperty("rack", encodePreset({ snapshotId, "Tone slot", slot->rack }));
        slots.add(juce::var(entry));
    }
    root->setProperty("slots", slots);
    return juce::var(root);
}
juce::Result decodeToneSlots(const juce::var& json, ToneSlotState& destination)
{
    const auto invalid = [] { return juce::Result::fail("A/B tone slots are invalid."); };
    if (!json.isObject() || !json["active"].isInt() || !validIndex(static_cast<int>(json["active"]))
        || !json["slots"].isArray() || json["slots"].size() != 2) return invalid();
    ToneSlotState state; state.active = json["active"];
    for (int index = 0; index < 2; ++index)
    {
        const auto entry = json["slots"][index];
        if (entry.isVoid()) continue;
        if (!entry.isObject() || !entry["presetId"].isString()) return invalid();
        ToneSnapshot snapshot;
        Preset rack;
        if (decodePreset(entry["identity"], snapshot.identity).failed() || decodePreset(entry["rack"], rack).failed()) return invalid();
        const auto id = entry["presetId"].toString();
        if (!isFactoryPreset(id) && (id.length() != 32 || !id.containsOnly("0123456789abcdef"))) return invalid();
        snapshot.identity.id = id;
        snapshot.rack = std::move(rack.rack);
        state.slots[static_cast<size_t>(index)] = std::move(snapshot);
    }
    destination = std::move(state);
    return juce::Result::ok();
}
}
