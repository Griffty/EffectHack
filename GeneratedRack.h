#pragma once
#include "RackDefinition.h"

namespace effecthack
{
juce::var generatedRackSchema();
juce::var generatedRackCapabilities();
juce::Result validateGeneratedRack(const juce::var&);
}
