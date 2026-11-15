#include "../PresetManager.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace effecthack;
namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void ok(const juce::Result& result)
{
    if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
}
void render(AudioEngine& engine, juce::AudioBuffer<float>& input, juce::AudioBuffer<float>& output)
{
    engine.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 1,
        output.getArrayOfWritePointers(), output.getNumChannels(), 128, {});
}
struct TracingStage : RackStage
{
    TracingStage(int index, std::array<int, rackStageIds.size()>& calls, int& next) : number(index), trace(calls), cursor(next) {}
    const char* id() const noexcept override { return rackStageIds[static_cast<size_t>(number)]; }
    const char* name() const noexcept override { return id(); }
    void prepare(double, int) override {}
    void reset() noexcept override {}
    void process(juce::AudioBuffer<float>&, int) noexcept override { trace[static_cast<size_t>(cursor++)] = number; }
    int number;
    std::array<int, rackStageIds.size()>& trace;
    int& cursor;
};
template<class Processor, class Parameters>
void isolatedChannels(Parameters& parameters, double rate)
{
    for (bool bypass : { false, true })
        for (int active = 0; active < 2; ++active)
        {
            parameters.bypass = bypass;
            ChannelWiseRackStage<Processor> stage("test", "Test", parameters);
            stage.prepare(rate, 128);
            juce::AudioBuffer<float> audio(2, 128);
            double energy = 0;
            for (int block = 0; block < 20; ++block)
            {
                audio.clear();
                if (block == 0) audio.setSample(active, 32, .2f);
                stage.process(audio, 128);
                require(audio.getMagnitude(1 - active, 0, 128) == 0, "Channel histories must not leak into the other channel");
                for (int i = 0; i < 128; ++i) energy += std::abs(audio.getSample(active, i));
            }
            require(energy > .0001, "The active channel must be processed rather than discarded");
        }
}
double chorusWidth(bool first, double rate)
{
    AudioEngine engine;
    engine.inputSource = AudioEngine::InputSource::hardware;
    engine.parameters.bypass = true; engine.cabinetParameters.bypass = true; engine.eqParameters.bypass = true;
    engine.chorusParameters.bypass = false; engine.chorusParameters.mix = 1; engine.chorusParameters.depth = 1;
    engine.parameters.outputDb = 0;
    engine.prepare(rate, 128);
    if (first) require(engine.moveStage("chorus", 0), "Could not put chorus first");
    engine.monitoring = true;
    juce::AudioBuffer<float> input(1, 128), output(2, 128);
    double width = 0;
    for (int block = 0; block < 180; ++block)
    {
        for (int i = 0; i < 128; ++i)
            input.setSample(0, i, .1f * static_cast<float>(std::sin(2 * std::numbers::pi * 440 * (block * 128 + i) / rate)));
        render(engine, input, output);
        if (block > 40)
            for (int i = 0; i < 128; ++i) width += std::abs(output.getSample(0, i) - output.getSample(1, i));
    }
    return width;
}
double distortedEqLevel(bool eqFirst)
{
    AudioEngine engine;
    engine.inputSource = AudioEngine::InputSource::hardware;
    engine.cabinetParameters.bypass = true;
    engine.eqParameters.midDb = 12;
    engine.prepare(48000, 128);
    if (eqFirst) require(engine.moveStage("eq", 0), "Could not move EQ before distortion");
    engine.monitoring = true;
    juce::AudioBuffer<float> input(1, 128), output(2, 128);
    double energy = 0;
    for (int block = 0; block < 180; ++block)
    {
        for (int i = 0; i < 128; ++i)
            input.setSample(0, i, .05f * static_cast<float>(std::sin(2 * std::numbers::pi * 750 * (block * 128 + i) / 48000)));
        render(engine, input, output);
        if (block >= 80)
            for (int i = 0; i < 128; ++i) energy += std::pow(output.getSample(0, i), 2);
    }
    return std::sqrt(energy / (100 * 128));
}

void monoOutputFoldDown()
{
    AudioEngine stereo, mono;
    for (auto* engine : { &stereo, &mono })
    {
        engine->inputSource = AudioEngine::InputSource::hardware;
        engine->parameters.bypass = true; engine->cabinetParameters.bypass = true; engine->eqParameters.bypass = true;
        engine->chorusParameters.bypass = false; engine->chorusParameters.mix = .8f;
        engine->reverbParameters.bypass = false;
        engine->prepare(48000, 128); engine->moveStage("chorus", 0); engine->monitoring = true;
    }
    juce::AudioBuffer<float> input(1, 128), stereoOut(2, 128), monoOut(1, 128);
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 128; ++i)
            input.setSample(0, i, .05f * static_cast<float>(std::sin(2 * std::numbers::pi * 440 * (block * 128 + i) / 48000)));
        render(stereo, input, stereoOut); render(mono, input, monoOut);
        for (int i = 0; i < 128; ++i)
            require(std::abs(monoOut.getSample(0, i) - .5f * (stereoOut.getSample(0, i) + stereoOut.getSample(1, i))) < .000001f,
                "Only the final mono hardware output may fold stereo down");
    }
}

float storedEcho(bool distortionLast)
{
    AudioEngine engine;
    engine.inputSource = AudioEngine::InputSource::hardware;
    engine.parameters.bypass = true; engine.cabinetParameters.bypass = true; engine.eqParameters.bypass = true;
    engine.delayParameters.bypass = false; engine.delayParameters.timeMs = 100;
    engine.delayParameters.feedback = .5f; engine.delayParameters.mix = 1;
    engine.parameters.outputDb = 0;
    engine.prepare(48000, 128);
    if (distortionLast) require(engine.moveStage("distortion", static_cast<int>(rackStageIds.size() - 1)), "Could not put distortion last");
    engine.monitoring = true;
    juce::AudioBuffer<float> input(1, 128), output(2, 128); input.clear();
    for (int i = 0; i < 40; ++i) render(engine, input, output);
    input.setSample(0, 0, .1f); render(engine, input, output); input.clear();
    for (int i = 0; i < 10; ++i) render(engine, input, output);
    engine.parameters.inputDb = 20;
    float peak = 0;
    for (int i = 0; i < 80; ++i)
    {
        render(engine, input, output); peak = juce::jmax(peak, output.getMagnitude(0, 128));
    }
    return peak;
}
}
void testRackOrderAndStereo(const juce::File& scratch)
{
    // Verify actual execution order, not just the reported stage list, for all permutations.
    std::array<int, rackStageIds.size()> trace {}; int cursor = 0;
    TracingStage a(0, trace, cursor), b(1, trace, cursor), c(2, trace, cursor),
        d(3, trace, cursor), e(4, trace, cursor), f(5, trace, cursor), g(6, trace, cursor), h(7, trace, cursor), j(8, trace, cursor);
    RackStageChain chain;
    for (auto* stage : { &a, &b, &c, &d, &e, &f, &g, &h, &j }) chain.add(*stage);
    juce::AudioBuffer<float> audio(2, 128); audio.clear();
    auto order = defaultRackOrder(); std::sort(order.begin(), order.end());
    int count = 0;
    do
    {
        require(chain.setOrder(order), "Valid permutation rejected");
        cursor = 0; chain.process(audio, 128);
        require(cursor == static_cast<int>(rackStageIds.size()) && chain.order() == order, "Every stage must execute exactly once");
        for (size_t i = 0; i < order.size(); ++i)
            require(order[i] == rackStageIds[static_cast<size_t>(trace[i])], "Processing order differs from requested order");
        ++count;
    } while (std::next_permutation(order.begin(), order.end()));
    require(count == 362880, "All nine-stage permutations must be checked");
    require(chain.setOrder(defaultRackOrder()), "Default order rejected");
    for (int source = 0; source < static_cast<int>(rackStageIds.size()); ++source)
        for (int target = 0; target < static_cast<int>(rackStageIds.size()); ++target)
        {
            chain.setOrder(defaultRackOrder());
            require(chain.move(rackStageIds[static_cast<size_t>(source)], target), "Valid move rejected");
            cursor = 0; chain.process(audio, 128);
            require(trace[static_cast<size_t>(target)] == source, "Move executed at the wrong destination");
        }
    const auto before = chain.order();
    auto invalid = before; invalid[0] = invalid[1];
    require(!chain.setOrder(invalid) && chain.order() == before, "Duplicate stages must not partially reorder the chain");
    invalid = before; invalid[0] = "unknown";
    require(!chain.setOrder(invalid) && chain.order() == before, "Unknown stage must leave order unchanged");
    require(!chain.move("unknown", 0) && !chain.move("eq", -1) && !chain.move("eq", static_cast<int>(rackStageIds.size())), "Invalid moves must be rejected");

    PresetManager manager(scratch.getChildFile("ordered"));
    AudioEngine engine; engine.prepare(48000, 128);
    const auto factory = captureRack(engine);
    require(engine.moveStage("delay", 0), "Could not reorder engine");
    const auto moved = captureRack(engine);
    require(!moved.matches(factory), "Moving a stage must mark the tone dirty");
    Preset saved; ok(manager.saveAs("Delay first", moved, saved));
    Preset loaded; ok(PresetManager(manager.directory()).read(saved.id, loaded));
    require(loaded.rack.matches(moved), "Preset order must survive JSON and disk round trips");
    AudioEngine reopened; reopened.prepare(48000, 128);
    ok(applyPreset(reopened, preparePreset(loaded.rack)));
    require(captureRack(reopened).matches(moved), "A fresh engine must restore the saved order");
    Preset renamed; ok(manager.rename(saved, "Ordered lead", renamed));
    require(renamed.rack.order == moved.order, "Rename must preserve order");
    ok(applyPreset(reopened, preparePreset(factory)));
    require(reopened.stageChain().order() == defaultRackOrder(), "Default preset must restore the default order");
    const auto clean = captureRack(reopened);
    auto malformed = factory; malformed.order[0] = malformed.order[1];
    require(applyPreset(reopened, preparePreset(malformed)).failed() && captureRack(reopened).matches(clean),
        "Invalid preset order must not partially apply settings");
    auto json = encodePreset(saved);
    require(static_cast<int>(json["version"]) == 4, "New presets must use schema version 4");
    json.getDynamicObject()->setProperty("version", 1);
    json.getDynamicObject()->removeProperty("order");
    ok(decodePreset(json, loaded));
    require(loaded.rack.order == defaultRackOrder(), "Version 1 presets must migrate to the original default order");
    // A version 2 preset contains only the original six stages and must keep their order.
    json = encodePreset(saved);
    json.getDynamicObject()->setProperty("version", 2);
    juce::Array<juce::var> legacy { "reverb", "eq", "distortion", "delay", "chorus", "cabinet" };
    json.getDynamicObject()->setProperty("order", legacy);
    json["bypass"].getDynamicObject()->removeProperty("amp");
    json.getDynamicObject()->removeProperty("ampVoice");
    ok(decodePreset(json, loaded));
    const RackOrder migrated { "gate", "compressor", "reverb", "eq", "distortion", "amp", "delay", "chorus", "cabinet" };
    require(loaded.rack.order == migrated && loaded.rack.bypass[3]
        && loaded.rack.values[16] == 12 && loaded.rack.values[21] == -6,
        "Legacy presets must insert a bypassed default amp without disturbing existing relative order");
    json.getDynamicObject()->setProperty("order", juce::Array<juce::var> { "distortion", "amp", "eq", "chorus", "delay", "reverb" });
    require(decodePreset(json, loaded).failed(), "Version 2 must reject invalid legacy IDs");
    json = encodePreset(saved);
    json.getDynamicObject()->setProperty("version", 3);
    json.getDynamicObject()->setProperty("order", juce::Array<juce::var> { "amp", "distortion", "eq", "cabinet", "chorus", "reverb", "delay" });
    ok(decodePreset(json, loaded));
    const RackOrder migratedAmp { "gate", "compressor", "amp", "distortion", "eq", "cabinet", "chorus", "reverb", "delay" };
    require(loaded.rack.order == migratedAmp && loaded.rack.bypass[0] && loaded.rack.bypass[1]
        && loaded.rack.values[22] == -50 && loaded.rack.values[27] == 4,
        "Version 3 presets must preserve their seven stages and add bypassed dynamics with defaults");
    json = encodePreset(saved); json.getDynamicObject()->removeProperty("order");
    require(decodePreset(json, loaded).failed(), "Version 4 must require an order");
    json = encodePreset(saved); json.getDynamicObject()->setProperty("ampVoice", "unknown");
    require(decodePreset(json, loaded).failed(), "Unknown amp voice must be rejected");
    json = encodePreset(saved); json.getDynamicObject()->removeProperty("ampVoice");
    require(decodePreset(json, loaded).failed(), "Version 4 must require the amp voice");
    json = encodePreset(saved); json["parameters"].getDynamicObject()->setProperty("ampGainDb", 1000);
    require(decodePreset(json, loaded).failed(), "Unsafe amp gain must be rejected");
    json = encodePreset(saved); json["bypass"].getDynamicObject()->removeProperty("amp");
    require(decodePreset(json, loaded).failed(), "Version 4 must require the amp bypass flag");
    json = encodePreset(saved); json["parameters"].getDynamicObject()->setProperty("compressorRatio", 0);
    require(decodePreset(json, loaded).failed(), "Invalid compressor ratio must be rejected");
    json = encodePreset(saved); json["parameters"].getDynamicObject()->setProperty("gateAttackMs", -1);
    require(decodePreset(json, loaded).failed(), "Invalid gate timing must be rejected");
    json = encodePreset(saved); json["bypass"].getDynamicObject()->removeProperty("gate");
    require(decodePreset(json, loaded).failed(), "Version 4 must require gate bypass");
    for (const auto& ids : { juce::Array<juce::var> { "delay" },
        juce::Array<juce::var> { "gate", "compressor", "delay", "delay", "amp", "eq", "chorus", "reverb", "cabinet" },
        juce::Array<juce::var> { "gate", "compressor", "distortion", "amp", "cabinet", "eq", "chorus", "delay", "unknown" },
        juce::Array<juce::var> { "gate", "compressor", "distortion", "amp", "cabinet", "eq", "chorus", "delay", 42 } })
    {
        json = encodePreset(saved); json.getDynamicObject()->setProperty("order", ids);
        require(decodePreset(json, loaded).failed(), "Malformed order JSON must be rejected");
    }
    for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0 })
    {
        AmpParameters amp; DistortionParameters distortion; CabinetParameters cabinet; EqualizerParameters eq;
        eq.midDb = 6;
        isolatedChannels<Amp>(amp, rate);
        isolatedChannels<Distortion>(distortion, rate);
        isolatedChannels<Cabinet>(cabinet, rate);
        isolatedChannels<Equalizer>(eq, rate);
        const auto normalWidth = chorusWidth(false, rate), movedWidth = chorusWidth(true, rate);
        require(normalWidth > 1 && movedWidth > normalWidth * .8, "Reordering chorus before mono DSP cores must preserve stereo width");
    }
    require(distortedEqLevel(false) > distortedEqLevel(true) * 1.15,
        "Moving EQ across distortion must change the actual rendered audio, not just the displayed order");
    monoOutputFoldDown();
    require(std::abs(storedEcho(false) - .1f) < .001f && std::abs(storedEcho(true) - .1f) < .001f,
        "Master input gain must stay before the rack and not boost existing echoes");
    AudioEngine live;
    live.inputSource = AudioEngine::InputSource::hardware;
    live.cabinetParameters.bypass = true; live.eqParameters.bypass = true; live.parameters.outputDb = 0;
    live.prepare(48000, 128); live.monitoring = true;
    juce::AudioBuffer<float> input(1, 128), output(2, 128);
    std::fill_n(input.getWritePointer(0), 128, .125f);
    for (int i = 0; i < 40; ++i) render(live, input, output);
    const auto original = output.getSample(0, 127);
    auto previous = original; float maximumStep = 0;
    require(live.moveStage("eq", 0), "Live reorder rejected");
    for (int block = 0; block < 80; ++block)
    {
        render(live, input, output);
        for (int i = 0; i < 128; ++i)
        {
            maximumStep = juce::jmax(maximumStep, std::abs(previous - output.getSample(0, i)));
            previous = output.getSample(0, i);
        }
    }
    require(maximumStep < .02f && live.monitoring, "Live order changes must fade across the reset without pausing playback");
    require(std::abs(previous - original) < .001f, "The live reorder must finish fading back to the expected output");
    live.moveStage("eq", 0); render(live, input, output);
    require(std::abs(output.getSample(0, 0) - original) < .001f, "A no-op move must not reset or fade audio");
    std::cout << "362880 execution orders, preset migration/order persistence, stereo isolation, master gain and live reorder fades passed\n";
}
