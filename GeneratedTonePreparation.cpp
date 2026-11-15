#include "GeneratedTonePreparation.h"
#include <cmath>

namespace effecthack
{
juce::Result decodeGeneratedTone(const juce::var& tone, Preset& destination)
{
    if (const auto valid = validateGeneratedRack(tone); valid.failed()) return valid;
    auto json = encodePreset({ juce::Uuid().toString(), tone["name"].toString(), {} });
    for (const auto* field : { "parameters", "bypass", "ampVoice", "order" }) json.getDynamicObject()->setProperty(field, tone[field]);
    Preset decoded;
    if (const auto result = decodePreset(json, decoded); result.failed()) return result;
    decoded.id.clear(); // Unsaved generated identity, not a user preset on disk.
    destination = std::move(decoded);
    return juce::Result::ok();
}
PreparedGeneratedTone prepareGeneratedTone(const juce::var& tone, const std::atomic<bool>* cancelled, bool preserveMaster)
{
    PreparedGeneratedTone result;
    const auto stopped = [cancelled] { return cancelled != nullptr && cancelled->load(); };
    if (auto valid = decodeGeneratedTone(tone, result.preset); valid.failed()) { result.prepared.error = valid.getErrorMessage(); return result; }
    if (stopped()) { result.prepared.error = "Cancelled."; return result; }
    result.prepared = preparePreset(result.preset.rack);
    if (result.prepared.error.isNotEmpty()) return result;
    AudioEngine preview;
    auto riff = makeTestRiff(); riff->samples.applyGain(2.0f);
    preview.testInput.setClip(riff); preview.testInput.setLooping(false);
    if (auto applied = applyPreset(preview, result.prepared); applied.failed()) { result.prepared.error = applied.getErrorMessage(); return result; }
    preview.prepare(48000, 256); preview.monitoring = true;
    juce::AudioBuffer<float> output(2, 256);
    float peak = 0;
    // Eight-second riff plus four seconds of repeats/reverb, without wall-clock waits.
    for (int frame = 0; frame < 48000 * 12; frame += 256)
    {
        if (stopped()) { result.prepared.error = "Cancelled."; return result; }
        preview.audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(), 2, 256, {});
        const auto measured = preview.outputUnclampedPeak.load();
        if (!std::isfinite(measured)) { result.prepared.error = "Generated rack produced invalid audio."; return result; }
        peak = juce::jmax(peak, measured);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                if (!std::isfinite(output.getSample(ch, i))) { result.prepared.error = "Generated rack produced invalid audio."; return result; }
    }
    if (peak > .7f)
    {
        // Output is linear after the complete rack, so one render determines the
        // needed attenuation without altering the generated effect relationships.
        const auto adjustment = juce::Decibels::gainToDecibels(.7f / peak);
        if (preserveMaster)
        {
            // The matching compiler uses amp -> cabinet -> EQ -> spatial effects.
            // All processing after amp Level is linear in this known chain.
            if (result.preset.rack.order != defaultRackOrder() || result.preset.rack.bypass[3])
            { result.prepared.error = "Cannot preserve Master levels for this generated signal chain."; return result; }
            const auto level = result.preset.rack.values[21] + adjustment;
            if (level < -24) { result.prepared.error = "This tone exceeds internal level headroom. Lower Master Output and try again."; return result; }
            result.ampAdjustmentDb = adjustment;
            result.preset.rack.values[21] = level;
            result.prepared.rack = result.preset.rack;
            return result;
        }
        const auto level = result.preset.rack.values[3] + adjustment;
        if (level < -60) { result.prepared.error = "Generated output exceeds available headroom. Generate another tone."; return result; }
        result.outputAdjustmentDb = adjustment;
        result.preset.rack.values[3] = level;
        result.prepared.rack = result.preset.rack;
    }
    return result;
}
}
