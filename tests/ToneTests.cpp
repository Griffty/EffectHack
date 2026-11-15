#include "../AudioEngine.h"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

void testAmp();
void testNoiseGate();
void testCompressor();

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Processor>
double measure(Processor& effect, double frequency, double rate)
{
    juce::AudioBuffer<float> block(1, 128);
    double energy = 0.0;
    int measured = 0;
    const int length = static_cast<int>(rate * 0.3);
    for (int offset = 0; offset < length; offset += 128)
    {
        for (int i = 0; i < 128; ++i)
            block.setSample(0, i, 0.1f * static_cast<float>(std::sin(2.0 * std::numbers::pi * frequency * (offset + i) / rate)));
        effect.process(block, 128);
        if (offset > length / 2)
            for (int i = 0; i < 128; ++i)
            {
                const auto value = block.getSample(0, i);
                require(std::isfinite(value), "Tone stages must produce finite samples");
                energy += value * value;
                ++measured;
            }
    }
    return std::sqrt(energy / measured) / (0.1 / std::sqrt(2.0));
}

void writeWav(const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    juce::WavAudioFormat format;
    auto out = file.createOutputStream();
    require(out != nullptr, "Could not create IR fixture");
    out->setPosition(0); out->truncate();
    std::unique_ptr<juce::OutputStream> destination(std::move(out));
    auto writer = format.createWriterFor(destination, juce::AudioFormatWriterOptions()
        .withSampleRate(48000.0).withNumChannels(audio.getNumChannels()).withBitsPerSample(24));
    require(writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Could not write IR fixture");
}
}

int main(int argc, char** argv)
{
    juce::MessageManager::getInstance();
    int result = 0;
    try
    {
        require(argc == 2, "Pass an IR-fixture scratch directory");
        testAmp();
        testNoiseGate();
        testCompressor();
        for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0 })
        {
            effecthack::EqualizerParameters controls;
            effecthack::Equalizer eq(controls);
            eq.prepare(rate);
            require(std::abs(measure(eq, 1000.0, rate) - 1.0) < 0.01, "Flat EQ must preserve the signal");
            controls.bassDb = 9.0f; eq.prepare(rate);
            require(measure(eq, 40.0, rate) > 2.4, "Bass boost must lift low frequencies");
            controls.bassDb = -9.0f; eq.prepare(rate);
            require(measure(eq, 40.0, rate) < 0.43, "Bass cut must reduce low frequencies");
            controls.bassDb = 0.0f; controls.midDb = 9.0f; eq.prepare(rate);
            require(measure(eq, 750.0, rate) > 2.7, "Mid boost must lift the mid band");
            controls.midDb = -9.0f; eq.prepare(rate);
            require(measure(eq, 750.0, rate) < 0.38, "Mid cut must reduce the mid band");
            controls.midDb = 0.0f; controls.trebleDb = 9.0f; eq.prepare(rate);
            require(measure(eq, 10000.0, rate) > 2.4, "Treble boost must lift high frequencies");
            controls.trebleDb = -9.0f; eq.prepare(rate);
            require(measure(eq, 10000.0, rate) < 0.43, "Treble cut must reduce high frequencies");
            controls.bypass = true; controls.bassDb = 12.0f; controls.midDb = -12.0f; eq.prepare(rate);
            require(std::abs(measure(eq, 750.0, rate) - 1.0) < 0.01, "EQ bypass must preserve dry audio");

            effecthack::CabinetParameters cabControls;
            effecthack::Cabinet cabinet(cabControls);
            cabinet.prepare(rate, 128);
            const auto middle = measure(cabinet, 1000.0, rate);
            const auto bass = measure(cabinet, 30.0, rate);
            const auto treble = measure(cabinet, 10000.0, rate);
            require(bass < middle * 0.3 && treble < middle * 0.15,
                "Built-in cabinet must roll off sub-bass and high-frequency fizz");
            require(cabinet.latencySamples() == 0, "Cabinet convolution must add no block latency");
            cabControls.bypass = true; cabinet.prepare(rate, 128);
            require(std::abs(measure(cabinet, 1000.0, rate) - 1.0) < 0.01, "Cabinet bypass must preserve dry audio");
        }

        auto impulse = std::make_shared<effecthack::CabinetImpulse>();
        impulse->samples.setSize(1, 2);
        impulse->samples.setSample(0, 0, 0.75f); impulse->samples.setSample(0, 1, 0.25f);
        effecthack::CabinetParameters controls;
        effecthack::Cabinet cabinet(controls);
        cabinet.setImpulse(impulse); cabinet.prepare(48000.0, 128);
        juce::AudioBuffer<float> block(1, 128); block.clear(); block.setSample(0, 0, 1.0f);
        cabinet.process(block, 128);
        require(std::abs(block.getSample(0, 0) - 0.75f) < 1.0e-5f
            && std::abs(block.getSample(0, 1) - 0.25f) < 1.0e-5f, "A custom IR must actually determine convolution output");
        for (int i = 2; i < 128; ++i)
            require(std::abs(block.getSample(0, i)) < 1.0e-5f, "Custom convolution must not invent a tail");

        const auto scratch = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        require(scratch.createDirectory().wasOk(), "Could not create scratch directory");
        auto file = scratch.getChildFile("tone-ir.wav");
        juce::AudioBuffer<float> stereo(2, 64); stereo.clear();
        stereo.setSample(0, 0, 0.5f); stereo.setSample(1, 0, 0.25f);
        writeWav(file, stereo);
        const auto loaded = effecthack::loadCabinetImpulse(file);
        require(loaded.impulse != nullptr && loaded.impulse->normalise
            && std::abs(loaded.impulse->samples.getSample(0, 0) - 0.375f) < 1.0e-6f,
            "IR files must decode, downmix, and request level normalization");
        stereo.clear(); writeWav(file, stereo);
        require(effecthack::loadCabinetImpulse(file).impulse == nullptr, "Silent IRs must be rejected");
        juce::AudioBuffer<float> longIr(1, 48000); longIr.clear(); longIr.setSample(0, 0, 1.0f);
        writeWav(file, longIr);
        require(effecthack::loadCabinetImpulse(file).impulse == nullptr, "Recordings longer than 500 ms must be rejected as IRs");

        effecthack::AudioEngine rack;
        rack.parameters.bypass = true; rack.cabinetParameters.bypass = true;
        rack.eqParameters.midDb = 12.0f; rack.parameters.outputDb = 0.0f;
        rack.prepare(48000.0, 128); rack.inputSource = effecthack::AudioEngine::InputSource::hardware;
        rack.monitoring = true;
        juce::AudioBuffer<float> input(1, 128), output(2, 128);
        for (int i = 0; i < 128; ++i)
            input.setSample(0, i, 0.1f * static_cast<float>(std::sin(2.0 * std::numbers::pi * i / 64.0)));
        auto run = [&]
        {
            rack.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 1,
                output.getArrayOfWritePointers(), 2, 128, {});
        };
        for (int i = 0; i < 60; ++i) run();
        const auto boosted = output.getRMSLevel(0, 0, 128);
        require(boosted > 0.25f, "EQ must be present in the full engine chain");
        rack.parameters.outputDb = -12.0f;
        for (int i = 0; i < 60; ++i) run();
        require(std::abs(output.getRMSLevel(0, 0, 128) / boosted - juce::Decibels::decibelsToGain(-12.0f)) < 0.01f,
            "Output level must control the full rack");
        rack.parameters.outputDb = 0.0f; rack.eqParameters.bassDb = 12.0f; rack.eqParameters.trebleDb = 12.0f;
        for (int i = 0; i < 128; ++i) input.setSample(0, i, 8.0f * input.getSample(0, i));
        for (int i = 0; i < 80; ++i)
        {
            run();
            for (int j = 0; j < 128; ++j)
                require(std::isfinite(output.getSample(0, j)) && std::abs(output.getSample(0, j)) <= 1.0f,
                    "The complete boosted rack must stay within digital output range");
        }
        std::cout << "PASS: EQ boost/cut and bypass at four sample rates, cabinet response/bypass, custom convolution, IR file validation, rack ordering/output level, bounded output\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n'; result = 1;
    }
    juce::MessageManager::deleteInstance();
    return result;
}
