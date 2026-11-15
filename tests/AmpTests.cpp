#include "../AudioEngine.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace effecthack;
namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct Analysis { double rms, distortion; };
Analysis analyse(Amp& amp, double rate, float amplitude, double frequency = 750)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioBuffer<float> block(1, 128);
    double energy = 0, sine = 0, cosine = 0, mean = 0;
    int frames = 0;
    for (int b = 0; b < 160; ++b)
    {
        for (int i = 0; i < 128; ++i)
            block.setSample(0, i, amplitude * static_cast<float>(std::sin(2 * std::numbers::pi * frequency * (b * 128 + i) / rate)));
        amp.process(block, 128);
        for (int i = 0; i < 128; ++i)
        {
            const auto value = block.getSample(0, i);
            require(std::isfinite(value) && std::abs(value) < 4, "Amp must produce finite, bounded samples");
            if (b < 60) continue;
            const auto phase = 2 * std::numbers::pi * frequency * (b * 128 + i) / rate;
            energy += value * value; mean += value;
            sine += value * std::sin(phase); cosine += value * std::cos(phase); ++frames;
        }
    }
    const auto power = energy / frames;
    const auto fundamental = 2 * (sine * sine + cosine * cosine) / (frames * frames);
    const auto residual = juce::jmax(0.0, power - fundamental - std::pow(mean / frames, 2));
    return { std::sqrt(power), std::sqrt(residual / juce::jmax(1.e-12, fundamental)) };
}
double response(float frequency, float amount, int control)
{
    AmpParameters p; p.bypass = false; p.voice = 0; p.gainDb = 0; p.levelDb = 0;
    std::atomic<float>* knobs[] { &p.bassDb, &p.midDb, &p.trebleDb, &p.presenceDb };
    knobs[control]->store(amount);
    Amp amp(p); amp.prepare(48000, 128);
    return analyse(amp, 48000, .001f, frequency).rms;
}
}
void testAmp()
{
    for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0 })
    {
        AmpParameters p;
        Amp amp(p); amp.prepare(rate, 128);
        // Bypass must preserve the dry signal exactly, including the amp's integer latency.
        const auto delay = amp.latencySamples();
        require(delay > 0 && delay < 128, "Amp must report usable oversampling latency");
        juce::AudioBuffer<float> block(1, 128);
        for (int b = 0; b < 5; ++b)
        {
            for (int i = 0; i < 128; ++i) block.setSample(0, i, .2f * static_cast<float>(std::sin((b * 128 + i) * .03)));
            amp.process(block, 128);
            for (int i = 0; i < 128; ++i)
            {
                const int frame = b * 128 + i - delay;
                const auto expected = frame < 0 ? 0.0f : .2f * static_cast<float>(std::sin(frame * .03));
                require(std::abs(block.getSample(0, i) - expected) < 1.e-6f, "Amp bypass must be latency-aligned and transparent");
            }
        }
        p.bypass = false;
        for (int voice = 0; voice < 3; ++voice)
        {
            p.voice = voice; amp.prepare(rate, 128);
            require(analyse(amp, rate, .05f).rms > .001, "Each amp voice must produce a useful signal");
        }
        p.voice = 2; p.gainDb = 36; p.bassDb = 12; p.midDb = -12; p.trebleDb = 12; p.presenceDb = 12; p.levelDb = 6;
        amp.prepare(rate, 128); analyse(amp, rate, .7f);
        amp.reset(); block.clear(); amp.process(block, 128);
        require(block.getMagnitude(0, 128) == 0, "Reset must clear preamp, tone, oversampling, and dry histories");
        for (int i = 0; i < 128; ++i) block.setSample(0, i, std::numeric_limits<float>::quiet_NaN());
        p.gainDb = std::numeric_limits<float>::infinity(); amp.process(block, 128);
        require(std::isfinite(block.getMagnitude(0, 128)), "Invalid inputs and control values must not poison amp state");
    }
    std::array<Analysis, 3> voices;
    for (int voice = 0; voice < 3; ++voice)
    {
        AmpParameters p; p.bypass = false; p.voice = voice;
        Amp amp(p); amp.prepare(48000, 128); voices[static_cast<size_t>(voice)] = analyse(amp, 48000, .05f);
    }
    require(voices[2].distortion > voices[0].distortion * 3 && voices[2].distortion > .01,
        "High Gain must have substantially more harmonic saturation than Clean");
    require(std::abs(voices[1].distortion - voices[2].distortion) > .01,
        "Crunch and High Gain must have distinct saturation behavior");
    AmpParameters p; p.bypass = false; p.voice = 0; p.gainDb = 0;
    Amp amp(p); amp.prepare(48000, 128); const auto lowGain = analyse(amp, 48000, .02f).rms;
    p.gainDb = 36; amp.prepare(48000, 128);
    require(analyse(amp, 48000, .02f).rms > lowGain * 3, "Gain must affect the preamp drive and headroom");
    for (int i = 0; i < 4; ++i)
    {
        const float frequencies[] { 60, 750, 6000, 8000 };
        require(response(frequencies[i], 9, i) > response(frequencies[i], -9, i) * 2,
            "Each amp tone control must affect its intended frequency region");
    }
    p.voice = 2; p.gainDb = 24; p.levelDb = 0; amp.prepare(48000, 128);
    juce::AudioBuffer<float> block(1, 128);
    for (int b = 0; b < 300; ++b)
    {
        std::fill_n(block.getWritePointer(0), 128, .2f); amp.process(block, 128);
    }
    require(block.getMagnitude(0, 128) < .0001, "Amp high-pass/DC filtering must reject steady DC");
    // Compare the first switched sample to a control instance at the same phase/state.
    AmpParameters control; control.bypass = false; control.voice = 0;
    AmpParameters switched; switched.bypass = false; switched.voice = 0;
    Amp reference(control), changing(switched); reference.prepare(48000, 128); changing.prepare(48000, 128);
    juce::AudioBuffer<float> a(1, 128), b(1, 128);
    for (int frame = 0; frame < 80; ++frame)
    {
        for (int i = 0; i < 128; ++i) a.setSample(0, i, .05f * static_cast<float>(std::sin((frame * 128 + i) * .05)));
        b.makeCopyOf(a); reference.process(a, 128); changing.process(b, 128);
    }
    switched.voice = 2;
    for (int i = 0; i < 128; ++i) a.setSample(0, i, .05f * static_cast<float>(std::sin((80 * 128 + i) * .05)));
    b.makeCopyOf(a); reference.process(a, 128); changing.process(b, 128);
    require(std::abs(a.getSample(0, 0) - b.getSample(0, 0)) < .01, "Voice changes must crossfade instead of switching abruptly");
    const auto begin = std::chrono::steady_clock::now();
    analyse(changing, 48000, .1f);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "Amp voices THD ratios: " << voices[0].distortion << ", " << voices[1].distortion << ", " << voices[2].distortion
        << "; mono render speed " << (.4266667 / seconds) << "x real time (current build)\n";
}
