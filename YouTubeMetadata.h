#pragma once
#include "HttpClient.h"

namespace effecthack
{
struct YouTubeVideo { juce::String id, url, title, channel, error; };
YouTubeVideo parseYouTubeLink(const juce::String&);
YouTubeVideo resolveYouTube(const juce::String&, const HttpTransport&, const Cancellation&);
}
