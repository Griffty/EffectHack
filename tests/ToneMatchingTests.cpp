#include "../ToneMatching.h"
#include "../ReferenceAudio.h"
#include "../GeneratedTonePreparation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace effecthack;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void ok(const juce::Result& result) { if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString()); }
juce::var copy(const juce::var& value) { return juce::JSON::parse(juce::JSON::toString(value)); }
juce::var plan(const juce::String& anchor = "factory.clean")
{
    auto value = juce::JSON::parse(R"({"name":"Reference tone","base_preset":"factory.clean",
        "gain_delta_db":0,"bass_delta_db":0,"mid_delta_db":0,"treble_delta_db":0,"presence_delta_db":0,
        "compression":0,"chorus_mix":0,"delay_mix":0,"delay_time_ms":350,"delay_feedback":0.2,
        "reverb_mix":0,"reverb_size":0.4,"reason":"Audible guitar character.","limitations":"","confidence":"medium"})");
    value.getDynamicObject()->setProperty("base_preset", anchor); return value;
}
HttpResponse response(const juce::var& value)
{
    auto root = juce::JSON::parse(R"({"candidates":[{"finishReason":"STOP","content":{"parts":[{"text":""}]}}]})");
    auto* body = new juce::DynamicObject;
    body->setProperty("needs_clarification", false); body->setProperty("tone", value);
    root["candidates"][0]["content"]["parts"][0].getDynamicObject()->setProperty("text", juce::JSON::toString(juce::var(body)));
    return {200, juce::JSON::toString(root), {}};
}
juce::AudioBuffer<float> readWav(const juce::String& base64, double& rate)
{
    juce::MemoryOutputStream memory;
    require(juce::Base64::convertFromBase64(memory, base64), "Example must be valid base64");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(memory.getData(), memory.getDataSize(), true), true));
    require(reader != nullptr && reader->numChannels == 1 && reader->bitsPerSample == 16, "Reference must be 16-bit mono WAV");
    rate = reader->sampleRate;
    juce::AudioBuffer<float> audio(1, static_cast<int>(reader->lengthInSamples));
    require(reader->read(&audio, 0, audio.getNumSamples(), 0, true, false), "WAV must decode");
    return audio;
}
void writeWav(const juce::File& file)
{
    juce::AudioBuffer<float> audio(2, 48000 * 3);
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const auto amplitude = i < 48000 ? .1f : .4f;
        const auto value = amplitude * std::sin(static_cast<float>(i) * .057595865f);
        audio.setSample(0, i, value); audio.setSample(1, i, value * .5f);
    }
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
    require(writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Write reference fixture");
}
void run(const juce::File& scratch)
{
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    const auto anchors = makeToneAnchors(true, cancel, false);
    require(anchors.size() == 9 && makeToneAnchors(false, cancel, false).size() == 6, "Custom and factory catalogs must remain distinct");
    double time = 0;
    require(parseReferenceTime("", time) && time < 0, "Empty time means Auto");
    require(parseReferenceTime("1:23.5", time) && std::abs(time - 83.5) < .001, "m:ss accepts fractional seconds");
    require(parseReferenceTime("1:02:03", time) && std::abs(time - 3723) < .001, "h:mm:ss accepted");
    for (const auto* value : {"-1", "1:60", "1::02", "1:2:3:4", "NaN", ".", "1.2:3", "2..3", "20000"})
        require(!parseReferenceTime(value, time), "Malformed times rejected");
    ToneMatchInput input;
    input.source = ToneSource::songName; input.query = "Artist - Song";
    input.inputDb = -3; input.outputDb = -18;
    ok(validateToneMatchInput(input));
    auto invalidInput = input; invalidInput.startSeconds = 4;
    require(validateToneMatchInput(invalidInput).failed(), "Name estimates cannot pretend to select audio sections");
    invalidInput = input; invalidInput.source = ToneSource::youtube; invalidInput.query = "https://example.org/song";
    require(validateToneMatchInput(invalidInput).failed(), "Only real YouTube links accepted");
    invalidInput = input; invalidInput.outputDb = std::numeric_limits<float>::quiet_NaN();
    require(validateToneMatchInput(invalidInput).failed(), "Nonfinite master rejected");

    const auto validPlan = plan();
    require(decodeToneAnalysis(response(validPlan), anchors).error.isEmpty(), "Valid bounded plan accepted");
    for (const auto* field : {"gain_delta_db", "compression", "delay_mix", "reverb_size"})
    {
        auto bad = copy(validPlan); bad.getDynamicObject()->setProperty(field, 1000);
        require(decodeToneAnalysis(response(bad), anchors).error.isNotEmpty(), "Out of range plan rejected");
        bad = copy(validPlan); bad.getDynamicObject()->setProperty(field, "0");
        require(decodeToneAnalysis(response(bad), anchors).error.isNotEmpty(), "String numbers rejected");
    }
    for (const auto* field : {"name", "base_preset", "confidence"})
    {
        auto bad = copy(validPlan); bad.getDynamicObject()->setProperty(field, "unsupported\nvalue");
        require(decodeToneAnalysis(response(bad), anchors).error.isNotEmpty(), "Invalid names/IDs/confidence rejected");
    }
    auto bad = copy(validPlan); bad.getDynamicObject()->setProperty("invented", true);
    require(decodeToneAnalysis(response(bad), anchors).error.isNotEmpty(), "Extra output fields rejected");
    bad = copy(validPlan); bad.getDynamicObject()->removeProperty("chorus_mix");
    require(decodeToneAnalysis(response(bad), anchors).error.isNotEmpty(), "Missing output fields rejected");
    auto abstain = response(validPlan);
    auto root = juce::JSON::parse(abstain.body);
    root["candidates"][0]["content"]["parts"][0].getDynamicObject()->setProperty("text", "{\"needs_clarification\":true,\"tone\":null}");
    abstain.body = juce::JSON::toString(root);
    require(decodeToneAnalysis(abstain, anchors).needsClarification, "No guitar can abstain");
    require(decodeToneAnalysis({429, {}, {}}, anchors).error.contains("quota"), "Quota errors are actionable");

    for (const auto& anchor : anchors)
    {
        auto compiled = compileTonePlan(plan(anchor.id), anchors, input);
        ok(validateGeneratedRack(compiled));
        require(std::abs(static_cast<double>(compiled["parameters"]["inputDb"]) + 3) < .001
            && std::abs(static_cast<double>(compiled["parameters"]["outputDb"]) + 18) < .001, "Master controls preserved");
        require(static_cast<bool>(compiled["bypass"]["chorus"]) && static_cast<bool>(compiled["bypass"]["delay"])
            && static_cast<bool>(compiled["bypass"]["reverb"]) && static_cast<bool>(compiled["bypass"]["compressor"]), "Zero mixes bypass optional processing");
        require(juce::JSON::toString(compiled["order"]) == juce::JSON::toString(anchor.rack["order"]), "Known chain order retained");
    }
    auto highGain = plan("factory.metal-rhythm"); highGain.getDynamicObject()->setProperty("compression", .5);
    auto compiled = compileTonePlan(highGain, anchors, input);
    require(static_cast<double>(compiled["parameters"]["compressorMix"]) <= .15
        && static_cast<double>(compiled["parameters"]["compressorMakeupDb"]) == 0, "Distorted tones retain pick attack and avoid makeup boosts");
    auto pedal = plan("match.pedal-drive"); pedal.getDynamicObject()->setProperty("gain_delta_db", 3);
    compiled = compileTonePlan(pedal, anchors, input);
    require(!static_cast<bool>(compiled["bypass"]["distortion"])
        && std::abs(static_cast<double>(compiled["parameters"]["driveDb"]) - 27) < .001
        && std::abs(static_cast<double>(compiled["parameters"]["ampGainDb"]) - 3) < .001, "Pedal tones adjust pedal saturation independently of the amp");
    input.preserveMaster = false;
    compiled = compileTonePlan(validPlan, anchors, input);
    require(static_cast<double>(compiled["parameters"]["inputDb"]) == 0
        && static_cast<double>(compiled["parameters"]["outputDb"]) == -9, "Optional independent default master levels");
    input.preserveMaster = true;

    const auto fixture = scratch.getChildFile("private-reference.wav"); writeWav(fixture);
    const auto section = prepareAudioReference(fixture, 1, 2, cancel);
    require(section.error.isEmpty() && std::abs(section.durationSeconds - 1) < .001, "Requested audio section decoded");
    double rate = 0; const auto audio = readWav(section.base64, rate);
    require(std::abs(rate - 16000) < 1 && audio.getNumSamples() == 16000, "References resampled to 16 kHz");
    require(audio.getRMSLevel(0, 100, 15000) > .20f && audio.getRMSLevel(0, 100, 15000) < .22f,
        "Section offset and stereo downmix preserved instead of reading the intro");
    require(prepareAudioReference(fixture, 4, 5, cancel).error.isNotEmpty(), "Past-end section rejected");
    require(prepareAudioReference(fixture, 1, 1.2, cancel).error.isNotEmpty(), "Tiny section rejected");
    juce::AudioBuffer<float> silence(1, 16000); silence.clear();
    require(encodeToneExample(silence, 16000, cancel).error.isNotEmpty(), "Silence rejected");
    cancel->store(true);
    require(prepareAudioReference(fixture, -1, -1, cancel).error == "Cancelled.", "Decoding is cancellable");
    cancel->store(false);

    ApiConfig config; config.key = "dummy-test-key"; config.model = "gemini-test-unavailable";
    int requests = 0;
    HttpTransport http = [&](const HttpRequest& request, const Cancellation&)
    {
        ++requests;
        require(!request.body.contains(fixture.getFullPathName()) && !request.body.contains(config.key), "No local path or API key in prompt");
        const auto body = juce::JSON::parse(request.body);
        const auto parts = body["contents"][0]["parts"];
        for (const auto& value : *parts.getArray()) require(!value.hasProperty("fileData") && !value.hasProperty("inlineData"), "Name-only estimates do not claim target audio");
        return requests == 1 ? HttpResponse {404, {}, {}} : response(validPlan);
    };
    const auto named = matchSongTone(input, config, http, cancel, {}, false);
    require(named.error.isEmpty() && named.tone.isObject() && named.calibrated && named.preserveMaster
        && named.sourceLabel == "Song estimate" && named.modelUsed == "gemini-3.5-flash-lite" && requests == 2, "Unavailable model fallback and explicit song-estimate label");
    const auto balancedInput = static_cast<double>(named.tone["parameters"]["inputDb"]);
    require(std::abs(balancedInput + 3) < .001 && std::abs(static_cast<double>(named.tone["parameters"]["outputDb"]) + 18) < .001, "Loudness balance does not move Master");
    requests = 0;
    const auto denied = matchSongTone(input, config, [&](const auto&, const auto&) { ++requests; return HttpResponse {403, {}, {}}; }, cancel, {}, false);
    require(denied.error.isNotEmpty() && !denied.tone.isObject() && requests == 1, "Access failures do not silently use another model");

    input.source = ToneSource::youtube; input.query = "https://youtu.be/CD-E-LDc384";
    input.startSeconds = 60; input.endSeconds = 90; input.custom = false;
    requests = 0;
    const auto youtube = matchSongTone(input, config, [&](const HttpRequest& request, const Cancellation&)
    {
        ++requests;
        if (request.url.contains("oembed")) return HttpResponse {200, R"({"title":"Target song","author_name":"Artist"})", {}};
        const auto body = juce::JSON::parse(request.body);
        const auto media = body["contents"][0]["parts"][1];
        require(media["fileData"]["fileUri"].toString() == "https://www.youtube.com/watch?v=CD-E-LDc384"
            && media["videoMetadata"]["startOffset"].toString().startsWith("60"), "YouTube actual media and excerpt sent to model");
        return response(validPlan);
    }, cancel, {}, false);
    require(youtube.error.isEmpty() && youtube.presetId == "factory.clean" && !youtube.tone.isObject()
        && youtube.sourceLabel == "Audio reference" && requests == 2, "Factory mode returns an existing preset unchanged");

    input.source = ToneSource::audioFile; input.file = fixture; input.startSeconds = 1; input.endSeconds = 2;
    const auto local = matchSongTone(input, config, [&](const HttpRequest& request, const Cancellation&)
    {
        const auto body = juce::JSON::parse(request.body);
        require(body["contents"][0]["parts"][1]["inlineData"]["mimeType"].toString() == "audio/wav", "Local reference uploaded as actual audio");
        require(!request.body.contains(fixture.getFullPathName()), "Reference path is kept private");
        return response(validPlan);
    }, cancel, {}, false);
    require(local.error.isEmpty() && local.presetId == "factory.clean", "Local audio works through same selector");

    const auto renderedAnchors = makeToneAnchors(true, cancel);
    require(renderedAnchors.size() == 9, "Actual processor example bank rendered");
    for (const auto& anchor : renderedAnchors)
    {
        const auto decoded = readWav(anchor.audioBase64, rate);
        require(decoded.getNumSamples() == 128000 && decoded.getMagnitude(0, decoded.getNumSamples()) < .81f,
            "Audible examples are eight seconds with headroom");
        require(std::abs(decoded.getRMSLevel(0, 0, decoded.getNumSamples()) - .07f) < .012f, "Examples have comparable listening level");
    }
    input.custom = true;
    requests = 0;
    const auto reviewed = matchSongTone(input, config, [&](const HttpRequest& request, const Cancellation&)
    {
        ++requests;
        require(request.body.contains("PROCESSOR EXAMPLE"), "Model hears real processor examples");
        if (requests == 2)
        {
            require(request.body.contains("CANDIDATE TONE"), "Second pass hears actual generated result");
            return response(plan("factory.crunch"));
        }
        return response(validPlan);
    }, cancel);
    require(reviewed.error.isEmpty() && requests == 2 && reviewed.analysis["base_preset"].toString() == "factory.crunch",
        "Audio audition can refine a validated plan");
    require(reviewed.details.contains("Base: Crunch"), "Explanation describes the final reviewed tone");
    requests = 0;
    const auto retained = matchSongTone(input, config, [&](const auto&, const auto&)
    { return ++requests == 1 ? response(validPlan) : HttpResponse {429, {}, {}}; }, cancel);
    require(retained.error.isEmpty() && retained.tone.isObject() && retained.warning.contains("review unavailable"), "Failed review retains initial usable match");
    const auto cancelled = matchSongTone(input, config, [&](const auto&, const auto& token)
    { token->store(true); return response(validPlan); }, cancel);
    require(cancelled.error == "Cancelled." && !cancelled.tone.isObject(), "Cancellation never applies partial results");
    cancel->store(false);

    auto loud = compileTonePlan(plan("factory.crunch"), anchors, input);
    loud["parameters"].getDynamicObject()->setProperty("ampLevelDb", 6);
    loud["parameters"].getDynamicObject()->setProperty("outputDb", -6);
    const auto prepared = prepareGeneratedTone(loud, cancel.get(), true);
    require(prepared.prepared.error.isEmpty() && prepared.ampAdjustmentDb < 0
        && std::abs(prepared.preset.rack.values[3] + 6) < .001f, "Final headroom check attenuates internally, keeping Output");
    require(scratch.deleteRecursively(), "Remove test scratch files");
}
}
int main(int argc, char** argv)
{
    juce::MessageManager::getInstance(); int result = 0;
    try
    {
        require(argc == 2, "Pass scratch folder");
        const auto scratch = juce::File(argv[1]).getChildFile(juce::Uuid().toString()); ok(scratch.createDirectory());
        run(scratch); std::cout << "Audio matching, bounded compilation, audition, cancellation and headroom checks passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    juce::DeletedAtShutdown::deleteAll(); juce::MessageManager::deleteInstance(); return result;
}
