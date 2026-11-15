#pragma once
#include "PresetManager.h"
#include <optional>

namespace effecthack
{
// Keep the saved/generated baseline separate from current edits, just as the
// main preset selector does. Slots never own devices, transport, or session mute.
struct ToneSnapshot { Preset identity; RackState rack; };
struct ToneSlotState
{
    int active = 0;
    std::array<std::optional<ToneSnapshot>, 2> slots;
};
/// Message-thread edit buffers, not two simultaneous audio processors. recall()
/// stages a destination; commit() updates active identity only after DSP commit.
/// MainComponent preserves shared Master gains and owns prepared cabinet caches.
class ToneSlots
{
public:
    int active() const { return saved.active; }
    const ToneSlotState& state() const { return saved; }
    void restore(ToneSlotState state) { saved = std::move(state); }
    void remember(const ToneSnapshot& current) { saved.slots[static_cast<size_t>(saved.active)] = current; }
    const ToneSnapshot* stored(int index) const;
    // Captures outgoing edits, but does not activate the destination until the
    // complete prepared rack has committed successfully.
    std::optional<ToneSnapshot> recall(int index, const ToneSnapshot& current);
    bool commit(int index, const ToneSnapshot& loaded);
    void copyToOther(const ToneSnapshot& current);
private:
    ToneSlotState saved;
};
juce::var encodeToneSlots(const ToneSlotState&);
juce::Result decodeToneSlots(const juce::var&, ToneSlotState&);
}
