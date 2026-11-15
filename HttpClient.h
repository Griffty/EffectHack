#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace effecthack
{
using Cancellation = std::shared_ptr<std::atomic<bool>>;
struct HttpRequest
{
    juce::String url, body;
    juce::StringArray headers;
    int timeoutMs = 20000;
};
struct HttpResponse
{
    int status = 0;
    juce::String body, error;
};
using HttpTransport = std::function<HttpResponse(const HttpRequest&, const Cancellation&)>;
HttpResponse performHttp(const HttpRequest&, const Cancellation&);
}
