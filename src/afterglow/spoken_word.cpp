// Afterglow - Go to the album or artist, and audiobooks and podcasts in Details and Browse.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/spoken_word.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace hui::afterglow
{
namespace
{
constexpr int kSpeedCount = static_cast<int>(std::size(kSpeeds));

// The nearest step, so a speed set elsewhere (0.9×) still steps sensibly.
int speed_index(int hundredths)
{
    int best = 1;
    for (int i = 0; i < kSpeedCount; ++i)
        if (std::abs(kSpeeds[i] - hundredths) < std::abs(kSpeeds[best] - hundredths))
            best = i;
    return best;
}
CompanionTrack target_of(const CompanionLink &link, const char *media_type)
{
    CompanionTrack target;
    target.title = link.name;
    target.item_id = link.item_id;
    target.provider = link.provider;
    target.media_type = media_type;
    target.uri = link.uri;
    return target;
}
DetailsRow go_to(std::string label, std::string value, const CompanionLink &link,
                 const char *media_type)
{
    DetailsRow row;
    row.kind = DetailsRowKind::go_to;
    row.label = std::move(label);
    row.value = std::move(value);
    row.target = target_of(link, media_type);
    return row;
}
std::vector<CompanionChapter> listed(const CompanionTrack &track)
{
    if (!spoken_word(track) || track.chapters.size() < 2)
        return {};
    auto chapters = track.chapters;
    if (chapters.size() > kMaxChapters)
        chapters.resize(kMaxChapters);
    return chapters;
}
} // namespace

bool spoken_word(const CompanionTrack &track)
{
    return track.media_type == "audiobook" || track.media_type == "podcast_episode";
}

std::string speed_label(int hundredths)
{
    const int value = std::max(0, hundredths);
    std::string text = std::to_string(value / 100);
    if (const int rest = value % 100; rest != 0)
        text += "." + std::string(rest < 10 ? "0" : "") +
                std::to_string(rest % 10 == 0 ? rest / 10 : rest);
    return text + "×";
}

int step_speed(int hundredths, int direction, bool wrap)
{
    const int next = speed_index(hundredths) + direction;
    if (wrap)
        return kSpeeds[(next % kSpeedCount + kSpeedCount) % kSpeedCount];
    return kSpeeds[std::clamp(next, 0, kSpeedCount - 1)];
}

int chapter_at(const std::vector<CompanionChapter> &chapters, double position)
{
    int found = -1;
    for (std::size_t i = 0; i < chapters.size(); ++i)
        if (chapters[i].start <= position + 0.5)
            found = static_cast<int>(i);
    return found;
}

std::string clock_label(int seconds)
{
    const int s = std::max(0, seconds), hours = s / 3600, minutes = s / 60 % 60;
    const auto two = [](int v) { return std::string(v < 10 ? "0" : "") + std::to_string(v); };
    return hours ? std::to_string(hours) + ":" + two(minutes) + ":" + two(s % 60)
                 : std::to_string(minutes) + ":" + two(s % 60);
}

std::vector<DetailsRow> details_rows(const CompanionTrack &track, int speed, double position)
{
    std::vector<DetailsRow> rows;
    if (track.media_type == "track")
    {
        if (!track.album_link.item_id.empty())
            rows.push_back(
                go_to("Go to the album", track.album_link.name, track.album_link, "album"));
        for (std::size_t i = 0; i < track.artist_links.size() && i < kMaxArtistLinks; ++i)
            rows.push_back(
                go_to("Go to " + track.artist_links[i].name, "", track.artist_links[i], "artist"));
        return rows;
    }
    if (track.media_type == "podcast_episode" && !track.podcast_link.item_id.empty())
        rows.push_back(
            go_to("Go to the podcast", track.podcast_link.name, track.podcast_link, "podcast"));
    if (!spoken_word(track) || track.duration <= 0)
        return rows;
    DetailsRow speed_row;
    speed_row.kind = DetailsRowKind::speed;
    speed_row.label = "Speed";
    speed_row.value = speed_label(speed);
    rows.push_back(std::move(speed_row));
    const auto chapters = listed(track);
    if (!chapters.empty())
    {
        const int at = chapter_at(chapters, position);
        DetailsRow chapter_row;
        chapter_row.kind = DetailsRowKind::chapters;
        chapter_row.label = "Chapters";
        chapter_row.value =
            at >= 0 ? "Chapter " + std::to_string(at + 1) + " of " + std::to_string(chapters.size())
                    : std::to_string(chapters.size()) + " chapters";
        rows.push_back(std::move(chapter_row));
    }
    return rows;
}

std::vector<ChapterRow> chapter_rows(const CompanionTrack &track, double position)
{
    const auto chapters = listed(track);
    const int at = chapter_at(chapters, position);
    std::vector<ChapterRow> rows;
    for (std::size_t i = 0; i < chapters.size(); ++i)
    {
        ChapterRow row;
        row.label =
            chapters[i].name.empty() ? "Chapter " + std::to_string(i + 1) : chapters[i].name;
        row.value = clock_label(chapters[i].start);
        row.start = chapters[i].start;
        row.current = static_cast<int>(i) == at;
        rows.push_back(std::move(row));
    }
    return rows;
}

bool offers_radio(const CompanionTrack &track)
{
    return !spoken_word(track) && track.media_type != "radio";
}

bool offers_lyrics_retry(const CompanionTrack &track)
{
    return !spoken_word(track);
}

std::pair<std::string, std::string> empty_list_copy(BrowseCategory category)
{
    switch (category)
    {
    case BrowseCategory::in_progress:
        return {"Nothing to carry on with", "Audiobooks and episodes you start show here"};
    case BrowseCategory::podcasts:
        return {"No podcasts yet", "Add podcasts in Music Assistant and they appear here"};
    case BrowseCategory::audiobooks:
        return {"No audiobooks yet", "Add audiobooks in Music Assistant and they appear here"};
    case BrowseCategory::podcast_episodes:
        return {"No episodes here", ""};
    default:
        return {};
    }
}
} // namespace hui::afterglow
