// Afterglow - Song-by-song lyrics: kept timings, other lyrics and Go to a line.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/song_lyrics.hpp"
#include "afterglow/lyrics.hpp"
#include "afterglow/online_lyrics.hpp"
#include <algorithm>
#include <chrono>

namespace hui::afterglow
{
namespace
{
SheetRow make_row(std::string title, int tag, std::string value = {})
{
    SheetRow row;
    row.item.title = std::move(title);
    row.item.tag = tag;
    row.item.value = std::move(value);
    return row;
}
SheetRow make_header(std::string title)
{
    SheetRow row;
    row.item.title = std::move(title);
    row.item.header = true;
    return row;
}
bool timed(const Presentation &s)
{
    return !s.companion.track.lrc_lyrics.empty();
}
std::string song_key(const Presentation &s)
{
    const auto &c = s.companion;
    if (!c.connected || (c.track.uri.empty() && c.track.queue_item_id.empty()))
        return {};
    return c.queue_id + "\n" + c.track.queue_item_id + "\n" + c.track.uri;
}
lrclib::Query song_query(const Presentation &s)
{
    const auto &t = s.companion.track;
    lrclib::Query q;
    q.title = t.title;
    q.artist = t.first_artist.empty() ? t.artist : t.first_artist;
    q.album = t.album;
    q.duration = t.duration;
    return q;
}
} // namespace

std::int64_t epoch_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string song_id(const Presentation &s)
{
    const auto &t = s.companion.track;
    return t.media_type == "track" && t.uri.size() <= kSongIdMax ? t.uri : std::string{};
}

std::optional<int> kept_lyrics_timing(const Presentation &s)
{
    return kept_timing(s.preferences.songs, song_id(s), s.song_lyrics.sum);
}

int lyrics_offset_ms(const Presentation &s)
{
    return kept_lyrics_timing(s).value_or(s.preferences.lyrics_offset_ms);
}

bool step_lyrics_timing(Presentation &s, int direction)
{
    auto &p = s.preferences;
    if (const auto kept = kept_lyrics_timing(s))
        return keep_timing(p.songs, song_id(s), s.song_lyrics.sum,
                           step_lyrics_offset(*kept, direction), epoch_ms());
    const int before = p.lyrics_offset_ms;
    p.lyrics_offset_ms = step_lyrics_offset(before, direction);
    return p.lyrics_offset_ms != before;
}

bool lyrics_offer_pending(const Presentation &s)
{
    return s.song_lyrics.offer.pending(s.preferences.lyrics_offset_ms,
                                       kept_lyrics_timing(s).has_value(), timed(s), song_id(s));
}

void follow_song_lyrics(Presentation &s)
{
    auto &state = s.song_lyrics;
    const auto &t = s.companion.track;
    const std::string &text = t.lrc_lyrics.empty() ? t.lyrics : t.lrc_lyrics;
    if (text != state.summed)
    {
        state.summed = text;
        state.sum = lyrics_fingerprint(text);
    }
    state.offer.song(song_key(s), s.preferences.lyrics_offset_ms);
    // A list for another song is no list at all.
    if (state.candidates_state != CandidateState::idle && state.candidates_song != song_key(s))
    {
        state.candidates_state = CandidateState::idle;
        state.candidates.clear();
    }
}

void pump_song_lyrics(Presentation &s, LyricsLookup &lookup, OnlineLyrics &online)
{
    auto &state = s.song_lyrics;
    follow_song_lyrics(s);
    if (state.pick)
    {
        lookup.use(state.pick->plain, state.pick->lrc);
        state.pick.reset();
    }
    if (state.candidates_wanted)
    {
        state.candidates_wanted = false;
        if (s.preferences.online_lyrics && !song_id(s).empty())
            online.find_candidates(++state.candidates_ticket, song_query(s));
        else
            state.candidates_state = CandidateState::error;
    }
    std::uint64_t ticket = 0;
    bool failed = false;
    std::vector<lrclib::Candidate> list;
    if (online.take_candidates(ticket, failed, list) && ticket == state.candidates_ticket &&
        state.candidates_state == CandidateState::loading)
    {
        state.candidates_state = failed ? CandidateState::error : CandidateState::ready;
        state.candidates = std::move(list);
    }
}

bool song_lyrics_tag(int tag)
{
    return tag >= keep_timing_row && tag < lyric_line_row + 100000;
}

void add_timing_rows(const Presentation &s, std::vector<SheetRow> &rows)
{
    if (lyrics_offer_pending(s))
    {
        rows.push_back(make_header("Keep this timing for this song?"));
        rows.push_back(make_row("Keep", keep_timing_row,
                                "Other songs: " + lyrics_timing_label(s.song_lyrics.offer.base())));
        rows.push_back(make_row("Not now", not_now_row));
    }
    if (kept_lyrics_timing(s))
        rows.push_back(
            make_row("Forget timing for this song", forget_timing_row, "Timing: kept for this song"));
}

void add_choose_row(const Presentation &s, std::vector<SheetRow> &rows)
{
    const auto status = s.lyrics.status;
    if (!s.preferences.online_lyrics || song_id(s).empty() || status == LyricsStatus::idle ||
        status == LyricsStatus::loading)
        return;
    std::string detail;
    if (chosen_lyrics(s.preferences.songs, song_id(s)) != 0)
        detail = "Chosen for this song";
    else if (s.song_lyrics.source == LyricsSource::music_assistant)
        detail = "From Music Assistant";
    else if (s.song_lyrics.source != LyricsSource::none)
        detail = "From lrclib.net";
    auto row = make_row("Choose other lyrics", choose_lyrics_row, std::move(detail));
    row.item.chevron = true;
    rows.push_back(std::move(row));
}

void add_line_row(const Presentation &s, std::vector<SheetRow> &rows)
{
    if (!timed(s) || s.companion.track.duration <= 0)
        return;
    auto row = make_row("Go to a line", go_to_line_row);
    row.item.chevron = true;
    row.item.disabled = !s.companion.connected;
    rows.push_back(std::move(row));
}

std::vector<SheetRow> lyrics_choice_rows(const Presentation &s)
{
    std::vector<SheetRow> rows;
    const auto chosen = chosen_lyrics(s.preferences.songs, song_id(s));
    auto automatic =
        make_row("Automatic", automatic_lyrics_row, "Music Assistant, else the best match");
    automatic.chosen = chosen == 0;
    rows.push_back(std::move(automatic));
    const auto &list = s.song_lyrics.candidates;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        auto row = make_row(list[i].title + " · " + list[i].artist,
                            candidate_row + static_cast<int>(i));
        row.item.subtitle = candidate_detail(list[i], s.companion.track.duration);
        row.chosen = list[i].id == chosen;
        rows.push_back(std::move(row));
    }
    if (s.song_lyrics.candidates_state == CandidateState::error)
        rows.push_back(make_row("Try again", try_again_row));
    return rows;
}

std::string lyrics_choice_note(const Presentation &s)
{
    switch (s.song_lyrics.candidates_state)
    {
    case CandidateState::loading:
        return "Searching lrclib.net…";
    case CandidateState::error:
        return "lrclib.net is busy. Try again shortly.";
    case CandidateState::ready:
        return s.song_lyrics.candidates.empty()
                   ? "lrclib.net has no other lyrics for this song."
                   : "Pick the lyrics for this song. Your pick is kept.";
    default:
        return {};
    }
}

std::vector<SheetRow> lyrics_line_rows(const Presentation &s, int &focus)
{
    std::vector<SheetRow> rows;
    const auto &t = s.companion.track;
    const auto lines = parse_lyrics(t.lrc_lyrics, true);
    const int offset = lyrics_offset_ms(s);
    // The line on screen: read the lyrics at an earlier time when they show later.
    const int current = current_lyric(lines, s.companion.elapsed - offset / 1000.0);
    focus = 0;
    for (std::size_t i = 0; i < lines.size() && i < 100000; ++i)
    {
        if (lines[i].text.find_first_not_of(" \t") == std::string::npos)
            continue;
        if (static_cast<int>(i) <= current)
            focus = static_cast<int>(rows.size());
        auto row = make_row(lines[i].text, lyric_line_row + static_cast<int>(i),
                            clock_label(line_seek_position(lines[i].seconds, offset, t.duration)));
        row.chosen = static_cast<int>(i) == current;
        row.item.disabled = !s.companion.connected;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string lyrics_lines_note(const Presentation &s)
{
    return s.companion.track.title + " · plays from the line you pick";
}

SongLyricsStep confirm_song_lyrics(Presentation &s, int tag)
{
    auto &p = s.preferences;
    auto &state = s.song_lyrics;
    const auto id = song_id(s);
    switch (tag)
    {
    case keep_timing_row:
        keep_timing(p.songs, id, state.sum, p.lyrics_offset_ms, epoch_ms());
        p.lyrics_offset_ms = state.offer.base();
        state.offer.answer();
        s.show_toast("Timing kept for this song");
        return SongLyricsStep::stay;
    case not_now_row:
        state.offer.answer();
        return SongLyricsStep::stay;
    case forget_timing_row:
        if (forget_timing(p.songs, id, epoch_ms()))
            s.show_toast("Timing forgotten for this song");
        return SongLyricsStep::stay;
    case choose_lyrics_row:
    case try_again_row:
        state.candidates_wanted = true;
        state.candidates_state = CandidateState::loading;
        state.candidates_song = song_key(s);
        state.candidates.clear();
        return tag == choose_lyrics_row ? SongLyricsStep::open_choice : SongLyricsStep::stay;
    case go_to_line_row:
        return SongLyricsStep::open_lines;
    case automatic_lyrics_row:
        if (clear_choice(p.songs, id, epoch_ms()))
        {
            s.lyrics_retry = true;
            s.show_toast("Lyrics back to automatic");
        }
        return SongLyricsStep::back;
    default:
        break;
    }
    const auto index = static_cast<std::size_t>(tag - candidate_row);
    if (tag >= candidate_row && tag < lyric_line_row && index < state.candidates.size())
    {
        const auto &c = state.candidates[index];
        choose_lyrics(p.songs, id, c.id, c.sum, epoch_ms());
        state.pick = c;
        s.show_toast("Lyrics chosen for this song");
        return SongLyricsStep::back;
    }
    if (tag >= lyric_line_row)
    {
        const auto lines = parse_lyrics(s.companion.track.lrc_lyrics, true);
        const auto line = static_cast<std::size_t>(tag - lyric_line_row);
        const auto &c = s.companion;
        if (line >= lines.size() || !c.connected || c.action_busy || s.companion_action_pending)
            return SongLyricsStep::stay;
        CompanionAction action;
        action.kind = CompanionActionKind::seek;
        action.value = line_seek_position(lines[line].seconds, lyrics_offset_ms(s), c.track.duration);
        s.companion_action = std::move(action);
        s.companion_action_pending = true;
        return SongLyricsStep::leave;
    }
    return SongLyricsStep::stay;
}
} // namespace hui::afterglow
