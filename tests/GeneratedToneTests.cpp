#include "../GeneratedTonePreparation.h"
#include "../FactoryPresets.h"
#include "../SessionState.h"
#include <cmath>
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
juce::var toneFrom(const effecthack::Preset& preset)
{
    const auto encoded = effecthack::encodePreset(preset);
    auto* object = new juce::DynamicObject;
    for (const auto* key : { "name", "parameters", "bypass", "ampVoice", "order" }) object->setProperty(key, encoded[key]);
    object->setProperty("cabinet", "builtin");
    return juce::JSON::parse(juce::JSON::toString(juce::var(object)));
}
}
void testGeneratedTone(const juce::File&)
{
    using namespace effecthack;
    auto tone = toneFrom(factoryPresets()[1].preset);
    tone.getDynamicObject()->setProperty("name", "Generated rhythm");
    tone["parameters"].getDynamicObject()->setProperty("inputDb", 12);
    tone["parameters"].getDynamicObject()->setProperty("outputDb", 0);
    tone["parameters"].getDynamicObject()->setProperty("ampLevelDb", 6);
    tone["parameters"].getDynamicObject()->setProperty("bassDb", 12);
    auto* order = tone.getDynamicObject()->getProperty("order").getArray();
    order->swap(0, 1);
    Preset decoded;
    const auto boundary = toneFrom(factoryPresets()[0].preset);
    auto minimum = juce::JSON::parse(juce::JSON::toString(boundary));
    minimum["parameters"].getDynamicObject()->setProperty("gateAttackMs", .1);
    minimum["parameters"].getDynamicObject()->setProperty("compressorAttackMs", .1);
    require(decodeGeneratedTone(minimum, decoded).wasOk(), "Decimal minimums must validate at the float precision used by DSP controls");
    require(decodeGeneratedTone(tone, decoded).wasOk() && decoded.id.isEmpty() && decoded.name == "Generated rhythm"
        && decoded.rack.values[0] == 12 && decoded.rack.values[3] == 0 && decoded.rack.order[0] == "compressor",
        "All generated controls, Master gains, name, and order must decode into an unsaved tone");
    const auto prepared = prepareGeneratedTone(tone);
    require(prepared.prepared.error.isEmpty() && prepared.outputAdjustmentDb < 0 && prepared.preset.rack.values[3] < 0,
        "A hot generated rack must receive deterministic output headroom attenuation");
    require(prepared.prepared.rack.matches(prepared.preset.rack) && prepared.preset.rack.values[0] == 12
        && prepared.preset.rack.order == decoded.rack.order, "Headroom check must preserve Input, drive, and order");
    const std::atomic<bool> cancelled { true };
    require(prepareGeneratedTone(tone, &cancelled).prepared.error == "Cancelled.", "Headroom preview must honor cancellation");
    AudioEngine engine;
    engine.inputSource = AudioEngine::InputSource::hardware; engine.rackBypass = true;
    require(applyPreset(engine, preparePreset(factoryPresets()[0].preset.rack)).wasOk(), "Could not initialize transition fixture");
    engine.prepare(48000, 128); engine.monitoring = true;
    juce::AudioBuffer<float> input(1, 128), output(2, 128);
    std::fill_n(input.getWritePointer(0), 128, .1f);
    auto render = [&](int blocks)
    {
        while (blocks-- > 0) engine.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 1,
            output.getArrayOfWritePointers(), 2, 128, {});
    };
    render(30); const auto before = captureRack(engine);
    engine.beginRackUpdate();
    require(!engine.rackUpdateIsSilent(), "Audible rack updates must wait for their fade");
    render(30);
    require(engine.rackUpdateIsSilent() && output.getMagnitude(0, 128) < .000001f && captureRack(engine).matches(before),
        "Fade-down must reach silence without changing any old controls");
    require(applyPreset(engine, prepared.prepared).wasOk(), "Prepared rack must commit at silence");
    engine.endRackUpdate(); render(1);
    require(std::abs(output.getSample(0, 0)) < .001f && captureRack(engine).matches(prepared.preset.rack),
        "New parameters/order must become audible together through fade-up");
    render(30);
    engine.beginRackUpdate(); render(30); engine.endRackUpdate(); render(30);
    require(captureRack(engine).matches(prepared.preset.rack), "Cancelling a faded update must retain the current rack");
    engine.beginRackUpdate(); engine.audioDeviceStopped();
    require(engine.rackUpdateIsSilent(), "Disconnected devices must not leave a pending commit waiting forever");
    engine.endRackUpdate();
    SessionState state; state.rack = prepared.preset.rack; state.generatedPreset = prepared.preset;
    state.rack.values[0] = -4; // Preserve subsequent edits separately from generated baseline.
    SessionState restored;
    require(decodeSession(encodeSession(state), restored).wasOk() && restored.generatedPreset.has_value()
        && restored.generatedPreset->name == prepared.preset.name && restored.generatedPreset->rack.matches(prepared.preset.rack)
        && restored.rack.values[0] == -4, "Generated identity/baseline and later edits must survive startup snapshots");
}
