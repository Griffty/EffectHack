#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../RackDefinition.h"
#include <vector>

namespace effecthack
{
/** Processing contract for one rack entry.
 * prepare() may allocate and must precede process(). process() works in-place on
 * two channels, with count <= the prepared capacity; it must not block or allocate.
 * reset() clears history without changing controls. Report processing latency even
 * when bypassed so dry/wet and whole-rack bypass remain aligned.
 */
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

/// Non-owning, ordered stage registry. Stage lifetimes must outlast the chain.
/// Mutating the desired order is serialized by AudioEngine; its callback uses a
/// separate active-stage snapshot until the transition fade reaches silence.
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
