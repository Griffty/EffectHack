#pragma once
#include <juce_core/juce_core.h>

namespace effecthack
{
struct ApiConfig
{
    juce::String key, model = "gemini-3.5-flash-lite", error;
};
ApiConfig parseApiConfig(const juce::String& dotenv, const juce::String& environmentKey = {}, const juce::String& environmentModel = {});
/// Find the first .env walking working-directory parents, then executable parents
/// (up to eight levels each). Nonempty process variables override file values.
/// Configuration contains a secret; never serialize it into a preset or session.
ApiConfig loadApiConfig();
}
