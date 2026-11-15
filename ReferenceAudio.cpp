#include "ReferenceAudio.h"
#include <cmath>

namespace effecthack
{
namespace
{
constexpr double referenceRate = 16000, maximumSeconds = 360;
ReferenceAudio encode(juce::AudioSource& source, double rate, int channels, double duration, const Cancellation& cancel)
{
    ReferenceAudio result;
    if (cancel->load()) { result.error = "Cancelled."; return result; }
    const auto total = juce::roundToInt(duration * referenceRate);
    if (total < 16000 || total > static_cast<int>(maximumSeconds * referenceRate))
    { result.error = "Choose an audio section between one second and six minutes."; return result; }
    juce::ResamplingAudioSource resampler(&source, false, channels);
    resampler.setResamplingRatio(rate / referenceRate);
    resampler.prepareToPlay(1024, referenceRate);
    juce::AudioBuffer<float> decoded(channels, 1024), mono(1, total);
    double energy = 0;
    float peak = 0;
    for (int offset = 0; offset < total; offset += 1024)
    {
        if (cancel->load()) { result.error = "Cancelled."; return result; }
        const auto count = juce::jmin(1024, total - offset);
        decoded.clear();
        resampler.getNextAudioBlock({ &decoded, 0, count });
        for (int i = 0; i < count; ++i)
        {
            const auto value = channels == 1 ? decoded.getSample(0, i) : .5f * (decoded.getSample(0, i) + decoded.getSample(1, i));
            if (!std::isfinite(value)) { result.error = "Reference audio contains invalid samples."; return result; }
            mono.setSample(0, offset + i, value);
            peak = juce::jmax(peak, std::abs(value)); energy += static_cast<double>(value) * value;
        }
    }
    if (peak < 1.e-5f || energy / total < 1.e-12)
    { result.error = "This reference section is silent. Choose a section with audible guitar."; return result; }
    if (peak > .99f) mono.applyGain(.99f / peak);
    juce::MemoryBlock wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(wav, false);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(referenceRate).withNumChannels(1).withBitsPerSample(16));
    if (writer == nullptr) { result.error = "Could not encode the audio reference."; return result; }
    if (!writer->writeFromAudioSampleBuffer(mono, 0, total))
    { result.error = "Could not encode the audio reference."; return result; }
    writer.reset();
    if (cancel->load()) { result.error = "Cancelled."; return result; }
    result.base64 = juce::Base64::toBase64(wav.getData(), wav.getSize());
    result.durationSeconds = total / referenceRate;
    return result;
}
}
ReferenceAudio prepareAudioReference(const juce::File& file, double startSeconds, double endSeconds, const Cancellation& cancel)
{
    ReferenceAudio result;
    if (cancel->load()) { result.error = "Cancelled."; return result; }
    if (!file.existsAsFile() || file.getSize() > 512 * 1024 * 1024)
    { result.error = "Choose a reference audio file up to 512 MB."; return result; }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0 || reader->lengthInSamples <= 0
        || reader->numChannels < 1 || reader->numChannels > 2)
    { result.error = "Could not read this reference. Choose mono/stereo WAV, FLAC, MP3, AIFF, or Ogg."; return result; }
    const auto duration = reader->lengthInSamples / reader->sampleRate;
    const auto start = startSeconds < 0 ? 0 : startSeconds;
    const auto end = endSeconds >= 0 ? juce::jmin(endSeconds, duration)
        : startSeconds >= 0 ? juce::jmin(start + 30, duration) : duration;
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || start >= duration || end - start < 1 || end - start > maximumSeconds)
    { result.error = "Choose a section between one second and six minutes. A start time alone selects 30 seconds."; return result; }
    const auto rate = reader->sampleRate;
    const auto channels = static_cast<int>(reader->numChannels);
    juce::AudioFormatReaderSource source(reader.release(), true);
    // ResamplingAudioSource prepares its input, which may rewind some readers.
    // The subsection wrapper makes the requested offset part of the input itself.
    class Section final : public juce::AudioSource
    {
    public:
        Section(juce::AudioFormatReaderSource& input, juce::int64 offset) : reader(input), start(offset) {}
        void prepareToPlay(int block, double sampleRate) override { reader.prepareToPlay(block, sampleRate); reader.setNextReadPosition(start); }
        void releaseResources() override { reader.releaseResources(); }
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override { reader.getNextAudioBlock(info); }
    private:
        juce::AudioFormatReaderSource& reader;
        juce::int64 start;
    } section(source, static_cast<juce::int64>(start * rate));
    result = encode(section, rate, channels, end - start, cancel);
    result.startSeconds = start;
    return result;
}
ReferenceAudio encodeToneExample(const juce::AudioBuffer<float>& samples, double rate, const Cancellation& cancel)
{
    if (!std::isfinite(rate) || rate <= 0 || samples.getNumChannels() < 1 || samples.getNumChannels() > 2)
        return { {}, "Invalid processor example.", 0, 0 };
    juce::AudioBuffer<float> readable(samples);
    juce::MemoryAudioSource source(readable, false, false);
    return encode(source, rate, samples.getNumChannels(), samples.getNumSamples() / rate, cancel);
}
}
