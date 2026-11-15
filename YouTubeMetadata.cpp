#include "YouTubeMetadata.h"

namespace effecthack
{
YouTubeVideo parseYouTubeLink(const juce::String& text)
{
    const auto invalid = [] { return YouTubeVideo { {}, {}, {}, {}, "Enter a YouTube video link." }; };
    auto input = text.trim();
    if (input.isEmpty() || input.length() > 2048 || input.containsAnyOf("\r\n\t ")) return invalid();
    if (!input.contains("://")) input = "https://" + input;
    const juce::URL url(input);
    if (url.getScheme() != "https" && url.getScheme() != "http") return invalid();
    const auto authority = input.fromFirstOccurrenceOf("://", false, false).upToFirstOccurrenceOf("/", false, false)
        .upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false).toLowerCase();
    const bool shortened = authority == "youtu.be" || authority == "www.youtu.be";
    if (!shortened && authority != "youtube.com" && authority != "www.youtube.com" && authority != "m.youtube.com"
        && authority != "music.youtube.com" && authority != "www.youtube-nocookie.com" && authority != "youtube-nocookie.com") return invalid();
    auto path = url.getSubPath().trimCharactersAtStart("/").trimCharactersAtEnd("/");
    juce::String id;
    if (shortened) id = path;
    else if (path == "watch")
    {
        const auto names = url.getParameterNames(), values = url.getParameterValues();
        for (int i = 0; i < names.size(); ++i)
            if (names[i] == "v") { if (id.isNotEmpty()) return invalid(); id = values[i]; }
    }
    else
        for (const auto* prefix : { "shorts/", "embed/", "live/" })
            if (path.startsWith(prefix)) id = path.substring(static_cast<int>(std::char_traits<char>::length(prefix)));
    if (id.length() != 11 || !id.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")) return invalid();
    return { id, "https://www.youtube.com/watch?v=" + id, {}, {}, {} };
}
YouTubeVideo resolveYouTube(const juce::String& link, const HttpTransport& transport, const Cancellation& cancel)
{
    auto video = parseYouTubeLink(link);
    if (video.error.isNotEmpty() || cancel->load()) return video;
    const auto endpoint = juce::URL("https://www.youtube.com/oembed").withParameter("url", video.url).withParameter("format", "json");
    const auto response = transport({ endpoint.toString(true), {}, {} }, cancel);
    if (cancel->load()) { video.error = "Cancelled."; return video; }
    if (response.error.isNotEmpty()) { video.error = response.error; return video; }
    if (response.status != 200) { video.error = "YouTube metadata is unavailable. Try a public video link."; return video; }
    juce::var metadata;
    if (juce::JSON::parse(response.body, metadata).failed() || !metadata.isObject() || !metadata["title"].isString()
        || metadata["title"].toString().trim().isEmpty() || metadata["title"].toString().length() > 1024)
    { video.error = "YouTube returned invalid video metadata."; return video; }
    video.title = metadata["title"].toString().replaceCharacters("\r\n\t", "   ").trim();
    video.channel = metadata["author_name"].isString() ? metadata["author_name"].toString().substring(0, 256) : juce::String();
    return video;
}
}
