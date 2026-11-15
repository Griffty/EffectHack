#include "ApiConfig.h"

namespace effecthack
{
ApiConfig parseApiConfig(const juce::String& dotenv, const juce::String& environmentKey, const juce::String& environmentModel)
{
    ApiConfig config;
    for (auto line : juce::StringArray::fromLines(dotenv.trimCharactersAtStart(juce::String::charToString(0xfeff))))
    {
        line = line.trim();
        if (line.startsWith("export ")) line = line.substring(7).trimStart();
        if (line.startsWithChar('#') || !line.containsChar('=')) continue;
        const auto name = line.upToFirstOccurrenceOf("=", false, false).trim();
        if (name != "GEMINI_API_KEY" && name != "GEMINI_MODEL") continue;
        auto value = line.fromFirstOccurrenceOf("=", false, false).trim();
        if (value.startsWithChar('\'') || value.startsWithChar('"'))
        {
            const auto quote = value.substring(0, 1);
            const auto closing = value.indexOf(1, quote);
            if (closing < 0) { config.error = "Unclosed quote in Gemini .env settings."; return config; }
            value = value.substring(1, closing);
        }
        else value = value.upToFirstOccurrenceOf(" #", false, false).trim();
        if (name == "GEMINI_API_KEY") config.key = value;
        else if (value.isNotEmpty()) config.model = value;
    }
    if (environmentKey.isNotEmpty()) config.key = environmentKey;
    if (environmentModel.isNotEmpty()) config.model = environmentModel;
    if (config.model.startsWith("models/")) config.model = config.model.substring(7);
    if (config.key.isEmpty()) config.error = "Set GEMINI_API_KEY in .env or the app environment.";
    else if (config.key.length() > 512 || config.key.containsAnyOf("\r\n") || config.key.containsChar('\0'))
        config.error = "GEMINI_API_KEY has an invalid format.";
    else if (!config.model.startsWith("gemini-") || config.model.length() > 128
        || !config.model.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_."))
        config.error = "GEMINI_MODEL must be a Gemini model ID.";
    return config;
}
ApiConfig loadApiConfig()
{
    juce::String dotenv;
    for (auto root : { juce::File::getCurrentWorkingDirectory(), juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory() })
    {
        for (int depth = 0; depth < 8; ++depth)
        {
            const auto file = root.getChildFile(".env");
            if (file.existsAsFile())
            {
                if (file.getSize() > 65536) return { {}, {}, "Gemini .env file is too large." };
                dotenv = file.loadFileAsString();
                return parseApiConfig(dotenv, juce::SystemStats::getEnvironmentVariable("GEMINI_API_KEY", {}),
                    juce::SystemStats::getEnvironmentVariable("GEMINI_MODEL", {}));
            }
            const auto parent = root.getParentDirectory();
            if (parent == root) break;
            root = parent;
        }
    }
    return parseApiConfig({}, juce::SystemStats::getEnvironmentVariable("GEMINI_API_KEY", {}),
        juce::SystemStats::getEnvironmentVariable("GEMINI_MODEL", {}));
}
}
