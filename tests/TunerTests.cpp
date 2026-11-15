#include "../Tuner.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace effecthack;
void testInputMonitor();
namespace
{
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
double centsError(float actual, double expected) { return actual > 0 ? std::abs(1200 * std::log2(actual / expected)) : 10000; }
}
int main()
{
    try
    {
        std::array<float, 2048> samples;
        for (double frequency : { 41.2034, 61.7354, 73.4162, 82.4069, 110.0, 146.8324, 195.9977, 246.9417, 329.6276, 440.0, 659.2551 })
        {
            for (size_t i = 0; i < samples.size(); ++i)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequency * static_cast<double>(i) / 12000;
                samples[i] = static_cast<float>(.2 * std::sin(phase) + .35 * std::sin(2 * phase) + .12 * std::sin(3 * phase) + .15);
            }
            const auto actual = detectPitch(samples.data(), static_cast<int>(samples.size()), 12000);
            if (centsError(actual, frequency) > 4) std::cerr << "Pitch " << frequency << " -> " << actual << '\n';
            require(centsError(actual, frequency) < 4, "Harmonic-rich guitar-range notes must track their fundamental");
        }
        samples.fill(0); require(detectPitch(samples.data(), 2048, 12000) == 0, "Silence must not produce a note");
        samples.fill(.5f); require(detectPitch(samples.data(), 2048, 12000) == 0, "DC must not produce a note");
        std::mt19937 random(17); std::uniform_real_distribution<float> noise(-.1f, .1f);
        for (auto& sample : samples) sample = noise(random);
        require(detectPitch(samples.data(), 2048, 12000) == 0, "Unpitched noise must not produce a confident note");
        samples[0] = std::numeric_limits<float>::quiet_NaN();
        require(detectPitch(samples.data(), 2048, 12000) == 0, "Invalid audio must not produce a note");
        const auto a = describePitch(440, 440), alternate = describePitch(442, 442), e = describePitch(82.4069f, 440);
        require(a.note == "A4" && std::abs(a.cents) < .001f && alternate.note == "A4" && std::abs(alternate.cents) < .001f
            && e.note == "E2", "Pitch naming and alternate A4 references must agree");
        require(describePitch(435, 440).cents < 0 && describePitch(445, 440).cents > 0, "Cents must indicate flat and sharp correctly");
        require(describePitch(0, 440).note.isEmpty(), "Unknown pitch must not show a note");
        testInputMonitor();
        InputMonitor monitor; monitor.prepare(48000); monitor.beginBlock(true, 0, true);
        {
            Tuner worker(monitor);
            for (int block = 0; block < 28; ++block)
            {
                for (int i = 0; i < 960; ++i)
                {
                    const auto sample = static_cast<float>(.2 * std::sin(juce::MathConstants<double>::twoPi * 110 * (block * 960 + i) / 48000));
                    monitor.push(sample, sample);
                }
                monitor.publish(); juce::Thread::sleep(20);
            }
            require(centsError(worker.frequency(), 110) < 3, "Background tuner must consume the audio FIFO and publish a valid note");
            monitor.beginBlock(true, 1, true);
            require(worker.frequency() == 0, "Changing source/channel must invalidate a stale note immediately");
            monitor.stop(); require(worker.frequency() == 0, "Stopping a device must clear the displayed note");
        }
        std::cout << "Tuner fundamentals, rejection, reference/cents, resampling, levels, status, and worker lifecycle passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
