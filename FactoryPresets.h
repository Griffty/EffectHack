#pragma once
#include "PresetManager.h"

namespace effecthack
{
struct FactoryPreset
{
    Preset preset;
    juce::String description;
};
const std::vector<FactoryPreset>& factoryPresets();
const FactoryPreset* findFactoryPreset(const juce::String& id);
bool isFactoryPreset(const juce::String& id); // Includes the empty-ID Default rack.
}
