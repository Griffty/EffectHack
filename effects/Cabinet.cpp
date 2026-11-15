#include "Cabinet.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace effecthack
{
std::shared_ptr<CabinetImpulse> makeBuiltinCabinet()
{
    auto result = std::make_shared<CabinetImpulse>();
    result->name = "Built-in speaker approximation";
    result->samples.setSize(1, 2048);
    result->samples.clear();
    result->samples.setSample(0, 0, 1.0f);
    // Original synthetic response: bass roll-off, broad speaker resonances, and
    // a steep treble roll-off. This is not a measured commercial cabinet IR.
    using C = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::IIR::Filter<float> highpass(C::makeHighPass(48000.0, 75.0f, 0.7071f));
    juce::dsp::IIR::Filter<float> lowpassA(C::makeLowPass(48000.0, 5000.0f, 0.7071f));
    juce::dsp::IIR::Filter<float> lowpassB(C::makeLowPass(48000.0, 5000.0f, 0.7071f));
    juce::dsp::IIR::Filter<float> body(C::makePeakFilter(48000.0, 350.0f, 0.85f, juce::Decibels::decibelsToGain(2.5f)));
    juce::dsp::IIR::Filter<float> presence(C::makePeakFilter(48000.0, 1800.0f, 1.0f, juce::Decibels::decibelsToGain(-2.0f)));
    for (int i = 0; i < result->samples.getNumSamples(); ++i)
    {
        auto value = highpass.processSample(result->samples.getSample(0, i));
        value = lowpassB.processSample(lowpassA.processSample(value));
        result->samples.setSample(0, i, presence.processSample(body.processSample(value)));
    }
    return result;
}

CabinetLoadResult loadCabinetImpulse(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return { {}, "Could not read the IR. Choose a WAV, AIFF, or FLAC impulse response." };
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->numChannels > 2)
        return { {}, "Choose a non-empty mono or stereo cabinet IR." };
    if (reader->lengthInSamples / reader->sampleRate > 0.5 || reader->lengthInSamples > 192000)
        return { {}, "Choose a cabinet IR up to 500 ms long, rather than a guitar recording." };
    const auto count = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> decoded(static_cast<int>(reader->numChannels), count);
    if (!reader->read(&decoded, 0, count, 0, true, true))
        return { {}, "The cabinet IR could not be decoded." };
    auto result = std::make_shared<CabinetImpulse>();
    result->name = file.getFileName();
    result->sourcePath = file.getFullPathName();
    result->sampleRate = reader->sampleRate;
    result->normalise = true;
    result->samples.setSize(1, count);
    for (int i = 0; i < count; ++i)
    {
        const auto value = reader->numChannels == 2 ? 0.5f * (decoded.getSample(0, i) + decoded.getSample(1, i))
                                                   : decoded.getSample(0, i);
        if (!std::isfinite(value))
            return { {}, "This IR contains invalid samples." };
        result->samples.setSample(0, i, value);
    }
    if (result->samples.getMagnitude(0, count) < 1.0e-8f)
        return { {}, "This IR is silent after mono conversion." };
    return { std::move(result), {} };
}

Cabinet::Cabinet(CabinetParameters& controls) : parameters(controls), impulse(makeBuiltinCabinet()) {}

void Cabinet::queueImpulse()
{
    juce::AudioBuffer<float> copy;
    copy.makeCopyOf(impulse->samples);
    convolution.loadImpulseResponse(std::move(copy), impulse->sampleRate, juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no,
        impulse->normalise ? juce::dsp::Convolution::Normalise::yes : juce::dsp::Convolution::Normalise::no);
}

void Cabinet::setImpulse(std::shared_ptr<const CabinetImpulse> newImpulse)
{
    jassert(newImpulse != nullptr);
    impulse = std::move(newImpulse);
    queueImpulse();
}

void Cabinet::prepare(double sampleRate, int maximumBlockSize)
{
    dryBuffer.setSize(1, maximumBlockSize);
    queueImpulse();
    convolution.prepare({ sampleRate, static_cast<juce::uint32>(maximumBlockSize), 1 });
    convolution.reset();
    wet.reset(sampleRate, 0.02);
    wet.setCurrentAndTargetValue(parameters.bypass.load() ? 0.0f : 1.0f);
}

void Cabinet::process(juce::AudioBuffer<float>& mono, int count) noexcept
{
    dryBuffer.copyFrom(0, 0, mono, 0, 0, count);
    auto block = juce::dsp::AudioBlock<float>(mono).getSubBlock(0, static_cast<size_t>(count));
    convolution.process(juce::dsp::ProcessContextReplacing<float>(block));
    wet.setTargetValue(parameters.bypass.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
    auto* samples = mono.getWritePointer(0);
    const auto* dry = dryBuffer.getReadPointer(0);
    for (int i = 0; i < count; ++i)
        samples[i] = dry[i] + wet.getNextValue() * (samples[i] - dry[i]);
}
}
