#include "../ToneSlots.h"
#include "../SessionState.h"
#include "../FactoryPresets.h"
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
void testToneSlots(const juce::File&)
{
    using namespace effecthack;
    ToneSlots slots;
    ToneSnapshot a { factoryPresets()[0].preset, factoryPresets()[0].preset.rack };
    a.rack.values[6] = 3; // Keep unsaved edits distinct from the preset baseline.
    auto b = a; b.identity.id.clear(); b.identity.name = "Generated lead";
    b.rack.values[16] = 19; b.rack.bypass[7] = false; std::swap(b.rack.order[2], b.rack.order[3]);
    require(slots.active() == 0 && slots.stored(0) == nullptr && slots.stored(-1) == nullptr, "Slots start empty on A");
    const auto first = slots.recall(1, a);
    require(first.has_value() && first->rack.matches(a.rack) && slots.active() == 0, "First switch clones current edits without changing selection early");
    require(slots.commit(1, *first), "Commit B");
    const auto recalledA = slots.recall(0, b);
    require(recalledA.has_value() && recalledA->rack.matches(a.rack) && recalledA->identity.rack.matches(a.identity.rack)
        && slots.stored(1)->rack.matches(b.rack), "Switching retains both live edits and original baseline");
    require(slots.active() == 1, "Failed or unfinished loads must retain outgoing selection");
    require(slots.commit(0, *recalledA), "Commit A");
    const auto recalledB = slots.recall(1, a);
    require(recalledB.has_value() && recalledB->rack.matches(b.rack) && recalledB->identity.name == b.identity.name, "Returning restores every effect, order and identity");
    const auto before = juce::JSON::toString(encodeToneSlots(slots.state()));
    require(!slots.recall(2, a).has_value() && !slots.commit(-1, b)
        && juce::JSON::toString(encodeToneSlots(slots.state())) == before, "Invalid selection is atomic");
    ToneSlotState restored;
    require(decodeToneSlots(encodeToneSlots(slots.state()), restored).wasOk() && restored.active == 0
        && restored.slots[0]->rack.matches(a.rack) && restored.slots[1]->rack.matches(b.rack)
        && restored.slots[1]->identity.id.isEmpty() && restored.slots[1]->identity.rack.matches(b.identity.rack), "Slot snapshots persist generated identity, baseline, edits and order");
    for (const auto* field : {"active", "slots"})
    {
        auto invalid = encodeToneSlots(slots.state()); invalid.getDynamicObject()->setProperty(field, "invalid");
        require(decodeToneSlots(invalid, restored).failed() && restored.slots[1]->rack.matches(b.rack), "Bad slot types fail atomically");
    }
    auto invalid = encodeToneSlots(slots.state());
    invalid["slots"][0]["rack"]["parameters"].getDynamicObject()->setProperty("ampGainDb", 1000);
    require(decodeToneSlots(invalid, restored).failed(), "Invalid slot controls rejected");
    invalid = encodeToneSlots(slots.state()); invalid["slots"][0].getDynamicObject()->setProperty("presetId", "../../escape");
    require(decodeToneSlots(invalid, restored).failed(), "Invalid slot identity rejected");
    slots.copyToOther(a);
    require(slots.active() == 0 && slots.stored(1)->rack.matches(a.rack), "Copy replaces other slot without switching");
    SessionState session; session.rack = a.rack; session.toneSlots = slots.state();
    SessionState resumed;
    require(decodeSession(encodeSession(session), resumed).wasOk() && resumed.toneSlots.has_value()
        && resumed.toneSlots->slots[1]->rack.matches(a.rack), "A/B slots participate in atomic startup persistence");
    auto legacy = encodeSession(session); legacy.getDynamicObject()->removeProperty("toneSlots");
    require(decodeSession(legacy, resumed).wasOk() && !resumed.toneSlots.has_value(), "Older startup files remain compatible");
}
