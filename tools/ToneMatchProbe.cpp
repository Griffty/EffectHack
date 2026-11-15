#include "../ToneMatching.h"
#include "../GeneratedTonePreparation.h"
#include <iostream>

int main(int argc, char** argv)
{
    using namespace effecthack;
    juce::MessageManager::getInstance();
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    int code = 0;
    const auto finish = [&] { juce::DeletedAtShutdown::deleteAll(); juce::MessageManager::deleteInstance(); return code; };
    if (argc == 3 && juce::String(argv[1]) == "--examples")
    {
        const juce::File folder(argv[2]);
        if (folder.createDirectory().failed()) { code = 1; return finish(); }
        for (const auto& anchor : makeToneAnchors(true, cancelled))
        {
            juce::MemoryOutputStream decoded;
            if (!juce::Base64::convertFromBase64(decoded, anchor.audioBase64)
                || !folder.getChildFile(anchor.id + ".wav").replaceWithData(decoded.getData(), decoded.getDataSize()))
            { code = 1; break; }
            std::cout << anchor.id << "\n";
        }
        return finish();
    }
    if (argc < 5)
    {
        std::cerr << "Usage: EffectHackToneProbe youtube|file|name reference output.json rhythm|lead|intro [from] [to]\n"
                     "       EffectHackToneProbe --examples absolute-directory\n";
        code = 2; return finish();
    }
    ToneMatchInput input;
    const juce::String source(argv[1]);
    input.source = source == "file" ? ToneSource::audioFile : source == "name" ? ToneSource::songName : ToneSource::youtube;
    input.query = argv[2];
    if (input.source == ToneSource::audioFile) input.file = juce::File(argv[2]);
    const juce::String part(argv[4]);
    input.part = part == "lead" ? GuitarPart::lead : part == "intro" ? GuitarPart::intro : GuitarPart::rhythm;
    if ((argc > 5 && !parseReferenceTime(argv[5], input.startSeconds)) || (argc > 6 && !parseReferenceTime(argv[6], input.endSeconds)))
    { std::cerr << "Invalid section time\n"; code = 2; return finish(); }
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto result = matchSongTone(input, loadApiConfig(), performHttp, cancelled, [](const juce::String& stage, double)
    { std::cout << stage << std::endl; });
    if (result.error.isNotEmpty()) { std::cerr << result.error << "\n"; code = 1; return finish(); }
    const auto prepared = prepareGeneratedTone(result.tone, cancelled.get(), input.preserveMaster);
    if (prepared.prepared.error.isNotEmpty()) { std::cerr << prepared.prepared.error << "\n"; code = 1; return finish(); }
    auto* record = new juce::DynamicObject;
    record->setProperty("analysis", result.analysis); record->setProperty("tone", result.tone);
    record->setProperty("prepared_preset", encodePreset(prepared.preset));
    record->setProperty("source", result.sourceLabel); record->setProperty("model", result.modelUsed);
    record->setProperty("details", result.details); record->setProperty("warning", result.warning);
    record->setProperty("headroom_amp_adjustment_db", prepared.ampAdjustmentDb);
    if (!juce::File(argv[3]).replaceWithText(juce::JSON::toString(juce::var(record)))) code = 1;
    std::cout << result.analysis["base_preset"].toString() << " / " << result.analysis["name"].toString()
        << " / " << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000, 1) << " s\n";
    return finish();
}
