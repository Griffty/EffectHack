#include "HttpClient.h"
#include <curl/curl.h>

namespace effecthack
{
namespace
{
constexpr size_t limit = 256 * 1024;
struct ResponseBuffer { std::string text; bool oversized = false; };
size_t receive(char* bytes, size_t size, size_t count, void* context) noexcept
{
    auto& response = *static_cast<ResponseBuffer*>(context);
    if (size != 0 && count > limit / size) { response.oversized = true; return 0; }
    const auto total = size * count;
    if (total > limit - response.text.size()) { response.oversized = true; return 0; }
    try { response.text.append(bytes, total); return total; }
    catch (...) { return 0; }
}
int progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept
{
    return static_cast<std::atomic<bool>*>(context)->load() ? 1 : 0;
}
}
HttpResponse performHttp(const HttpRequest& request, const Cancellation& cancel)
{
    if (cancel->load()) return { 0, {}, "Cancelled." };
    // Destinations are fixed by the two services, never taken from a supplied link.
    if (!request.url.startsWith("https://www.youtube.com/oembed?")
        && !request.url.startsWith("https://generativelanguage.googleapis.com/v1beta/models/"))
        return { 0, {}, "Unsupported network destination." };
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) return { 0, {}, "Could not initialize HTTPS." };
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (curl == nullptr) return { 0, {}, "Could not initialize HTTPS." };
    const auto url = request.url.toStdString(), body = request.body.toStdString();
    curl_slist* headers = nullptr;
    for (const auto& header : request.headers)
    {
        auto* next = curl_slist_append(headers, header.toRawUTF8());
        if (next == nullptr) { curl_slist_free_all(headers); return { 0, {}, "Could not prepare HTTPS headers." }; }
        headers = next;
    }
    const std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> ownedHeaders(headers, curl_slist_free_all);
    ResponseBuffer response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L); // Never redirect an API key.
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, static_cast<long>(juce::jlimit(1000, 180000, request.timeoutMs)));
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "EffectHack/0.1");
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, cancel.get());
    if (request.body.isNotEmpty())
    {
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    const auto result = curl_easy_perform(curl.get());
    if (cancel->load()) return { 0, {}, "Cancelled." };
    if (response.oversized) return { 0, {}, "Service response exceeded the size limit." };
    if (result != CURLE_OK) return { 0, {}, result == CURLE_OPERATION_TIMEDOUT ? "Request timed out. Try again." : "HTTPS request failed. Check the connection." };
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    return { static_cast<int>(status), juce::String::fromUTF8(response.text.data(), static_cast<int>(response.text.size())), {} };
}
}
