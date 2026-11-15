#include "../PresetManager.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace effecthack;
void testInputToolsEngine();
namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
struct Harness
{
    AudioEngine engine;
    juce::AudioBuffer<float> input { 1, 128 }, output { 2, 128 };
    explicit Harness(double rate)
    {
        input.clear(); output.clear(); engine.inputSource = AudioEngine::InputSource::hardware;
        engine.parameters.outputDb = -12; // Exercise actual attenuation, independently of the 0 dB default.
        engine.rackBypass = true; engine.prepare(rate, 128); engine.monitoring = true;
    }
    void render(int blocks = 1)
    {
        while (blocks-- > 0)
        {
            engine.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 1,
                output.getArrayOfWritePointers(), 2, 128, {});
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < 128; ++i)
                    require(std::isfinite(output.getSample(channel, i)) && std::abs(output.getSample(channel, i)) <= 1,
                        "Master output must remain finite and bounded");
        }
    }
};
}
int main()
{
    juce::MessageManager::getInstance();
    int result = 0;
    try
    {
        testInputToolsEngine();
        for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0 })
        {
            Harness h(rate);
            h.render(40);
            h.input.setSample(0, 0, .5f); h.render();
            int peakIndex = 0;
            for (int i = 1; i < 128; ++i)
                if (std::abs(h.output.getSample(0, i)) > std::abs(h.output.getSample(0, peakIndex))) peakIndex = i;
            require(peakIndex == h.engine.effectLatency.load(), "Whole-rack dry path must preserve the rack's processing latency");
            require(std::abs(h.output.getSample(0, peakIndex) - .5f * juce::Decibels::decibelsToGain(-12.0f)) < .00001f,
                "Master output gain must apply while rack is bypassed");
            std::fill_n(h.input.getWritePointer(0), 128, .125f);
            h.engine.parameters.inputDb = 6;
            h.engine.chorusParameters.bypass = false;
            h.engine.delayParameters.bypass = false;
            h.engine.reverbParameters.bypass = false;
            h.render(40);
            const float expected = .125f * juce::Decibels::decibelsToGain(-6.0f);
            require(std::abs(h.output.getSample(0, 127) - expected) < .00001f
                && h.output.getSample(0, 127) == h.output.getSample(1, 127),
                "Whole-rack bypass must skip every effect while keeping input/output gain");
            const auto tone = captureRack(h.engine);
            h.engine.rackBypass = false;
            auto previous = h.output.getSample(0, 127);
            float largestStep = 0;
            for (int block = 0; block < 50; ++block)
            {
                h.render();
                for (int i = 0; i < 128; ++i)
                {
                    largestStep = juce::jmax(largestStep, std::abs(h.output.getSample(0, i) - previous));
                    previous = h.output.getSample(0, i);
                }
            }
            require(largestStep < .05f, "Rack bypass transition must crossfade rather than jump");
            h.engine.rackBypass = true; h.render(40);
            require(captureRack(h.engine).matches(tone), "Master bypass must not alter effect settings");
            h.engine.masterMute = true; h.render(40);
            require(h.output.getMagnitude(0, 128) == 0 && h.engine.monitoring, "Mute must silence without stopping monitoring");
            h.engine.masterMute = false; h.render(40);
            require(std::abs(h.output.getSample(0, 127) - expected) < .00001f, "Unmute must restore output");
            h.engine.parameters.inputDb = 24; h.engine.parameters.outputDb = 0;
            std::fill_n(h.input.getWritePointer(0), 128, .5f); h.render(40);
            require(h.engine.outputClipped && !h.engine.inputClipped, "Output overload must be detected before clamping");
            require(h.output.getSample(0, 127) == 1, "Output overload must be clamped");
            h.input.clear(); h.render(40);
            require(h.engine.outputClipped, "Brief output clipping must latch until cleared");
            h.engine.outputClipped = false; h.render();
            require(!h.engine.outputClipped, "Clearing the clip latch must work");
            h.input.setSample(0, 0, 1.1f); h.render(); h.input.clear(); h.render(40);
            require(h.engine.inputClipped, "Brief input clipping must latch");
        }
        Harness h(48000);
        h.engine.inputSource = AudioEngine::InputSource::testRiff;
        h.engine.seekPlayback(1); h.render(40);
        const auto initialPosition = h.engine.playbackPosition();
        h.engine.masterMute = true; h.render(60);
        require(h.engine.playbackPosition() > initialPosition + .1 && h.engine.monitoring
            && h.output.getMagnitude(0, 128) == 0, "Muted file playback must keep advancing");
        h.engine.seekPlayback(5); h.render(40);
        require(h.engine.playbackPosition() >= 5 && h.engine.playbackPosition() < 5.2 && h.engine.monitoring,
                "Seeking while playing must continue at the requested position");
        h.engine.stopPlayback(); h.render(40);
        require(!h.engine.monitoring && h.engine.playbackPosition() == 0
            && h.output.getMagnitude(0, 128) == 0, "Stop must fade, rewind, and remain paused");
        h.render(100);
        require(h.engine.playbackPosition() == 0, "Stopped transport must not drift after rewinding");
        h.engine.seekPlayback(3); h.render(40);
        require(!h.engine.monitoring && h.engine.playbackPosition() == 3, "Paused seek must not start playback");
        h.engine.seekPlayback(std::numeric_limits<double>::quiet_NaN());
        require(h.engine.playbackPosition() == 3, "Invalid seek values must be ignored");
        h.engine.seekPlayback(-20); require(h.engine.playbackPosition() == 0, "Seek must clamp to the start");
        h.engine.testInput.setLooping(false);
        h.engine.seekPlayback(200); require(h.engine.playbackPosition() == 8, "Seek must clamp to the duration");
        h.engine.resumePlayback(); h.render(40);
        require(h.engine.monitoring && h.engine.playbackPosition() > 0 && h.engine.playbackPosition() < .2,
                "Play at EOF must restart from the beginning");
        h.engine.seekPlayback(0); h.render(40);
        require(h.engine.monitoring && h.engine.playbackPosition() < .2, "Restart must preserve playing state");
        h.engine.masterMute = true; h.engine.rackBypass = true;
        const auto loaded = applyPreset(h.engine, preparePreset(RackState {}));
        require(loaded.wasOk() && h.engine.masterMute && h.engine.rackBypass,
                "Preset loading must preserve session mute and master bypass");

        // A delayed echo must survive a seek only if it was generated after that seek.
        Harness echo(48000);
        auto clip = std::make_shared<AudioClip>();
        clip->samples.setSize(1, 48000); clip->samples.clear(); clip->samples.setSample(0, 4096, .5f);
        clip->sampleRate = 48000;
        echo.engine.fileInput.setClip(clip); echo.engine.fileInput.setLooping(false);
        echo.engine.inputSource = AudioEngine::InputSource::file;
        echo.engine.rackBypass = false;
        echo.engine.parameters.bypass = true; echo.engine.cabinetParameters.bypass = true;
        echo.engine.delayParameters.bypass = false; echo.engine.delayParameters.mix = 1;
        echo.engine.delayParameters.timeMs = 300; echo.engine.delayParameters.feedback = .8f;
        echo.render(40);
        echo.engine.seekPlayback(.6);
        float oldEcho = 0;
        for (int i = 0; i < 140; ++i)
        {
            echo.render();
            if (i > 16) oldEcho = juce::jmax(oldEcho, echo.output.getMagnitude(0, 128));
        }
        require(oldEcho == 0, "Seeking must clear echoes from the previous position");
        Harness tails(48000);
        auto shortClip = std::make_shared<AudioClip>();
        shortClip->samples.setSize(1, 4800); shortClip->samples.clear();
        shortClip->samples.setSample(0, 2048, .5f); shortClip->sampleRate = 48000;
        tails.engine.fileInput.setClip(shortClip); tails.engine.fileInput.setLooping(false);
        tails.engine.inputSource = AudioEngine::InputSource::file;
        tails.engine.rackBypass = false; tails.engine.masterMute = true;
        tails.engine.parameters.bypass = true; tails.engine.cabinetParameters.bypass = true;
        tails.engine.delayParameters.bypass = false; tails.engine.delayParameters.mix = 1;
        tails.engine.delayParameters.timeMs = 150; tails.engine.delayParameters.feedback = .8f;
        tails.render(250);
        require(tails.engine.monitoring && tails.engine.tailPlaying && tails.output.getMagnitude(0, 128) == 0,
                "Mute must not prematurely end a file's decaying effect tails");
        tails.engine.masterMute = false;
        float recoveredTail = 0;
        for (int i = 0; i < 100; ++i)
        {
            tails.render(); recoveredTail = juce::jmax(recoveredTail, tails.output.getMagnitude(0, 128));
        }
        require(recoveredTail > .001f, "Unmute must reveal the continuing tail");
        h.engine.stopPlayback(); h.engine.resumePlayback(); h.render(40);
        require(h.engine.monitoring && h.engine.playbackPosition() > 0 && h.engine.playbackPosition() < .2,
                "An immediate Play after Stop must still rewind before playing");
        std::cout << "Master gain/bypass alignment, fades, mute, clip latches, preset independence, seek/restart/stop checks passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    juce::DeletedAtShutdown::deleteAll(); juce::MessageManager::deleteInstance();
    return result;
}
