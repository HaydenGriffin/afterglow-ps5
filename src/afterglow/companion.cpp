// Afterglow - Native LAN Sendspin player, metadata and controller client.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include "afterglow/controls.hpp"
#include "afterglow/ma_socket.hpp"
#include "afterglow/speaker_group.hpp"
#include "afterglow/speakers.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <deque>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <utility>
namespace hui::afterglow
{
namespace
{
constexpr std::size_t kMaxMessage = ma_socket::kMaxMessage;
} // namespace

namespace
{
// Slice large API replies before using the small scalar JSON decoder. This
// keeps deeply populated music libraries out of the UI and bounds work/memory.
std::size_t value_end(std::string_view s, std::size_t at)
{
    bool quoted = false, escape = false;
    int depth = 0;
    for (std::size_t i = at; i < s.size(); ++i)
    {
        const char c = s[i];
        if (quoted)
        {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
            {
                quoted = false;
                if (!depth)
                    return i + 1;
            }
        }
        else if (c == '"')
            quoted = true;
        else if (c == '[' || c == '{')
        {
            if (++depth > 64)
                return s.size();
        }
        else if (c == ']' || c == '}')
        {
            if (!depth)
                return i;
            if (--depth == 0)
                return i + 1;
        }
        else if (!depth && (c == ',' || std::isspace(static_cast<unsigned char>(c))))
            return i;
    }
    return s.size();
}
void skip_space(std::string_view s, std::size_t &at)
{
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
        ++at;
}
std::string_view field(std::string_view s, std::string_view key)
{
    std::size_t at = 0;
    skip_space(s, at);
    if (at == s.size() || s[at++] != '{')
        return {};
    while (at < s.size())
    {
        skip_space(s, at);
        if (at == s.size() || s[at] != '"')
            return {};
        const auto end = value_end(s, at);
        const bool match = s.substr(at + 1, end - at - 2) == key;
        at = end;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ':')
            return {};
        skip_space(s, at);
        const auto finish = value_end(s, at);
        if (match)
            return s.substr(at, finish - at);
        at = finish;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ',')
            return {};
    }
    return {};
}
std::vector<std::string_view> elements(std::string_view s, std::size_t limit = 256)
{
    std::vector<std::string_view> result;
    std::size_t at = 0;
    skip_space(s, at);
    if (at == s.size() || s[at++] != '[')
        return result;
    while (at < s.size() && result.size() < limit)
    {
        skip_space(s, at);
        if (at == s.size() || s[at] == ']')
            break;
        const auto end = value_end(s, at);
        if (end <= at)
            break;
        result.push_back(s.substr(at, end - at));
        at = end;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ',')
            break;
    }
    return result;
}
std::string string_value(std::string_view s)
{
    if (s.empty() || s.size() > 65536)
        return {};
    const std::string object = "{\"v\":" + std::string(s) + "}";
    Json json(object);
    return json.string(json.get(0, "v"));
}
std::string text_field(std::string_view s, std::string_view key)
{
    return string_value(field(s, key));
}
double decimal(std::string_view s, std::string_view key, double fallback = 0)
{
    const auto value = field(s, key);
    if (value.empty())
        return fallback;
    if (value.size() > 64)
        return fallback;
    const std::string text(value);
    char *end = nullptr;
    errno = 0;
    const double decoded = std::strtod(text.c_str(), &end);
    if (errno || end != text.c_str() + text.size() || !std::isfinite(decoded))
        return fallback;
    return std::clamp(decoded, -2147483647.0, 2147483647.0);
}
int number(std::string_view s, std::string_view key, int fallback = 0)
{
    return static_cast<int>(decimal(s, key, fallback));
}
std::string url_encode(std::string_view s)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += digits[c >> 4];
            out += digits[c & 15];
        }
    }
    return out;
}
std::map<std::string, std::string> object_fields(std::string_view raw)
{
    std::map<std::string, std::string> result;
    std::size_t at = 0;
    skip_space(raw, at);
    if (at == raw.size() || raw[at++] != '{')
        return result;
    while (at < raw.size() && result.size() < 8192)
    {
        skip_space(raw, at);
        if (at == raw.size() || raw[at] != '"')
            break;
        auto end = value_end(raw, at);
        auto key = string_value(raw.substr(at, end - at));
        at = end;
        skip_space(raw, at);
        if (at == raw.size() || raw[at++] != ':')
            break;
        skip_space(raw, at);
        end = value_end(raw, at);
        result[key] = std::string(raw.substr(at, end - at));
        at = end;
        skip_space(raw, at);
        if (at == raw.size() || raw[at++] != ',')
            break;
    }
    return result;
}
std::string encode_object(const std::map<std::string, std::string> &fields)
{
    std::string out = "{";
    for (const auto &[key, value] : fields)
    {
        if (out.size() > 1)
            out += ',';
        out += json_quote(key) + ':' + value;
    }
    return out + '}';
}
std::int64_t epoch_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
// Library items list people as objects with a name; provider items (Spotify
// audiobook authors) as plain strings.
std::string join_names(std::string_view list)
{
    std::string out;
    for (auto entry : elements(list, 16))
    {
        const auto name = entry.front() == '"' ? string_value(entry) : text_field(entry, "name");
        if (!name.empty())
            out += (out.empty() ? "" : ", ") + name;
    }
    return out;
}
std::string trimmed(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(0, 1);
    return text;
}
// "2021-09-11T00:00:00+00:00" -> "11 Sep 2021"; anything else is left out.
std::string short_date(std::string_view iso)
{
    constexpr std::array<const char *, 12> months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-')
        return {};
    for (std::size_t i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!std::isdigit(static_cast<unsigned char>(iso[i])))
            return {};
    const int month = (iso[5] - '0') * 10 + (iso[6] - '0');
    const int day = (iso[8] - '0') * 10 + (iso[9] - '0');
    if (month < 1 || month > 12 || day < 1 || day > 31)
        return {};
    return std::to_string(day) + " " + months[static_cast<std::size_t>(month - 1)] + " " +
           std::string(iso.substr(0, 4));
}
std::string short_duration(int seconds)
{
    if (seconds <= 0)
        return {};
    const int minutes = std::max(1, (seconds + 30) / 60);
    if (minutes < 60)
        return std::to_string(minutes) + " min";
    return std::to_string(minutes / 60) + " h" +
           (minutes % 60 ? " " + std::to_string(minutes % 60) + " min" : "");
}
std::string joined(std::initializer_list<std::string> parts, const char *separator)
{
    std::string out;
    for (const auto &part : parts)
        if (!part.empty())
            out += (out.empty() ? "" : separator) + part;
    return out;
}
// Ported from the LG app's src/stream-details.js. Music Assistant grades the
// input itself (audio_processing.input_fidelity.quality); the app only names it.
// MA 2.10 says "standard" where the LG mock says "lossy": both read Compressed.
std::string quality_label(std::string_view grade)
{
    if (grade == "hi_res")
        return "Hi-res";
    if (grade == "lossless")
        return "Lossless";
    if (grade == "lossy" || grade == "standard")
        return "Compressed";
    if (grade == "low")
        return "Low quality";
    return {};
}
// Math.round(rate / 100) / 10 + " kHz" in the LG app: 44100 -> "44.1 kHz", 48000 -> "48 kHz".
std::string kilohertz(int rate)
{
    const int tenths = (rate + 50) / 100;
    return std::to_string(tenths / 10) + (tenths % 10 ? "." + std::to_string(tenths % 10) : "") +
           " kHz";
}
void read_stream(CompanionTrack &out, std::string_view item)
{
    const auto stream = field(item, "streamdetails");
    if (stream.empty() || stream.front() != '{')
        return;
    const auto audio = field(stream, "audio_format");
    out.content_type = text_field(audio, "content_type");
    if (out.content_type == "?")
        out.content_type.clear();
    out.sample_rate = std::clamp(number(audio, "sample_rate"), 0, 1536000);
    out.bit_depth = std::clamp(number(audio, "bit_depth"), 0, 64);
    out.bit_rate = std::clamp(number(audio, "bit_rate"), 0, 100000);
    const auto fidelity = field(field(stream, "audio_processing"), "input_fidelity");
    const bool graded = !fidelity.empty() && fidelity.front() == '{';
    const auto grade = text_field(fidelity, "quality");
    out.quality = quality_label(grade);
    std::string type = out.content_type;
    for (char &c : type)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto format = joined({type, out.sample_rate ? kilohertz(out.sample_rate) : ""}, " ");
    const auto detail = out.bit_depth && graded && grade != "lossy" && grade != "standard"
                            ? std::to_string(out.bit_depth) + "-bit"
                        : out.bit_rate ? std::to_string(out.bit_rate) + " kbps"
                                       : std::string{};
    out.quality_badge = joined({out.quality, joined({format, detail}, " / ")}, " · ");
}
std::string item_subtitle(const CompanionTrack &track, std::string_view media)
{
    if (track.media_type == "album" || track.media_type == "track")
        return track.artist;
    if (track.media_type == "audiobook")
        return join_names(field(media, "authors"));
    if (track.media_type == "podcast_episode")
        return joined({short_date(text_field(field(media, "metadata"), "release_date")),
                       short_duration(track.duration)},
                      " · ");
    if (track.media_type == "podcast")
    {
        const int episodes = number(media, "total_episodes");
        return joined({trimmed(text_field(media, "publisher")),
                       episodes > 0
                           ? std::to_string(episodes) + (episodes == 1 ? " episode" : " episodes")
                           : ""},
                      " · ");
    }
    return {};
}
CompanionTrack parse_track(std::string_view item)
{
    CompanionTrack out;
    auto media = field(item, "media_item");
    if (media.empty() || media == "null")
        media = item;
    out.title = text_field(media, "name");
    if (out.title.empty())
        out.title = text_field(item, "name");
    out.uri = text_field(media, "uri");
    out.item_id = text_field(media, "item_id");
    out.provider = text_field(media, "provider");
    out.media_type = text_field(media, "media_type");
    out.queue_item_id = text_field(item, "queue_item_id");
    out.favorite = field(media, "favorite") == "true";
    auto audio_format = field(field(item, "streamdetails"), "audio_format");
    if (audio_format.empty())
    {
        const auto mappings = elements(field(media, "provider_mappings"), 1);
        if (!mappings.empty())
            audio_format = field(mappings.front(), "audio_format");
    }
    out.format = text_field(audio_format, "content_type");
    const int rate = number(audio_format, "sample_rate"), depth = number(audio_format, "bit_depth");
    if (rate > 0)
        out.format += " / " + std::to_string(rate) + " Hz";
    if (depth > 0)
        out.format += " / " + std::to_string(depth) + " bit";
    out.duration = number(item, "duration", number(media, "duration"));
    for (auto artist : elements(field(media, "artists"), 16))
    {
        const auto name = text_field(artist, "name");
        if (!name.empty())
        {
            if (!out.artist.empty())
                out.artist += ", ";
            out.artist += name;
            if (out.first_artist.empty())
                out.first_artist = name;
        }
    }
    auto album = field(media, "album");
    out.album = text_field(album, "name");
    int year = number(album, "year");
    // An album item carries its own year; a song's comes from its album.
    if (out.media_type == "album")
        year = number(media, "year", year);
    if (year > 0)
        out.year = std::to_string(year);
    auto metadata = field(media, "metadata");
    out.label = text_field(metadata, "label");
    out.lyrics = text_field(metadata, "lyrics");
    out.lrc_lyrics = text_field(metadata, "lrc_lyrics");
    auto images = elements(field(metadata, "images"), 8);
    if (images.empty())
        images = elements(field(field(album, "metadata"), "images"), 8);
    if (images.empty())
    {
        const auto image = field(item, "image");
        if (!image.empty() && image != "null")
            images.push_back(image);
    }
    if (!images.empty())
    {
        const auto proxy = text_field(images.front(), "proxy_id");
        if (!proxy.empty())
            out.artwork = "/imageproxy/" + url_encode(proxy) + "?size=512&fmt=png";
        else
            out.artwork = text_field(images.front(), "path");
    }
    if (out.artist.empty())
    {
        auto split = out.title.find(" - ");
        if (split != std::string::npos)
        {
            out.artist = out.title.substr(0, split);
            out.first_artist = out.artist;
            out.title.erase(0, split + 3);
        }
    }
    out.subtitle = item_subtitle(out, media);
    read_stream(out, item);
    return out;
}
} // namespace

using ma_socket::Socket;

namespace companion_wire
{
void WaveResampler::reset()
{
    previous_time_ = 0;
    source_rate_ = 48000;
}
void WaveResampler::append(const Wave &wave, std::vector<float> &stereo)
{
    const auto gap = wave.time - previous_time_;
    if (previous_time_ && gap > 0 && gap < 100000)
    {
        const double measured = 1024e6 / static_cast<double>(gap);
        for (int rate : {44100, 48000, 88200, 96000, 176400, 192000})
            if (std::abs(measured - rate) < rate * 0.01)
            {
                source_rate_ = rate;
                break;
            }
    }
    previous_time_ = wave.time;
    const int factor = source_rate_ >= 176400 ? 4 : source_rate_ >= 88200 ? 2 : 1;
    stereo.reserve(stereo.size() + 2048 / static_cast<std::size_t>(factor));
    for (std::size_t i = 0; i < wave.samples.size(); i += static_cast<std::size_t>(factor))
    {
        float sample = 0;
        for (int j = 0; j < factor; ++j)
            sample += static_cast<float>(wave.samples[i + static_cast<std::size_t>(j)]) - 128.0f;
        sample /= 128.0f * static_cast<float>(factor);
        stereo.push_back(sample);
        stereo.push_back(sample);
    }
}
bool decode_wave(const std::uint8_t *data, std::size_t size, Wave &out)
{
    if (!data || size != 1033 || data[0] != 22)
        return false;
    std::uint64_t timestamp = 0;
    for (int i = 1; i < 9; ++i)
        timestamp = (timestamp << 8) | data[i];
    if (timestamp > static_cast<std::uint64_t>(INT64_MAX))
        return false;
    out.time = static_cast<std::int64_t>(timestamp);
    std::copy_n(data + 9, 1024, out.samples.begin());
    return true;
}
CompanionTrack decode_track(std::string_view item)
{
    return parse_track(item);
}

std::vector<CompanionTrack> decode_items(std::string_view items, std::size_t limit)
{
    std::vector<CompanionTrack> result;
    if (items.size() > kMaxMessage)
        return result;
    if (!items.empty() && items.front() == '{')
        items = field(items, "items");
    for (auto item : elements(items, std::min<std::size_t>(limit, 200)))
    {
        auto track = parse_track(item);
        if (!track.uri.empty() || !track.queue_item_id.empty())
            result.push_back(std::move(track));
    }
    return result;
}

namespace
{
constexpr std::size_t kSearchLimit = 20;
int browse_offset(const BrowseRequest &request)
{
    return std::clamp(request.offset, 0, kMaxBrowseOffset);
}
std::string provider_args(const BrowseRequest &request)
{
    return "{\"item_id\":" + json_quote(request.item_id) + ",\"provider_instance_id_or_domain\":" +
           json_quote(request.provider.empty() ? "library" : request.provider) + "}";
}
} // namespace

BrowsePage browse_page(const BrowseRequest &request)
{
    BrowsePage page;
    switch (request.category)
    {
    case BrowseCategory::recent:
        page.limit = 30;
        break;
    case BrowseCategory::tracks:
        page.limit = 200;
        break;
    case BrowseCategory::search:
        page.limit = 7 * kSearchLimit;
        return page;
    case BrowseCategory::artist_albums:
    case BrowseCategory::artist_tracks:
    case BrowseCategory::podcast_episodes:
        break;
    default:
        page.server_paged = true;
        return page;
    }
    page.skip = static_cast<std::size_t>(browse_offset(request));
    return page;
}

void decode_browse(std::string_view result, BrowseCategory category, BrowsePage &page)
{
    if (result.size() > kMaxMessage)
        return;
    auto keep = [&](std::string_view item)
    {
        auto track = parse_track(item);
        if (!track.uri.empty())
            page.items.push_back(std::move(track));
    };
    if (category == BrowseCategory::search)
    {
        // Sections in the order the UI shows them; media_type stays on each item.
        for (const char *key :
             {"artists", "albums", "tracks", "playlists", "radio", "podcasts", "audiobooks"})
            for (auto item : elements(field(result, key), kSearchLimit))
                keep(item);
        return;
    }
    if (!result.empty() && result.front() == '{')
        result = field(result, "items");
    // One element past the page tells a whole-list command that more remain.
    for (auto item : elements(result, page.skip + page.limit + 1))
    {
        ++page.received;
        if (page.skip)
            --page.skip;
        else if (page.items.size() >= page.limit)
            page.more = true;
        else
            keep(item);
    }
    if (page.server_paged)
        page.more = page.received >= page.limit;
}

bool browse_has_more(bool page_more, std::size_t loaded)
{
    return page_more && loaded < static_cast<std::size_t>(kMaxBrowseOffset);
}

Request artist_fallback(const BrowseRequest &request, std::string_view artist)
{
    if (request.category != BrowseCategory::artist_albums &&
        request.category != BrowseCategory::artist_tracks)
        return {};
    for (auto mapping : elements(field(artist, "provider_mappings"), 16))
    {
        const auto instance = text_field(mapping, "provider_instance");
        const auto id = text_field(mapping, "item_id");
        if (field(mapping, "available") == "false" || instance.empty() || id.empty() ||
            id.size() > 2048 || instance.size() > 256)
            continue;
        BrowseRequest retry = request;
        retry.item_id = id;
        retry.provider = instance;
        return browse_request(retry);
    }
    return {};
}

Request browse_request(const BrowseRequest &request)
{
    Request result;
    if (request.item_id.size() > 2048 || request.provider.size() > 256)
        return {{}, {}, "Browse item is too long"};
    if (request.offset > kMaxBrowseOffset)
        return {{}, {}, "This list is too long to load more"};
    std::string kind, order = "sort_name";
    bool favorite = false;
    const auto offset = browse_offset(request);
    switch (request.category)
    {
    case BrowseCategory::recent:
        // recently_played_items has no offset: ask for the pages so far and skip here.
        return {"music/recently_played_items",
                "{\"limit\":" + std::to_string(offset + 31) + ",\"fully_played_only\":false}",
                {}};
    case BrowseCategory::favorite_playlists:
        kind = "playlists";
        favorite = true;
        break;
    case BrowseCategory::favorite_albums:
        kind = "albums";
        favorite = true;
        break;
    case BrowseCategory::favorite_tracks:
        kind = "tracks";
        favorite = true;
        order = "timestamp_added_desc";
        break;
    case BrowseCategory::favorite_artists:
        kind = "artists";
        favorite = true;
        break;
    case BrowseCategory::radio:
        kind = "radios";
        break;
    case BrowseCategory::playlists:
        kind = "playlists";
        break;
    case BrowseCategory::albums:
        kind = "albums";
        order = "timestamp_added_desc";
        break;
    case BrowseCategory::tracks:
        if (request.item_id.empty() ||
            (request.media_type != "album" && request.media_type != "playlist"))
            return {{}, {}, "Choose an album or playlist"};
        return {request.media_type == "album" ? "music/albums/album_tracks"
                                              : "music/playlists/playlist_tracks",
                provider_args(request),
                {}};
    case BrowseCategory::podcasts:
        kind = "podcasts";
        break;
    case BrowseCategory::audiobooks:
        kind = "audiobooks";
        break;
    case BrowseCategory::search:
    {
        const auto query = trimmed(request.query);
        if (query.empty())
            return {{}, {}, "Type something to search for"};
        if (query.size() > 256)
            return {{}, {}, "Search text is too long"};
        return {"music/search",
                "{\"search_query\":" + json_quote(query) +
                    ",\"media_types\":[\"artist\",\"album\",\"track\",\"playlist\",\"radio\","
                    "\"podcast\",\"audiobook\"],\"limit\":" +
                    std::to_string(kSearchLimit) + "}",
                {}};
    }
    case BrowseCategory::artist_albums:
    case BrowseCategory::artist_tracks:
        if (request.item_id.empty())
            return {{}, {}, "Choose an artist"};
        return {request.category == BrowseCategory::artist_albums ? "music/artists/artist_albums"
                                                                  : "music/artists/artist_tracks",
                provider_args(request),
                {}};
    case BrowseCategory::podcast_episodes:
        if (request.item_id.empty())
            return {{}, {}, "Choose a podcast"};
        return {"music/podcasts/podcast_episodes", provider_args(request), {}};
    }
    result.command = "music/" + kind + "/library_items";
    result.args = "{\"limit\":60,\"offset\":" + std::to_string(offset) +
                  ",\"order_by\":" + json_quote(order) + (favorite ? ",\"favorite\":true}" : "}");
    return result;
}

Request queue_items_request(const std::string &queue_id, int offset)
{
    return {"player_queues/items",
            "{\"queue_id\":" + json_quote(queue_id) + ",\"limit\":" + std::to_string(kQueueWindow) +
                ",\"offset\":" + std::to_string(std::clamp(offset, 0, 100000)) + "}",
            {}};
}

std::vector<CompanionPlayer> decode_players(std::string_view players)
{
    std::vector<CompanionPlayer> list;
    auto ids = [](std::string_view raw)
    {
        std::vector<std::string> out;
        for (auto id : elements(raw, 64))
        {
            auto text = string_value(id);
            if (!text.empty() && text.size() <= 256)
                out.push_back(std::move(text));
        }
        return out;
    };
    for (auto p : elements(players))
    {
        // Sync groups are speakers too (LG speakersOf), listed before the rest.
        const auto type = text_field(p, "type");
        if ((type == "player" || type == "stereo_pair" || type == "group") &&
            field(p, "available") == "true" && field(p, "enabled") != "false" &&
            field(p, "hide_in_ui") != "true")
        {
            CompanionPlayer player(text_field(p, "player_id"), text_field(p, "name"));
            player.type = type;
            player.group_members = ids(field(p, "group_members"));
            player.can_group_with = ids(field(p, "can_group_with"));
            player.static_members = ids(field(p, "static_group_members"));
            player.provider = text_field(p, "provider");
            const auto features = ids(field(p, "supported_features"));
            player.can_set_members =
                std::find(features.begin(), features.end(), "set_members") != features.end();
            player.synced_to = text_field(p, "synced_to");
            player.available = true;
            list.push_back(std::move(player));
        }
    }
    std::sort(list.begin(), list.end(),
              [](const auto &a, const auto &b)
              {
                  const bool ga = a.type == "group", gb = b.type == "group";
                  return ga != gb ? ga : a.name < b.name;
              });
    return list;
}

// MA 2.10.6 players/cmd/set_members: option leads the group, text joins or leaves it.
// Grouping needs no playing queue, so it is checked before the queue target.
Request group_request(const CompanionAction &action, const CompanionSnapshot &target)
{
    if (!target.connected)
        return {{}, {}, "Connect Music Assistant to group speakers"};
    if (action.option.empty() || action.text.empty() || action.option == action.text)
        return {{}, {}, "Choose a speaker to group"};
    if (action.option.size() > 256 || action.text.size() > 256)
        return {{}, {}, "Speaker id is too long"};
    const bool add = action.kind == CompanionActionKind::group_add;
    return {"players/cmd/set_members",
            "{\"target_player\":" + json_quote(action.option) +
                (add ? ",\"player_ids_to_add\":[" : ",\"player_ids_to_remove\":[") +
                json_quote(action.text) + "]}",
            {}};
}

Request transfer_request(std::string_view from, std::string_view to, bool auto_play)
{
    if (from.empty() || to.empty() || from == to)
        return {{}, {}, "Choose another speaker"};
    if (from.size() > 256 || to.size() > 256)
        return {{}, {}, "Speaker id is too long"};
    return {"player_queues/transfer",
            "{\"source_queue_id\":" + json_quote(from) + ",\"target_queue_id\":" + json_quote(to) +
                (auto_play ? ",\"auto_play\":true}" : "}"),
            {}};
}

Request recent_request()
{
    // Music Assistant counts partly played songs too, as LG asks.
    return {"music/recently_played_items",
            "{\"limit\":" + std::to_string(kRecentLimit + 2) + ",\"fully_played_only\":false}",
            {}};
}

std::vector<CompanionTrack> decode_recent(std::string_view result)
{
    std::vector<CompanionTrack> items;
    for (auto &item : decode_items(result, kRecentLimit + 2))
        if (!item.uri.empty() && items.size() < static_cast<std::size_t>(kRecentLimit))
            items.push_back(std::move(item));
    return items;
}

Request action_request(const CompanionAction &action, const CompanionSnapshot &target)
{
    if (action.kind == CompanionActionKind::group_add ||
        action.kind == CompanionActionKind::group_remove)
        return group_request(action, target);
    if (action.kind == CompanionActionKind::transfer)
    {
        if (!target.connected)
            return {{}, {}, "Connect Music Assistant to move the music"};
        return transfer_request(target.queue_id, action.text, action.enabled);
    }
    // A player setting, not a queue command: no playing queue is needed.
    // text/option name the player and key the value was set for, so a change
    // still waiting when this PS5's player changes goes to its own player.
    if (action.kind == CompanionActionKind::static_delay)
        return action.text.empty() ? static_delay_request(target.static_delay_player,
                                                          target.static_delay_key, action.value)
                                   : static_delay_request(action.text, action.option, action.value);
    if (!target.connected || target.queue_id.empty())
        return {{}, {}, "No connected player. Choose a playback source."};
    if (action.text.size() > 4096 || action.option.size() > 32)
        return {{}, {}, "Playback request is too long"};
    std::string command, args = "{\"queue_id\":" + json_quote(target.queue_id);
    const auto bool_value = action.enabled ? "true" : "false";
    switch (action.kind)
    {
    case CompanionActionKind::seek:
        // The drawn song's length says nothing about a silent chosen speaker.
        if (target.target_elsewhere)
            return {{}, {}, "Nothing is playing on " + target.target_name};
        if (target.track.duration <= 0)
            return {{}, {}, "Seeking is unavailable for this stream"};
        command = "seek";
        args +=
            ",\"position\":" + std::to_string(std::clamp(action.value, 0, target.track.duration));
        break;
    case CompanionActionKind::shuffle:
        command = "shuffle";
        args += ",\"shuffle_enabled\":" + std::string(bool_value);
        break;
    case CompanionActionKind::repeat:
        if (action.text != "off" && action.text != "one" && action.text != "all")
            return {{}, {}, "Unsupported repeat mode"};
        command = "repeat";
        args += ",\"repeat_mode\":" + json_quote(action.text);
        break;
    case CompanionActionKind::autoplay:
        command = "autoplay";
        args += ",\"autoplay_enabled\":" + std::string(bool_value);
        break;
    case CompanionActionKind::crossfade:
        command = "crossfade";
        args += ",\"crossfade_enabled\":" + std::string(bool_value);
        break;
    case CompanionActionKind::sleep:
    {
        // Sleep belongs to the audible queue/group, matching the TV client;
        // targeting a synced child could stop only that speaker.
        const auto &player = target.queue_id;
        return {action.value > 0 ? "players/sleep_timer/set" : "players/sleep_timer/clear",
                "{\"player_id\":" + json_quote(player) +
                    (action.value > 0
                         ? ",\"seconds\":" + std::to_string(std::clamp(action.value, 1, 1440) * 60)
                         : "") +
                    "}",
                {}};
    }
    case CompanionActionKind::favorite:
    {
        const auto uri = action.text.empty() ? target.track.uri : action.text;
        if (uri.empty())
            return {{}, {}, "No music item to favourite"};
        if (!action.enabled)
        {
            std::string type = target.track.media_type, id = target.track.item_id;
            if (uri.starts_with("library://"))
            {
                const auto slash = uri.find('/', 10);
                if (slash != std::string::npos)
                {
                    type = uri.substr(10, slash - 10);
                    id = uri.substr(slash + 1);
                }
                if (!type.empty() && !id.empty())
                    return {"music/favorites/remove_item",
                            "{\"media_type\":" + json_quote(type) +
                                ",\"library_item_id\":" + json_quote(id) + "}",
                            {}};
            }
            if (type.empty() || id.empty() || target.track.provider.empty())
                return {{}, {}, "Library identity is unavailable for this favourite"};
            return {"music/get_library_item",
                    "{\"media_type\":" + json_quote(type) + ",\"item_id\":" + json_quote(id) +
                        ",\"provider_instance_id_or_domain\":" + json_quote(target.track.provider) +
                        "}",
                    {}};
        }
        return {"music/favorites/add_item", "{\"item\":" + json_quote(uri) + "}", {}};
    }
    case CompanionActionKind::radio:
    case CompanionActionKind::play_media:
    {
        auto uri = action.text.empty() && action.kind == CompanionActionKind::radio
                       ? target.track.uri
                       : action.text;
        if (uri.empty())
            return {{}, {}, "No music item selected"};
        const auto option = action.kind == CompanionActionKind::radio ? "replace" : action.option;
        if (option != "replace" && option != "next" && option != "add")
            return {{}, {}, "Unsupported queue option"};
        if (action.kind == CompanionActionKind::radio)
            uri = "radio_playlist://playlist/" + uri;
        command = "play_media";
        args += ",\"media\":" + json_quote(uri) + ",\"option\":" + json_quote(option);
        break;
    }
    case CompanionActionKind::play_index:
    case CompanionActionKind::delete_item:
    case CompanionActionKind::move_item:
        if (action.text.empty())
            return {{}, {}, "No queue item selected"};
        if (action.kind == CompanionActionKind::play_index)
        {
            command = "play_index";
            args += ",\"index\":" + json_quote(action.text);
        }
        else if (action.kind == CompanionActionKind::delete_item)
        {
            command = "delete_item";
            args += ",\"item_id_or_index\":" + json_quote(action.text);
        }
        else
        {
            command = "move_item";
            args += ",\"queue_item_id\":" + json_quote(action.text) +
                    ",\"pos_shift\":" + std::to_string(std::clamp(action.value, -200, 200));
        }
        break;
    case CompanionActionKind::static_delay:
    case CompanionActionKind::group_add:
    case CompanionActionKind::group_remove:
    case CompanionActionKind::transfer:
    case CompanionActionKind::member_volume:
        // Handled before this switch: they need no playing queue.
        return {{}, {}, "Not a queue command"};
    }
    return {"player_queues/" + command, args + "}", {}};
}
std::string merge_favorites(std::string_view raw, const FavoriteChanges &changes,
                            std::int64_t now_ms)
{
    auto preferences = object_fields(raw);
    auto vizman = object_fields(preferences["vizman"]);
    auto meta = object_fields(vizman["favoritesMeta"]);
    std::vector<std::string> mirrored, web;
    for (auto item : elements(vizman["mirrored"], 8192))
        mirrored.push_back(string_value(item));
    for (auto item : elements(preferences["visualizer_favorites"], 8192))
        web.push_back(string_value(item));
    const auto now = now_ms;
    auto set_entry = [&](const std::string &name, bool on, std::int64_t at)
    {
        meta[name] =
            "{\"on\":" + std::string(on ? "true" : "false") + ",\"at\":" + std::to_string(at) + "}";
    };
    for (const auto &name : web)
        if (!meta.contains(name))
            set_entry(name, true, 0);
        else if (std::find(mirrored.begin(), mirrored.end(), name) == mirrored.end() &&
                 field(meta[name], "on") != "true")
            set_entry(name, true, now);
    for (const auto &name : mirrored)
        if (std::find(web.begin(), web.end(), name) == web.end() &&
            field(meta[name], "on") == "true")
            set_entry(name, false, now);
    for (const auto &[name, change] : changes)
    {
        Json old(meta[name]);
        if (change.second >= old.integer(old.get(0, "at")))
            set_entry(name, change.first, change.second);
    }
    std::string list = "[";
    for (const auto &[name, value] : meta)
        if (field(value, "on") == "true")
        {
            if (list.size() > 1)
                list += ',';
            list += json_quote(name);
        }
    list += ']';
    vizman["favoritesMeta"] = encode_object(meta);
    vizman["mirrored"] = list;
    preferences["vizman"] = encode_object(vizman);
    preferences["visualizer_favorites"] = list;
    return encode_object(preferences);
}
std::string account_save(std::string_view preferences, const FavoriteChanges &changes,
                         std::string_view device_id, std::string_view device_json,
                         std::int64_t now_ms)
{
    if (changes.empty() && device_json.empty())
        return {};
    std::string out =
        changes.empty() ? std::string(preferences) : merge_favorites(preferences, changes, now_ms);
    if (!device_json.empty())
        out = merge_device_preferences(out, device_id, device_json);
    return out;
}
std::string device_preferences(std::string_view preferences, std::string_view device_id)
{
    if (device_id.empty())
        return {};
    const auto device = field(field(field(preferences, "vizman"), "devices"), device_id);
    return !device.empty() && device.front() == '{' ? std::string(device) : std::string{};
}
std::string merge_device_preferences(std::string_view raw, std::string_view device_id,
                                     std::string_view device_json)
{
    auto preferences = object_fields(raw);
    auto vizman = object_fields(preferences["vizman"]);
    auto devices = object_fields(vizman["devices"]);
    devices[std::string(device_id)] = std::string(device_json);
    vizman["devices"] = encode_object(devices);
    preferences["vizman"] = encode_object(vizman);
    return encode_object(preferences);
}
bool decode_static_delay(std::string_view config, std::string &key, int &milliseconds)
{
    constexpr std::string_view name = "sendspin_static_delay";
    for (const auto &[candidate, entry] : object_fields(field(config, "values")))
    {
        if (candidate != name && !candidate.ends_with("||" + std::string(name)))
            continue;
        key = candidate;
        milliseconds = std::clamp(number(entry, "value", number(entry, "default_value")), 0, 5000);
        return true;
    }
    return false;
}
Request static_delay_request(std::string_view player_id, std::string_view key, int milliseconds)
{
    if (player_id.empty() || key.empty())
        return {{}, {}, "Output compensation is unavailable until this PS5's player is online"};
    if (player_id.size() > 256 || key.size() > 256)
        return {{}, {}, "Player id is too long"};
    return {"config/players/save",
            "{\"player_id\":" + json_quote(player_id) + ",\"values\":{" + json_quote(key) + ":" +
                std::to_string(std::clamp(milliseconds, 0, 5000)) + "}}",
            {}};
}
VolumeTarget volume_target(std::string_view player)
{
    VolumeTarget target;
    target.player_id = text_field(player, "player_id");
    // A sync leader lists itself among its members; only the others make a group.
    for (auto member : elements(field(player, "group_members"), 64))
        if (string_value(member) != target.player_id)
            target.group = true;
    target.group = target.group || text_field(player, "type") == "group";
    // A group's own volume_level is null; group_volume is the level it shows.
    const int own = number(player, "volume_level", 100);
    target.level = std::clamp(target.group ? number(player, "group_volume", own) : own, 0, 100);
    return target;
}
Request volume_request(const VolumeTarget &target, int level)
{
    if (target.player_id.empty() || target.player_id.size() > 256)
        return {{}, {}, "No speaker to set the volume on"};
    return {target.group ? "players/cmd/group_volume" : "players/cmd/volume_set",
            "{\"player_id\":" + json_quote(target.player_id) +
                ",\"volume_level\":" + std::to_string(std::clamp(level, 0, 100)) + "}",
            {}};
}
bool volume_settable(std::string_view player)
{
    if (text_field(player, "type") == "group" || text_field(player, "volume_control") == "none")
        return false;
    const auto level = field(player, "volume_level");
    if (level.empty() || level == "null")
        return false;
    for (auto feature : elements(field(player, "supported_features"), 64))
        if (string_value(feature) == "volume_set")
            return true;
    return false;
}
RelayCheck relay_check(std::int64_t now_us, std::int64_t opened_us, std::int64_t last_wave_us,
                       bool streaming)
{
    if (streaming)
        return now_us - last_wave_us >= kRelayStallUs ? RelayCheck::reconnect : RelayCheck::keep;
    return now_us - std::max(last_wave_us, opened_us) > kRelayStallUs ? RelayCheck::skip
                                                                      : RelayCheck::keep;
}
} // namespace companion_wire

struct Companion::Impl
{
    struct Queue : QueueInfo
    {
        std::string item_id;
        CompanionTrack track, next;
        int index = 0, total = 0;
        double elapsed = 0;
        std::int64_t elapsed_at = 0;
        bool shuffle = false, autoplay = false, crossfade = false;
        std::string repeat = "off";
        std::int64_t started = 0;
    };
    using Wave = companion_wire::Wave;
    std::atomic<bool> stopping{true}, relay_enabled{true};
    bool last_relay_enabled = true;
    mutable std::mutex mutex;
    CompanionSnapshot state;
    std::string host, token, preferred;
    std::uint16_t port = 0;
    pthread_t thread{};
    bool started = false, authenticated = false, relay_authenticated = false;
    // current is what the poster shows; relay_for is the queue whose
    // visualizer relay is open, only while that queue plays. target is what
    // commands act on (speakers.hpp command_target); last outlives reconnects.
    std::string relay_for;
    std::string current, displayed_player, enriched_uri, enrichment_id, enrichment_key;
    TargetPick target;
    std::string last, window_key;
    // Idle poster: a recently played read asked for by load_recent().
    bool recent_wanted = false;
    std::string recent_inflight;
    CompanionTrack enriched_track;
    std::uint64_t enrichment_number = 0;
    // Retry lyrics (UI thread): the worker enriches the playing song again.
    std::atomic<bool> lyrics_retry{false};
    std::vector<CompanionTrack> upcoming;
    std::uint64_t sequence = 0;
    Speakers players;
    std::map<std::string, Queue> queues;
    std::map<std::string, std::int64_t> skipped;
    BrowseRequest browse_wanted;
    bool browse_pending = false;
    std::uint64_t browse_generation = 0;
    std::string browse_inflight, action_inflight;
    std::string action_removal_type;
    bool action_remove_lookup = false;
    companion_wire::BrowsePage browse_page;
    bool browse_fallback_tried = false;
    // Queue window: queue_window (mutex) is -1 for "after the current item";
    // it applies only while window_queue is still the followed queue.
    int queue_window = -1;
    bool queue_window_dirty = false;
    std::string window_queue, items_request;
    int items_offset = 0, upcoming_offset = 0;
    // The playing item's index when last seen; a change resets the window.
    int playing_index = -1;
    companion_wire::Request action_pending;
    std::int64_t browse_deadline = 0, action_deadline = 0;
    std::map<std::string, std::pair<bool, std::int64_t>> favorite_changes, favorite_inflight;
    std::int64_t next_account = 0;
    bool account_pending = false;
    // Account mirroring: this device's object under vizman.devices. The UI
    // thread writes device_pending (debounced by device_due); guarded by mutex.
    std::string account_device, account_json, device_pending, device_inflight;
    std::uint64_t account_revision = 0;
    std::int64_t device_due = 0;
    // Static delay: candidates are read in order until one holds the setting.
    std::string sendspin_id, own_player;
    std::vector<std::string> delay_candidates;
    std::size_t delay_probe = 0;
    bool delay_reading = false, delay_refresh = false;
    std::int64_t next_delay_read = 0;
    int delay_requested = -1;
    // The player a static delay save went to, and which read a reply answers:
    // a delay never lands on another player than its own (LG 1.4.3).
    std::string delay_requested_player;
    std::uint64_t delay_generation = 0;
    std::deque<Wave> waves;
    companion_wire::WaveResampler resampler;
    std::int64_t offset = 0, best_rtt = 10000000, clock_sample = 0;
    bool clock_ready = false;
    std::int64_t relay_opened = 0, last_wave = 0, next_ping = 0, next_poll = 0;
    Socket api{stopping}, relay{stopping};
    // Frames arrived on this relay socket and no stream/end followed; relay_stalls
    // counts the sockets replaced because frames stopped (LG 1.4.4).
    bool relay_streaming = false;
    int relay_stalls = 0;
    // Transport presses (mutex): one command in flight, the rest combined.
    ControlQueue controls;
    std::string control_inflight;
    Assumed volume_sent, position_sent, playing_sent;
    // The settling action's id (CompanionAction::id), reported in action_done.
    std::uint64_t action_inflight_id = 0;
    // The token the last auth on the API socket carried; a refusal of a token
    // replaced since (set_token) is not a refusal of this PS5's sign-in.
    std::string auth_sent;

    std::string current_token()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return token;
    }
    bool send_auth()
    {
        auth_sent = current_token();
        return call("auth", "auth", "{\"token\":" + json_quote(auth_sent) + "}");
    }
    // Caller holds the mutex. An action settled: report it once by its id.
    void action_settled(bool ok)
    {
        if (action_inflight_id != 0)
        {
            state.action_done = action_inflight_id;
            state.action_done_ok = ok;
        }
        action_inflight_id = 0;
    }

    bool call(const std::string &id, const std::string &command, const std::string &args = "{}")
    {
        return api.send_text("{\"message_id\":" + json_quote(id) +
                             ",\"command\":" + json_quote(command) + ",\"args\":" + args + "}");
    }
    void status(const char *message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        state.status = message;
    }
    void clear_wave()
    {
        std::lock_guard<std::mutex> lock(mutex);
        waves.clear();
        resampler.reset();
        state.streaming = false;
    }
    void read_player(std::string_view raw)
    {
        SpeakerInfo p;
        p.id = text_field(raw, "player_id");
        if (p.id.empty())
            return;
        p.name = text_field(raw, "name");
        p.type = text_field(raw, "type");
        p.synced_to = text_field(raw, "synced_to");
        p.active_group = text_field(raw, "active_group");
        p.active_source = text_field(raw, "active_source");
        p.available = field(raw, "available") == "true";
        p.enabled = field(raw, "enabled") != "false";
        p.hidden = field(raw, "hide_in_ui") == "true";
        p.volume = number(raw, "volume_level", 100);
        p.group_volume = number(raw, "group_volume", -1);
        p.volume_settable = companion_wire::volume_settable(raw);
        for (auto id : elements(field(raw, "group_members"), 64))
            if (auto text = string_value(id); !text.empty() && text.size() <= 256)
                p.group_members.push_back(std::move(text));
        players[p.id] = std::move(p);
    }
    QueueLookup queue_lookup() const
    {
        return [this](const std::string &id) -> const QueueInfo *
        {
            const auto it = queues.find(id);
            return it == queues.end() ? nullptr : &it->second;
        };
    }
    void read_queue(std::string_view raw)
    {
        Queue q;
        q.id = text_field(raw, "queue_id");
        if (q.id.empty())
            return;
        q.state = text_field(raw, "state");
        q.track = parse_track(field(raw, "current_item"));
        q.next = parse_track(field(raw, "next_item"));
        const auto item = field(raw, "current_item");
        q.has_item = !item.empty() && item != "null";
        q.title = q.track.title;
        q.artist = q.track.artist;
        q.item_id = text_field(item, "queue_item_id");
        q.elapsed = decimal(raw, "elapsed_time");
        q.elapsed_at = sys::monotonic_us();
        q.index = number(raw, "current_index", -1);
        q.total = std::max(0, number(raw, "items"));
        q.shuffle = field(raw, "shuffle_enabled") == "true";
        q.autoplay = field(raw, "autoplay_enabled") == "true";
        q.crossfade = field(raw, "crossfade_enabled") == "true";
        q.repeat = text_field(raw, "repeat_mode");
        auto old = queues.find(q.id);
        q.started = old != queues.end() && old->second.state == "playing" ? old->second.started
                                                                          : sys::monotonic_us();
        if (q.state != "playing")
            skipped.erase(q.id);
        queues[q.id] = q;
    }
    std::string audible(const std::string &id)
    {
        return audible_queue(id, players, queue_lookup());
    }
    // An empty library artist first looks itself up (id + "-artist"), then
    // retries on a provider mapping under the same id.
    bool browse_message(const std::string &id, std::string_view raw, std::string_view result)
    {
        companion_wire::Request retry;
        std::string base;
        {
            std::lock_guard<std::mutex> lock(mutex);
            base = "browse-" + std::to_string(browse_generation);
            if (id == base + "-artist")
            {
                retry = companion_wire::artist_fallback(browse_wanted, result);
                if (retry.command.empty())
                    finish_browse({});
            }
            else if (id != base)
                return true;
            else
            {
                companion_wire::decode_browse(result, browse_wanted.category, browse_page);
                if (field(raw, "partial") == "true")
                    return true;
                const auto error = field(raw, "error_code");
                const bool library_artist =
                    (browse_wanted.category == BrowseCategory::artist_albums ||
                     browse_wanted.category == BrowseCategory::artist_tracks) &&
                    browse_wanted.provider == "library";
                if (error.empty() && library_artist && !browse_fallback_tried &&
                    browse_page.items.empty() && browse_page.received == 0)
                {
                    browse_fallback_tried = true;
                    retry = {"music/artists/get",
                             "{\"item_id\":" + json_quote(browse_wanted.item_id) +
                                 ",\"provider_instance_id_or_domain\":\"library\"}",
                             {}};
                    base += "-artist";
                }
                else
                    finish_browse(error);
            }
        }
        if (retry.command.empty())
            return true;
        if (retry.command != "music/artists/get")
        {
            std::lock_guard<std::mutex> lock(mutex);
            browse_page = companion_wire::browse_page(browse_wanted);
        }
        return call(base, retry.command, retry.args);
    }
    // Caller holds the mutex. Pages after the first append to what the UI has.
    void finish_browse(std::string_view error)
    {
        state.browse_loading = false;
        state.browse_error = error.empty() ? "" : "Music Assistant could not load this list";
        state.browse_offset = std::clamp(browse_wanted.offset, 0, companion_wire::kMaxBrowseOffset);
        const bool page_more = error.empty() && browse_page.more;
        if (!error.empty())
        {
            if (browse_wanted.offset <= 0)
                state.browse_items.clear();
        }
        else if (browse_wanted.offset > 0)
            state.browse_items.insert(state.browse_items.end(),
                                      std::make_move_iterator(browse_page.items.begin()),
                                      std::make_move_iterator(browse_page.items.end()));
        else
            state.browse_items = std::move(browse_page.items);
        // The next page starts at the items loaded so far (Panel::load_more).
        state.browse_has_more =
            companion_wire::browse_has_more(page_more, state.browse_items.size());
        browse_page = {};
        ++state.browse_revision;
        browse_inflight.clear();
    }
    bool api_message(int opcode, const std::uint8_t *data, std::size_t size)
    {
        if (opcode != 1)
            return true;
        std::string_view raw(reinterpret_cast<const char *>(data), size);
        const auto id = text_field(raw, "message_id");
        const auto result = field(raw, "result");
        if (id.starts_with("browse-"))
            return browse_message(id, raw, result);
        if (!action_inflight.empty() && id == action_inflight)
        {
            if (action_remove_lookup && field(raw, "error_code").empty())
            {
                action_remove_lookup = false;
                const auto library_id = text_field(result, "item_id");
                if (!library_id.empty())
                {
                    action_inflight += "-remove";
                    action_deadline = sys::monotonic_us() + 10000000;
                    return call(action_inflight, "music/favorites/remove_item",
                                "{\"media_type\":" + json_quote(action_removal_type) +
                                    ",\"library_item_id\":" + json_quote(library_id) + "}");
                }
                std::lock_guard<std::mutex> lock(mutex);
                state.action_busy = false;
                state.action_error = state.action_status = "Favourite was not found in the library";
                action_settled(false);
                action_inflight.clear();
                return true;
            }
            std::lock_guard<std::mutex> lock(mutex);
            state.action_busy = false;
            state.action_error =
                field(raw, "error_code").empty() ? "" : "Music Assistant rejected this action";
            state.action_status = state.action_error.empty() ? "Done" : state.action_error;
            action_settled(state.action_error.empty());
            if (delay_requested >= 0 && state.action_error.empty() &&
                delay_requested_player == state.static_delay_player)
                state.static_delay_ms = delay_requested;
            delay_requested = -1;
            action_inflight.clear();
            next_poll = 0;
            enrichment_key.clear();
            window_key.clear();
            return true;
        }
        if (!control_inflight.empty() && id == control_inflight)
        {
            control_inflight.clear();
            std::lock_guard<std::mutex> lock(mutex);
            controls.settle();
            return true;
        }
        if (!field(raw, "server_version").empty() && id.empty())
            return send_auth();
        if (id == "auth")
        {
            authenticated = field(result, "authenticated") == "true";
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.connected = authenticated;
            }
            if (!authenticated)
            {
                std::lock_guard<std::mutex> lock(mutex);
                // A token renewed since is no sign-out: reconnect with the fresh one.
                state.token_rejected = auth_sent == token;
                state.status = "Music Assistant token rejected";
                return false;
            }
            next_poll = 0;
            account_pending = true;
            call("account", "auth/me");
        }
        else if (id == "account" && !result.empty())
            read_account(result);
        else if (id == "save-favorites")
        {
            if (field(raw, "error_code").empty() && !result.empty())
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (const auto &[name, change] : favorite_inflight)
                {
                    auto it = favorite_changes.find(name);
                    if (it != favorite_changes.end() && it->second == change)
                        favorite_changes.erase(it);
                }
                device_saved(true);
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex);
                device_saved(false);
            }
            favorite_inflight.clear();
        }
        else if (id.starts_with("delay-") && delay_reading)
            read_delay(id, result);
        else if (id == "players")
        {
            players.clear();
            for (auto p : elements(result))
                read_player(p);
            auto list = companion_wire::decode_players(result);
            std::lock_guard<std::mutex> lock(mutex);
            state.players = std::move(list);
        }
        else if (id == "queues")
        {
            std::vector<std::string> present;
            for (auto q : elements(result))
            {
                present.push_back(text_field(q, "queue_id"));
                read_queue(q);
            }
            for (auto it = queues.begin(); it != queues.end();)
                if (std::find(present.begin(), present.end(), it->first) == present.end())
                    it = queues.erase(it);
                else
                    ++it;
        }
        else if (id == "queue")
            read_queue(result);
        else if (id == "item-" + enrichment_id && !enrichment_id.empty())
        {
            auto track = parse_track(result);
            if (track.uri == enriched_uri)
            {
                enriched_track = std::move(track);
                // The API needs the complete Track object, not its URI.
                if (result.size() < 48000)
                    call("lyrics-" + enrichment_id, "metadata/get_track_lyrics",
                         "{\"track\":" + std::string(result) + "}");
            }
        }
        else if (id == "lyrics-" + enrichment_id && !enrichment_id.empty())
        {
            const auto lines = elements(result, 2);
            if (!lines.empty())
                enriched_track.lyrics = string_value(lines[0]);
            if (lines.size() > 1)
                enriched_track.lrc_lyrics = string_value(lines[1]);
            // A failure is told apart from "no lyrics": the first is retried.
            enriched_track.lyrics_reply = field(raw, "error_code").empty() ? 1 : 2;
            enriched_track.lyrics_request = enrichment_number;
        }
        else if (id == recent_inflight && !recent_inflight.empty())
            read_recent(raw, result);
        else if (id == items_request && !items_request.empty())
        {
            upcoming.clear();
            auto items = result;
            if (!items.empty() && items.front() == '{')
                items = field(items, "items");
            for (auto item : elements(items, companion_wire::kQueueWindow))
                upcoming.push_back(parse_track(item));
            upcoming_offset = items_offset;
        }
        else
        {
            const auto event = text_field(raw, "event");
            const auto object = text_field(raw, "object_id");
            if (event == "player_added" || event == "player_updated")
                read_player(field(raw, "data"));
            else if (event == "player_removed")
                players.erase(object);
            else if (event == "queue_added" || event == "queue_updated")
                read_queue(field(raw, "data"));
            else if (event == "queue_removed")
                queues.erase(object);
            else if (event == "queue_time_updated" && queues.contains(object))
            {
                queues[object].elapsed = decimal(raw, "data", queues[object].elapsed);
                queues[object].elapsed_at = sys::monotonic_us();
            }
        }
        return true;
    }
    void read_account(std::string_view result)
    {
        account_pending = false;
        next_account = sys::monotonic_us() + 60000000;
        const auto raw_preferences = field(result, "preferences");
        if (raw_preferences.empty() || raw_preferences.front() != '{')
            return;

        companion_wire::FavoriteChanges changes;
        std::string device, device_json;
        {
            std::lock_guard<std::mutex> lock(mutex);
            changes = favorite_changes;
            device = account_device;
            device_json = take_device_save(raw_preferences);
        }
        const auto save =
            companion_wire::account_save(raw_preferences, changes, device, device_json, epoch_ms());
        const std::string_view preferences = save.empty() ? raw_preferences : save;
        std::vector<std::string> names;
        for (auto name : elements(field(preferences, "visualizer_favorites"), 8192))
            names.push_back(string_value(name));
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (state.favorites != names)
                ++state.favorite_revision;
            state.favorites = names;
            state.favorites_ready = true;
        }
        const auto args = save.empty() ? std::string() : "{\"preferences\":" + save + "}";
        const bool too_large = args.size() > companion_wire::kMaxAccountSave;
        {
            std::lock_guard<std::mutex> lock(mutex);
            state.account_too_large = too_large;
            // Unsendable: the edits stay pending and are tried again at the
            // next regular account read, not on every loop.
            if (too_large)
                device_saved(false);
        }
        if (!save.empty() && !too_large)
        {
            if (call("save-favorites", "auth/user/update", args))
            {
                favorite_inflight = changes;
                return;
            }
            std::lock_guard<std::mutex> lock(mutex);
            for (const auto &[name, change] : changes)
                if (!favorite_changes.contains(name))
                    favorite_changes[name] = change;
            device_saved(false);
        }
    }
    // Called with mutex held. Publishes the account's copy of this device and
    // returns the debounced local copy to write, if one is due ("" otherwise).
    std::string take_device_save(std::string_view raw_preferences)
    {
        if (account_device.empty())
            return {};
        const auto stored = companion_wire::device_preferences(raw_preferences, account_device);
        if (stored != account_json || account_revision == 0)
        {
            account_json = stored;
            ++account_revision;
        }
        if (device_pending.empty() || !device_inflight.empty() ||
            sys::monotonic_us() < device_due || device_pending == account_json)
        {
            if (device_pending == account_json)
                device_pending.clear();
            return {};
        }
        device_inflight = device_pending;
        return device_inflight;
    }
    // Called with mutex held when an auth/user/update reply (or send) settles.
    void device_saved(bool ok)
    {
        if (device_inflight.empty())
            return;
        if (ok)
        {
            if (device_pending == device_inflight)
                device_pending.clear();
            if (account_json != device_inflight)
            {
                account_json = device_inflight;
                ++account_revision;
            }
        }
        else
            device_due = sys::monotonic_us() + 60000000;
        device_inflight.clear();
    }
    bool device_save_due(std::int64_t now)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !account_device.empty() && !device_pending.empty() && device_inflight.empty() &&
               now >= device_due;
    }
    bool delay_refresh_wanted()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(delay_refresh, false);
    }
    // Reads config/players/get for each candidate until one holds the delay.
    bool request_delay(std::int64_t now)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            delay_candidates.clear();
            for (const auto &id : {sendspin_id, own_player})
                if (!id.empty() && std::find(delay_candidates.begin(), delay_candidates.end(),
                                             id) == delay_candidates.end())
                    delay_candidates.push_back(id);
        }
        next_delay_read = now + 60000000;
        delay_probe = 0;
        ++delay_generation;
        delay_reading = !delay_candidates.empty();
        return !delay_reading || call_delay();
    }
    std::string delay_id() const
    {
        return "delay-" + std::to_string(delay_generation) + "-" + std::to_string(delay_probe);
    }
    bool call_delay()
    {
        return call(delay_id(), "config/players/get",
                    "{\"player_id\":" + json_quote(delay_candidates[delay_probe]) + "}");
    }
    void read_delay(const std::string &id, std::string_view result)
    {
        if (id != delay_id())
            return;
        std::string key;
        int milliseconds = 0;
        if (companion_wire::decode_static_delay(result, key, milliseconds))
        {
            delay_reading = false;
            std::lock_guard<std::mutex> lock(mutex);
            state.static_delay_ms = milliseconds;
            state.static_delay_player = delay_candidates[delay_probe];
            state.static_delay_key = key;
            return;
        }
        if (++delay_probe < delay_candidates.size())
        {
            call_delay();
            return;
        }
        delay_reading = false;
        std::lock_guard<std::mutex> lock(mutex);
        state.static_delay_ms = -1;
        state.static_delay_player.clear();
        state.static_delay_key.clear();
    }
    bool relay_message(int opcode, const std::uint8_t *data, std::size_t size)
    {
        const auto now = sys::monotonic_us();
        if (opcode == 2 && size == 1033 && data[0] == 22)
        {
            Wave w;
            if (!companion_wire::decode_wave(data, size, w))
                return true;
            std::lock_guard<std::mutex> lock(mutex);
            if (!waves.empty() && waves.back().time > w.time)
            {
                waves.clear();
                resampler.reset();
            }
            if (waves.size() >= 2048)
                waves.pop_front();
            waves.push_back(w);
            state.streaming = true;
            last_wave = now;
            relay_streaming = true;
            return true;
        }
        if (opcode != 1)
            return true;
        std::string_view raw(reinterpret_cast<const char *>(data), size);
        const auto type = text_field(raw, "type");
        if (type == "auth_ok")
        {
            relay_authenticated = true;
            next_ping = 0;
        }
        else if (type == "stream/clear" || type == "stream/end")
        {
            // A new track (clear) keeps the stream; only an end stops the stall watch.
            relay_streaming = relay_streaming && type == "stream/clear";
            clear_wave();
        }
        else if (type == "error")
        {
            clear_wave();
            skipped[current] = now + 15000000;
            return false;
        }
        else if (type == "server/time")
        {
            auto payload = field(raw, "payload");
            Json clock(payload);
            const auto sent_us = clock.integer(clock.get(0, "client_transmitted")) * 1000;
            const auto server_us = clock.integer(clock.get(0, "server_transmitted"));
            const auto rtt = now - sent_us;
            if (rtt < 0 || rtt > 10000000 || server_us <= 0)
                return true;
            std::lock_guard<std::mutex> lock(mutex);
            if (rtt <= best_rtt || now - clock_sample > 60000000)
            {
                best_rtt = rtt;
                offset = server_us - (sent_us + now) / 2;
                clock_sample = now;
                clock_ready = true;
                state.clock_rtt_ms = static_cast<double>(rtt) / 1000;
            }
        }
        return true;
    }
    // Nothing usable plays here: follow the newest playing queue, else draw
    // the command target, so the poster shows what Cross acts on.
    void choose_silent(std::int64_t now, std::string &pick, std::string &speaker)
    {
        std::int64_t newest = -1;
        std::string playing;
        for (const auto &[id, q] : queues)
            if (q.state == "playing" && (!skipped.contains(id) || now >= skipped[id]) &&
                q.started > newest)
            {
                playing = id;
                newest = q.started;
            }
        if (!playing.empty())
        {
            pick = playing;
            speaker = playing;
            return;
        }
        pick = target.queue_id;
        speaker = target.player_id;
    }
    // speakers.hpp command_target. This PS5's own player only stands in while
    // it is the one sounding (Source: This PS5); otherwise its output is off.
    void pick_target(const std::string &wanted, const std::string &own)
    {
        target = command_target(players, queue_lookup(), wanted, current, last,
                                relay_enabled.load() ? std::string{} : own);
    }
    void close_relay()
    {
        if (relay_for.empty())
            return;
        relay.close();
        relay_for.clear();
        clear_wave();
    }
    // The relay follows the target only while it plays; a silent relay is
    // skipped for a while without losing the target itself.
    void update_relay(std::int64_t now)
    {
        const auto q = queues.find(current);
        const bool wanted_relay = last_relay_enabled && q != queues.end() &&
                                  q->second.state == "playing" &&
                                  (!skipped.contains(current) || now >= skipped[current]);
        if (!wanted_relay)
        {
            close_relay();
            return;
        }
        if (relay_for == current)
        {
            const auto check =
                companion_wire::relay_check(now, relay_opened, last_wave, relay_streaming);
            if (check == companion_wire::RelayCheck::keep)
                return;
            if (check == companion_wire::RelayCheck::skip)
            {
                skipped[current] = now + 15000000;
                close_relay();
                return;
            }
            // Frames stopped mid-stream: a fresh socket, opened below.
            sys::log("[Afterglow] relay: no frames for 10 s while playing; reconnecting (%d)",
                     ++relay_stalls);
        }
        close_relay();
        relay_authenticated = false;
        relay_streaming = false;
        relay_opened = now;
        last_wave = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            clock_ready = false;
            best_rtt = 10000000;
        }
        if (relay.open(host, port, "/milkdrop_visualizer?player=" + url_encode(current)) &&
            relay.send_text("{\"type\":\"auth\",\"token\":" + json_quote(current_token()) + "}"))
            relay_for = current;
        else
        {
            relay.close();
            skipped[current] = now + 15000000;
        }
    }
    void choose(std::int64_t now)
    {
        std::string wanted, own;
        {
            std::lock_guard<std::mutex> lock(mutex);
            wanted = preferred;
            own = own_player;
        }
        auto playable = [&](const std::string &id)
        {
            auto q = queues.find(id);
            return q != queues.end() && q->second.state == "playing" &&
                   (!skipped.contains(id) || now >= skipped[id]);
        };
        pick_target(wanted, own);
        std::string pick = audible(wanted), speaker = wanted;
        if (!playable(pick) && (relay_enabled.load() || wanted.empty()))
        {
            pick = current;
            speaker = displayed_player;
            if (!playable(pick) && now - last_wave > 3000000)
                choose_silent(now, pick, speaker);
        }
        if (pick != current || relay_enabled.load() != last_relay_enabled)
        {
            close_relay();
            current = pick;
            displayed_player = speaker;
            last_relay_enabled = relay_enabled.load();
        }
        if (!current.empty())
            last = current;
        // Again with the queue just chosen: "following" is the one drawn now.
        pick_target(wanted, own);
        update_relay(now);
        std::lock_guard<std::mutex> lock(mutex);
        publish_drawn(now);
        publish_target(own);
        state.status =
            current.empty() ? "Waiting for a playing speaker" : "Following " + state.player_name;
    }
    // Caller holds the mutex. Relative imageproxy paths become absolute URLs
    // the artwork fetcher can open; anything else it cannot fetch is dropped.
    void absolute_artwork(CompanionTrack &track) const
    {
        if (track.artwork.starts_with("/imageproxy/"))
            track.artwork = "http://" + host + ":" + std::to_string(port) + track.artwork;
        else if (!track.artwork.starts_with("http://") && !track.artwork.starts_with("https://"))
            track.artwork.clear();
    }
    std::string name_of(const std::string &player_id)
    {
        const auto it = players.find(player_id);
        return it != players.end() && !it->second.name.empty() ? it->second.name : player_id;
    }
    // Caller holds the mutex. What the poster shows: the drawn queue's song.
    void publish_drawn(std::int64_t now)
    {
        state.player_id = displayed_player;
        state.player_name =
            players.contains(displayed_player) ? players[displayed_player].name : displayed_player;
        const auto drawn = queues.find(current);
        if (drawn == queues.end())
        {
            state.track = {};
            state.playing = false;
            state.elapsed = 0;
            state.queue_state.clear();
            state.next_up.clear();
            return;
        }
        const auto &q = drawn->second;
        state.queue_state = q.state;
        state.track = q.track;
        state.playing = q.state == "playing";
        state.elapsed = std::max(
            0.0, q.elapsed +
                     (state.playing ? static_cast<double>(now - q.elapsed_at) / 1000000.0 : 0.0));
        if (q.track.duration > 0)
            state.elapsed = std::min(state.elapsed, static_cast<double>(q.track.duration));
        if (enriched_uri == q.track.uri && !enriched_track.title.empty())
            state.track = enriched_track;
        state.track.queue_item_id = q.item_id;
        if (state.track.format.empty())
            state.track.format = q.track.format;
        // Stream details exist only on the queue item, not the enriched library copy.
        state.track.content_type = q.track.content_type;
        state.track.sample_rate = q.track.sample_rate;
        state.track.bit_depth = q.track.bit_depth;
        state.track.bit_rate = q.track.bit_rate;
        state.track.quality = q.track.quality;
        state.track.quality_badge = q.track.quality_badge;
        absolute_artwork(state.track);
        // The Up next window belongs to the target; it extends Next only while
        // it is this queue's default window, the songs straight after this one.
        state.next_up.clear();
        if (target.queue_id == current && upcoming_offset == q.index + 1)
            for (std::size_t i = 0; i < upcoming.size() && i < 2; ++i)
                state.next_up.push_back(upcoming[i]);
        if (state.next_up.empty() && !q.next.title.empty())
            state.next_up.push_back(q.next);
        for (auto &track : state.next_up)
            absolute_artwork(track);
    }
    // Caller holds the mutex. What commands and the Queue sheet act on.
    void publish_target(const std::string &own)
    {
        state.queue_id = target.queue_id;
        state.target_player_id = target.player_id;
        state.target_name = target.player_id.empty() ? "" : name_of(target.player_id);
        state.target_reason = target.reason;
        state.target_elsewhere = !target.queue_id.empty() && target.queue_id != current;
        const auto lookup = queue_lookup();
        state.can_move_here = !own.empty() && target.queue_id != own &&
                              moves_music(players, lookup, target.queue_id, own, true);
        for (auto &player : state.players)
            player.takes_music = moves_music(players, lookup, target.queue_id, player.id, false);
        describe_speakers(state.players, players, lookup);
        const auto join = join_plan(state.players, players, lookup, target.queue_id, own);
        state.can_join = !join.leader.empty();
        state.join_leader = join.leader;
        state.join_leader_name = join.leader.empty() ? "" : leader_name(players, join.leader);
        const auto leader = players.find(target.queue_id);
        state.volume = leader == players.end() ? 100
                       : group_player(leader->second) && leader->second.group_volume >= 0
                           ? leader->second.group_volume
                           : leader->second.volume;
        const auto it = queues.find(target.queue_id);
        if (it == queues.end())
        {
            state.upcoming.clear();
            state.queue_offset = state.queue_total = 0;
            state.queue_index = -1;
            state.shuffle = state.autoplay = state.crossfade = false;
            state.repeat = "off";
            return;
        }
        const auto &q = it->second;
        state.shuffle = q.shuffle;
        state.autoplay = q.autoplay;
        state.crossfade = q.crossfade;
        state.repeat = q.repeat.empty() ? "off" : q.repeat;
        state.upcoming = upcoming;
        state.queue_offset = upcoming.empty() ? std::max(0, q.index + 1) : upcoming_offset;
        state.queue_total = q.total;
        state.queue_index = q.index;
        if (state.upcoming.empty() && !q.next.title.empty())
            state.upcoming.push_back(q.next);
        for (auto &track : state.upcoming)
            absolute_artwork(track);
    }
    // Up next follows the song after the target's playing one: a paged window
    // is dropped when its index moves, and the default one asked for.
    void follow_playing_index()
    {
        const auto it = queues.find(target.queue_id);
        if (it == queues.end() || it->second.index == playing_index)
            return;
        playing_index = it->second.index;
        std::lock_guard<std::mutex> lock(mutex);
        if (queue_window < 0)
            return;
        queue_window = -1;
        queue_window_dirty = true;
    }
    bool take_window_move()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(queue_window_dirty, false);
    }
    // Each request gets its own id so a reply for an older window is ignored.
    bool request_queue_items()
    {
        const auto &id = target.queue_id;
        int window = -1;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (window_queue == id)
                window = queue_window;
        }
        items_offset = window >= 0 ? window : std::max(0, queues[id].index + 1);
        items_request = "items-" + std::to_string(++sequence);
        const auto wire = companion_wire::queue_items_request(id, items_offset);
        return call(items_request, wire.command, wire.args);
    }
    // A new target, or a new song on it: its Up next is read afresh.
    bool follow_window()
    {
        const auto it = queues.find(target.queue_id);
        if (it == queues.end())
        {
            // A late reply for the old target must not fill the empty window.
            window_key.clear();
            items_request.clear();
            upcoming.clear();
            return true;
        }
        const auto key = target.queue_id + ":" + it->second.item_id;
        if (key == window_key)
            return true;
        window_key = key;
        upcoming.clear();
        return request_queue_items();
    }
    bool request_recent()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!std::exchange(recent_wanted, false) || !recent_inflight.empty())
                return true;
            state.recent_state = RecentState::loading;
        }
        recent_inflight = "recent-" + std::to_string(++sequence);
        const auto wire = companion_wire::recent_request();
        return call(recent_inflight, wire.command, wire.args);
    }
    void read_recent(std::string_view raw, std::string_view result)
    {
        auto items = companion_wire::decode_recent(result);
        std::lock_guard<std::mutex> lock(mutex);
        recent_inflight.clear();
        if (!field(raw, "error_code").empty())
        {
            state.recent_state = RecentState::error;
            return;
        }
        for (auto &item : items)
            absolute_artwork(item);
        state.recent = std::move(items);
        state.recent_state = RecentState::ready;
    }
    // The queue and the speaker transport presses act on.
    const std::string &control_queue() const
    {
        return target.queue_id;
    }
    const std::string &control_player() const
    {
        return target.player_id;
    }
    // Sends the next waiting press once the one before it has been answered.
    bool pump_controls(std::int64_t now)
    {
        std::optional<ControlJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex);
            job = controls.take(now);
        }
        if (!job)
            return true;
        const auto request = control_request(*job, now);
        if (request.command.empty())
        {
            std::lock_guard<std::mutex> lock(mutex);
            controls.settle();
            return true;
        }
        control_inflight = "control-" + std::to_string(++sequence);
        return call(control_inflight, request.command, request.args);
    }
    // A press's command, worked out when it is sent so that steps build on
    // the value sent before (LG playback.js build()). Empty: nothing to send.
    companion_wire::Request control_request(const ControlJob &job, std::int64_t now)
    {
        const auto &queue_id = control_queue();
        const auto q = queues.find(queue_id);
        if (queue_id.empty() || q == queues.end())
            return {};
        const auto &queue = q->second;
        const bool playing = queue.state == "playing";
        const std::string target = "{\"queue_id\":" + json_quote(queue_id);
        switch (job.kind)
        {
        case Control::toggle:
        {
            // A second press before the queue reports the first one undoes it.
            const bool was = playing_sent.value_or(queue_id, playing ? 1 : 0, now) != 0;
            playing_sent.sent(queue_id, was ? 0 : 1, now);
            return {was ? "player_queues/pause" : "player_queues/play", target + "}", {}};
        }
        case Control::next:
            return {"player_queues/next", target + "}", {}};
        case Control::previous:
            return {"player_queues/previous", target + "}", {}};
        case Control::volume:
        {
            const auto p = players.find(control_player());
            if (job.amount == 0 || p == players.end())
                return {};
            companion_wire::VolumeTarget volume;
            volume.player_id = p->first;
            volume.group = group_player(p->second);
            volume.level = volume.group && p->second.group_volume >= 0 ? p->second.group_volume
                                                                       : p->second.volume;
            const int level =
                std::clamp(volume_sent.value_or(p->first, volume.level, now) + job.amount, 0, 100);
            volume_sent.sent(p->first, level, now);
            return companion_wire::volume_request(volume, level);
        }
        case Control::member_volume:
        {
            // One speaker of the group: steps build on the level last sent to it.
            const auto p = players.find(job.player);
            if (job.amount == 0 || p == players.end() || !p->second.volume_settable)
                return {};
            companion_wire::VolumeTarget volume;
            volume.player_id = p->first;
            volume.level = p->second.volume;
            const int level =
                std::clamp(volume_sent.value_or(p->first, volume.level, now) + job.amount, 0, 100);
            volume_sent.sent(p->first, level, now);
            return companion_wire::volume_request(volume, level);
        }
        case Control::seek:
        {
            if (job.amount == 0 || queue.track.duration <= 0)
                return {};
            const auto key = queue_id + ":" + queue.item_id;
            const auto since = [&](std::int64_t at)
            { return playing ? static_cast<double>(now - at) / 1000000.0 : 0.0; };
            const double base = position_sent.fresh(key, now)
                                    ? position_sent.value() + since(position_sent.sent_at())
                                    : queue.elapsed + since(queue.elapsed_at);
            const int position = std::clamp(static_cast<int>(std::lround(base)) + job.amount, 0,
                                            queue.track.duration);
            position_sent.sent(key, position, now);
            return {"player_queues/seek",
                    target + ",\"position\":" + std::to_string(position) + "}",
                    {}};
        }
        }
        return {};
    }
    void run()
    {
        api.handler = [this](int op, const auto *data, std::size_t size)
        { return api_message(op, data, size); };
        relay.handler = [this](int op, const auto *data, std::size_t size)
        { return relay_message(op, data, size); };
        while (!stopping)
        {
            authenticated = false;
            account_pending = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (const auto &[name, change] : favorite_inflight)
                    if (!favorite_changes.contains(name))
                        favorite_changes[name] = change;
                favorite_inflight.clear();
                if (device_pending.empty())
                    device_pending = device_inflight;
                device_inflight.clear();
            }
            delay_reading = false;
            next_delay_read = 0;
            close_relay();
            current.clear();
            target = {};
            window_key.clear();
            recent_inflight.clear();
            players.clear();
            queues.clear();
            skipped.clear();
            status("Connecting to Music Assistant");
            bool okay = api.open(host, port, "/ws");
            const auto opened = sys::monotonic_us();
            while (okay && !stopping)
            {
                const auto now = sys::monotonic_us();
                okay = api.tick();
                if (now - api.last_receive_ > 30000000 ||
                    (!authenticated && now - opened > 10000000))
                    break;
                if (authenticated)
                {
                    if (now >= next_poll)
                    {
                        okay =
                            call("players", "players/all") && call("queues", "player_queues/all");
                        if (!current.empty())
                            okay = okay && call("queue", "player_queues/get",
                                                "{\"queue_id\":" + json_quote(current) + "}");
                        if (!window_key.empty() && queues.contains(target.queue_id))
                            okay = okay && request_queue_items();
                        next_poll = now + 4000000;
                    }
                    else if (take_window_move() && queues.contains(target.queue_id))
                        okay = okay && request_queue_items();
                    okay = okay && request_recent();
                    bool favorites_pending = false;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        favorites_pending = !favorite_changes.empty() && !state.account_too_large;
                    }
                    if (!account_pending && favorite_inflight.empty() &&
                        (now >= next_account || favorites_pending || device_save_due(now)))
                    {
                        account_pending = true;
                        okay = okay && send_auth();
                    }
                    // A new own player pre-empts a read in progress (its reply is then ignored).
                    if (delay_refresh_wanted() || (!delay_reading && now >= next_delay_read))
                        okay = okay && request_delay(now);
                    choose(now);
                    follow_playing_index();
                    if (lyrics_retry.exchange(false))
                        enrichment_key.clear();
                    if (queues.contains(current) &&
                        current + ":" + queues[current].item_id + ":" + queues[current].track.uri !=
                            enrichment_key)
                    {
                        const auto &q = queues[current];
                        enrichment_key = current + ":" + q.item_id + ":" + q.track.uri;
                        enriched_uri = q.track.uri;
                        enriched_track = {};
                        enrichment_number = ++sequence;
                        enrichment_id = std::to_string(enrichment_number);
                        if (!enriched_uri.empty())
                            okay = okay && call("item-" + enrichment_id, "music/item_by_uri",
                                                "{\"uri\":" + json_quote(enriched_uri) +
                                                    ",\"allow_update_metadata\":false}");
                    }
                    okay = okay && follow_window();
                    if (!relay_for.empty())
                    {
                        if (!relay.tick())
                        {
                            skipped[relay_for] = now + 15000000;
                            close_relay();
                        }
                        else if (relay_authenticated && now >= next_ping)
                        {
                            if (!relay.send_text("{\"type\":\"client/"
                                                 "time\",\"payload\":{\"client_transmitted\":" +
                                                 std::to_string(now / 1000) + "}}"))
                            {
                                close_relay();
                            }
                            next_ping = now + (now - relay_opened < 2000000 ? 250000 : 5000000);
                        }
                    }
                    okay = okay && pump_controls(now);
                    companion_wire::Request browse_request, action_request;
                    std::string browse_id, action_id;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (browse_pending)
                        {
                            browse_pending = false;
                            browse_request = companion_wire::browse_request(browse_wanted);
                            browse_id = "browse-" + std::to_string(browse_generation);
                            browse_inflight = browse_id;
                            browse_page = companion_wire::browse_page(browse_wanted);
                            browse_fallback_tried = false;
                            // Album/playlist/artist contents, episodes and search can reach
                            // uncached providers; library category reads are local and faster.
                            browse_deadline =
                                now + (browse_page.server_paged ||
                                               browse_wanted.category == BrowseCategory::recent
                                           ? 10000000
                                           : 45000000);
                        }
                        if (!action_pending.command.empty())
                        {
                            action_request = std::move(action_pending);
                            action_pending = {};
                            action_id = "action-" + std::to_string(++sequence);
                            action_inflight = action_id;
                            action_remove_lookup =
                                action_request.command == "music/get_library_item";
                            action_removal_type = text_field(action_request.args, "media_type");
                            action_deadline = now + 10000000;
                        }
                        if (!browse_inflight.empty() && now >= browse_deadline)
                        {
                            state.browse_loading = false;
                            state.browse_error = "Browse request timed out. Try again.";
                            browse_inflight.clear();
                            ++browse_generation;
                            ++state.browse_revision;
                        }
                        if (!action_inflight.empty() && now >= action_deadline)
                        {
                            state.action_busy = false;
                            state.action_error =
                                "No action confirmation. Check playback before retrying.";
                            state.action_status = state.action_error;
                            action_settled(false);
                            action_inflight.clear();
                        }
                    }
                    if (!browse_request.command.empty())
                        okay = okay && call(browse_id, browse_request.command, browse_request.args);
                    if (!action_request.command.empty())
                        okay = okay && call(action_id, action_request.command, action_request.args);
                }
                poll(nullptr, 0, 10);
            }
            api.close();
            relay.close();
            clear_wave();
            bool rejected = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.connected = false;
                if (state.recent_state == RecentState::loading)
                    state.recent_state = RecentState::error;
                rejected = state.token_rejected;
                state.status =
                    rejected ? "Music Assistant token rejected" : "Music Assistant disconnected";
                if (state.action_busy)
                    state.action_error = state.action_status =
                        "Disconnected before confirmation. Check playback before retrying.";
                state.action_busy = false;
                action_settled(false);
                action_pending = {};
                action_inflight.clear();
                controls.clear();
                control_inflight.clear();
                if (state.browse_loading)
                    state.browse_error = "Music Assistant disconnected. Try again.";
                state.browse_loading = false;
                browse_pending = false;
                browse_inflight.clear();
                ++browse_generation;
            }
            // A refused token stays refused until stop() or a renewed token (set_token).
            while (rejected && !stopping && current_token() == auth_sent)
                poll(nullptr, 0, 100);
            for (int i = 0; i < 30 && !stopping; ++i)
                poll(nullptr, 0, 100);
        }
    }
};

Companion::Companion() : impl_(std::make_unique<Impl>())
{
}
Companion::~Companion()
{
    stop();
    join();
}
bool Companion::start(const char *host, std::uint16_t port, const char *token)
{
    auto &p = *impl_;
    in_addr address{};
    if (p.started || !host || !token || !*token || std::strlen(token) > 8192 || !port ||
        inet_pton(AF_INET, host, &address) != 1)
        return false;
    p.host = host;
    p.port = port;
    p.token = token;
    p.stopping = false;
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.state.token_rejected = false;
    }
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
    {
        p.stopping = true;
        return false;
    }
    int result = pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (!result)
        result = pthread_create(
            &p.thread, &attr,
            [](void *v) -> void *
            {
                static_cast<Impl *>(v)->run();
                return nullptr;
            },
            &p);
    pthread_attr_destroy(&attr);
    p.started = result == 0;
    if (!p.started)
        p.stopping = true;
    return p.started;
}
void Companion::stop()
{
    impl_->stopping = true;
}
void Companion::join()
{
    if (impl_->started)
    {
        pthread_join(impl_->thread, nullptr);
        impl_->started = false;
    }
}
CompanionSnapshot Companion::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto state = impl_->state;
    state.buffered_frames = impl_->waves.size();
    return state;
}
void Companion::select_player(const std::string &id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->preferred = id;
}
void Companion::set_relay_enabled(bool enabled)
{
    impl_->relay_enabled = enabled;
}
void Companion::set_favorite(const std::string &preset, bool favorite)
{
    if (preset.empty() || preset.size() > 1024)
        return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->favorite_changes.size() < 1024)
        impl_->favorite_changes[preset] = {favorite, epoch_ms()};
}
std::string Companion::pending_favorites() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::map<std::string, std::string> values;
    for (const auto &[name, change] : impl_->favorite_changes)
        values[name] = "{\"on\":" + std::string(change.first ? "true" : "false") +
                       ",\"at\":" + std::to_string(change.second) + "}";
    return encode_object(values);
}
void Companion::restore_pending_favorites(std::string_view journal)
{
    if (journal.size() > 262144)
        return;
    const auto values = object_fields(journal);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto &[name, raw] : values)
    {
        if (name.empty() || name.size() > 1024 || impl_->favorite_changes.size() >= 1024)
            continue;
        Json json(raw);
        const auto at = json.integer(json.get(0, "at"));
        const auto on = field(raw, "on");
        if (at < 0 || (on != "true" && on != "false"))
            continue;
        const auto it = impl_->favorite_changes.find(name);
        if (it == impl_->favorite_changes.end() || at > it->second.second)
            impl_->favorite_changes[name] = {on == "true", at};
    }
}
void Companion::command(Command command)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    switch (command)
    {
    case Command::toggle:
        impl_->controls.press(Control::toggle);
        break;
    case Command::next:
        impl_->controls.press(Control::next);
        break;
    case Command::previous:
        impl_->controls.press(Control::previous);
        break;
    case Command::volume_up:
    case Command::volume_down:
        impl_->controls.press(Control::volume, command == Command::volume_up ? 5 : -5);
        break;
    }
}
void Companion::seek_by(int seconds)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->controls.press(Control::seek, std::clamp(seconds, -3600, 3600));
}
void Companion::set_token(const std::string &token)
{
    if (token.empty() || token.size() > 8192)
        return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->token = token;
    impl_->state.token_rejected = false;
}
void Companion::browse(const BrowseRequest &request)
{
    const auto wire = companion_wire::browse_request(request);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->browse_generation;
    ++impl_->state.browse_revision;
    // A later page keeps what is shown; its reply appends (finish_browse).
    if (request.offset <= 0)
        impl_->state.browse_items.clear();
    impl_->state.browse_has_more = false;
    impl_->state.browse_error = !wire.error.empty()       ? wire.error
                                : !impl_->state.connected ? "Connect Music Assistant to browse"
                                                          : "";
    impl_->state.browse_loading = impl_->state.browse_error.empty();
    impl_->browse_wanted = request;
    impl_->browse_pending = impl_->state.browse_loading;
}
bool Companion::action(const CompanionAction &action)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // A refused action settles at once, so whoever waits on its id hears of it.
    const auto refuse = [&]
    {
        if (action.id != 0)
        {
            impl_->state.action_done = action.id;
            impl_->state.action_done_ok = false;
        }
        return false;
    };
    // One speaker's volume joins the transport presses rather than waiting
    // for another action to settle.
    if (action.kind == CompanionActionKind::member_volume)
        return !action.text.empty() && action.text.size() <= 256 &&
               impl_->controls.press(Control::member_volume, std::clamp(action.value, -100, 100),
                                     action.text);
    if (impl_->state.action_busy)
        return refuse();
    auto request = companion_wire::action_request(action, impl_->state);
    impl_->state.action_error = request.error;
    impl_->state.action_status = request.error.empty() ? "Working" : request.error;
    if (!request.error.empty())
        return refuse();
    impl_->delay_requested =
        action.kind == CompanionActionKind::static_delay ? std::clamp(action.value, 0, 5000) : -1;
    impl_->delay_requested_player =
        action.text.empty() ? impl_->state.static_delay_player : action.text;
    impl_->action_pending = std::move(request);
    impl_->state.action_busy = true;
    impl_->action_inflight_id = action.id;
    return true;
}
void Companion::retry_lyrics()
{
    impl_->lyrics_retry = true;
}
void Companion::set_queue_offset(int offset)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const int total = impl_->state.queue_total;
    impl_->queue_window = offset < 0 ? -1 : std::clamp(offset, 0, std::max(0, total - 1));
    impl_->window_queue = impl_->state.queue_id;
    impl_->queue_window_dirty = true;
}
void Companion::load_recent()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state.connected)
        impl_->recent_wanted = true;
    else
        impl_->state.recent_state = RecentState::error;
}
std::vector<float> Companion::take_pcm(int trim_ms)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<float> pcm;
    if (!impl_->clock_ready)
        return pcm;
    const auto now = sys::monotonic_us() + impl_->offset -
                     static_cast<std::int64_t>(std::clamp(trim_ms, -5000, 5000)) * 1000;
    while (!impl_->waves.empty() && impl_->waves.front().time <= now)
    {
        const auto wave = impl_->waves.front();
        impl_->waves.pop_front();
        if (now - wave.time > 300000)
            continue;
        impl_->resampler.append(wave, pcm);
        if (pcm.size() > 16384)
            pcm.erase(pcm.begin(), pcm.end() - 16384);
    }
    return pcm;
}
} // namespace hui::afterglow

namespace hui::afterglow
{
// Account mirroring and this player's static delay.
void Companion::set_own_player(const std::string &sendspin_id, const std::string &player_id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->sendspin_id == sendspin_id && impl_->own_player == player_id)
        return;
    impl_->sendspin_id = sendspin_id;
    impl_->own_player = player_id;
    impl_->state.own_player_id = player_id;
    impl_->delay_refresh = true;
    // The new player's own delay is read afresh; until then there is none to show.
    impl_->state.static_delay_ms = -1;
    impl_->state.static_delay_player.clear();
    impl_->state.static_delay_key.clear();
}
void Companion::set_account_device(const std::string &device_id)
{
    if (device_id.size() > 128 || device_id.find_first_of("\"\\") != std::string::npos)
        return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->account_device = device_id;
}
void Companion::save_account_preferences(const std::string &preferences_json)
{
    if (preferences_json.size() > 16384 || !preferences_json.starts_with('{') ||
        !preferences_json.ends_with('}'))
        return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->device_pending == preferences_json)
        return;
    impl_->device_pending = preferences_json;
    // Debounced: a held slider becomes one write 1.5 s after it settles.
    impl_->device_due = sys::monotonic_us() + 1500000;
}
std::string Companion::account_preferences() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->account_json;
}
std::uint64_t Companion::account_revision() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->account_revision;
}
} // namespace hui::afterglow
