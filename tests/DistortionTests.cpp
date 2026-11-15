#include "../effects/Distortion.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<float> render(effecthack::DistortionParameters& parameters,
                          const std::vector<float>& source, int blockSize, int* latency = nullptr)
{
    effecthack::Distortion effect(parameters);
    effect.prepare(sampleRate, blockSize);
    if (latency != nullptr)
        *latency = effect.latencySamples();
    juce::AudioBuffer<float> buffer(1, blockSize);
    std::vector<float> result(source.size());
    for (size_t offset = 0; offset < source.size();)
    {
        const auto count = std::min(blockSize, static_cast<int>(source.size() - offset));
        std::copy_n(source.data() + offset, count, buffer.getWritePointer(0));
        effect.process(buffer, count);
        std::copy_n(buffer.getReadPointer(0), count, result.data() + offset);
        offset += static_cast<size_t>(count);
    }
    return result;
}

std::vector<float> sine(int count, float amplitude = 0.25f)
{
    std::vector<float> samples(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        samples[static_cast<size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * i / sampleRate));
    return samples;
}

double harmonic(const std::vector<float>& samples, int multiplier)
{
    double real = 0.0, imaginary = 0.0;
    for (size_t i = 4800; i < samples.size(); ++i)
    {
        const auto phase = 2.0 * std::numbers::pi * 1000.0 * multiplier * static_cast<double>(i) / sampleRate;
        real += samples[i] * std::cos(phase);
        imaginary += samples[i] * std::sin(phase);
    }
    return std::hypot(real, imaginary);
}
}

int main()
{
    try
    {
        effecthack::DistortionParameters parameters;
        parameters.outputDb = 0.0f;
        const auto source = sine(12000);

        auto silence = render(parameters, std::vector<float>(6000, 0.0f), 256);
        require(std::all_of(silence.begin(), silence.end(), [](float v) { return v == 0.0f; }), "Silence must stay silent");

        int latency = 0;
        parameters.bypass = true;
        auto dry = render(parameters, source, 256, &latency);
        require(latency > 0 && latency < 256, "Oversampling latency must be known and bounded");
        for (size_t i = static_cast<size_t>(latency); i < dry.size(); ++i)
            require(std::abs(dry[i] - source[i - static_cast<size_t>(latency)]) < 1.0e-6f, "Bypass must preserve latency-aligned dry audio");

        parameters.bypass = false;
        parameters.mix = 0.0f;
        parameters.driveDb = 48.0f;
        auto dryMix = render(parameters, source, 127);
        for (size_t i = 0; i < dry.size(); ++i)
            require(std::abs(dryMix[i] - dry[i]) < 1.0e-6f, "Zero mix must equal bypass even at maximum drive");

        parameters.mix = 1.0f;
        parameters.driveDb = 0.0f;
        const auto lowDrive = render(parameters, source, 256);
        parameters.driveDb = 36.0f;
        const auto highDrive = render(parameters, source, 256);
        const auto lowRatio = harmonic(lowDrive, 3) / harmonic(lowDrive, 1);
        const auto highRatio = harmonic(highDrive, 3) / harmonic(highDrive, 1);
        require(highRatio > 0.1 && highRatio > lowRatio * 5.0, "Drive must introduce meaningful odd harmonics");

        auto otherBlocks = render(parameters, source, 127);
        for (size_t i = 0; i < highDrive.size(); ++i)
            require(std::abs(highDrive[i] - otherBlocks[i]) < 1.0e-5f, "Audio must remain consistent across callback sizes");

        parameters.inputDb = 24.0f;
        parameters.driveDb = 48.0f;
        auto loud = render(parameters, sine(6000, 4.0f), 256);
        require(std::all_of(loud.begin(), loud.end(), [](float v) { return std::isfinite(v) && std::abs(v) <= 1.0f; }), "Extreme inputs must remain finite and within digital output range");

        parameters.inputDb = 0.0f;
        parameters.outputDb = -12.0f;
        parameters.bypass = true;
        auto quiet = render(parameters, source, 256);
        const auto expectedGain = juce::Decibels::decibelsToGain(-12.0f);
        for (size_t i = static_cast<size_t>(latency); i < quiet.size(); ++i)
            require(std::abs(quiet[i] - source[i - static_cast<size_t>(latency)] * expectedGain) < 1.0e-6f, "Output level must also apply during bypass");

        // Parameter and bypass changes must ramp instead of jumping in a held signal.
        effecthack::Distortion effect(parameters);
        effect.prepare(sampleRate, 256);
        juce::AudioBuffer<float> buffer(1, 256);
        float previous = 0.0f;
        for (int block = 0; block < 30; ++block)
        {
            if (block == 10) { parameters.bypass = false; parameters.outputDb = 0.0f; }
            std::fill_n(buffer.getWritePointer(0), 256, 0.1f);
            effect.process(buffer, 256);
            for (int i = 0; i < 256; ++i)
            {
                const auto current = buffer.getSample(0, i);
                if (block >= 5)
                    require(std::abs(current - previous) < 0.01f, "Live controls must change smoothly");
                previous = current;
            }
        }
        std::cout << "PASS: silence, bypass alignment, dry mix, distortion harmonics, callback sizes, output bounds, output gain, control smoothing\n";
        std::cout << "Latency: " << latency << " samples; third-harmonic ratios: " << lowRatio << " -> " << highRatio << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
