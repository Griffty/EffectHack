#include "../AudioEngine.h"
#include <cmath>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}
void testInputToolsEngine()
{
    using namespace effecthack;
    AudioEngine engine;
    engine.inputSource = AudioEngine::InputSource::hardware;
    engine.tunerEnabled = true; engine.prepare(48000, 128);
    juce::AudioBuffer<float> in(1, 128), out(2, 128);
    for (int i = 0; i < 128; ++i) in.setSample(0, i, .2f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * i / 64)));
    auto render = [&](int count)
    {
        for (int i = 0; i < count; ++i)
            engine.audioDeviceIOCallbackWithContext(in.getArrayOfReadPointers(), 1, out.getArrayOfWritePointers(), 2, 128, {});
    };
    render(80);
    require(!engine.monitoring && engine.inputMonitor.levels().rawPeak > .19f, "Hardware levels and tuning must work while monitoring is paused");
    require(out.getMagnitude(0, 128) == 0, "Paused input analysis must never enable the output");
    engine.monitoring = true; engine.parameters.inputDb = 6; render(60);
    require(out.getMagnitude(0, 128) == 0 && engine.inputMonitor.levels().rawPeak > .19f
        && engine.inputMonitor.levels().trimmedPeak > .39f, "Tuning mute must silence the rack while retaining raw and trimmed readings");
    engine.tunerMute = false; render(60);
    require(out.getMagnitude(0, 128) > .001f, "Disabling tuning mute must allow monitoring");
    engine.masterMute = true; engine.tunerEnabled = false; render(60);
    require(out.getMagnitude(0, 128) == 0 && engine.masterMute.load(), "Leaving the tuner must preserve Master Mute");
    engine.inputSource = AudioEngine::InputSource::testRiff; engine.monitoring = false; render(30);
    require(!engine.inputMonitor.active() && engine.inputMonitor.levels().rawPeak == 0, "Paused virtual sources must not retain old hardware readings");
    engine.audioDeviceStopped(); require(!engine.inputMonitor.active(), "Device stop must clear analysis activity");
}
