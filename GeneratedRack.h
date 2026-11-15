#pragma once
#include "RackDefinition.h"

namespace effecthack
{
/// Shared full-rack JSON contract. Keep schema, local validator, RackDefinition,
/// and preset encoding consistent when introducing a parameter or stage.
juce::var generatedRackSchema();
juce::var generatedRackCapabilities();
juce::Result validateGeneratedRack(const juce::var&);
}
