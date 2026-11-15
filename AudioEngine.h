#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "effects/Distortion.h"
#include "effects/Amp.h"
#include "effects/NoiseGate.h"
#include "effects/Compressor.h"
#include "VirtualInput.h"
#include "DryPhraseRecorder.h"
#include "InputMonitor.h"
#include "effects/Cabinet.h"
#include "effects/Equalizer.h"
#include "effects/Chorus.h"
#include "effects/Delay.h"
#include "effects/Reverb.h"
#include "effects/RackStage.h"
#include "effects/ChannelWiseRackStage.h"

namespace effecthack
{
/** Owns the live signal path, effect histories, and transport transitions.
 *
 * Scalar controls are atomic; that does not make compound rack/source mutations
 * thread-safe. UI callers serialize those operations with the device manager's
 * callback lock. Prepare/IR loading runs outside the callback. The callback uses
 * preallocated buffers and publishes meter values for the UI to read.
 *
 * See documentation/ARCHITECTURE.md for routing and the silent-commit protocol.
 */
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    enum class InputSource { file, testRiff, hardware };
    NoiseGateParameters gateParameters;
    CompressorParameters compressorParameters;
    DistortionParameters parameters;
    AmpParameters ampParameters;
    CabinetParameters cabinetParameters;
    EqualizerParameters eqParameters;
    ChorusParameters chorusParameters;
    DelayParameters delayParameters;
    ReverbParameters reverbParameters;
    std::atomic<InputSource> inputSource { InputSource::testRiff };
    VirtualInput fileInput, testInput;
    VirtualInput phraseInput;
    DryPhraseRecorder phraseRecorder;
    // Serialize changes with the callback lock. Audition switching fades the old
    // source out before changing to the phrase, or back to the original input.
    void requestPhraseAudition(bool);
    bool isPhraseAuditioning() const { return phraseAudition.load(); }
    bool phraseSwitchPending() const { return pendingPhraseAudition.load() >= 0; }
    // Session controls: intentionally excluded from tone presets.
    std::atomic<bool> masterMute { false }, rackBypass { false };
    std::atomic<bool> tunerEnabled { false }, tunerMute { true };
    InputMonitor inputMonitor;
    std::atomic<unsigned> deviceGeneration { 0 };
    std::atomic<bool> inputClipped { false }, outputClipped { false };
    std::atomic<bool> monitoring { false };
    std::atomic<int> inputChannel { 0 };
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
    std::atomic<float> outputUnclampedPeak { 0.0f };
    // Message-thread rack commits occur only once this fade has reached silence.
    // Call these while holding the audio callback lock.
    void beginRackUpdate() { rackUpdateRequested = true; }
    bool rackUpdateIsSilent() const { return blockCapacity == 0 || monitorGain.getCurrentValue() <= 0 || orderGain.getCurrentValue() <= 0; }
    void endRackUpdate() { rackUpdateRequested = false; }
    std::atomic<int> effectLatency { 0 };
    std::atomic<bool> tailPlaying { false };

    AudioEngine() : gate(gateParameters), compressor(compressorParameters), distortion("distortion", "Distortion", parameters),
        amp("amp", "Amp", ampParameters), cabinet("cabinet", "Cabinet", cabinetParameters), equalizer("eq", "EQ", eqParameters),
        chorus(chorusParameters), delay(delayParameters), reverb(reverbParameters)
    {
        testInput.setClip(makeTestRiff());
        rack.add(gate);
        rack.add(compressor);
        rack.add(distortion);
        rack.add(amp);
        rack.add(cabinet);
        rack.add(equalizer);
        rack.add(chorus);
        rack.add(delay);
        rack.add(reverb);
        std::copy(rack.stages().begin(), rack.stages().end(), activeStages.begin());
    }
    /// Install the same immutable IR into independent L/R convolvers.
    /// Call off the audio thread, with callbacks excluded.
    void setCabinetImpulse(std::shared_ptr<const CabinetImpulse> impulse)
    {
        cabinet.channel(0).setImpulse(impulse);
        cabinet.channel(1).setImpulse(std::move(impulse));
    }
    juce::String cabinetSourcePath() const { return cabinet.channel(0).sourcePath(); }
    // Caller holds the callback lock; retain the exact currently loaded response.
    std::shared_ptr<const CabinetImpulse> cabinetImpulse() const { return cabinet.channel(0).currentImpulse(); }
    // Caller holds the audio callback lock. Desired order changes immediately;
    // audible processing switches only after its fade reaches silence.
    bool moveStage(const juce::String& stageId, int destinationIndex);
    bool setStageOrder(const RackOrder&);
    float gateReductionDb() const noexcept { return gate.gainReductionDb(); }
    float compressionReductionDb() const noexcept { return compressor.gainReductionDb(); }
    const RackStageChain& stageChain() const noexcept { return rack; }
    /// Allocate/reset DSP for a device configuration while callbacks are stopped.
    /// Offline renderers use this before invoking the callback without a device.
    void prepare(double sampleRate, int maximumBlockSize);
    // Serialize transport commands with the manager's callback lock.
    void seekPlayback(double seconds);
    void stopPlayback();
    void cancelPlaybackSeek() noexcept { pendingSeek.store(-1.0); }
    void resumePlayback();
    double playbackPosition() const;
    void clearEffectTails() noexcept { resetSpatial.store(true); }
    VirtualInput* currentVirtualInput();
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                          const juce::AudioIODeviceCallbackContext&) override;

private:
    std::atomic<bool> phraseAudition { false };
    std::atomic<int> pendingPhraseAudition { -1 };
    void finishPhraseSwitch();
    NoiseGate gate;
    Compressor compressor;
    ChannelWiseRackStage<Distortion> distortion;
    ChannelWiseRackStage<Amp> amp;
    ChannelWiseRackStage<Cabinet> cabinet;
    ChannelWiseRackStage<Equalizer> equalizer;
    Chorus chorus;
    Delay delay;
    Reverb reverb;
    RackStageChain rack;
    std::array<RackStage*, rackStageIds.size()> activeStages {};
    bool rackChangePending = false;
    bool rackUpdateRequested = false;
    juce::SmoothedValue<float> orderGain;   
    void commitRackOrder();
    void queueRackChange();
    juce::AudioBuffer<float> mono;
    juce::AudioBuffer<float> stereo, dry;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> masterDryDelay { 512 };
    juce::SmoothedValue<float> dryInputGain, rackWet, muteGain;
    std::atomic<double> pendingSeek { -1.0 };
    void resetSignalHistory();
    bool anySpatialEnabled() const noexcept;
    double quietWindowSeconds() const noexcept;
    void finishSeek(VirtualInput&);
    juce::SmoothedValue<float> monitorGain;
    juce::SmoothedValue<float> outputGain;
    int blockCapacity = 0;
    double deviceRate = 48000.0;
    bool draining = false, clearedWhileMuted = true;
    int tailSamplesRemaining = 0, quietSamples = 0;
    std::atomic<bool> resetSpatial { false };
};
}
