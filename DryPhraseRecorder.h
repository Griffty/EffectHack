#pragma once
#include "VirtualInput.h"
#include <array>
#include <atomic>

namespace effecthack
{
struct DryPhrase
{
    std::shared_ptr<AudioClip> clip;
    std::array<float, 128> peaks {};
    juce::String error;
};
// Allocation/finalization belong to the UI/worker. The callback only copies
// sanitized mono samples into an already allocated, resident buffer.
class DryPhraseRecorder
{
public:
    enum class State { idle, armed, recording, ready };
    static constexpr double maximumSeconds = 30, minimumSeconds = .25;
    static std::shared_ptr<AudioClip> allocate(double sampleRate);
    static DryPhrase finish(std::shared_ptr<AudioClip>, int samples);
    // Serialize mutation with the audio callback lock.
    void arm(std::shared_ptr<AudioClip>);
    void stop() noexcept;
    void cancel();
    std::shared_ptr<AudioClip> take();
    void process(const float* dry, int samples, bool canCapture) noexcept;
    State state() const { return status.load(std::memory_order_acquire); }
    bool capturing() const { const auto s = state(); return s == State::armed || s == State::recording; }
    int length() const { return publishedLength.load(std::memory_order_relaxed); }
    double duration(double sampleRate) const { return sampleRate > 0 ? length() / sampleRate : 0; }
private:
    std::shared_ptr<AudioClip> buffer;
    std::atomic<State> status { State::idle };
    std::atomic<int> publishedLength { 0 };
    int cursor = 0;
};
}
