#include "../AudioEngine.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace effecthack;
namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Processor>
void run(Processor& processor, juce::AudioBuffer<float>& block, double rate, double seconds, float left, float right)
{
    const int blocks = juce::jmax(1, static_cast<int>(std::ceil(seconds * rate / 128)));
    for (int b = 0; b < blocks; ++b)
    {
        std::fill_n(block.getWritePointer(0), 128, left);
        std::fill_n(block.getWritePointer(1), 128, right);
        processor.process(block, 128);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 128; ++i)
                require(std::isfinite(block.getSample(ch, i)), "Effect must produce finite samples");
    }
}
}
void testNoiseGate()
{
    juce::ScopedNoDenormals noDenormals;
    for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0 })
    {
        juce::AudioBuffer<float> block(2, 128);
        NoiseGateParameters gp;
        NoiseGate gate(gp);
        gate.prepare(rate, 128);
        run(gate, block, rate, .1, .25f, -.125f);
        require(block.getSample(0, 127) == .25f && block.getSample(1, 127) == -.125f && gate.gainReductionDb() == 0,
            "Bypassed gate must preserve independent channels with zero latency");
        gp.bypass = false; gp.thresholdDb = -20; gp.attackMs = 1; gp.holdMs = 100; gp.releaseMs = 10;
        gate.prepare(rate, 128);
        run(gate, block, rate, .1, .001f, -.0005f);
        require(block.getMagnitude(0, 128) == 0 && gate.gainReductionDb() > 80, "Closed gate must suppress low-level noise");
        run(gate, block, rate, .1, .2f, -.001f);
        require(std::abs(block.getSample(0, 127) - .2f) < 1.e-6f && std::abs(block.getSample(1, 127) + .001f) < 1.e-6f,
            "Either loud channel must open one shared gate without changing stereo balance");
        run(gate, block, rate, .2, .075f, -.0375f);
        require(std::abs(block.getSample(0, 127) - .075f) < 1.e-6f, "Hysteresis must keep the gate open below its opening threshold");
        run(gate, block, rate, .05, .02f, -.01f);
        require(block.getSample(0, 127) > .019f, "Hold must preserve the end of a note before release starts");
        run(gate, block, rate, .5, .02f, -.01f);
        require(block.getMagnitude(0, 128) < 1.e-7f, "Gate must close after hold and release");
        run(gate, block, rate, .2, .075f, -.0375f);
        require(block.getMagnitude(0, 128) < 1.e-7f, "Closed gate must require the higher threshold to reopen");
        run(gate, block, rate, .1, .2f, -.1f);
        require(std::abs(block.getSample(0, 127) - .2f) < 1.e-6f, "Gate must reopen on a new note");
        gate.reset(); block.clear(); gate.process(block, 128);
        require(block.getMagnitude(0, 128) == 0, "Reset must clear gate detector, hold, and gain history");
        gp.bypass = true; run(gate, block, rate, .05, .001f, -.0005f);
        require(std::abs(block.getSample(0, 127) - .001f) < 1.e-7f, "Gate bypass must fade back to unattenuated input");

        gp.thresholdDb = std::numeric_limits<float>::quiet_NaN();
        run(gate, block, rate, .01, std::numeric_limits<float>::infinity(), .1f);
    }
    AudioEngine engine; engine.inputSource = AudioEngine::InputSource::hardware;
    engine.parameters.bypass = true; engine.cabinetParameters.bypass = true; engine.eqParameters.bypass = true;
    engine.gateParameters.bypass = false;
    engine.prepare(48000, 128); engine.monitoring = true;
    juce::AudioBuffer<float> input(1, 128), output(2, 128);
    std::fill_n(input.getWritePointer(0), 128, 1.e-5f);
    for (int i = 0; i < 50; ++i)
        engine.audioDeviceIOCallbackWithContext(input.getArrayOfReadPointers(), 1, output.getArrayOfWritePointers(), 2, 128, {});
    require(output.getMagnitude(0, 128) == 0 && engine.gateReductionDb() > 80, "The engine must route the signal through the enabled gate");
    std::cout << "Noise gate suppression, hysteresis, hold/reopen, stereo linking, bypass, invalid inputs, reset and engine routing passed\n";
}
