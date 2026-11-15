#pragma once
#include "ApiConfig.h"
#include "HttpClient.h"
#include "GeneratedRack.h"
#include "YouTubeMetadata.h"

namespace effecthack
{
enum class ToneSource { youtube, audioFile, songName };
enum class GuitarPart { rhythm, lead, intro };
/// User intent, not a rack snapshot. Negative section times mean Auto; a start
/// without an end selects 30 seconds. Master values are captured for preparation
/// and refreshed at commit when preserveMaster is enabled.
struct ToneMatchInput
{
    ToneSource source = ToneSource::youtube;
    GuitarPart part = GuitarPart::rhythm;
    juce::String query;
    juce::File file;
    double startSeconds = -1, endSeconds = -1;
    bool custom = true, preserveMaster = true;
    float inputDb = 0, outputDb = 0;
};
/// Known processor configuration and its optional level-balanced audio example.
/// rack contains mutable juce::var objects; compilation must deep-copy it.
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
/// Treat model JSON as untrusted: validate exact fields, anchor IDs, types/ranges,
/// and bounded strings before allowing it into deterministic compilation.
ToneAnalysis decodeToneAnalysis(const HttpResponse&, const std::vector<ToneAnchor>&);
// Compile bounded edits around a known, rendered processor configuration.
/// Requires a validated plan from decodeToneAnalysis(). Keeps the anchor's order
/// and amp voice; returns a new full rack rather than editing the anchor in place.
juce::var compileTonePlan(const juce::var&, const std::vector<ToneAnchor>&, const ToneMatchInput&);
}
