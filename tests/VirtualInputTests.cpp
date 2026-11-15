#include "../AudioEngine.h"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
void testDryPhrase();

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void callback(effecthack::AudioEngine& engine, juce::AudioBuffer<float>& output, int count = 1)
{
    for (int i = 0; i < count; ++i)
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(),
            output.getNumChannels(), output.getNumSamples(), {});
}
}

int main(int argc, char** argv)
{
    juce::MessageManager::getInstance();
    int result = 0;
    try
    {
        require(argc == 3 || argc == 4, "Pass a scratch directory, MP3 fixture, and optional audio file");
        testDryPhrase();
        const auto mp3 = effecthack::loadAudioClip(juce::File(argv[2]));
        require(mp3.clip != nullptr && mp3.error.isEmpty(), "MP3 fixture must decode through the production loader");
        require(mp3.clip->sampleRate == 44100.0 && mp3.clip->duration() > 0.2
            && mp3.clip->samples.getMagnitude(0, mp3.clip->samples.getNumSamples()) > 0.01f,
            "MP3 decoding must produce actual audio at the correct rate");
        const auto scratch = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
        require(scratch.createDirectory().wasOk(), "Could not create fixture directory");
        const auto wav = scratch.getChildFile("stereo-44100.wav");
        const auto invalid = scratch.getChildFile("invalid.wav");
        invalid.replaceWithText("This is not audio");
        require(effecthack::loadAudioClip(invalid).clip == nullptr, "Invalid audio must fail cleanly");
        juce::AudioBuffer<float> stereo(2, 4410);
        for (int i = 0; i < stereo.getNumSamples(); ++i)
        {
            const auto wave = static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * i / 44100.0));
            stereo.setSample(0, i, 0.4f * wave);
            stereo.setSample(1, i, 0.2f * wave);
        }
        {
            juce::WavAudioFormat format;
            auto stream = wav.createOutputStream();
            require(stream != nullptr, "Could not write fixture");
            stream->setPosition(0); stream->truncate();
            std::unique_ptr<juce::OutputStream> destination(std::move(stream));
            auto writer = format.createWriterFor(destination, juce::AudioFormatWriterOptions()
                .withSampleRate(44100.0).withNumChannels(2).withBitsPerSample(24));
            require(writer != nullptr && writer->writeFromAudioSampleBuffer(stereo, 0, stereo.getNumSamples()), "Could not write WAV");
        }
        const auto decoded = effecthack::loadAudioClip(wav);
        require(decoded.clip != nullptr && decoded.error.isEmpty(), "WAV must decode");
        require(decoded.clip->samples.getNumChannels() == 1 && std::abs(decoded.clip->duration() - 0.1) < 1.0e-6, "Stereo must become mono with correct duration");
        for (int i = 0; i < 4410; ++i)
            require(std::abs(decoded.clip->samples.getSample(0, i) - 0.75f * stereo.getSample(0, i)) < 1.0e-6f, "Stereo downmix must average channels");
        const auto riff = effecthack::makeTestRiff();
        require(riff->duration() == 8.0 && std::abs(riff->samples.getMagnitude(0, riff->samples.getNumSamples()) - 0.35f) < 1.0e-5f, "Riff must be eight seconds at a bounded level");
        for (int i = 0; i < riff->samples.getNumSamples(); ++i)
            require(std::isfinite(riff->samples.getSample(0, i)), "Generated riff must remain finite");

        effecthack::VirtualInput player;
        player.setClip(decoded.clip); player.setLooping(false); player.prepare(48000.0, 128);
        juce::AudioBuffer<float> mono(1, 128);
        player.render(mono, 128);
        require(std::abs(player.position() - 128.0 / 48000.0) < 0.002, "Playback timing must follow device rate");
        for (int i = 0; i < 60; ++i) player.render(mono, 128);
        require(player.isFinished() && mono.getMagnitude(0, 128) == 0.0f, "EOF must finish in silence");
        player.resume(); player.render(mono, 128);
        require(!player.isFinished() && mono.getMagnitude(0, 128) > 0.01f, "Play after EOF must restart");
        player.setLooping(true); player.seek(0.0);
        for (int i = 0; i < 200; ++i) player.render(mono, 128);
        require(!player.isFinished() && player.position() < 0.1, "Looping must wrap and continue");
        player.seek(0.05);
        require(std::abs(player.position() - 0.05) < 0.001, "Restart/seek must reposition");

        effecthack::AudioEngine engine;
        engine.prepare(48000.0, 128);
        juce::AudioBuffer<float> output(2, 128);
        callback(engine, output);
        require(output.getMagnitude(0, 128) == 0.0f && engine.testInput.position() == 0.0, "Startup must be silent at position zero");
        engine.monitoring = true;
        float peak = 0.0f;
        for (int i = 0; i < 100; ++i)
        {
            callback(engine, output);
            peak = juce::jmax(peak, output.getMagnitude(0, 128));
            for (int j = 0; j < 128; ++j)
                require(output.getSample(0, j) == output.getSample(1, j), "Mono must feed both outputs");
        }
        require(peak > 0.01f && engine.testInput.position() > 0.2, "Virtual guitar must work with zero hardware inputs");
        engine.monitoring = false;
        callback(engine, output, 20);
        const auto pausedAt = engine.testInput.position();
        callback(engine, output, 50);
        require(output.getMagnitude(0, 128) == 0.0f && engine.testInput.position() == pausedAt, "Pause must silence and freeze after the fade");
        engine.testInput.resume(); engine.monitoring = true; callback(engine, output);
        require(engine.testInput.position() > pausedAt, "Resume must continue from paused position");

        engine.monitoring = false;
        engine.inputSource = effecthack::AudioEngine::InputSource::file;
        engine.fileInput.setClip(decoded.clip); engine.fileInput.setLooping(false);
        engine.monitoring = true; callback(engine, output, 100);
        require(!engine.monitoring.load() && output.getMagnitude(0, 128) == 0.0f, "EOF must stop engine playback");

        engine.inputSource = effecthack::AudioEngine::InputSource::hardware;
        engine.cabinetParameters.bypass = true;
        engine.eqParameters.bypass = true;
        engine.inputChannel = 1; engine.parameters.bypass = true; engine.parameters.outputDb = 0.0f;
        engine.monitoring = true;
        juce::AudioBuffer<float> inputs(2, 128);
        std::fill_n(inputs.getWritePointer(0), 128, 0.125f);
        std::fill_n(inputs.getWritePointer(1), 128, 0.375f);
        for (int i = 0; i < 30; ++i)
            engine.audioDeviceIOCallbackWithContext(inputs.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 128, {});
        require(std::abs(output.getSample(0, 127) - 0.375f) < 1.0e-5f, "Physical input channel selection must still work");
        if (argc == 4)
        {
            const auto audio = effecthack::loadAudioClip(juce::File(argv[3]));
            require(audio.clip != nullptr, ("Requested file failed: " + audio.error).toRawUTF8());
            require(audio.clip->samples.getMagnitude(0, audio.clip->samples.getNumSamples()) > 0.001f,
                "Requested recording must contain decoded audio");
            std::cout << "Verified recording: " << audio.clip->name << " | " << audio.clip->duration()
                << " s | " << audio.clip->sampleRate << " Hz | mono into rack\n";
        }
        std::cout << "PASS: MP3 regression, invalid files, stereo WAV decoding/downmix, generated riff, resampling, EOF/replay, loop, restart, output-only playback, pause/resume, physical routing\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n'; result = 1;
    }
    juce::MessageManager::deleteInstance();
    return result;
}
