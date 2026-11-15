#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../RackDefinition.h"
#include <vector>

namespace effecthack
{
class RackStage
{
public:
    virtual ~RackStage() = default;
    virtual const char* id() const noexcept = 0;
    virtual const char* name() const noexcept = 0;
    virtual void prepare(double sampleRate, int maximumBlockSize) = 0;
    // Every stage receives two channels. Channel-wise effects keep independent histories.
    virtual void process(juce::AudioBuffer<float>& stereo, int count) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual int latencySamples() const noexcept { return 0; }
};

class RackStageChain
{
public:
    void add(RackStage& stage);
    bool move(const juce::String& stageId, int destinationIndex);
    bool setOrder(const RackOrder&);
    RackOrder order() const;
    void prepare(double sampleRate, int maximumBlockSize);
    void process(juce::AudioBuffer<float>& stereo, int count) noexcept;
    void reset() noexcept;
    int latencySamples() const noexcept;
    const std::vector<RackStage*>& stages() const noexcept { return rackStages; }

private:
    std::vector<RackStage*> rackStages;
};
}
