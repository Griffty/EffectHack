#pragma once
#include "ToneAnalysis.h"
#include "GeminiRecommender.h"
#include "PresetManager.h"

namespace effecthack
{
using ToneProgress = std::function<void(const juce::String&, double)>;
/// Render the factory catalog plus three custom anchors on a worker.
/// includeAudio=false omits example rendering for deterministic request tests.
std::vector<ToneAnchor> makeToneAnchors(bool custom, const Cancellation&, bool includeAudio = true);
/** Blocking worker-side orchestration; never call from the audio or UI thread.
 * The injected transport enables offline tests. Pass a non-null shared cancellation
 * flag. Progress runs on the calling worker, so UI consumers must marshal updates.
 * Returns a recommendation/error without mutating the live engine. Custom media
 * matching may make a second review request; callers still prepare/check the final
 * rack with prepareGeneratedTone() before committing it.
 */
SongRecommendation matchSongTone(const ToneMatchInput&, const ApiConfig&, const HttpTransport&,
    const Cancellation&, const ToneProgress& = {}, bool includeAudioExamples = true);
// Match a fixed internal output level to a clean reference, keeping Master controls independent.
juce::Result balanceMatchedTone(juce::var& tone, const Cancellation&);
}
