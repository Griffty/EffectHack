#pragma once
#include "PresetManager.h"
#include "GeneratedRack.h"
#include <atomic>

namespace effecthack
{
struct PreparedGeneratedTone
{
    Preset preset;
    PreparedPreset prepared;
    float outputAdjustmentDb = 0;
    float ampAdjustmentDb = 0;
};
juce::Result decodeGeneratedTone(const juce::var&, Preset&);
// Off-thread headroom check. Attenuates Output, or the amp's post-gain level
// when Master controls must be preserved. Never changes preamp drive.
PreparedGeneratedTone prepareGeneratedTone(const juce::var&, const std::atomic<bool>* cancelled = nullptr, bool preserveMaster = false);
}
