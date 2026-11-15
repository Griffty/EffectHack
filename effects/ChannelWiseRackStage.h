#pragma once
#include "RackStage.h"

namespace effecthack
{
// Two mono processors share parameters but never share filter/delay/convolution history.
// Scratch memory is allocated only in prepare().
template<class Processor>
class ChannelWiseRackStage final : public RackStage
{
public:
    template<class Parameters>
    ChannelWiseRackStage(const char* identifier, const char* displayName, Parameters& parameters)
        : stageId(identifier), stageName(displayName), left(parameters), right(parameters) {}
    const char* id() const noexcept override { return stageId; }
    const char* name() const noexcept override { return stageName; }
    Processor& channel(int index) noexcept { return index == 0 ? left : right; }
    const Processor& channel(int index) const noexcept { return index == 0 ? left : right; }
    void prepare(double rate, int blockSize) override
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            scratch[static_cast<size_t>(ch)].setSize(1, blockSize);
            channel(ch).prepare(rate, blockSize);
        }
    }
    void process(juce::AudioBuffer<float>& audio, int count) noexcept override
    {
        jassert(audio.getNumChannels() == 2);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& buffer = scratch[static_cast<size_t>(ch)];
            buffer.copyFrom(0, 0, audio, ch, 0, count);
            channel(ch).processRackChannel(buffer, count);
            audio.copyFrom(ch, 0, buffer, 0, 0, count);
        }
    }
    void reset() noexcept override { left.reset(); right.reset(); }
    int latencySamples() const noexcept override { return left.latencySamples(); }
private:
    const char* stageId;
    const char* stageName;
    Processor left, right;
    std::array<juce::AudioBuffer<float>, 2> scratch;
};
}
