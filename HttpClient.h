#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace effecthack
{
/// Shared cooperative stop flag. Keep it non-null for requests and worker jobs;
/// setting it does not itself commit, roll back, or change the audio engine.
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
/// Blocking injectable HTTPS boundary. Production uses libcurl; tests supply
/// deterministic responses. Request headers may contain credentials: do not log them.
using HttpTransport = std::function<HttpResponse(const HttpRequest&, const Cancellation&)>;
HttpResponse performHttp(const HttpRequest&, const Cancellation&);
}
