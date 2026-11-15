#pragma once
#include "../FactoryPresets.h"
#include <cmath>
#include <stdexcept>

struct FactoryMeasurement { double rms = 0; float peak = 0; double width = 0; bool clipped = false; };
inline FactoryMeasurement measureFactoryPreset(const effecthack::Preset& preset, float inputScale = 1)
{
    effecthack::AudioEngine engine;
    auto clip = effecthack::makeTestRiff(); clip->samples.applyGain(inputScale);
    engine.testInput.setClip(clip); engine.testInput.setLooping(false);
    const auto result = effecthack::applyPreset(engine, effecthack::preparePreset(preset.rack));
    if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
    engine.prepare(48000, 256); engine.monitoring = true;
    juce::AudioBuffer<float> out(2, 256);
    FactoryMeasurement measurement; double energy = 0; int samples = 0;
    for (int frame = 0; frame < 480000; frame += 256)
    {
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, out.getArrayOfWritePointers(), 2, 256, {});
        for (int i = 0; i < 256; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto value = out.getSample(ch, i);
                if (!std::isfinite(value)) throw std::runtime_error("Factory preset produced nonfinite audio");
                measurement.peak = juce::jmax(measurement.peak, std::abs(value));
                if (frame + i < 384000) { energy += value * value; ++samples; }
            }
            measurement.width += std::abs(out.getSample(0, i) - out.getSample(1, i));
        }
    }
    measurement.rms = std::sqrt(energy / samples);
    measurement.clipped = engine.outputClipped.load();
    return measurement;
}
