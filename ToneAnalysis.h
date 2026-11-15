#pragma once
#include "ApiConfig.h"
#include "HttpClient.h"
#include "GeneratedRack.h"
#include "YouTubeMetadata.h"

namespace effecthack
{
enum class ToneSource { youtube, audioFile, songName };
enum class GuitarPart { rhythm, lead, intro };
struct ToneMatchInput
{
    ToneSource source = ToneSource::youtube;
    GuitarPart part = GuitarPart::rhythm;
    juce::String query;
    juce::File file;
    double startSeconds = -1, endSeconds = -1;
    bool custom = true, preserveMaster = true;
    float inputDb = 0, outputDb = -12;
};
struct ToneAnchor
{
    juce::String id, name, description;
    juce::var rack;
    juce::String audioBase64;
};
struct ToneAnalysis
{
    juce::var plan;
    bool needsClarification = false;
    juce::String error;
};
juce::String guitarPartName(GuitarPart);
bool parseReferenceTime(const juce::String&, double& seconds);
juce::Result validateToneMatchInput(const ToneMatchInput&);
juce::var toneAnalysisSchema(const std::vector<ToneAnchor>&);
HttpRequest makeToneAnalysisRequest(const ApiConfig&, const ToneMatchInput&, const YouTubeVideo&,
    const juce::var& referencePart, const std::vector<ToneAnchor>&, bool audioReference,
    const juce::var& previousPlan = {}, const juce::String& candidateAudio = {});
ToneAnalysis decodeToneAnalysis(const HttpResponse&, const std::vector<ToneAnchor>&);
// Compile bounded edits around a known, rendered processor configuration.
juce::var compileTonePlan(const juce::var&, const std::vector<ToneAnchor>&, const ToneMatchInput&);
}
