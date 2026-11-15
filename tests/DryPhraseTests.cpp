#include "../AudioEngine.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
void testDryPhrase()
{
    using namespace effecthack;
    require(AudioEngine().parameters.outputDb.load() == 0, "Fresh Master Output must default to 0 dB");
    require(!DryPhraseRecorder::allocate(0) && !DryPhraseRecorder::allocate(std::numeric_limits<double>::infinity()), "Invalid capture rates rejected");
    DryPhraseRecorder limited;
    limited.arm(DryPhraseRecorder::allocate(8000));
    juce::AudioBuffer<float> block(1, 1024);
    std::fill_n(block.getWritePointer(0), 1024, .2f);
    for (int i = 0; i < 300; ++i) limited.process(block.getReadPointer(0), 1024, true);
    require(limited.state() == DryPhraseRecorder::State::ready && limited.length() == 240000, "Capture stops at 30 seconds without overrunning storage");
    const auto full = DryPhraseRecorder::finish(limited.take(), limited.length());
    require(full.clip != nullptr && std::abs(full.clip->duration() - 30) < .001, "Capacity-limited take remains usable");
    auto silent = DryPhraseRecorder::allocate(48000);
    require(DryPhraseRecorder::finish(silent, 24000).error.isNotEmpty() && DryPhraseRecorder::finish(silent, 100).error.isNotEmpty(), "Silent and short recordings rejected");
    for (const auto rate : {44100.0, 48000.0, 96000.0})
    {
        AudioEngine engine;
        engine.inputSource = AudioEngine::InputSource::hardware; engine.inputChannel = 1;
        engine.parameters.inputDb = -18; engine.masterMute = true; engine.rackBypass = true;
        engine.prepare(rate, 128); engine.monitoring = true;
        engine.phraseRecorder.arm(DryPhraseRecorder::allocate(rate));
        juce::AudioBuffer<float> input(2, 128), output(2, 128);
        const auto blocks = static_cast<int>(std::ceil(rate * .35 / 128));
        int frame = 0;
        const auto render = [&](int count)
        {
            for (int n = 0; n < count; ++n)
            {
                for (int i = 0; i < 128; ++i)
                {
                    input.setSample(0, i, .8f);
                    const auto value = frame + i == 1000 ? std::numeric_limits<float>::quiet_NaN()
                        : .2f * std::sin(static_cast<float>((frame + i) * 440 * juce::MathConstants<double>::twoPi / rate));
                    input.setSample(1, i, value);
                }
                engine.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 128, {});
                frame += 128;
            }
        };
        render(blocks);
        require(output.getMagnitude(0, 128) < .00001f, "Master mute does not prevent dry capture");
        engine.phraseRecorder.stop();
        const auto samples = engine.phraseRecorder.length();
        auto phrase = DryPhraseRecorder::finish(engine.phraseRecorder.take(), samples);
        require(phrase.clip != nullptr && phrase.clip->sampleRate == rate && samples == blocks * 128, "Capture retains original rate and length");
        for (int i = 500; i < samples - 500; ++i)
        {
            const auto expected = i == 1000 ? 0 : .2f * std::sin(static_cast<float>(i * 440 * juce::MathConstants<double>::twoPi / rate));
            require(std::abs(phrase.clip->samples.getSample(0, i) - expected) < .000001f, "Selected channel recorded before Input trim/effects/mute, with invalid samples sanitized");
        }
        require(std::abs(phrase.clip->samples.getSample(0, 0)) < .000001f && std::abs(phrase.clip->samples.getSample(0, samples - 1)) < .002f, "Loop edges fade briefly");
        engine.phraseInput.setClip(phrase.clip); engine.phraseInput.setLooping(true);
        engine.masterMute = false; engine.parameters.inputDb = 0;
        engine.requestPhraseAudition(true);
        require(engine.phraseSwitchPending() && !engine.isPhraseAuditioning(), "Audition waits for fade-down");
        render(100);
        require(engine.isPhraseAuditioning() && engine.currentVirtualInput() == &engine.phraseInput
            && output.getMagnitude(0, 128) > .01f && output.getMagnitude(0, 128) < .25f, "Audition reprocesses the phrase without mixing live hardware");
        render(blocks * 3);
        require(!engine.phraseInput.isFinished() && engine.phraseInput.position() < phrase.clip->duration(), "Audition repeats continuously");
        engine.phraseRecorder.arm(DryPhraseRecorder::allocate(rate)); render(10);
        require(engine.phraseRecorder.length() == 0, "A phrase never records itself while auditioning");
        engine.phraseRecorder.cancel(); engine.monitoring = false; render(20);
        const auto paused = engine.phraseInput.position(); render(20);
        require(std::abs(paused - engine.phraseInput.position()) < .00001 && output.getMagnitude(0, 128) == 0, "Pause freezes audition after the fade");
        engine.requestPhraseAudition(false);
        require(!engine.isPhraseAuditioning() && engine.currentVirtualInput() == nullptr, "Live returns to the original hardware source");
        engine.monitoring = true; render(30);
        require(output.getMagnitude(0, 128) > .01f, "Live guitar resumes");
        engine.requestPhraseAudition(true); render(30); engine.prepare(32000, 128);
        require(!engine.monitoring && engine.phraseInput.hasClip() && engine.phraseInput.getClip()->sampleRate == rate, "Device changes pause but preserve the recording rate");
        engine.monitoring = true; render(100);
        require(output.getMagnitude(0, 128) > .01f, "Phrase replays at another device rate");
    }
    AudioEngine engine;
    engine.prepare(48000, 128); engine.monitoring = true; engine.testInput.setLooping(false);
    const auto* original = engine.testInput.getClip();
    engine.phraseRecorder.arm(DryPhraseRecorder::allocate(48000));
    juce::AudioBuffer<float> output(2, 128);
    auto render = [&](int count)
    { for (int i = 0; i < count; ++i) engine.audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(), 2, 128, {}); };
    render(200); engine.phraseRecorder.stop();
    const auto count = engine.phraseRecorder.length();
    const auto phrase = DryPhraseRecorder::finish(engine.phraseRecorder.take(), count);
    require(phrase.clip != nullptr, "Test riff simulates recording without a guitar");
    engine.phraseInput.setClip(phrase.clip); engine.requestPhraseAudition(true); render(30);
    const auto position = engine.testInput.position(); render(30);
    require(engine.testInput.getClip() == original && std::abs(engine.testInput.position() - position) < .00001
        && !engine.testInput.isLooping(), "Audition leaves the original source and loop preference intact");
    engine.requestPhraseAudition(false); render(30);
    require(engine.testInput.position() > position, "Original playback resumes from its previous position");
    engine.phraseRecorder.arm(DryPhraseRecorder::allocate(48000)); render(100);
    const auto captured = engine.phraseRecorder.length(); engine.audioDeviceStopped();
    require(engine.phraseRecorder.state() == DryPhraseRecorder::State::ready && engine.phraseRecorder.length() == captured
        && !engine.monitoring, "Device stop ends recording before the rate can change");
}
