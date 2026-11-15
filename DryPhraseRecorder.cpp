#include "DryPhraseRecorder.h"
#include <cmath>

namespace effecthack
{
std::shared_ptr<AudioClip> DryPhraseRecorder::allocate(double sampleRate)
{
    if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000) return {};
    auto clip = std::make_shared<AudioClip>();
    clip->name = "Dry phrase"; clip->sampleRate = sampleRate;
    clip->samples.setSize(1, juce::roundToInt(sampleRate * maximumSeconds));
    clip->samples.clear(); // Touch storage off the audio thread, before recording.
    return clip;
}
void DryPhraseRecorder::arm(std::shared_ptr<AudioClip> clip)
{
    buffer = std::move(clip); cursor = 0; publishedLength = 0;
    status.store(buffer != nullptr ? State::armed : State::idle, std::memory_order_release);
}
void DryPhraseRecorder::stop() noexcept
{
    if (capturing()) status.store(State::ready, std::memory_order_release);
}
void DryPhraseRecorder::cancel()
{
    status.store(State::idle, std::memory_order_release); buffer.reset(); cursor = 0; publishedLength = 0;
}
std::shared_ptr<AudioClip> DryPhraseRecorder::take()
{
    if (state() != State::ready) return {};
    status.store(State::idle, std::memory_order_release);
    return std::exchange(buffer, {});
}
void DryPhraseRecorder::process(const float* dry, int samples, bool canCapture) noexcept
{
    const auto s = state();
    if (s != State::armed && s != State::recording) return;
    if (!canCapture) { if (s == State::recording) stop(); return; }
    if (buffer == nullptr || dry == nullptr || samples <= 0) return;
    status.store(State::recording, std::memory_order_release);
    const auto count = juce::jmin(samples, buffer->samples.getNumSamples() - cursor);
    buffer->samples.copyFrom(0, cursor, dry, count);
    cursor += count;
    publishedLength.store(cursor, std::memory_order_relaxed);
    if (cursor >= buffer->samples.getNumSamples()) status.store(State::ready, std::memory_order_release);
}
DryPhrase DryPhraseRecorder::finish(std::shared_ptr<AudioClip> clip, int samples)
{
    DryPhrase result;
    if (clip == nullptr || samples < clip->sampleRate * minimumSeconds || samples > clip->samples.getNumSamples())
    { result.error = "Record at least a quarter-second of playing."; return result; }
    if (clip->samples.getMagnitude(0, 0, samples) < .00001f)
    { result.error = "No signal captured. Check the selected input channel and interface level."; return result; }
    clip->samples.setSize(1, samples, true, false, true);
    // Preserve the original level/dynamics. Only the loop boundaries get a short fade.
    const auto fade = juce::jmin(samples / 4, juce::roundToInt(clip->sampleRate * .005));
    if (fade > 0)
    {
        clip->samples.applyGainRamp(0, 0, fade, 0, 1);
        clip->samples.applyGainRamp(0, samples - fade, fade, 1, 0);
    }
    for (size_t index = 0; index < result.peaks.size(); ++index)
    {
        const auto start = static_cast<int>(index * static_cast<size_t>(samples) / result.peaks.size());
        const auto end = static_cast<int>((index + 1) * static_cast<size_t>(samples) / result.peaks.size());
        result.peaks[index] = clip->samples.getMagnitude(0, start, juce::jmax(1, end - start));
    }
    result.clip = std::move(clip);
    return result;
}
}
