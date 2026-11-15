#pragma once
#include <juce_core/juce_core.h>

namespace effecthack
{
struct ApiConfig
{
    juce::String key, model = "gemini-3.5-flash-lite", error;
};
ApiConfig parseApiConfig(const juce::String& dotenv, const juce::String& environmentKey = {}, const juce::String& environmentModel = {});
ApiConfig loadApiConfig();
}
