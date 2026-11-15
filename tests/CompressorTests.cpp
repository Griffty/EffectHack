#include "../effects/Compressor.h"
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
void testCompressor()
{
    juce::ScopedNoDenormals noDenormals;
    for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0 })
    {
        juce::AudioBuffer<float> block(2, 128);
        CompressorParameters cp;
        Compressor compressor(cp);
        compressor.prepare(rate, 128);
        run(compressor, block, rate, .1, .25f, -.125f);
        require(block.getSample(0, 127) == .25f && block.getSample(1, 127) == -.125f && compressor.gainReductionDb() == 0,
            "Bypassed compressor must preserve independent channels with zero latency");
        cp.bypass = false; cp.thresholdDb = -18; cp.ratio = 4; cp.attackMs = .1f; cp.releaseMs = 10;
        compressor.prepare(rate, 128);
        run(compressor, block, rate, .1, .5f, -.25f);
        const auto inputDb = juce::Decibels::gainToDecibels(.5f);
        const auto expected = juce::Decibels::decibelsToGain(-18.0f + (inputDb + 18.0f) / 4.0f);
        require(std::abs(block.getSample(0, 127) - expected) < .0002f
            && std::abs(block.getSample(1, 127) + expected * .5f) < .0002f,
            "Compressor steady-state ratio must be correct and stereo linked");
        require(std::abs(compressor.gainReductionDb() - (inputDb - (-18 + (inputDb + 18) / 4))) < .02f,
            "Gain reduction meter must report actual compression before makeup");
        run(compressor, block, rate, .3, .01f, 0);
        require(std::abs(block.getSample(0, 127) - .01f) < 1.e-6f && block.getMagnitude(1, 0, 128) == 0,
            "Compressor must recover below threshold without leaking across channels");
        cp.ratio = 1; compressor.prepare(rate, 128);
        run(compressor, block, rate, .05, .5f, -.25f);
        require(block.getSample(0, 127) == .5f, "A 1:1 ratio and zero makeup must be transparent");
        cp.makeupDb = 6; cp.mix = .5f; compressor.prepare(rate, 128);
        run(compressor, block, rate, .05, .1f, -.05f);
        require(std::abs(block.getSample(0, 127) - .1f * (.5f + .5f * juce::Decibels::decibelsToGain(6.0f))) < 1.e-6f,
            "Makeup gain and dry/wet mix must be applied independently of compression");
        cp.mix = 0; cp.ratio = 12; cp.thresholdDb = -60; compressor.prepare(rate, 128);
        run(compressor, block, rate, .1, .5f, -.25f);
        require(block.getSample(0, 127) == .5f && compressor.gainReductionDb() == 0, "Zero mix must preserve dry input");
        cp.mix = 1; cp.makeupDb = 0; cp.thresholdDb = -18; cp.ratio = 4; cp.attackMs = 20; cp.releaseMs = 200;
        compressor.prepare(rate, 128);
        run(compressor, block, rate, .002, .5f, .25f);
        require(compressor.gainReductionDb() > 0 && compressor.gainReductionDb() < 4, "Attack must let gain reduction build over time");
        run(compressor, block, rate, .3, .5f, .25f);
        require(compressor.gainReductionDb() > 8, "Sustained loud input must settle into compression");
        run(compressor, block, rate, .02, .001f, -.0005f);
        require(compressor.gainReductionDb() > 6, "Release must avoid abruptly restoring full gain");
        run(compressor, block, rate, 2, .001f, -.0005f);
        require(compressor.gainReductionDb() < .01f, "Release must eventually recover");
        compressor.reset(); block.clear(); compressor.process(block, 128);
        require(block.getMagnitude(0, 128) == 0, "Reset must clear compressor history");
        cp.bypass = true; run(compressor, block, rate, .1, .5f, -.25f);
        require(std::abs(block.getSample(0, 127) - .5f) < 1.e-6f, "Compressor bypass must restore full dry level");
        cp.ratio = std::numeric_limits<float>::quiet_NaN(); cp.releaseMs = std::numeric_limits<float>::infinity();
        run(compressor, block, rate, .01, std::numeric_limits<float>::quiet_NaN(), .1f);
    }
    // Exercise both joins of the soft knee, plus its centre, against the static curve.
    for (float over : { -4.0f, -3.0f, -2.0f, 0.0f, 2.0f, 3.0f, 4.0f })
    {
        CompressorParameters p; p.bypass = false; p.attackMs = .1f;
        Compressor compressor(p); compressor.prepare(48000, 128);
        juce::AudioBuffer<float> block(2, 128);
        run(compressor, block, 48000, .1, juce::Decibels::decibelsToGain(-18 + over), 0);
        const auto expected = over <= -3 ? 0.0f : over >= 3 ? over * .75f : .75f * (over + 3) * (over + 3) / 12;
        require(std::abs(compressor.gainReductionDb() - expected) < .02f, "Soft-knee curve must join smoothly and meet the selected ratio");
    }
    std::cout << "Compressor curve, attack/release, mix/makeup, stereo linking, bypass, invalid inputs and resets passed\n";
}
