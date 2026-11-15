#pragma once

#include "RackStage.h"

namespace effecthack
{
class StereoRackStage : public RackStage
{
public:
    void process(juce::AudioBuffer<float>& stereo, int count) noexcept final { processStereo(stereo, count); }

protected:
    virtual void processStereo(juce::AudioBuffer<float>& stereo, int count) noexcept = 0;
};
}
