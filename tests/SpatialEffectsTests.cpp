#include "../AudioEngine.h"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
std::vector<float> impulseResponse(effecthack::Chorus& chorus, effecthack::Delay& delay,
    effecthack::Reverb& reverb, double rate, double seconds)
{
    juce::AudioBuffer<float> block(2, 128);
    std::vector<float> result(static_cast<size_t>(rate * seconds) * 2);
    const auto frames = static_cast<int>(result.size() / 2);
    for (int offset = 0; offset < frames; offset += 128)
    {
        const auto count = juce::jmin(128, frames - offset);
        block.clear();
        if (offset == 0) { block.setSample(0, 0, 1.0f); block.setSample(1, 0, 1.0f); }
        chorus.process(block, count);
        delay.process(block, count);
        reverb.process(block, count);
        for (int i = 0; i < count; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto value = block.getSample(ch, i);
                require(std::isfinite(value), "Spatial effects must produce finite samples");
                result[static_cast<size_t>((offset + i) * 2 + ch)] = value;
            }
    }
    return result;
}
double lateEnergy(const std::vector<float>& samples, double rate)
{
    double energy = 0;
    for (size_t i = static_cast<size_t>(rate) * 2; i < samples.size(); ++i)
        energy += samples[i] * samples[i];
    return energy;
}
}

int main()
{
    juce::MessageManager::getInstance();
    int result = 0;
    try
    {
        for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0 })
        {
            effecthack::ChorusParameters chorus;
            effecthack::DelayParameters delay;
            effecthack::ReverbParameters reverb;
            effecthack::Chorus chorusEffect(chorus);
            effecthack::Delay delayEffect(delay);
            effecthack::Reverb reverbEffect(reverb);
            chorusEffect.prepare(rate, 128);
            delayEffect.prepare(rate, 128);
            reverbEffect.prepare(rate, 128);
            auto bypassed = impulseResponse(chorusEffect, delayEffect, reverbEffect, rate, 0.1);
            require(bypassed[0] == 1.0f && bypassed[1] == 1.0f, "Bypassed effects must preserve dry input");
            for (size_t i = 2; i < bypassed.size(); ++i)
                require(bypassed[i] == 0.0f, "Bypassed effects must add no tail");

            chorus.bypass = false; delay.bypass = false; reverb.bypass = false;
            chorus.mix = 0.0f; delay.mix = 0.0f; reverb.mix = 0.0f;
            chorusEffect.prepare(rate, 128);
            delayEffect.prepare(rate, 128);
            reverbEffect.prepare(rate, 128);
            auto zeroMix = impulseResponse(chorusEffect, delayEffect, reverbEffect, rate, 0.1);
            require(zeroMix == bypassed, "Zero-mix effects must preserve the dry signal");

            chorus.bypass = true; reverb.bypass = true;
            delay.mix = 1.0f; delay.timeMs = 50.0f; delay.feedback = 0.5f;
            chorusEffect.prepare(rate, 128);
            delayEffect.prepare(rate, 128);
            reverbEffect.prepare(rate, 128);
            auto echoes = impulseResponse(chorusEffect, delayEffect, reverbEffect, rate, 0.22);
            const auto echo = juce::roundToInt(rate * 0.05);
            require(std::abs(echoes[static_cast<size_t>(echo * 2)] - 1.0f) < 0.002f,
                "The first echo must occur at the selected delay time");
            require(std::abs(echoes[static_cast<size_t>(echo * 4)] - 0.5f) < 0.002f
                && std::abs(echoes[static_cast<size_t>(echo * 6)] - 0.25f) < 0.002f,
                "Feedback must produce geometrically decaying echoes");
            chorusEffect.reset(); delayEffect.reset(); reverbEffect.reset();
            juce::AudioBuffer<float> silence(2, 128); silence.clear();
            chorusEffect.process(silence, 128); delayEffect.process(silence, 128); reverbEffect.process(silence, 128);
            require(silence.getMagnitude(0, 128) == 0.0f, "Reset must clear stored echoes");

            delay.bypass = true; chorus.bypass = false; chorus.mix = 0.5f; chorus.depth = 1.0f; chorus.rateHz = 1.3f;
            chorusEffect.prepare(rate, 128); delayEffect.prepare(rate, 128); reverbEffect.prepare(rate, 128);
            double stereoDifference = 0;
            juce::AudioBuffer<float> tone(2, 128);
            for (int offset = 0; offset < static_cast<int>(rate * 0.5); offset += 128)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const auto value = 0.2f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * (offset + i) / rate));
                    tone.setSample(0, i, value); tone.setSample(1, i, value);
                }
                chorusEffect.process(tone, 128);
                for (int i = 0; i < 128; ++i)
                    stereoDifference += std::abs(tone.getSample(0, i) - tone.getSample(1, i));
            }
            require(stereoDifference > 1.0, "Chorus must create distinct stereo modulation from mono input");

            chorus.bypass = true; reverb.bypass = false; reverb.mix = 1.0f; reverb.size = 0.9f;
            chorusEffect.prepare(rate, 128); delayEffect.prepare(rate, 128); reverbEffect.prepare(rate, 128);
            const auto largeRoom = impulseResponse(chorusEffect, delayEffect, reverbEffect, rate, 2.0);
            const auto largeEnergy = lateEnergy(largeRoom, rate);
            double reverbStereoDifference = 0;
            for (size_t i = 0; i < largeRoom.size(); i += 2)
                reverbStereoDifference += std::abs(largeRoom[i] - largeRoom[i + 1]);
            require(largeEnergy > 1.0e-6 && reverbStereoDifference > 0.01,
                "Reverb must produce a decaying stereo tail");
            reverb.size = 0.1f; reverbEffect.prepare(rate, 128);
            require(largeEnergy > lateEnergy(impulseResponse(chorusEffect, delayEffect, reverbEffect, rate, 2.0), rate) * 3.0,
                "Larger reverb size must sustain a longer tail");
        }

        // Exercise natural EOF tails with no physical input and a short impulse clip.
        effecthack::AudioEngine rack;
        rack.parameters.bypass = true; rack.cabinetParameters.bypass = true; rack.eqParameters.bypass = true;
        rack.parameters.outputDb = 0.0f;
        rack.delayParameters.bypass = false; rack.delayParameters.mix = 1.0f;
        rack.delayParameters.timeMs = 100.0f; rack.delayParameters.feedback = 0.5f;
        rack.prepare(48000.0, 128);
        auto clip = std::make_shared<effecthack::AudioClip>();
        clip->sampleRate = 48000.0; clip->samples.setSize(1, 64); clip->samples.clear(); clip->samples.setSample(0, 0, 0.5f);
        rack.fileInput.setClip(clip); rack.fileInput.setLooping(false);
        rack.inputSource = effecthack::AudioEngine::InputSource::file;
        rack.monitoring = true;
        juce::AudioBuffer<float> out(2, 128);
        auto run = [&] { rack.audioDeviceIOCallbackWithContext(nullptr, 0, out.getArrayOfWritePointers(), 2, 128, {}); };
        double tailEnergy = 0;
        for (int i = 0; i < 120; ++i)
        {
            run();
            if (i > 20) tailEnergy += out.getRMSLevel(0, 0, 128);
        }
        require(rack.monitoring.load() && rack.tailPlaying.load() && tailEnergy > 0.01,
            "EOF must allow audible echoes to continue before stopping");
        for (int i = 0; i < 2000; ++i) run();
        require(!rack.monitoring.load() && !rack.tailPlaying.load() && out.getMagnitude(0, 128) == 0.0f,
            "Decayed tails must eventually stop and become silent");

        rack.fileInput.resume(); rack.clearEffectTails(); rack.monitoring = true;
        for (int i = 0; i < 80; ++i) run();
        rack.monitoring = false;
        for (int i = 0; i < 30; ++i) run();
        require(out.getMagnitude(0, 128) == 0.0f && !rack.tailPlaying.load(), "Pause must silence a tail promptly");

        // Extreme settings and automation must remain finite and bounded by the final output stage.
        rack.inputSource = effecthack::AudioEngine::InputSource::testRiff;
        rack.chorusParameters.bypass = false; rack.chorusParameters.depth = 1.0f; rack.chorusParameters.mix = 1.0f;
        rack.delayParameters.feedback = 0.8f; rack.delayParameters.mix = 0.7f;
        rack.reverbParameters.bypass = false; rack.reverbParameters.size = 0.95f; rack.reverbParameters.mix = 1.0f;
        rack.eqParameters.bypass = false;
        rack.eqParameters.bassDb = 12.0f; rack.eqParameters.midDb = 12.0f; rack.eqParameters.trebleDb = 12.0f;
        rack.testInput.resume(); rack.clearEffectTails(); rack.monitoring = true;
        for (int block = 0; block < 1600; ++block)
        {
            if (block % 100 == 0)
            {
                rack.delayParameters.timeMs = block % 200 == 0 ? 20.0f : 1500.0f;
                rack.chorusParameters.rateHz = block % 200 == 0 ? 0.1f : 5.0f;
            }
            run();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    require(std::isfinite(out.getSample(ch, i)) && std::abs(out.getSample(ch, i)) <= 1.0f,
                        "The full stereo rack must remain finite and within output bounds during automation");
        }
        std::cout << "PASS: bypass/zero mix, delay timing and feedback, stereo chorus, reverb decay, reset, EOF tails, pause, parameter automation and bounded stereo output\n";
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    juce::MessageManager::deleteInstance();
    return result;
}
