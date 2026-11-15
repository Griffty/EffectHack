#pragma once
#include "ApiConfig.h"
#include "YouTubeMetadata.h"
#include "GeneratedRack.h"

namespace effecthack
{
struct SelectionPreset { juce::String id, name, description; juce::var rack; };
enum class RecommendationMode { factoryPreset, customRack };
struct SongRecommendation
{
    YouTubeVideo video;
    juce::String presetId, error;
    bool needsClarification = false;
    juce::var tone;
    juce::var analysis;
    juce::String sourceLabel, details, warning, modelUsed;
    bool calibrated = false, preserveMaster = false;
};
HttpRequest makeRecommendationRequest(const ApiConfig&, const YouTubeVideo&, const std::vector<SelectionPreset>&,
    RecommendationMode = RecommendationMode::factoryPreset, const juce::var& inputContext = {});
SongRecommendation decodeRecommendation(const HttpResponse&, const YouTubeVideo&, const std::vector<SelectionPreset>&,
    RecommendationMode = RecommendationMode::factoryPreset);
SongRecommendation recommendYouTube(const juce::String&, const ApiConfig&, const std::vector<SelectionPreset>&,
    const HttpTransport&, const Cancellation&, RecommendationMode = RecommendationMode::factoryPreset, const juce::var& inputContext = {});
}
