#include "../GeminiRecommender.h"
#include <iostream>
#include <stdexcept>

using namespace effecthack;
namespace
{
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
HttpResponse choiceResponse(const juce::String& choice, const juce::String& finish = "STOP")
{
    auto root = juce::JSON::parse(R"({"candidates":[{"finishReason":"STOP","content":{"parts":[{"text":""}]}}]})");
    auto candidate = root["candidates"][0];
    candidate.getDynamicObject()->setProperty("finishReason", finish);
    candidate["content"]["parts"][0].getDynamicObject()->setProperty("text", choice);
    return { 200, juce::JSON::toString(root), {} };
}
}
int main()
{
    try
    {
        for (const auto* link : { "https://www.youtube.com/watch?v=CD-E-LDc384&t=35", "https://youtu.be/CD-E-LDc384?si=share",
            "https://m.youtube.com/watch?v=CD-E-LDc384", "https://music.youtube.com/watch?v=CD-E-LDc384&list=abc",
            "https://youtube.com/shorts/CD-E-LDc384", "https://youtube.com/embed/CD-E-LDc384", "https://youtube.com/live/CD-E-LDc384",
            "youtu.be/CD-E-LDc384", "http://youtube.com/watch?v=CD-E-LDc384", "https://www.youtube-nocookie.com/embed/CD-E-LDc384" })
        {
            const auto video = parseYouTubeLink(link);
            require(video.error.isEmpty() && video.id == "CD-E-LDc384" && video.url == "https://www.youtube.com/watch?v=CD-E-LDc384",
                "Supported YouTube formats must normalize to one canonical video URL");
        }
        for (const auto* link : { "", "https://spotify.com/track/abc", "https://youtube.com.evil.test/watch?v=CD-E-LDc384",
            "https://evil.test/youtube.com/watch?v=CD-E-LDc384", "https://youtube.com@evil.test/watch?v=CD-E-LDc384",
            "https://evil@youtube.com/watch?v=CD-E-LDc384", "file:///tmp/video", "https://localhost/watch?v=CD-E-LDc384",
            "https://youtube.com/playlist?list=abc", "https://youtu.be/short", "https://youtu.be/CD-E-LDc384/extra",
            "https://youtube.com/watch?v=CD-E-LDc384&v=CD-E-LDc384", "https://youtube.com:999/watch?v=CD-E-LDc384" })
            require(parseYouTubeLink(link).error.isNotEmpty(), "Unsupported/malformed URLs must be rejected without network access");
        auto config = parseApiConfig("# comment\nexport GEMINI_API_KEY='local-key'\nGEMINI_MODEL=gemini-2.5-flash # note\nGEMINI_API_NAME=display-name\n");
        require(config.error.isEmpty() && config.key == "local-key" && config.model == "gemini-2.5-flash", "dotenv quoting/export/comments must load correctly");
        config = parseApiConfig("GEMINI_API_KEY=local-key", "environment-key", "models/gemini-2.5-flash-lite");
        require(config.error.isEmpty() && config.key == "environment-key" && config.model == "gemini-2.5-flash-lite", "Environment must override dotenv and normalize model IDs");
        require(parseApiConfig({}).error.isNotEmpty(), "Missing key must be actionable");
        require(parseApiConfig("GEMINI_API_KEY=local-key").model == "gemini-3.5-flash-lite", "Default must use the live-verified model");
        require(parseApiConfig("GEMINI_API_KEY=\"unterminated").error.isNotEmpty(), "Malformed quotes must be reported without echoing secrets");
        require(parseApiConfig("GEMINI_API_KEY=local-key\nGEMINI_MODEL=https://evil.test").error.isNotEmpty(), "Models must not turn into arbitrary endpoints");
        const std::vector<SelectionPreset> catalog { { "factory.clean", "Clean", "Clean guitar with light room", {} },
            { "factory.metal-rhythm", "Metal Rhythm", "Tight dry high gain rhythm", {} } };
        const auto token = std::make_shared<std::atomic<bool>>(false);
        int calls = 0;
        HttpTransport fake = [&](const HttpRequest& request, const Cancellation&)
        {
            ++calls;
            if (calls == 1)
            {
                require(request.url.startsWith("https://www.youtube.com/oembed?") && request.headers.isEmpty()
                    && !request.url.contains(config.key), "YouTube must receive only canonical metadata lookup, without API credentials");
                return HttpResponse { 200, R"json({"title":"Metallica: Enter Sandman (Official Music Video)","author_name":"Metallica"})json", {} };
            }
            require(request.url.startsWith("https://generativelanguage.googleapis.com/v1beta/models/")
                && !request.url.contains(config.key) && request.headers.contains("x-goog-api-key: " + config.key), "Gemini authentication must use the intended host and header");
            const auto body = juce::JSON::parse(request.body);
            const auto ids = body["generationConfig"]["responseSchema"]["properties"]["preset_id"]["enum"];
            require(ids.isArray() && ids.size() == 3 && ids[0].toString() == "factory.clean" && ids[2].toString() == "unknown",
                "Structured output must enumerate only catalog IDs and abstention");
            require(!request.body.contains(config.key) && request.body.contains("Enter Sandman") && request.body.contains("available_presets"),
                "Prompt must include video/catalog data but never credentials");
            return choiceResponse(R"({"preset_id":"factory.metal-rhythm","needs_clarification":false})");
        };
        const auto selected = recommendYouTube("https://youtu.be/CD-E-LDc384", config, catalog, fake, token);
        require(selected.error.isEmpty() && selected.presetId == "factory.metal-rhythm" && calls == 2, "Metadata-to-selection pipeline must return a validated catalog entry");
        for (const auto* choice : { R"({"preset_id":"invented","needs_clarification":false})",
            R"({"preset_id":"factory.clean","needs_clarification":"false"})", R"({"preset_id":"factory.clean","needs_clarification":false,"parameters":{}})",
            R"({"preset_id":"factory.clean","needs_clarification":true})", "not-json" })
            require(decodeRecommendation(choiceResponse(choice), selected.video, catalog).error.isNotEmpty(), "Malformed, contradictory, or expanded model output must never be applied");
        const auto uncertain = decodeRecommendation(choiceResponse(R"({"preset_id":"unknown","needs_clarification":true})"), selected.video, catalog);
        require(uncertain.needsClarification && uncertain.presetId.isEmpty() && uncertain.error.isNotEmpty(), "Abstention must preserve the current rack");
        require(decodeRecommendation(choiceResponse(R"({"preset_id":"factory.clean","needs_clarification":false})", "MAX_TOKENS"), selected.video, catalog).error.isNotEmpty(),
            "Truncated candidates must never be accepted");
        for (int status : { 400, 401, 403, 404, 429, 500 })
        {
            const auto failed = decodeRecommendation({ status, "private server details", {} }, selected.video, catalog);
            require(failed.presetId.isEmpty() && failed.error.isNotEmpty() && !failed.error.contains("private server details"), "HTTP errors must be actionable and sanitized");
        }
        calls = 0; token->store(true);
        require(recommendYouTube("https://youtu.be/CD-E-LDc384", config, catalog, fake, token).error.isNotEmpty() && calls == 0,
            "Cancelled requests must not contact services");
        require(performHttp({ "https://www.youtube.com/oembed?format=json", {}, {} }, token).error.isNotEmpty(), "HTTP transport must honor pre-cancellation");
        token->store(false);
        require(performHttp({ "https://evil.test/", {}, {} }, token).error.isNotEmpty(), "HTTP transport must reject arbitrary destinations");
        HttpTransport badMetadata = [](const HttpRequest&, const Cancellation&) { return HttpResponse { 200, "{}", {} }; };
        require(resolveYouTube("https://youtu.be/CD-E-LDc384", badMetadata, token).error.isNotEmpty(), "Invalid YouTube metadata must be rejected before Gemini");
        auto* toneObject = new juce::DynamicObject;
        auto* parameters = new juce::DynamicObject;
        auto* bypasses = new juce::DynamicObject;
        juce::Array<juce::var> order;
        for (const auto& field : rackParameterSpecs) parameters->setProperty(field.key, field.minimum);
        for (const auto* stage : rackStageIds) { order.add(stage); bypasses->setProperty(stage, true); }
        toneObject->setProperty("name", "Song custom"); toneObject->setProperty("parameters", juce::var(parameters));
        toneObject->setProperty("bypass", juce::var(bypasses)); toneObject->setProperty("order", order);
        toneObject->setProperty("ampVoice", "clean"); toneObject->setProperty("cabinet", "builtin");
        const auto toneJson = juce::JSON::toString(juce::var(toneObject));
        require(validateGeneratedRack(juce::JSON::parse(toneJson)).wasOk(), "Complete generated rack fixture must be valid");
        const auto customResponse = choiceResponse("{\"needs_clarification\":false,\"tone\":" + toneJson + "}");
        const auto custom = decodeRecommendation(customResponse, selected.video, catalog, RecommendationMode::customRack);
        require(custom.error.isEmpty() && custom.tone.isObject() && custom.presetId.isEmpty(), "Custom mode must return a complete rack without pretending it is a factory preset");
        const auto generatedRequest = makeRecommendationRequest(config, selected.video, catalog, RecommendationMode::customRack);
        const auto generationSchema = juce::JSON::parse(generatedRequest.body)["generationConfig"]["responseSchema"];
        require(generationSchema["properties"]["tone"]["properties"]["parameters"]["required"].size() == 32
            && generationSchema["properties"]["tone"]["properties"]["order"]["minItems"] == juce::var(9), "Generation schema must cover every parameter and stage");
        require(decodeRecommendation(choiceResponse(R"({"needs_clarification":true,"tone":null})"), selected.video, catalog,
            RecommendationMode::customRack).needsClarification, "Custom generation must support explicit abstention");
        auto bad = juce::JSON::parse(toneJson); bad["parameters"].getDynamicObject()->setProperty("inputDb", 999);
        require(validateGeneratedRack(bad).failed(), "Generated Master gains must be range checked");
        bad = juce::JSON::parse(toneJson); bad["parameters"].getDynamicObject()->removeProperty("delayMix");
        require(validateGeneratedRack(bad).failed(), "Missing controls must not silently inherit defaults");
        bad = juce::JSON::parse(toneJson); bad["order"].getArray()->set(1, "gate");
        require(validateGeneratedRack(bad).failed(), "Duplicate generated stages must be rejected");
        bad = juce::JSON::parse(toneJson); bad["bypass"].getDynamicObject()->setProperty("amp", 1);
        require(validateGeneratedRack(bad).failed(), "Generated bypass types must be exact");
        bad = juce::JSON::parse(toneJson); bad.getDynamicObject()->setProperty("cabinet", "/tmp/arbitrary.wav");
        require(validateGeneratedRack(bad).failed(), "The model cannot invent local IR paths");
        bad = juce::JSON::parse(toneJson); bad.getDynamicObject()->setProperty("ampVoice", "invented amp");
        require(validateGeneratedRack(bad).failed(), "The model cannot invent amp voices");
        bad = juce::JSON::parse(toneJson); bad.getDynamicObject()->setProperty("extra", true);
        require(validateGeneratedRack(bad).failed(), "Unknown generated fields must fail");
        std::cout << "YouTube URL/metadata, dotenv, structured request/response, abstention, cancellation, HTTP errors and credential handling passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
