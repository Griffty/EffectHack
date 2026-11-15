#include "AudioEngine.h"
#include <cmath>

namespace effecthack
{
void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

VirtualInput* AudioEngine::currentVirtualInput()
{
    switch (inputSource.load())
    {
        case InputSource::file: return &fileInput;
        case InputSource::testRiff: return &testInput;
        case InputSource::hardware: return nullptr;
    }
    return nullptr;
}

void AudioEngine::prepare(double sampleRate, int maximumBlockSize)
{
    blockCapacity = juce::jmax(1, maximumBlockSize);
    mono.setSize(1, blockCapacity);
    stereo.setSize(2, blockCapacity);
    dry.setSize(1, blockCapacity);
    pendingSeek.store(-1.0);
    deviceRate = sampleRate;
    inputMonitor.prepare(sampleRate);
    deviceGeneration.fetch_add(1);
    rack.prepare(sampleRate, blockCapacity);
    std::copy(rack.stages().begin(), rack.stages().end(), activeStages.begin());
    rackChangePending = false;
    orderGain.reset(sampleRate, 0.02);
    orderGain.setCurrentAndTargetValue(1.0f);
    draining = false; clearedWhileMuted = true;
    tailSamplesRemaining = 0; quietSamples = 0;
    tailPlaying.store(false);
    resetSpatial.store(false);
    fileInput.prepare(sampleRate, blockCapacity);
    testInput.prepare(sampleRate, blockCapacity);
    effectLatency.store(rack.latencySamples(), std::memory_order_relaxed);
    masterDryDelay.prepare({ sampleRate, static_cast<juce::uint32>(blockCapacity), 1 });
    masterDryDelay.setDelay(static_cast<float>(effectLatency.load()));
    masterDryDelay.reset();
    dryInputGain.reset(sampleRate, 0.02);
    dryInputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(parameters.inputDb.load()));
    rackWet.reset(sampleRate, 0.02);
    rackWet.setCurrentAndTargetValue(rackBypass.load() ? 0.0f : 1.0f);
    muteGain.reset(sampleRate, 0.02);
    muteGain.setCurrentAndTargetValue(masterMute.load() || (tunerEnabled.load() && tunerMute.load()) ? 0.0f : 1.0f);
    inputClipped.store(false); outputClipped.store(false);
    outputGain.reset(sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 0.0f, parameters.outputDb.load())));
    monitorGain.reset(sampleRate, 0.02);
    monitorGain.setCurrentAndTargetValue(0.0f);
    // Selecting a new device always requires deliberately enabling monitoring again.
    monitoring.store(false, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceStopped()
{
    monitorGain.setCurrentAndTargetValue(0);
    inputMonitor.stop();
    monitoring.store(false, std::memory_order_relaxed);
    inputPeak.store(0.0f, std::memory_order_relaxed);
    outputPeak.store(0.0f, std::memory_order_relaxed);
    outputUnclampedPeak.store(0.0f, std::memory_order_relaxed);
    effectLatency.store(0, std::memory_order_relaxed);
    tailPlaying.store(false);
    pendingSeek.store(-1.0);
    fileInput.release();
    testInput.release();
}

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputs, int numberOfInputs,
    float* const* outputs, int numberOfOutputs, int numberOfSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    const juce::ScopedNoDenormals noDenormals;
    const int channel = inputChannel.load(std::memory_order_relaxed);
    const auto* input = channel >= 0 && channel < numberOfInputs ? inputs[channel] : nullptr;
    auto* virtualInput = currentVirtualInput();
    const bool enabled = monitoring.load(std::memory_order_relaxed);
    if (resetSpatial.exchange(false))
    {
        if (!rackChangePending) resetSignalHistory();
    }
    if (!enabled && draining)
    {
        draining = false;
        tailPlaying.store(false);
    }
    if (enabled)
        clearedWhileMuted = false;
    const bool sourceAvailable = virtualInput != nullptr ? virtualInput->hasClip() : input != nullptr;
    inputMonitor.beginBlock(sourceAvailable && (virtualInput == nullptr || (enabled && !draining)),
        static_cast<int>(inputSource.load()) * 4 + channel, tunerEnabled.load());
    if (rackChangePending && (orderGain.getCurrentValue() <= 0.0f || monitorGain.getCurrentValue() <= 0.0f))
        commitRackOrder();
    orderGain.setTargetValue(rackChangePending || rackUpdateRequested ? 0.0f : 1.0f);
    if (virtualInput != nullptr && pendingSeek.load() >= 0.0 && monitorGain.getCurrentValue() <= 0.0f)
        finishSeek(*virtualInput);
    float inPeak = 0.0f, outPeak = 0.0f, tailPeak = 0.0f, preLimitPeak = 0.0f;
    bool inputOver = false, outputOver = false;
    monitorGain.setTargetValue(enabled && sourceAvailable && pendingSeek.load() < 0.0 ? 1.0f : 0.0f);
    dryInputGain.setTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 24.0f, parameters.inputDb.load())));
    rackWet.setTargetValue(rackBypass.load() ? 0.0f : 1.0f);
    muteGain.setTargetValue(masterMute.load() || (tunerEnabled.load() && tunerMute.load()) ? 0.0f : 1.0f);
    outputGain.setTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-60.0f, 0.0f, parameters.outputDb.load(std::memory_order_relaxed))));

    for (int offset = 0; offset < numberOfSamples;)
    {
        const auto count = juce::jmin(blockCapacity, numberOfSamples - offset);
        if (count <= 0)
            break;
        auto* samples = mono.getWritePointer(0);
        if (virtualInput != nullptr)
        {
            if (!draining && (enabled || monitorGain.isSmoothing()))
                virtualInput->render(mono, count);
            else
                mono.clear(0, count);
        }
        // Copy first: some audio backends can share input and output memory.
        for (int i = 0; i < count; ++i)
        {
            const auto value = virtualInput != nullptr ? samples[i] : (input != nullptr ? input[offset + i] : 0.0f);
            const auto clean = std::isfinite(value) ? value : 0.0f;
            inPeak = juce::jmax(inPeak, std::abs(clean));
            inputOver = inputOver || std::abs(clean) >= 1.0f;
            const auto trim = dryInputGain.getNextValue();
            inputMonitor.push(clean, clean * trim);
            samples[i] = enabled || monitorGain.isSmoothing() ? clean * trim : 0.0f;
            masterDryDelay.pushSample(0, samples[i]);
            dry.setSample(0, i, masterDryDelay.popSample(0));
        }
        stereo.copyFrom(0, 0, mono, 0, 0, count);
        stereo.copyFrom(1, 0, mono, 0, 0, count);
        for (auto* stage : activeStages) stage->process(stereo, count);
        for (int i = 0; i < count; ++i)
        {
            const auto gain = outputGain.getNextValue();
            const auto volume = monitorGain.getNextValue() * orderGain.getNextValue();
            const auto wet = rackWet.getNextValue();
            const auto mute = muteGain.getNextValue();
            for (int out = 0; out < numberOfOutputs; ++out)
                if (outputs[out] != nullptr)
                {
                    const auto sample = numberOfOutputs == 1 ? 0.5f * (stereo.getSample(0, i) + stereo.getSample(1, i))
                        : stereo.getSample(out % 2, i);
                    const auto clean = dry.getSample(0, i);
                    const auto mixed = clean + wet * (sample - clean);
                    const auto beforeMute = mixed * gain * volume;
                    tailPeak = juce::jmax(tailPeak, std::abs(beforeMute));
                    const auto beforeLimit = beforeMute * mute;
                    preLimitPeak = juce::jmax(preLimitPeak, std::abs(beforeLimit));
                    outputOver = outputOver || std::abs(beforeLimit) > 1.0f;
                    const auto value = juce::jlimit(-1.0f, 1.0f, beforeLimit);
                    outputs[out][offset + i] = value;
                    outPeak = juce::jmax(outPeak, std::abs(value));
                }
        }
        offset += count;
    }
    if (inputOver) inputClipped.store(true, std::memory_order_relaxed);
    if (outputOver) outputClipped.store(true, std::memory_order_relaxed);
    inputPeak.store(inPeak, std::memory_order_relaxed);
    inputMonitor.publish();
    outputPeak.store(outPeak, std::memory_order_relaxed);
    outputUnclampedPeak.store(preLimitPeak, std::memory_order_relaxed);
    if (!enabled && !monitorGain.isSmoothing() && !clearedWhileMuted)
    {
        rack.reset();
        clearedWhileMuted = true;
    }
    if (virtualInput != nullptr && enabled && !rackChangePending && pendingSeek.load() < 0.0 && virtualInput->isFinished())
    {
        if (!anySpatialEnabled() || rackBypass.load())
            monitoring.store(false, std::memory_order_relaxed);
        else if (!draining)
        {
            draining = true; quietSamples = 0;
            tailSamplesRemaining = static_cast<int>(deviceRate * 30.0);
            tailPlaying.store(true);
        }
    }
    if (draining)
    {
        tailSamplesRemaining -= numberOfSamples;
        quietSamples = tailPeak < 0.00001f ? quietSamples + numberOfSamples : 0;
        if (tailSamplesRemaining <= 0 || quietSamples > static_cast<int>(deviceRate * quietWindowSeconds()))
        {
            draining = false; tailPlaying.store(false);
            monitoring.store(false, std::memory_order_relaxed);
        }
    }
}
bool AudioEngine::moveStage(const juce::String& stageId, int destinationIndex)
{
    const auto before = rack.order();
    if (!rack.move(stageId, destinationIndex)) return false;
    if (rack.order() != before) queueRackChange();
    return true;
}

bool AudioEngine::setStageOrder(const RackOrder& order)
{
    if (!validRackOrder(order)) return false;
    if (rack.order() == order) return true;
    if (!rack.setOrder(order)) return false;
    queueRackChange();
    return true;
}

void AudioEngine::queueRackChange()
{
    rackChangePending = true;
    if (blockCapacity == 0 || monitorGain.getCurrentValue() <= 0.0f) commitRackOrder();
}

void AudioEngine::commitRackOrder()
{
    std::copy(rack.stages().begin(), rack.stages().end(), activeStages.begin());
    resetSignalHistory();
    if (blockCapacity > 0) masterDryDelay.setDelay(static_cast<float>(rack.latencySamples()));
    effectLatency.store(rack.latencySamples(), std::memory_order_relaxed);
    rackChangePending = false;
}

bool AudioEngine::anySpatialEnabled() const noexcept
{
    return (!chorusParameters.bypass.load() && chorusParameters.mix.load() > 0.0f)
        || (!delayParameters.bypass.load() && delayParameters.mix.load() > 0.0f)
        || (!reverbParameters.bypass.load() && reverbParameters.mix.load() > 0.0f);
}

double AudioEngine::quietWindowSeconds() const noexcept
{
    return !delayParameters.bypass.load() && delayParameters.mix.load() > 0.0f
        ? juce::jmax(0.5, static_cast<double>(juce::jlimit(20.0f, 1500.0f,
            delayParameters.timeMs.load())) * 0.001 + 0.15) : 0.5;
}

void AudioEngine::resetSignalHistory()
{
    if (blockCapacity > 0)
    {
        rack.reset(); masterDryDelay.reset();
    }
    draining = false; quietSamples = 0; tailPlaying.store(false);
}

void AudioEngine::finishSeek(VirtualInput& source)
{
    inputMonitor.stop();
    source.seek(pendingSeek.exchange(-1.0));
    resetSignalHistory();
}

void AudioEngine::seekPlayback(double seconds)
{
    if (auto* source = currentVirtualInput(); source != nullptr && source->hasClip() && std::isfinite(seconds))
    {
        pendingSeek.store(juce::jlimit(0.0, source->duration(), seconds));
        if (monitorGain.getCurrentValue() <= 0.0f)
            finishSeek(*source);
    }
}

void AudioEngine::stopPlayback()
{
    monitoring.store(false);
    seekPlayback(0.0);
}

void AudioEngine::resumePlayback()
{
    if (auto* source = currentVirtualInput(); source != nullptr && source->hasClip())
    {
        if (pendingSeek.load() < 0.0)
        {
            if (source->isFinished()) seekPlayback(0.0);
            else source->resume();
        }
        monitoring.store(true);
    }
}

double AudioEngine::playbackPosition() const
{
    const auto requested = pendingSeek.load();
    if (requested >= 0.0) return requested;
    switch (inputSource.load())
    {
        case InputSource::file: return fileInput.position();
        case InputSource::testRiff: return testInput.position();
        case InputSource::hardware: return 0.0;
    }
    return 0.0;
}

}
