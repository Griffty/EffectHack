#pragma once
#include "ToneAnalysis.h"
#include "GeminiRecommender.h"
#include "PresetManager.h"

namespace effecthack
{
using ToneProgress = std::function<void(const juce::String&, double)>;
std::vector<ToneAnchor> makeToneAnchors(bool custom, const Cancellation&, bool includeAudio = true);
SongRecommendation matchSongTone(const ToneMatchInput&, const ApiConfig&, const HttpTransport&,
    const Cancellation&, const ToneProgress& = {}, bool includeAudioExamples = true);
// Match a fixed internal output level to a clean reference, keeping Master controls independent.
juce::Result balanceMatchedTone(juce::var& tone, const Cancellation&);
}
