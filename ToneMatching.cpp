#include "ToneMatching.h"
#include "FactoryPresets.h"
#include "ReferenceAudio.h"
#include "GeneratedTonePreparation.h"
#include <cmath>
#include <mutex>

namespace effecthack
{
namespace
{
juce::var object(std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* result = new juce::DynamicObject;
    for (const auto& property : properties) result->setProperty(property.first, property.second);
    return juce::var(result);
}
struct Render { juce::AudioBuffer<float> samples; double rms = 0; float peak = 0; juce::String error; };
Render render(const RackState& rack, float riffPeak, double seconds, const Cancellation& cancel)
{
    Render result;
    if (cancel->load()) { result.error = "Cancelled."; return result; }
    AudioEngine engine;
    auto riff = makeTestRiff();
    riff->samples.applyGain(riffPeak / .35f);
    engine.testInput.setClip(riff); engine.inputSource = AudioEngine::InputSource::testRiff;
    const auto prepared = preparePreset(rack);
    if (auto applied = applyPreset(engine, prepared); applied.failed()) { result.error = applied.getErrorMessage(); return result; }
    engine.prepare(48000, 256); engine.monitoring = true;
    const auto count = juce::roundToInt(seconds * 48000);
    result.samples.setSize(2, count);
    juce::AudioBuffer<float> block(2, 256);
    double energy = 0;
    for (int offset = 0; offset < count; offset += 256)
    {
        if (cancel->load()) { result.error = "Cancelled."; return result; }
        const auto n = juce::jmin(256, count - offset);
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, block.getArrayOfWritePointers(), 2, n, {});
        result.peak = juce::jmax(result.peak, engine.outputUnclampedPeak.load());
        for (int channel = 0; channel < 2; ++channel)
        {
            result.samples.copyFrom(channel, offset, block, channel, 0, n);
            for (int i = 0; i < n; ++i)
            {
                const auto value = block.getSample(channel, i);
                if (!std::isfinite(value)) { result.error = "Processor produced invalid reference audio."; return result; }
                energy += static_cast<double>(value) * value;
            }
        }
    }
    result.rms = std::sqrt(energy / (2 * count));
    return result;
}
std::vector<ToneAnchor> anchorDefinitions(bool custom)
{
    std::vector<ToneAnchor> result;
    for (const auto& preset : factoryPresets())
        result.push_back({ preset.preset.id, preset.preset.name, preset.description, encodePreset(preset.preset), {} });
    if (custom)
    {
        auto edge = factoryPresets()[0].preset;
        edge.id = "match.edge"; edge.name = "Edge of breakup";
        edge.rack.values[16] = 18; edge.rack.values[21] = -4;
        edge.rack.bypass[1] = true; edge.rack.bypass[8] = true;
        result.push_back({ edge.id, edge.name, "Touch-sensitive clean amp pushed into light breakup; articulate, not heavy distortion.", encodePreset(edge), {} });
        auto classic = factoryPresets()[1].preset;
        classic.id = "match.classic-crunch"; classic.name = "Classic crunch";
        classic.rack.values[16] = 2; classic.rack.values[21] = -14;
        classic.rack.bypass[8] = true;
        result.push_back({ classic.id, classic.name, "Lower-gain crunchy amp with open pick attack and classic rock rhythm character.", encodePreset(classic), {} });
        auto pedal = factoryPresets()[0].preset;
        pedal.id = "match.pedal-drive"; pedal.name = "Pedal drive";
        pedal.rack.values[1] = 24; pedal.rack.values[2] = 1;
        pedal.rack.values[16] = 3; pedal.rack.values[21] = -10;
        pedal.rack.values[4] = -2; pedal.rack.values[6] = 1;
        pedal.rack.bypass[1] = true; pedal.rack.bypass[2] = false; pedal.rack.bypass[8] = true;
        result.push_back({ pedal.id, pedal.name, "Buzzy soft-clipping distortion pedal into a clean amp; garage-rock edge. Gain delta adjusts pedal Drive for this example.", encodePreset(pedal), {} });
    }
    return result;
}
}
std::vector<ToneAnchor> makeToneAnchors(bool custom, const Cancellation& cancel, bool includeAudio)
{
    if (!includeAudio) return anchorDefinitions(custom);
    static std::mutex mutex;
    static std::vector<ToneAnchor> cached;
    // This lock is exclusively worker-side; it is never acquired by the audio callback.
    const std::lock_guard<std::mutex> lock(mutex);
    if (cached.empty())
    {
        auto definitions = anchorDefinitions(true);
        for (auto& anchor : definitions)
        {
            if (cancel->load()) return {};
            Preset preset;
            auto encoded = juce::JSON::parse(juce::JSON::toString(anchor.rack));
            encoded.getDynamicObject()->setProperty("id", juce::Uuid().toString());
            if (decodePreset(encoded, preset).failed()) return {};
            preset.rack.values[0] = 0; preset.rack.values[3] = -24;
            auto audio = render(preset.rack, .25f, 8, cancel);
            if (audio.error.isNotEmpty() || audio.rms < 1.e-8) return {};
            // Equal-loudness examples prevent louder saturation being mistaken for a better match.
            const auto gain = static_cast<float>(juce::jmin(.07 / audio.rms, .8 / juce::jmax(.00001f, audio.peak)));
            audio.samples.applyGain(gain);
            auto wav = encodeToneExample(audio.samples, 48000, cancel);
            if (wav.error.isNotEmpty()) return {};
            anchor.audioBase64 = std::move(wav.base64);
        }
        cached = std::move(definitions);
    }
    auto result = cached;
    if (!custom)
        result.erase(std::remove_if(result.begin(), result.end(), [](const auto& a) { return !a.id.startsWith("factory."); }), result.end());
    return result;
}
juce::Result balanceMatchedTone(juce::var& tone, const Cancellation& cancel)
{
    Preset preset;
    if (auto valid = decodeGeneratedTone(tone, preset); valid.failed()) return valid;
    auto clean = factoryPresets()[0].preset.rack;
    clean.values[0] = preset.rack.values[0]; clean.values[3] = -24; clean.values[21] = 0;
    clean.bypass[0] = clean.bypass[1] = clean.bypass[8] = true;
    auto reference = render(clean, .25f, 8, cancel);
    if (reference.error.isNotEmpty()) return juce::Result::fail(reference.error);
    auto calibration = preset.rack;
    calibration.values[3] = -24; calibration.values[21] = -12;
    auto measured = render(calibration, .25f, 8, cancel);
    if (measured.error.isNotEmpty()) return juce::Result::fail(measured.error);
    if (reference.rms < 1.e-8 || measured.rms < 1.e-8) return juce::Result::fail("Could not balance this tone's reference level.");
    const auto adjustment = juce::Decibels::gainToDecibels(static_cast<float>(reference.rms / measured.rms));
    tone["parameters"].getDynamicObject()->setProperty("ampLevelDb", juce::jlimit(-24.0f, 6.0f, -12.0f + adjustment));
    return juce::Result::ok();
}
SongRecommendation matchSongTone(const ToneMatchInput& input, const ApiConfig& config, const HttpTransport& http,
    const Cancellation& cancel, const ToneProgress& progress, bool includeAudioExamples)
{
    SongRecommendation result;
    const auto update = [&](const juce::String& text, double value) { if (progress) progress(text, value); };
    const auto cancelled = [&]() { if (!cancel->load()) return false; result.error = "Cancelled."; return true; };
    if (cancelled()) return result;
    if (auto valid = validateToneMatchInput(input); valid.failed()) { result.error = valid.getErrorMessage(); return result; }
    if (config.error.isNotEmpty() || config.key.isEmpty()) { result.error = config.error.isNotEmpty() ? config.error : "Gemini API key is missing."; return result; }
    juce::var referencePart;
    bool referenceAudio = input.source != ToneSource::songName;
    update("Reading reference", .05);
    if (input.source == ToneSource::youtube)
    {
        result.video = resolveYouTube(input.query, http, cancel);
        if (cancelled()) return result;
        if (result.video.error.isNotEmpty())
        {
            result.video = parseYouTubeLink(input.query);
            result.warning = "YouTube title unavailable; matching the audio reference.";
        }
        referencePart = object({ { "fileData", object({ { "fileUri", result.video.url }, { "mimeType", "video/mp4" } }) } });
        if (input.startSeconds >= 0)
        {
            const auto end = input.endSeconds >= 0 ? input.endSeconds : input.startSeconds + 30;
            referencePart.getDynamicObject()->setProperty("videoMetadata", object({
                { "startOffset", juce::String(input.startSeconds, 3) + "s" }, { "endOffset", juce::String(end, 3) + "s" }, { "fps", .1 } }));
        }
    }
    else if (input.source == ToneSource::audioFile)
    {
        auto audio = prepareAudioReference(input.file, input.startSeconds, input.endSeconds, cancel);
        if (audio.error.isNotEmpty()) { result.error = audio.error; return result; }
        result.video.title = input.file.getFileNameWithoutExtension();
        referencePart = object({ { "inlineData", object({ { "mimeType", "audio/wav" }, { "data", audio.base64 } }) } });
    }
    else result.video.title = input.query.trim();
    update("Preparing processor examples", .15);
    auto anchors = makeToneAnchors(input.custom, cancel, includeAudioExamples);
    if (cancelled()) return result;
    if (anchors.empty()) { result.error = "Could not prepare the processor's tone examples."; return result; }
    update(referenceAudio ? "Listening to reference" : "Estimating song tone", .4);
    juce::StringArray models;
    models.add(config.model);
    models.addIfNotAlreadyThere("gemini-3.5-flash-lite");
    ToneAnalysis analysis;
    for (int attempt = 0; attempt < models.size(); ++attempt)
    {
        if (cancelled()) return result;
        auto selectedConfig = config; selectedConfig.model = models[attempt];
        const auto response = http(makeToneAnalysisRequest(selectedConfig, input, result.video, referencePart, anchors, referenceAudio), cancel);
        if (cancelled()) return result;
        if (response.status == 404 && attempt + 1 < models.size())
        { update("Trying available Gemini model", .45); continue; }
        result.modelUsed = selectedConfig.model;
        if (selectedConfig.model != config.model)
            result.warning += (result.warning.isEmpty() ? "" : "\n") + juce::String("Configured model unavailable; used ") + selectedConfig.model + ".";
        analysis = decodeToneAnalysis(response, anchors);
        break;
    }
    if (analysis.error.isNotEmpty()) { result.error = analysis.error; result.needsClarification = analysis.needsClarification; return result; }
    if (!analysis.plan.isObject()) { result.error = "No usable tone analysis was returned."; return result; }
    if (!referenceAudio && analysis.plan["confidence"].toString() == "high")
        analysis.plan.getDynamicObject()->setProperty("confidence", "medium");
    result.analysis = analysis.plan;
    result.sourceLabel = referenceAudio ? "Audio reference" : "Song estimate";
    const auto anchor = std::find_if(anchors.begin(), anchors.end(), [&analysis](const auto& a) { return a.id == analysis.plan["base_preset"].toString(); });
    const auto describe = [&]
    {
        const auto finalAnchor = std::find_if(anchors.begin(), anchors.end(), [&](const auto& a)
            { return a.id == result.analysis["base_preset"].toString(); });
        result.details = result.analysis["reason"].toString() + "\nBase: " + finalAnchor->name
            + "\n" + guitarPartName(input.part) + " / " + result.sourceLabel
            + "\nModel confidence: " + result.analysis["confidence"].toString() + "\nModel: " + result.modelUsed;
        if (result.analysis["limitations"].toString().isNotEmpty()) result.details += "\nApproximation: " + result.analysis["limitations"].toString();
        if (result.warning.isNotEmpty()) result.details += "\n" + result.warning;
    };
    describe();
    if (!input.custom) { result.presetId = anchor->id; update("Factory tone ready", 1); return result; }
    update("Balancing tone", .8);
    result.tone = compileTonePlan(analysis.plan, anchors, input);
    if (auto valid = validateGeneratedRack(result.tone); valid.failed()) { result.error = valid.getErrorMessage(); result.tone = juce::var(); return result; }
    if (auto balanced = balanceMatchedTone(result.tone, cancel); balanced.failed())
    { result.error = balanced.getErrorMessage(); result.tone = juce::var(); return result; }
    if (cancelled()) { result.tone = juce::var(); return result; }
    // Review the sound rendered by this processor, not just its numeric settings.
    // A failed optional review retains the valid first pass; cancellation discards it.
    if (referenceAudio && includeAudioExamples)
    {
        update("Auditioning generated tone", .85);
        Preset auditionPreset;
        if (decodeGeneratedTone(result.tone, auditionPreset).wasOk())
        {
            auditionPreset.rack.values[3] = -24;
            auto rendered = render(auditionPreset.rack, .25f, 8, cancel);
            if (cancelled()) { result.tone = juce::var(); return result; }
            if (rendered.error.isEmpty() && rendered.rms > 1.e-8)
            {
                rendered.samples.applyGain(static_cast<float>(juce::jmin(.07 / rendered.rms, .8 / juce::jmax(.00001f, rendered.peak))));
                const auto candidate = encodeToneExample(rendered.samples, 48000, cancel);
                if (cancelled()) { result.tone = juce::var(); return result; }
                if (candidate.error.isEmpty())
                {
                    auto reviewConfig = config; reviewConfig.model = result.modelUsed;
                    update("Comparing tone with reference", .9);
                    const auto response = http(makeToneAnalysisRequest(reviewConfig, input, result.video, referencePart, anchors,
                        true, analysis.plan, candidate.base64), cancel);
                    if (cancelled()) { result.tone = juce::var(); return result; }
                    const auto reviewed = decodeToneAnalysis(response, anchors);
                    if (reviewed.error.isEmpty() && reviewed.plan.isObject())
                    {
                        auto revised = compileTonePlan(reviewed.plan, anchors, input);
                        if (validateGeneratedRack(revised).wasOk())
                        {
                            if (auto balanced = balanceMatchedTone(revised, cancel); balanced.failed())
                            { result.error = balanced.getErrorMessage(); result.tone = juce::var(); return result; }
                            result.tone = std::move(revised); result.analysis = reviewed.plan;
                        }
                    }
                    else
                    {
                        result.warning += (result.warning.isEmpty() ? "" : "\n") + juce::String("Audition review unavailable; retained the initial audio match.");
                    }
                }
            }
        }
    }
    if (cancelled()) { result.tone = juce::var(); return result; }
    result.calibrated = true; result.preserveMaster = input.preserveMaster;
    describe();
    update("Tone ready", 1);
    return result;
}
}
