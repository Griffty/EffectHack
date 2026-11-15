#include "RackStage.h"
#include <algorithm>
#include <iterator>

namespace effecthack
{
void RackStageChain::add(RackStage& stage)
{
    rackStages.push_back(&stage);
}

bool RackStageChain::move(const juce::String& stageId, int destinationIndex)
{
    const auto found = std::find_if(rackStages.begin(), rackStages.end(),
        [&stageId](const auto& stage) { return stageId == stage->id(); });
    if (found == rackStages.end())
        return false;

    const auto sourceIndex = static_cast<int>(std::distance(rackStages.begin(), found));
    if (destinationIndex < 0 || destinationIndex >= static_cast<int>(rackStages.size())) return false;
    if (sourceIndex == destinationIndex)
        return true;

    auto stage = *found;
    rackStages.erase(found);
    rackStages.insert(rackStages.begin() + destinationIndex, stage);
    return true;
}

RackOrder RackStageChain::order() const
{
    RackOrder result;
    jassert(rackStages.size() == result.size());
    for (size_t i = 0; i < juce::jmin(result.size(), rackStages.size()); ++i) result[i] = rackStages[i]->id();
    return result;
}

bool RackStageChain::setOrder(const RackOrder& order)
{
    if (!validRackOrder(order) || rackStages.size() != order.size()) return false;
    std::array<RackStage*, rackStageIds.size()> sorted {};
    for (size_t i = 0; i < order.size(); ++i)
    {
        const auto found = std::find_if(rackStages.begin(), rackStages.end(),
            [&](const auto* stage) { return order[i] == stage->id(); });
        if (found == rackStages.end()) return false;
        sorted[i] = *found;
    }
    std::copy(sorted.begin(), sorted.end(), rackStages.begin());
    return true;
}

void RackStageChain::prepare(double sampleRate, int maximumBlockSize)
{
    for (auto* stage : rackStages)
        stage->prepare(sampleRate, maximumBlockSize);
}

void RackStageChain::process(juce::AudioBuffer<float>& stereo, int count) noexcept
{
    jassert(stereo.getNumChannels() == 2 && count <= stereo.getNumSamples());
    for (auto* stage : rackStages) stage->process(stereo, count);
}

void RackStageChain::reset() noexcept
{
    for (auto* stage : rackStages)
        stage->reset();
}

int RackStageChain::latencySamples() const noexcept
{
    int total = 0;
    for (const auto* stage : rackStages)
        total += stage->latencySamples();
    return total;
}

}
