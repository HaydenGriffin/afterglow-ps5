// Afterglow - LG-style sheets: Browse, Queue, Details, Display and Settings.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/panel.hpp"
#include "afterglow/lyric_overlay.hpp"
#include "afterglow/share_link.hpp"
#include "afterglow/speakers.hpp"
#include "afterglow/style.hpp"
#include "afterglow/version.hpp"
#include "ui/glyphs.hpp"
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>

namespace hui::afterglow
{
namespace
{
using Page = Panel::Page;

struct Category
{
    const char *name;
    BrowseCategory category;
};
// Browse's category column, below its Search entry. Row tags are indexes here.
constexpr Category kCategories[] = {
    {"Recently played", BrowseCategory::recent},
    {"Favourite playlists", BrowseCategory::favorite_playlists},
    {"Favourite albums", BrowseCategory::favorite_albums},
    {"Favourite songs", BrowseCategory::favorite_tracks},
    {"Favourite artists", BrowseCategory::favorite_artists},
    {"Radio stations", BrowseCategory::radio},
    {"All playlists", BrowseCategory::playlists},
    {"All albums", BrowseCategory::albums},
    {"Podcasts", BrowseCategory::podcasts},
    {"Audiobooks", BrowseCategory::audiobooks},
};
constexpr int kCategoryCount = static_cast<int>(std::size(kCategories));
// Search results are grouped in this order; anything else goes last.
struct ResultGroup
{
    const char *media_type;
    const char *title;
};
constexpr ResultGroup kResultGroups[] = {
    {"artist", "Artists"},       {"album", "Albums"}, {"track", "Songs"},
    {"playlist", "Playlists"},   {"radio", "Radio"},  {"podcast", "Podcasts"},
    {"audiobook", "Audiobooks"},
};
constexpr const char *kModes[] = {"Visuals only", "Visuals + track", "Album", "Lyrics"};
constexpr const char *kLayers[] = {"Off", "One line", "Stage"};
constexpr const char *kTrackCards[] = {"Always", "At track changes", "Hidden"};
constexpr const char *kVisualStyles[] = {"MilkDrop", "Halo", "Spectrum"};
constexpr const char *kRepeatModes[] = {"off", "all", "one"};
constexpr const char *kRepeatLabels[] = {"Off", "All", "One"};
// Companion keeps a window of 50 upcoming songs; Earlier / Later move it.
constexpr int kQueueWindow = 50;
// TV output compensation (sendspin_static_delay), as LG speaker.js steps it.
constexpr int kDelayMax = 5000;
constexpr int kDelayStep = 10;
constexpr int kDelayFastStep = 100;
constexpr int kDelayFastAfter = 10;
constexpr float kDelaySaveSeconds = 0.6f;
constexpr float kDelayHoldSeconds = 4.0f;
// The sync figure's scale reaches this far either side of the music.
constexpr float kSyncSpanMs = 600;

// Right sheets follow LG's panel: it starts where the artwork slab ends and
// its words align to the poster's column, inside the title-safe right edge.
constexpr float kSheetX = style::kSlabWidth;
constexpr float kLeft = style::kColumn;
constexpr float kRight = style::kSafeRight;
constexpr float kTitleBaseline = 118;
constexpr float kNoteBaseline = 160;
constexpr float kRuleBare = 164;
constexpr float kRuleWithNote = 188;
// Rows end above the notice line and the glyph row; the 34 px glyphs centred
// on kFooterY end at 1017, inside the safe bottom (1026).
constexpr float kListBottom = 936;
constexpr float kNoticeBaseline = 966;
constexpr float kFooterY = 1000;
constexpr float kFactRow = 72;
// Status has a dozen facts: shorter rows keep them all above the footer.
constexpr float kStatusFactRow = 56;
constexpr float kDetailsLabelColumn = 229;
constexpr float kStatusLabelColumn = 320;
constexpr float kLyricsListTop = 400;
constexpr float kFigureHeight = 190;
constexpr float kQueueThumbnail = 56;
// Between the facts and the Scan to save code (LG's 2.5vw gutter).
constexpr float kShareGap = 48;

// Browse covers the screen: LG's 9vh title, categories in grid columns 1-3,
// the collection or an item's actions in columns 4-12.
constexpr float kBrowseLeft = style::kSafeLeft;
constexpr float kBrowseTitleBaseline = 142;
constexpr float kBrowseNoteBaseline = 188;
constexpr float kBrowseRule = 214;
constexpr float kCategoryRight = 515;
// Search and ten categories fit whole between the rule and the notice line.
constexpr float kCategoryRow = 63;
constexpr float kBrowseTop = 236;
constexpr float kCollectionLeft = 539;
constexpr float kHeadingBaseline = 282;
constexpr float kHeadingSize = 48;
constexpr float kCollectionTop = 305;
constexpr float kCollectionRow = 104;
constexpr float kActionRow = 86;
// How long an item's row reads "Plays next" / "Added to the queue" (LG CONFIRM_MS).
constexpr float kConfirmSeconds = 2.5f;

// What a row does. Negative so they never collide with the indexes that
// list rows carry (queued songs, browse results, presets, speakers).
enum Row : int
{
    info = -200,
    retry,
    playing_on,
    shuffle,
    repeat,
    crossfade,
    autoplay,
    volume,
    sleep,
    queue_earlier,
    queue_later,
    queue_play,
    queue_next,
    queue_up,
    queue_down,
    queue_remove,
    favourite,
    radio,
    mode_visuals,
    mode_track,
    mode_album,
    mode_lyrics,
    layer_off,
    layer_line,
    layer_stage,
    poster_visuals,
    track_card,
    visual_style,
    reduce_motion,
    karaoke,
    open_source,
    open_visuals,
    open_lyrics_timing,
    open_presets,
    open_sync,
    open_account,
    open_status,
    play_here,
    follow_auto,
    brightness,
    text_brightness,
    tint,
    tint_strength,
    night_register,
    oled,
    preset_seconds,
    order,
    lock,
    favourites_only,
    skip_slow,
    minimum_fps,
    auto_level,
    beat_sensitivity,
    fps,
    trim,
    hold_idle,
    scan,
    lightbar,
    lightbar_beat,
    lyrics_offset,
    play_now,
    play_next,
    add_to_queue,
    start_radio,
    show_songs,
    show_albums,
    show_artist_songs,
    show_episodes,
    search_entry,
    search_again,
    more,
    static_delay,
    sign_in,
    log_out,
    account_sync,
    move_here,
    lyrics_reset,
    online_lyrics,
    retry_lyrics,
    overlay_position,
    overlay_background,
    overlay_size,
    open_setup,
    setup_again,
    setup_row,
};
// "Group with" rows carry this plus the speaker's index in players.
constexpr int kGroupTag = 100000;

// The look the Display rows under "Lyrics over visuals" change: the chosen
// overlay's; null while lyrics are off.
OverlayLook *chosen_look(Preferences &p)
{
    return p.lyrics_layer == 1 ? &p.line_look : p.lyrics_layer == 2 ? &p.stage_look : nullptr;
}
const OverlayLook *chosen_look(const Preferences &p)
{
    return p.lyrics_layer == 1 ? &p.line_look : p.lyrics_layer == 2 ? &p.stage_look : nullptr;
}
// Steps one of an overlay's three-way choices, wrapping like Track card.
bool step_look(Preferences &p, int tag, int direction)
{
    OverlayLook *look = chosen_look(p);
    if (look == nullptr)
        return false;
    int &value = tag == overlay_position     ? look->position
                 : tag == overlay_background ? look->background
                                             : look->size;
    value = (std::clamp(value, 0, 2) + direction + 3) % 3;
    return true;
}

std::string on_off(bool value)
{
    return value ? "On" : "Off";
}
std::string capitalised(std::string value)
{
    if (!value.empty() && value[0] >= 'a' && value[0] <= 'z')
        value[0] = static_cast<char>(value[0] - 'a' + 'A');
    return value;
}
std::string duration_label(int seconds)
{
    if (seconds < 60)
        return std::to_string(seconds) + " s";
    const std::string minutes = std::to_string(seconds / 60) + " min";
    return seconds % 60 == 0 ? minutes : minutes + " " + std::to_string(seconds % 60) + " s";
}
std::string percent(int value)
{
    return std::to_string(value) + "%";
}
// LG names a timing by what it is measured against (sync-figure.js timingLabel).
std::string timing_label(int ms)
{
    if (ms == 0)
        return "With the music";
    return std::to_string(std::abs(ms)) + " ms " + (ms > 0 ? "after" : "before") + " the music";
}
// LG's lyric notes (player-view.js), under the Lyrics title.
std::string lyrics_note(const LyricsState &lyrics)
{
    switch (lyrics.status)
    {
    case LyricsStatus::loading:
        return "Finding lyrics…";
    case LyricsStatus::error:
        return lyrics.retry_at_ms != 0 ? "Lyrics unavailable. Retrying shortly…"
                                       : "Lyrics couldn’t load. Retry below.";
    case LyricsStatus::missing:
        return "No lyrics for this song.";
    default:
        return "Timing moves the words only, not the music.";
    }
}
std::string delay_label(int ms)
{
    if (ms < 0)
        return "Unknown";
    return ms == 0 ? "None" : "Sound sent " + std::to_string(ms) + " ms early";
}
std::string media_label(const std::string &type)
{
    if (type == "track")
        return "Song";
    if (type == "podcast_episode")
        return "Episode";
    return capitalised(type);
}
std::string trimmed(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
int repeat_index(const std::string &mode)
{
    return mode == "all" ? 1 : mode == "one" ? 2 : 0;
}
// Moves value by delta inside [low, high]; true when it changed.
bool step_clamped(int &value, int delta, int low, int high)
{
    const int next = std::clamp(value + delta, low, high);
    const bool changed = next != value;
    value = next;
    return changed;
}
// Steps through a fixed set of choices, wrapping at both ends.
template <std::size_t N> int step_choice(const int (&choices)[N], int value, int direction)
{
    const auto it = std::find(std::begin(choices), std::end(choices), value);
    const int at = it == std::end(choices) ? 0 : static_cast<int>(it - std::begin(choices));
    const int count = static_cast<int>(N);
    return choices[(at + direction + count) % count];
}
// Two requests list the same thing (pagination aside).
bool same_request(const BrowseRequest &a, const BrowseRequest &b)
{
    return a.category == b.category && a.item_id == b.item_id && a.provider == b.provider &&
           a.media_type == b.media_type && a.query == b.query;
}
bool sign_in_purpose(KeyboardPurpose purpose)
{
    return purpose == KeyboardPurpose::sign_in_host ||
           purpose == KeyboardPurpose::sign_in_username ||
           purpose == KeyboardPurpose::sign_in_password;
}

// The speaker the queue belongs to, as the Queue and Source rows name it.
std::string speaker_name(const Presentation &s)
{
    const auto &p = s.preferences;
    if (!p.follow)
        return "This PS5";
    // What commands act on: the chosen speaker, else the one followed (speakers.hpp).
    if (!s.companion.target_name.empty())
        return s.companion.target_name;
    for (const auto &player : s.companion.players)
        if (!p.player.empty() && player.id == p.player)
            return player.name;
    if (!s.companion.player_name.empty())
        return s.companion.player_name;
    return p.player.empty() ? "Active speaker" : "Chosen speaker";
}
std::string plays_on(const Presentation &s)
{
    if (!s.companion.target_name.empty())
        return "Plays on " + s.companion.target_name;
    if (!s.companion.player_name.empty())
        return "Plays on " + s.companion.player_name;
    return s.preferences.follow ? "Plays on the active speaker" : "Plays on this PS5";
}
bool companion_ready(const Presentation &s)
{
    return s.companion.connected && !s.companion.action_busy && !s.companion_action_pending;
}
const CompanionPlayer *player_named(const Presentation &s, const std::string &id)
{
    for (const auto &player : s.companion.players)
        if (player.id == id)
            return &player;
    return nullptr;
}
// Playing on: what a speaker is, and whether picking it moves the music.
std::string speaker_detail(const Presentation &s, const CompanionPlayer &player)
{
    if (!player.available)
        return "Unavailable";
    const std::string kind = player.id == s.companion.own_player_id ? "This PS5"
                             : player.type == "group"               ? "Group"
                             : !player.synced_to.empty() || !player.group_members.empty()
                                 ? "Grouped"
                                 : "Speaker";
    return player.takes_music && s.companion.connected ? kind + " · moves the music here" : kind;
}
std::string account_summary(const AccountState &a)
{
    switch (a.source)
    {
    case AccountState::Source::signed_in:
        return a.username.empty() ? "Signed in" : a.username;
    case AccountState::Source::config:
        return "afterglow.conf";
    default:
        return "Not signed in";
    }
}

// Builds a page's rows: one call per row, the tag says what it does.
struct Rows
{
    std::vector<SheetRow> list;
    SheetRow &row(std::string title, int tag, std::string value = {})
    {
        SheetRow row;
        row.item.title = std::move(title);
        row.item.tag = tag;
        row.item.value = std::move(value);
        list.push_back(std::move(row));
        return list.back();
    }
    void header(std::string title)
    {
        SheetRow row;
        row.item.title = std::move(title);
        row.item.header = true;
        list.push_back(std::move(row));
    }
    // A browse result: artwork, title, and who made it or what it is.
    void item(const CompanionTrack &item, int tag)
    {
        auto &r = row(item.title.empty() ? "Untitled" : item.title, tag);
        r.item.subtitle = browse_detail(item);
        r.thumbnail = true;
        r.artwork = item.artwork;
    }
};

void draw_facts(ui::Canvas &canvas, const std::vector<std::pair<std::string, std::string>> &facts,
                float top, float column, float row, float right = kRight)
{
    auto &list = canvas.list;
    const auto &font = canvas.fonts.semibold;
    // Baselines sit where they did in LG's 72 px rows, scaled to the row.
    const float label = row * 0.611f, value = row * 0.639f;
    for (std::size_t i = 0; i < facts.size(); ++i)
    {
        const float y = top + row * static_cast<float>(i);
        ui::text(list, font, font.font->fit(facts[i].first, style::kTypeMin, column - 16), kLeft,
                 y + label, style::kTypeMin, style::kMuted);
        ui::text(list, font,
                 font.font->fit(facts[i].second, style::kTypeLabel, right - kLeft - column),
                 kLeft + column, y + value, style::kTypeLabel, style::kPaper);
        list.rounded_rect({kLeft, y + row - 2, right - kLeft, 2}, 0, style::kRule);
    }
}

const char *select_label(const SheetRow &row)
{
    switch (row.item.tag)
    {
    case more:
        return "Load more";
    case search_again:
    case search_entry:
        return "Search";
    case sign_in:
        return "Sign in";
    case log_out:
        return "Log out";
    case retry:
        return "Try again";
    case queue_earlier:
    case queue_later:
        return "Show";
    case setup_again:
        return "Check again";
    case setup_row:
        return nullptr;
    default:
        break;
    }
    if (row.item.tag >= kGroupTag)
        return row.item.value == "Grouped" ? "Ungroup" : "Group";
    return "Select";
}
} // namespace

int up_next_start(const CompanionSnapshot &c)
{
    return std::max(c.queue_index + 1, 0);
}
int up_next_first(const CompanionSnapshot &c)
{
    return std::max(c.queue_offset - up_next_start(c), 0);
}
int later_songs(const CompanionSnapshot &c)
{
    const int shown = std::min(static_cast<int>(c.upcoming.size()), kQueueWindow);
    const int first = up_next_first(c);
    const int remaining = std::max(c.queue_total - up_next_start(c), first + shown);
    return remaining - (first + shown);
}

std::string browse_detail(const CompanionTrack &item)
{
    const auto &type = item.media_type;
    std::string out = media_label(type);
    const auto add = [&out](const std::string &part)
    {
        if (!part.empty())
            out += (out.empty() ? "" : " · ") + part;
    };
    if (type == "album" || type == "track")
    {
        add(item.artist);
        if (type == "album")
            add(item.year);
        else if (item.duration > 0)
            add(std::to_string(item.duration / 60) + ":" + (item.duration % 60 < 10 ? "0" : "") +
                std::to_string(item.duration % 60));
    }
    // Companion's per-type line: authors, episode date and length, publisher
    // and episode count, an artist's item count.
    else if (!item.subtitle.empty())
        add(item.subtitle);
    else
        add(item.artist);
    return out;
}

float sync_picture_at(int trim_ms)
{
    return std::clamp(0.5f + static_cast<float>(trim_ms) / (2 * kSyncSpanMs), 0.02f, 0.98f);
}

Panel::Panel()
{
    categories_.look.row_height = kCategoryRow;
    categories_.set_bounds(
        {kBrowseLeft, kBrowseTop, kCategoryRight - kBrowseLeft, kListBottom - kBrowseTop});
    rebuild();
}

bool Panel::browse_page() const
{
    return page_ == Page::browse || page_ == Page::collection || page_ == Page::item_actions ||
           page_ == Page::search || page_ == Page::artist;
}

const char *Panel::page_name() const
{
    constexpr const char *kNames[] = {
        "browse",   "collection", "item_actions", "queue",   "queue_actions", "details", "display",
        "settings", "source",     "visuals",      "presets", "lyrics_timing", "status",  "search",
        "artist",   "players",    "sync",         "account", "home",          "setup"};
    const auto index = static_cast<std::size_t>(page_);
    return index < std::size(kNames) ? kNames[index] : "";
}

bool Panel::covers_screen() const
{
    return browse_page() || (page_ == Page::home && home_.covers_screen());
}

std::string Panel::title() const
{
    const auto &s = presentation();
    switch (page_)
    {
    case Page::queue:
        return "Queue";
    case Page::queue_actions:
        return selected_track_.title.empty() ? "Queued song" : selected_track_.title;
    case Page::details:
        return !s.companion.track.title.empty() ? s.companion.track.title
               : !s.title.empty()               ? s.title
                                                : "Nothing playing";
    case Page::display:
        return "Display";
    case Page::settings:
        return "Settings";
    case Page::source:
        return "Source";
    case Page::visuals:
        return "Visuals & presets";
    case Page::presets:
        return "Presets";
    case Page::lyrics_timing:
        return "Lyrics";
    case Page::status:
        return "Status";
    case Page::players:
        return "Playing on";
    case Page::sync:
        return "Sound & sync";
    case Page::account:
        return "Account";
    case Page::home:
        return "Home";
    case Page::setup:
        return "Check my setup";
    default:
        return search_root_ ? "Search" : "Browse";
    }
}

bool Panel::delay_available() const
{
    const auto &s = presentation();
    return !s.preferences.follow && s.companion_configured && s.companion.static_delay_ms >= 0;
}

std::string Panel::note() const
{
    const auto &s = presentation();
    const auto &p = s.preferences;
    switch (page_)
    {
    case Page::queue_actions:
        return selected_track_.artist.empty() ? "In the queue"
                                              : selected_track_.artist + " · in the queue";
    case Page::details:
    {
        const auto &artist =
            !s.companion.track.artist.empty() ? s.companion.track.artist : s.artist;
        return artist.empty() ? speaker_name(s) : artist + " · " + speaker_name(s);
    }
    case Page::display:
        return "Saved on this PS5.";
    case Page::source:
    case Page::players:
        return "Afterglow shows and controls this player’s music.";
    case Page::visuals:
        if (p.fps == 120 && s.accepted_refresh_hz < 120)
            return "Reopen Afterglow for 120 Hz; the display runs at " +
                   std::to_string(s.accepted_refresh_hz) + " Hz now.";
        if (!s.refresh_confirmed)
            return "The display’s refresh rate is not confirmed yet.";
        return "The display runs at " + std::to_string(s.accepted_refresh_hz) + " Hz.";
    case Page::presets:
        return std::to_string(s.presets.size()) + " presets · " +
               std::to_string(p.favorites.size()) + " favourites";
    case Page::lyrics_timing:
        return lyrics_note(s.lyrics);
    case Page::sync:
        // Compensation is this PS5's own Music Assistant player setting; say
        // plainly why it cannot change rather than offering a dead row.
        if (!s.companion_configured)
            return "TV output compensation needs Music Assistant.";
        if (p.follow)
            return "TV output compensation applies when the source is This PS5.";
        if (s.companion.static_delay_ms < 0)
            return "Reading this PS5’s player settings from Music Assistant…";
        return "Compensation is saved as this PS5’s delay in Music Assistant.";
    case Page::account:
        if (!s.account.status.empty())
            return s.account.status;
        return s.account.source == AccountState::Source::none
                   ? "Sign in to browse and control Music Assistant."
                   : "Afterglow uses this account for Music Assistant.";
    case Page::status:
        return std::string("Afterglow ") + kVersion + " for PS5.";
    case Page::home:
        return home_.note();
    case Page::setup:
        return setup_note(s.setup_rows);
    default:
        return {};
    }
}

std::vector<std::pair<std::string, std::string>> Panel::facts() const
{
    const auto &s = presentation();
    std::vector<std::pair<std::string, std::string>> out;
    const auto add = [&](const char *label, const std::string &value)
    {
        if (!value.empty())
            out.emplace_back(label, value);
    };
    if (page_ == Page::details)
    {
        const auto &track = s.companion.track;
        add("Artist", !track.artist.empty() ? track.artist : s.artist);
        add("Album", !track.album.empty() ? track.album : s.album);
        add("Year", track.year);
        // The grade, then the badge's stream details without repeating it.
        add("Quality", track.quality);
        std::string badge = track.quality_badge;
        const std::string lead = track.quality + " · ";
        if (!track.quality.empty() && badge.rfind(lead, 0) == 0)
            badge = badge.substr(lead.size());
        if (badge == track.quality)
            badge.clear();
        add("Format", !badge.empty() ? badge : track.format);
        add("Source", capitalised(track.provider));
    }
    else if (page_ == Page::status)
    {
        out.emplace_back("Connection", s.status);
        out.emplace_back("Music Assistant", !s.companion_configured ? "Not set up"
                                            : s.companion.status.empty()
                                                ? (s.companion.connected ? "Connected" : "Offline")
                                                : s.companion.status);
        out.emplace_back("Audio buffer", std::to_string(static_cast<int>(s.buffered_ms)) + " ms");
        out.emplace_back("Underruns", std::to_string(s.underruns));
        out.emplace_back("Late audio frames", std::to_string(s.late_frames));
        out.emplace_back("Output errors", std::to_string(s.output_errors));
        out.emplace_back("Reconnects", std::to_string(s.reconnects));
        out.emplace_back("Received packets", std::to_string(s.received_packets));
        out.emplace_back("Frame rate", std::to_string(static_cast<int>(s.fps + 0.5f)) + " fps");
        out.emplace_back("Display", s.refresh_confirmed
                                        ? std::to_string(s.accepted_refresh_hz) + " Hz"
                                        : "Not confirmed");
        out.emplace_back("Preferences", s.save_status.empty() ? "Not saved yet" : s.save_status);
        add("Dev request", s.dev_status);
    }
    else if (page_ == Page::account)
    {
        const auto &a = s.account;
        out.emplace_back("Signed in with", a.source == AccountState::Source::signed_in ? "This PS5"
                                           : a.source == AccountState::Source::config
                                               ? "Token in afterglow.conf"
                                               : "Nobody yet");
        add("Server", a.host);
        add("User", a.username);
        add("Settings sync", a.sync_status);
    }
    return out;
}

float Panel::fact_row() const
{
    return page_ == Page::status ? kStatusFactRow : kFactRow;
}

float Panel::rule_y() const
{
    return note().empty() ? kRuleBare : kRuleWithNote;
}

float Panel::list_top() const
{
    if (page_ == Page::lyrics_timing)
        return kLyricsListTop;
    const float top = rule_y() + 12;
    if (page_ == Page::details && !share_link().url.empty())
        return top + std::max(fact_row() * static_cast<float>(facts().size()), kShareBlockHeight) +
               24;
    if (page_ == Page::details || page_ == Page::account)
        return top + fact_row() * static_cast<float>(facts().size()) + 24;
    if (page_ == Page::sync)
        return top + kFigureHeight;
    return top;
}

std::vector<SheetRow> Panel::queue_rows() const
{
    const auto &s = presentation();
    const auto &c = s.companion;
    const bool live = c.connected;
    Rows rows;
    rows.row("Playing on", playing_on, speaker_name(s)).item.chevron = true;
    if (c.can_move_here)
        rows.row("Play on this PS5", move_here, "Moves the music here").item.disabled = !live;
    const int shown = std::min(static_cast<int>(c.upcoming.size()), kQueueWindow);
    const int first = up_next_first(c);
    const int remaining = std::max(c.queue_total - up_next_start(c), first + shown);
    // Numbers only once there is more than one window of songs.
    const bool paged = first > 0 || remaining > shown;
    rows.header(paged && shown > 0
                    ? "Up next · " + std::to_string(first + 1) + "–" +
                          std::to_string(first + shown) + " of " + std::to_string(remaining)
                    : "Up next");
    if (first > 0)
        rows.row("Earlier songs", queue_earlier, std::to_string(std::min(first, kQueueWindow)));
    for (int i = 0; i < shown; ++i)
    {
        const auto &track = c.upcoming[static_cast<std::size_t>(i)];
        auto &row = rows.row(track.title.empty() ? "Untitled" : track.title, i, track.artist);
        row.thumbnail = true;
        row.artwork = track.artwork;
    }
    if (shown == 0)
        rows.row("Nothing queued next", info).item.disabled = true;
    const int later = later_songs(c);
    if (shown > 0 && later > 0)
        rows.row("Later songs", queue_later, std::to_string(later) + " more");
    rows.header("Playback");
    rows.row("Shuffle", shuffle, on_off(c.shuffle)).item.disabled = !live;
    auto &rep = rows.row("Repeat", repeat, kRepeatLabels[repeat_index(c.repeat)]);
    rep.adjustable = true;
    rep.item.disabled = !live;
    rows.row("Crossfade", crossfade, on_off(c.crossfade)).item.disabled = !live;
    rows.row("Volume", volume, std::to_string(s.volume) + "%").adjustable = true;
    const std::string running =
        c.sleep_seconds > 0
            ? "Sleep timer · " + std::to_string((c.sleep_seconds + 59) / 60) + " min left"
            : "Sleep timer";
    auto &timer = rows.row(running, sleep,
                           sleep_minutes_ > 0 ? std::to_string(sleep_minutes_) + " min" : "Off");
    timer.adjustable = true;
    timer.item.disabled = !live;
    rows.row("Keep playing when the queue ends", autoplay, on_off(c.autoplay)).item.disabled =
        !live;
    return std::move(rows.list);
}

std::vector<SheetRow> Panel::players_rows() const
{
    const auto &s = presentation();
    const auto &p = s.preferences;
    const auto &c = s.companion;
    Rows rows;
    // With music elsewhere that can move here, this PS5's row brings it along.
    if (c.can_move_here)
        rows.row("Play on this PS5", move_here, "Moves the music here").item.disabled =
            !c.connected;
    else
        rows.row("This PS5", play_here, "Sound from the PS5").chosen = !p.follow;
    auto &automatic =
        rows.row("Active speaker", follow_auto,
                 s.companion_configured ? "Whatever is playing" : "Needs Music Assistant");
    automatic.chosen = p.follow && p.player.empty();
    automatic.item.disabled = !s.companion_configured;
    if (!c.players.empty())
        rows.header("Speakers");
    for (std::size_t i = 0; i < c.players.size(); ++i)
    {
        const auto &player = c.players[i];
        auto &speaker = rows.row(player.name, static_cast<int>(i), speaker_detail(s, player));
        speaker.chosen = p.follow && p.player == player.id;
        speaker.item.disabled = !s.companion_configured || !player.available;
    }
    // Speakers together: who sounds the target's music (LG pick.js groupOptions).
    const auto options = group_options(c.players, c.queue_id);
    if (std::none_of(options.begin(), options.end(), [](const auto &o) { return o.allowed; }))
        return std::move(rows.list);
    rows.header("Speakers together");
    for (const auto &option : options)
    {
        const auto index = static_cast<std::size_t>(
            std::find_if(c.players.begin(), c.players.end(),
                         [&](const auto &player) { return player.id == option.id; }) -
            c.players.begin());
        auto &row = rows.row(option.name, kGroupTag + static_cast<int>(index),
                             option.leader    ? "Leads the group"
                             : option.allowed ? ""
                             : option.member  ? "Always in the group"
                                              : "Can’t join");
        row.chosen = option.member;
        row.item.disabled = !c.connected || !option.allowed;
    }
    return std::move(rows.list);
}

std::vector<SheetRow> Panel::visuals_rows() const
{
    const auto &s = presentation();
    const auto &p = s.preferences;
    Rows rows;
    rows.header("Picture");
    rows.row("Visual brightness", brightness, percent(p.brightness)).adjustable = true;
    rows.row("Text brightness", text_brightness, percent(p.text_brightness)).adjustable = true;
    rows.row("Album colour tint", tint, on_off(p.tint));
    if (p.tint)
        rows.row("Colour strength", tint_strength, percent(p.tint_strength)).adjustable = true;
    rows.row("Night register", night_register, on_off(p.night_register));
    rows.row("OLED text movement", oled, on_off(p.oled_drift));
    rows.header("Presets");
    rows.row("Preset duration", preset_seconds, duration_label(p.preset_seconds)).adjustable = true;
    rows.row("Preset order", order, p.sequential ? "In order" : "Shuffled");
    rows.row("Lock this preset", lock, on_off(p.locked));
    rows.row("Favourites only", favourites_only, on_off(p.favorites_only));
    rows.row("Skip slow presets", skip_slow, on_off(p.skip_slow));
    rows.row("Minimum preset frame rate", minimum_fps, std::to_string(p.minimum_fps) + " fps")
        .adjustable = true;
    rows.row("MilkDrop automatic level", auto_level, on_off(p.auto_level));
    rows.row("Beat sensitivity", beat_sensitivity, percent(p.beat_sensitivity)).adjustable = true;
    rows.header("Performance");
    rows.row("Visual frame rate", fps,
             p.fps == 120 && s.accepted_refresh_hz < 120 ? "120 fps after reopening"
                                                         : std::to_string(p.fps) + " fps")
        .adjustable = true;
    rows.row("Hold visuals when idle", hold_idle, on_off(p.hold_idle));
    rows.row(s.scanning ? "Stop measuring presets" : "Measure preset performance", scan,
             s.scanning ? "Running" : "About 35 min");
    rows.header("Controller light");
    rows.row("Light bar follows artwork", lightbar, on_off(p.lightbar));
    rows.row("Pulse on the beat", lightbar_beat, on_off(p.lightbar_beat)).item.disabled =
        !p.lightbar;
    return std::move(rows.list);
}

std::vector<SheetRow> Panel::account_rows() const
{
    const auto &s = presentation();
    const auto &a = s.account;
    Rows rows;
    const bool signed_in = a.source == AccountState::Source::signed_in;
    rows.row(signed_in ? "Sign in again" : "Sign in", sign_in, "Music Assistant account")
        .item.disabled = a.busy || sign_in_purpose(keyboard_wait_);
    auto &out = rows.row("Log out", log_out);
    out.item.disabled = !signed_in || a.busy;
    // As LG says for its development token: a file-given token is not ours to drop.
    if (a.source == AccountState::Source::config)
        out.item.subtitle = "The token in afterglow.conf has nothing to log out of";
    rows.row("Sync settings with Music Assistant", account_sync,
             on_off(s.preferences.account_sync));
    return std::move(rows.list);
}

std::vector<SheetRow> Panel::sheet_rows() const
{
    const auto &s = presentation();
    const auto &p = s.preferences;
    const auto &c = s.companion;
    Rows rows;
    switch (page_)
    {
    case Page::queue:
        return queue_rows();
    case Page::players:
        return players_rows();
    case Page::visuals:
        return visuals_rows();
    case Page::account:
        return account_rows();
    case Page::queue_actions:
    {
        // The song may have moved since it was chosen: find it by its stable id.
        int index = -1;
        for (std::size_t i = 0; i < c.upcoming.size(); ++i)
            if (c.upcoming[i].queue_item_id == selected_track_.queue_item_id)
                index = static_cast<int>(i);
        const bool ready = companion_ready(s) && index >= 0;
        // Up next counts from the song after the playing one, across windows.
        const int position = up_next_first(c) + index;
        const int last = std::max(c.queue_total - up_next_start(c),
                                  up_next_first(c) + static_cast<int>(c.upcoming.size())) -
                         1;
        rows.row("Play now", queue_play).item.disabled = !ready;
        rows.row("Play next", queue_next).item.disabled = !ready || position == 0;
        rows.row("Move up", queue_up).item.disabled = !ready || position == 0;
        rows.row("Move down", queue_down).item.disabled = !ready || position >= last;
        rows.row("Remove from the queue", queue_remove).item.disabled = !ready;
        break;
    }
    case Page::details:
    {
        const bool ready = companion_ready(s) && !c.track.uri.empty();
        rows.row(c.track.favorite ? "Remove from music favourites" : "Add to music favourites",
                 favourite)
            .item.disabled = !ready;
        rows.row("Start radio from this song", radio, "Replaces the queue").item.disabled = !ready;
        break;
    }
    case Page::display:
        rows.header("Show");
        for (int i = 0; i < 4; ++i)
            rows.row(kModes[i], mode_visuals + i).chosen = p.display_mode == i;
        rows.header("Lyrics over visuals");
        for (int i = 0; i < 3; ++i)
            rows.row(kLayers[i], layer_off + i).chosen = p.lyrics_layer == i;
        if (const OverlayLook *look = chosen_look(p))
        {
            rows.row("Position", overlay_position,
                     kOverlayPositions[std::clamp(look->position, 0, 2)])
                .adjustable = true;
            rows.row("Background", overlay_background,
                     kOverlayBackgrounds[std::clamp(look->background, 0, 2)])
                .adjustable = true;
            rows.row("Size", overlay_size, kOverlaySizes[std::clamp(look->size, 0, 2)]).adjustable =
                true;
        }
        rows.header("Extras");
        rows.row("MilkDrop behind album and lyrics", poster_visuals, on_off(p.poster_visuals));
        rows.row("Track card", track_card, kTrackCards[std::clamp(p.track_info, 0, 2)]).adjustable =
            true;
        rows.row("Visual style", visual_style, kVisualStyles[std::clamp(p.visual_style, 0, 2)])
            .adjustable = true;
        rows.row("Reduce motion", reduce_motion, on_off(p.reduce_motion));
        rows.row("Word-by-word lyrics", karaoke, on_off(p.karaoke));
        break;
    case Page::settings:
        rows.row("Source", open_source,
                 !p.follow                                   ? "This PS5"
                 : p.player.empty() && c.player_name.empty() ? "Following the active speaker"
                                                             : "Following " + speaker_name(s))
            .item.chevron = true;
        rows.row("Visuals & presets", open_visuals).item.chevron = true;
        rows.row("Lyrics", open_lyrics_timing,
                 p.lyrics_offset_ms != 0 ? lyrics_timing_label(p.lyrics_offset_ms) : "")
            .item.chevron = true;
        rows.row("Presets", open_presets,
                 s.presets.empty() ? "None" : std::to_string(s.presets.size()))
            .item.chevron = true;
        rows.row("Sound & sync", open_sync, timing_label(p.trim_ms)).item.chevron = true;
        rows.row("Account", open_account, account_summary(s.account)).item.chevron = true;
        rows.row("Status", open_status, s.connected ? "Connected" : "Offline").item.chevron = true;
        rows.row("Check my setup", open_setup, setup_summary(s.setup_rows, s.setup_has_run))
            .item.chevron = true;
        break;
    case Page::setup:
        // A failed or busy check says so in semibold, its next step beneath.
        for (const auto &check : s.setup_rows)
        {
            auto &r = rows.row(check.label, setup_row, check.value);
            r.item.subtitle = check.next;
            r.wide_subtitle = true;
            r.strong = check.state == SetupState::fail || check.state == SetupState::busy;
        }
        rows.row("Check again", setup_again);
        break;
    case Page::source:
    {
        rows.row("This PS5", play_here, "Sound from the PS5").chosen = !p.follow;
        auto &automatic =
            rows.row("Active speaker", follow_auto,
                     s.companion_configured ? "Whatever is playing" : "Needs Music Assistant");
        automatic.chosen = p.follow && p.player.empty();
        automatic.item.disabled = !s.companion_configured;
        if (!c.players.empty())
            rows.header("Speakers");
        for (std::size_t i = 0; i < c.players.size(); ++i)
        {
            auto &speaker = rows.row(c.players[i].name, static_cast<int>(i), "Speaker");
            speaker.chosen = p.follow && p.player == c.players[i].id;
            speaker.item.disabled = !s.companion_configured;
        }
        break;
    }
    case Page::sync:
    {
        auto &delay = rows.row("TV output compensation", static_delay, delay_label(shown_delay()));
        delay.adjustable = true;
        delay.item.disabled = !delay_available();
        rows.row("Picture & lyrics timing", trim, timing_label(p.trim_ms)).adjustable = true;
        break;
    }
    case Page::presets:
        for (std::size_t i = 0; i < s.presets.size(); ++i)
        {
            const auto &name = s.presets[i];
            std::string value = p.favorite(name) ? "Favourite" : "";
            const auto add = [&value](const std::string &part)
            { value += (value.empty() ? "" : " · ") + part; };
            const auto found = s.quality.ratings().find(name);
            if (found != s.quality.ratings().end())
            {
                const auto &q = found->second;
                if (q.quarantined)
                    add("Quarantined");
                else if (q.measured)
                    add(std::to_string(static_cast<int>(q.fps + 0.5)) + " fps");
                if (q.keep)
                    add("Kept");
            }
            rows.row(name, static_cast<int>(i), value).chosen = name == s.preset;
        }
        if (rows.list.empty())
            rows.row("No presets found", info).item.disabled = true;
        break;
    case Page::lyrics_timing:
    {
        // LG Display → Lyrics: timing, its reset, the online search and a retry.
        rows.row("Lyrics timing", lyrics_offset, lyrics_timing_label(p.lyrics_offset_ms))
            .adjustable = true;
        if (p.lyrics_offset_ms != 0)
            rows.row("Reset lyrics timing", lyrics_reset);
        rows.row("Online lyrics", online_lyrics, on_off(p.online_lyrics)).item.subtitle =
            "When Music Assistant has none, ask lrclib.net";
        if (s.lyrics.status == LyricsStatus::error || s.lyrics.status == LyricsStatus::missing)
            rows.row("Retry lyrics", retry_lyrics);
        break;
    }
    default:
        break;
    }
    return std::move(rows.list);
}

std::vector<SheetRow> Panel::category_rows() const
{
    Rows rows;
    rows.row("Search", search_entry).chosen = search_root_;
    for (int i = 0; i < kCategoryCount; ++i)
        rows.row(kCategories[i].name, i).chosen = !search_root_ && i == category_;
    return std::move(rows.list);
}

std::vector<SheetRow> Panel::list_rows() const
{
    const auto &s = presentation();
    const auto &c = s.companion;
    const auto &items = c.browse_items;
    const bool loading = s.browse_pending || c.browse_loading;
    const bool search = page_ == Page::search;
    Rows rows;
    if (search)
        rows.row(query_.empty() ? "Type what to look for" : "Search again", search_again, query_);
    if (search && query_.empty())
        return std::move(rows.list);
    // A first page replaces the list; "More" keeps it and appends below.
    if (!appending_ && loading)
    {
        rows.row(search ? "Searching…" : "Loading music…", info).item.disabled = true;
        return std::move(rows.list);
    }
    if (!appending_ && !c.browse_error.empty())
    {
        rows.row(search ? "The search failed" : "Music couldn’t load", retry, "Try again");
        return std::move(rows.list);
    }
    if (search)
    {
        std::vector<bool> placed(items.size(), false);
        for (const auto &group : kResultGroups)
        {
            bool headed = false;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                if (items[i].media_type != group.media_type)
                    continue;
                if (!headed)
                    rows.header(group.title);
                headed = true;
                placed[i] = true;
                rows.item(items[i], static_cast<int>(i));
            }
        }
        bool headed = false;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (placed[i])
                continue;
            if (!headed)
                rows.header("Other results");
            headed = true;
            rows.item(items[i], static_cast<int>(i));
        }
    }
    else
        for (std::size_t i = 0; i < items.size(); ++i)
            rows.item(items[i], static_cast<int>(i));
    // The item a Play next / Add answered for reads what happened, with a tick.
    for (auto &row : rows.list)
        if (confirmed_left_ > 0 && row.item.tag >= 0 &&
            static_cast<std::size_t>(row.item.tag) < items.size() &&
            items[static_cast<std::size_t>(row.item.tag)].uri == confirmed_uri_)
        {
            row.chosen = true;
            row.item.subtitle = confirmed_text_;
        }
    if (items.empty())
        rows.row(search                  ? "Nothing found for “" + query_ + "”"
                 : page_ == Page::browse ? "Nothing here yet"
                 : page_ == Page::artist ? "Nothing by this artist yet"
                                         : "No songs in this collection",
                 info)
            .item.disabled = true;
    const bool arrived = items.size() > append_from_ || !c.browse_has_more;
    if (appending_ && !c.browse_error.empty())
        rows.row("More couldn’t load", retry, "Try again");
    else if (appending_ && (loading || !arrived))
        rows.row("Loading more…", info).item.disabled = true;
    else if (c.browse_has_more && !items.empty())
        rows.row("More", more, std::to_string(items.size()) + " shown");
    return std::move(rows.list);
}

std::vector<SheetRow> Panel::action_rows() const
{
    const auto &s = presentation();
    const auto &type = subject_.media_type;
    const bool ready = companion_ready(s) && !subject_.uri.empty();
    const bool artist = type == "artist";
    Rows rows;
    rows.row("Play now", play_now).item.disabled = !ready;
    if (!artist)
    {
        rows.row("Play next", play_next).item.disabled = !ready;
        rows.row("Add to the queue", add_to_queue).item.disabled = !ready;
    }
    // Music Assistant builds radio from songs, albums, playlists and artists.
    if (type != "radio" && type != "podcast" && type != "podcast_episode" && type != "audiobook")
        rows.row(artist ? "Start radio from this artist" : "Start radio from this", start_radio)
            .item.disabled = !ready;
    if (type == "album" || type == "playlist")
        rows.row("Show songs", show_songs).item.chevron = true;
    if (artist)
    {
        rows.row("Show albums", show_albums).item.chevron = true;
        rows.row("Show songs", show_artist_songs).item.chevron = true;
    }
    if (type == "podcast")
        rows.row("Show episodes", show_episodes).item.chevron = true;
    for (auto &row : rows.list)
        if (failed_tag_ != 0 && row.item.tag == failed_tag_)
            row.item.subtitle = "Couldn’t do that. Cross tries again";
    return std::move(rows.list);
}

void Panel::rebuild_browse()
{
    const auto &s = presentation();
    const auto &c = s.companion;
    categories_.set_rows(category_rows());
    const bool loading = s.browse_pending || c.browse_loading;
    if (appending_ && !loading && c.browse_error.empty() &&
        (c.browse_items.size() > append_from_ || !c.browse_has_more))
        appending_ = false;
    const bool actions = page_ == Page::item_actions;
    sheet_.look = SheetLook{};
    sheet_.look.row_height = actions ? kActionRow : kCollectionRow;
    sheet_.set_bounds(
        {kCollectionLeft, kCollectionTop, kRight - kCollectionLeft, kListBottom - kCollectionTop});
    sheet_.set_rows(actions ? action_rows() : list_rows());
    if (pending_focus_ >= 0 && !actions && !loading)
    {
        sheet_.set_focus(pending_focus_);
        pending_focus_ = -1;
    }
    categories_.set_active(!browse_right_);
    sheet_.set_active(browse_right_);
}

void Panel::rebuild()
{
    if (browse_page())
    {
        rebuild_browse();
        return;
    }
    if (page_ == Page::lyrics_timing)
    {
        const auto &source = presentation().companion.track.lrc_lyrics;
        if (source != lyric_source_)
        {
            lyric_source_ = source;
            lyric_lines_ = parse_lyrics(source, true);
        }
    }
    sheet_.look = SheetLook{};
    if (page_ == Page::queue)
        sheet_.look.thumbnail = kQueueThumbnail;
    sheet_.set_active(true);
    const float top = list_top();
    sheet_.set_bounds({kLeft, top, kRight - kLeft, kListBottom - top});
    sheet_.set_rows(sheet_rows());
}

void Panel::navigate(Page page, int focus)
{
    page_ = page;
    refresh_ = 0.5f;
    rebuild();
    if (focus <= 0)
        sheet_.focus_first();
    else
        sheet_.set_focus(focus);
}

void Panel::open(Page page)
{
    // Drill-downs need a chosen item; opened cold they mean Browse.
    const bool search = page == Page::search;
    if (page == Page::collection || page == Page::item_actions || page == Page::artist || search)
        page = Page::browse;
    root_ = page;
    browse_right_ = false;
    trail_.clear();
    search_root_ = false;
    appending_ = false;
    pending_focus_ = -1;
    play_id_ = 0;
    failed_tag_ = 0;
    confirmed_left_ = 0;
    leave_ = false;
    navigate(page, page == Page::settings ? settings_focus_ : 0);
    if (page == Page::home)
        home_.open();
    // Opened from the idle poster's offer: the check runs at once.
    if (page == Page::setup)
        presentation().setup_requested = true;
    categories_.set_focus(category_ + 1);
    // Browse opens on the last category, already loading, like the LG app.
    if (page == Page::browse && presentation().companion_configured)
    {
        if (search)
            open_search();
        else
            request_category(category_);
    }
}

bool Panel::back()
{
    if (page_ == Page::home)
        return home_.back();
    if (browse_page() && !trail_.empty())
    {
        pop_browse();
        return true;
    }
    if (page_ == root_)
        return false;
    switch (page_)
    {
    case Page::search:
        browse_right_ = false;
        request_category(category_);
        categories_.set_focus(category_ + 1);
        return true;
    case Page::queue_actions:
    case Page::players:
        navigate(Page::queue, parent_focus_);
        return true;
    case Page::source:
    case Page::visuals:
    case Page::presets:
    case Page::lyrics_timing:
    case Page::status:
    case Page::sync:
    case Page::account:
    case Page::setup:
        navigate(Page::settings, parent_focus_);
        return true;
    default:
        return false;
    }
}

void Panel::load(const BrowseRequest &request)
{
    auto &s = presentation();
    s.browse_request = request;
    s.browse_pending = true;
    loaded_ = request;
    appending_ = false;
}

void Panel::request_category(int category)
{
    category_ = std::clamp(category, 0, kCategoryCount - 1);
    trail_.clear();
    search_root_ = false;
    pending_focus_ = -1;
    request_ = {};
    request_.category = kCategories[category_].category;
    subject_ = {};
    load(request_);
    navigate(Page::browse);
}

void Panel::open_search()
{
    trail_.clear();
    search_root_ = true;
    pending_focus_ = -1;
    request_ = {};
    request_.category = BrowseCategory::search;
    request_.query = query_;
    subject_ = {};
    // The last results stay behind the keyboard, so cancelling loses nothing.
    if (!query_.empty() && !same_request(request_, loaded_))
        load(request_);
    browse_right_ = true;
    navigate(Page::search);
    categories_.set_focus(0);
    ask_keyboard(KeyboardPurpose::search, "Search Music Assistant", query_);
}

void Panel::open_actions(const CompanionTrack &item)
{
    trail_.push_back({page_, request_, subject_, sheet_.focus()});
    subject_ = item;
    failed_tag_ = 0;
    browse_right_ = true;
    navigate(Page::item_actions);
}

void Panel::open_list(Page page, BrowseCategory category)
{
    trail_.push_back({page_, request_, subject_, sheet_.focus()});
    request_ = {};
    request_.category = category;
    request_.item_id = subject_.item_id;
    request_.provider = subject_.provider;
    request_.media_type = subject_.media_type;
    load(request_);
    browse_right_ = true;
    navigate(page);
}

void Panel::load_more()
{
    auto &s = presentation();
    BrowseRequest request = loaded_;
    request.offset = static_cast<int>(s.companion.browse_items.size());
    s.browse_request = request;
    s.browse_pending = true;
    appending_ = true;
    append_from_ = s.companion.browse_items.size();
}

void Panel::pop_browse()
{
    const BrowseFrame frame = trail_.back();
    trail_.pop_back();
    request_ = frame.request;
    subject_ = frame.subject;
    browse_right_ = true;
    pending_focus_ = -1;
    // An item's actions leave the list loaded; a deeper list replaced it.
    const bool searched = request_.category != BrowseCategory::search || !request_.query.empty();
    if (frame.page != Page::item_actions && searched && !same_request(request_, loaded_))
    {
        load(request_);
        pending_focus_ = frame.focus;
        navigate(frame.page);
        return;
    }
    navigate(frame.page, frame.focus);
}

void Panel::ask_keyboard(KeyboardPurpose purpose, std::string title, std::string initial,
                         bool secret)
{
    auto &s = presentation();
    s.keyboard.purpose = purpose;
    s.keyboard.title = std::move(title);
    s.keyboard.initial = std::move(initial);
    s.keyboard.secret = secret;
    s.keyboard_pending = true;
    // An answer to an older question must not be read as this one's.
    s.keyboard_result_ready = false;
    keyboard_wait_ = purpose;
}

void Panel::take_keyboard()
{
    auto &s = presentation();
    if (keyboard_wait_ == KeyboardPurpose::none || !s.keyboard_result_ready ||
        s.keyboard_result.purpose != keyboard_wait_)
        return;
    KeyboardResult result = std::move(s.keyboard_result);
    s.keyboard_result = {};
    s.keyboard_result_ready = false;
    s.keyboard_pending = false;
    keyboard_wait_ = KeyboardPurpose::none;
    answer_keyboard(result);
    // A password must not linger in this copy once it has been handed on.
    result.text.assign(result.text.size(), '\0');
}

void Panel::answer_keyboard(const KeyboardResult &result)
{
    auto &s = presentation();
    const std::string text =
        result.purpose == KeyboardPurpose::sign_in_password ? std::string() : trimmed(result.text);
    const auto cancel = [&](const char *why)
    {
        sign_in_host_.clear();
        sign_in_username_.clear();
        s.show_toast(why);
    };
    switch (result.purpose)
    {
    case KeyboardPurpose::search:
        if (!result.accepted || text.empty())
            return;
        query_ = text;
        trail_.clear();
        search_root_ = true;
        request_ = {};
        request_.category = BrowseCategory::search;
        request_.query = query_;
        load(request_);
        // Land on the first result, under its group heading.
        pending_focus_ = 1;
        browse_right_ = true;
        navigate(Page::search);
        categories_.set_focus(0);
        break;
    case KeyboardPurpose::sign_in_host:
        if (!result.accepted || text.empty())
            return cancel(result.accepted ? "Sign-in needs the server address"
                                          : "Sign-in cancelled");
        sign_in_host_ = text;
        ask_keyboard(KeyboardPurpose::sign_in_username, "Music Assistant user name",
                     s.account.username);
        break;
    case KeyboardPurpose::sign_in_username:
        if (!result.accepted || text.empty())
            return cancel(result.accepted ? "Sign-in needs a user name" : "Sign-in cancelled");
        sign_in_username_ = text;
        ask_keyboard(KeyboardPurpose::sign_in_password, "Password for " + text, {}, true);
        break;
    case KeyboardPurpose::sign_in_password:
        if (!result.accepted)
            return cancel("Sign-in cancelled");
        // Passwords are taken as typed: spaces may be part of them.
        s.sign_in_host = std::move(sign_in_host_);
        s.sign_in_username = std::move(sign_in_username_);
        s.sign_in_password = result.text;
        s.account_command = AccountCommand::sign_in;
        sign_in_host_.clear();
        sign_in_username_.clear();
        s.show_toast("Signing in…");
        break;
    case KeyboardPurpose::none:
        break;
    }
}

bool Panel::send(CompanionAction action, const char *toast)
{
    auto &s = presentation();
    if (!companion_ready(s))
        return false;
    s.companion_action = std::move(action);
    s.companion_action_pending = true;
    if (toast != nullptr)
        s.show_toast(toast);
    return true;
}

void Panel::play(int tag)
{
    // One at a time: a second press while the first is out would only race it.
    if (play_id_ != 0)
        return;
    CompanionAction action;
    action.text = subject_.uri;
    action.kind = tag == start_radio ? CompanionActionKind::radio : CompanionActionKind::play_media;
    action.option = tag == play_next ? "next" : tag == add_to_queue ? "add" : "replace";
    action.id = ++play_ids_;
    if (!send(action, nullptr))
        return;
    play_id_ = action.id;
    play_tag_ = tag;
    play_item_ = subject_;
    failed_tag_ = 0;
}

// LG browse-view.js run(): Play now and radio go back to Now playing with a
// notice; Play next and Add return to the list, where the item's own row says
// what happened. A refusal stays on the actions, named on the row pressed.
void Panel::settle_play()
{
    auto &s = presentation();
    if (play_id_ == 0 || s.companion.action_done != play_id_)
        return;
    play_id_ = 0;
    // Browse moved on meanwhile: the answer belongs to a page no longer shown.
    if (page_ != Page::item_actions || subject_.uri != play_item_.uri)
        return;
    if (!s.companion.action_done_ok)
    {
        failed_tag_ = play_tag_;
        refresh_ = 0;
        return;
    }
    const std::string name = play_item_.title.empty() ? "this" : play_item_.title;
    if (play_tag_ == play_now || play_tag_ == start_radio)
    {
        s.show_toast("Playing " + name + (play_tag_ == start_radio ? " radio" : ""));
        leave_ = true;
        return;
    }
    confirmed_uri_ = play_item_.uri;
    confirmed_text_ = play_tag_ == play_next ? "Plays next" : "Added to the queue";
    confirmed_left_ = kConfirmSeconds;
    pop_browse();
}

bool Panel::take_leave()
{
    return std::exchange(leave_, false);
}

bool Panel::confirm_list(const SheetRow &row)
{
    auto &s = presentation();
    const int tag = row.item.tag;
    switch (tag)
    {
    case retry:
        s.browse_pending = true;
        return true;
    case more:
        load_more();
        return true;
    case search_again:
        ask_keyboard(KeyboardPurpose::search, "Search Music Assistant", query_);
        return true;
    default:
        break;
    }
    if (tag < 0 || static_cast<std::size_t>(tag) >= s.companion.browse_items.size())
        return false;
    open_actions(s.companion.browse_items[static_cast<std::size_t>(tag)]);
    return true;
}

void Panel::confirm_browse(const SheetRow &row)
{
    const int tag = row.item.tag;
    if (!browse_right_)
    {
        // Choosing a category loads it and hands the focus to its list.
        if (tag == search_entry)
        {
            open_search();
            return;
        }
        browse_right_ = true;
        request_category(tag);
        return;
    }
    if (page_ == Page::item_actions)
        confirm(row);
    else
        confirm_list(row);
}

void Panel::confirm_group(int tag)
{
    auto &s = presentation();
    const auto &players = s.companion.players;
    const CompanionPlayer *leader = player_named(s, s.companion.queue_id);
    const auto index = static_cast<std::size_t>(tag - kGroupTag);
    if (leader == nullptr || index >= players.size())
        return;
    const auto &member = players[index];
    const auto options = group_options(players, leader->id);
    const auto option = std::find_if(options.begin(), options.end(),
                                     [&](const auto &o) { return o.id == member.id; });
    if (option == options.end() || !option->allowed)
        return;
    CompanionAction action;
    action.kind =
        option->member ? CompanionActionKind::group_remove : CompanionActionKind::group_add;
    action.option = leader->id;
    action.text = member.id;
    const std::string toast =
        option->member ? member.name + " left the group" : member.name + " joins " + leader->name;
    send(action, toast.c_str());
}

// Playing on: a speaker with nothing of its own playing takes the target's
// music along (LG player-view.js moveTo); one that plays is simply followed.
void Panel::choose_speaker(const CompanionPlayer &player)
{
    auto &s = presentation();
    // This PS5's own player only sounds with Source: This PS5; followed, it is silent.
    if (!player.id.empty() && player.id == s.companion.own_player_id)
    {
        CompanionAction action;
        action.kind = CompanionActionKind::transfer;
        action.text = player.id;
        action.enabled = true;
        if (page_ == Page::players && player.takes_music)
            send(action, "Moved the music to this PS5");
        s.preferences.follow = false;
        return;
    }
    if (page_ == Page::players && player.takes_music)
    {
        CompanionAction action;
        action.kind = CompanionActionKind::transfer;
        action.text = player.id;
        send(action, ("Moved the music to " + player.name).c_str());
    }
    s.preferences.follow = true;
    s.preferences.player = player.id;
}

void Panel::confirm(const SheetRow &row)
{
    auto &s = presentation();
    auto &p = s.preferences;
    const auto &c = s.companion;
    const int tag = row.item.tag;
    const auto open_child = [this](Page page)
    {
        parent_focus_ = sheet_.focus();
        navigate(page);
    };
    if (tag >= kGroupTag)
    {
        confirm_group(tag);
        return;
    }
    if (tag >= 0)
    {
        const auto index = static_cast<std::size_t>(tag);
        if (page_ == Page::queue && index < c.upcoming.size())
        {
            selected_track_ = c.upcoming[index];
            selected_queue_index_ = tag;
            open_child(Page::queue_actions);
        }
        else if ((page_ == Page::source || page_ == Page::players) && s.companion_configured &&
                 index < c.players.size() && c.players[index].available)
            choose_speaker(c.players[index]);
        else if (page_ == Page::presets && index < s.presets.size())
        {
            s.selected_preset = index;
            s.command = UiCommand::preset_select;
        }
        return;
    }
    CompanionAction action;
    switch (tag)
    {
    case playing_on:
        open_child(Page::players);
        break;
    case open_source:
        open_child(Page::source);
        break;
    case open_visuals:
        open_child(Page::visuals);
        break;
    case open_lyrics_timing:
        open_child(Page::lyrics_timing);
        break;
    case open_presets:
        open_child(Page::presets);
        break;
    case open_status:
        open_child(Page::status);
        break;
    case open_sync:
        open_child(Page::sync);
        break;
    case open_account:
        open_child(Page::account);
        break;
    case open_setup:
        open_child(Page::setup);
        s.setup_requested = true;
        break;
    case setup_again:
        s.setup_requested = true;
        break;
    case queue_earlier:
    {
        // Back at the start, the window follows the playing song again.
        const int earlier = c.queue_offset - kQueueWindow;
        s.queue_offset_request = earlier <= up_next_start(c) ? kQueueOffsetDefault : earlier;
        break;
    }
    case queue_later:
        s.queue_offset_request = std::max(c.queue_offset, 0) + static_cast<int>(c.upcoming.size());
        break;
    case shuffle:
        action.kind = CompanionActionKind::shuffle;
        action.enabled = !c.shuffle;
        send(action, nullptr);
        break;
    case repeat:
        action.kind = CompanionActionKind::repeat;
        action.text = kRepeatModes[(repeat_index(c.repeat) + 1) % 3];
        send(action, nullptr);
        break;
    case crossfade:
        action.kind = CompanionActionKind::crossfade;
        action.enabled = !c.crossfade;
        send(action, nullptr);
        break;
    case autoplay:
        action.kind = CompanionActionKind::autoplay;
        action.enabled = !c.autoplay;
        send(action, c.autoplay ? "Stops when the queue ends" : "Keeps playing after the queue");
        break;
    case sleep:
    {
        action.kind = CompanionActionKind::sleep;
        action.value = sleep_minutes_;
        const std::string toast =
            sleep_minutes_ > 0 ? "Sleep timer set for " + std::to_string(sleep_minutes_) + " min"
                               : "Sleep timer cleared";
        send(action, toast.c_str());
        break;
    }
    case favourite:
        action.kind = CompanionActionKind::favorite;
        action.text = c.track.uri;
        action.enabled = !c.track.favorite;
        send(action,
             c.track.favorite ? "Removed from music favourites" : "Added to music favourites");
        break;
    case radio:
        action.kind = CompanionActionKind::radio;
        action.text = c.track.uri;
        send(action, "Starting radio from this song");
        break;
    case play_now:
    case play_next:
    case add_to_queue:
    case start_radio:
        play(tag);
        break;
    case show_songs:
        open_list(Page::collection, BrowseCategory::tracks);
        break;
    case show_albums:
        open_list(Page::artist, BrowseCategory::artist_albums);
        break;
    case show_artist_songs:
        open_list(Page::artist, BrowseCategory::artist_tracks);
        break;
    case show_episodes:
        open_list(Page::collection, BrowseCategory::podcast_episodes);
        break;
    case queue_play:
    case queue_next:
    case queue_remove:
    case queue_up:
    case queue_down:
    {
        int index = selected_queue_index_;
        for (std::size_t i = 0; i < c.upcoming.size(); ++i)
            if (c.upcoming[i].queue_item_id == selected_track_.queue_item_id)
                index = static_cast<int>(i);
        action.text = selected_track_.queue_item_id;
        action.kind = tag == queue_play     ? CompanionActionKind::play_index
                      : tag == queue_remove ? CompanionActionKind::delete_item
                                            : CompanionActionKind::move_item;
        // Up next is relative to the playing song: shifting by minus its
        // position (window offset included) makes a song the next one.
        action.value = tag == queue_up     ? -1
                       : tag == queue_down ? 1
                       : tag == queue_next ? -(up_next_first(c) + index)
                                           : 0;
        const char *toast = tag == queue_play     ? "Playing now"
                            : tag == queue_next   ? "Plays next"
                            : tag == queue_remove ? "Removed from the queue"
                                                  : nullptr;
        // Moving stays on the song so it can move again; the rest are done with it.
        if (send(action, toast) && toast != nullptr)
            navigate(Page::queue, parent_focus_);
        break;
    }
    case mode_visuals:
    case mode_track:
    case mode_album:
    case mode_lyrics:
        p.display_mode = tag - mode_visuals;
        break;
    case layer_off:
    case layer_line:
    case layer_stage:
        p.lyrics_layer = tag - layer_off;
        break;
    case overlay_position:
    case overlay_background:
    case overlay_size:
        step_look(p, tag, 1);
        break;
    case poster_visuals:
        p.poster_visuals = !p.poster_visuals;
        break;
    case track_card:
        p.track_info = (std::clamp(p.track_info, 0, 2) + 1) % 3;
        break;
    case visual_style:
        p.visual_style = (std::clamp(p.visual_style, 0, 2) + 1) % 3;
        break;
    case reduce_motion:
        p.reduce_motion = !p.reduce_motion;
        break;
    case karaoke:
        p.karaoke = !p.karaoke;
        break;
    case play_here:
        p.follow = false;
        break;
    case move_here:
        // Starts it here even when it had stopped, and plays it through the PS5.
        action.kind = CompanionActionKind::transfer;
        action.text = c.own_player_id;
        action.enabled = true;
        if (send(action, "Moved the music to this PS5"))
            p.follow = false;
        break;
    case follow_auto:
        if (s.companion_configured)
        {
            p.follow = true;
            p.player.clear();
        }
        break;
    case tint:
        p.tint = !p.tint;
        break;
    case night_register:
        p.night_register = !p.night_register;
        break;
    case oled:
        p.oled_drift = !p.oled_drift;
        break;
    case order:
        p.sequential = !p.sequential;
        break;
    case lock:
        p.locked = !p.locked;
        break;
    case favourites_only:
        p.favorites_only = !p.favorites_only;
        break;
    case skip_slow:
        p.skip_slow = !p.skip_slow;
        break;
    case auto_level:
        p.auto_level = !p.auto_level;
        break;
    case hold_idle:
        p.hold_idle = !p.hold_idle;
        break;
    case scan:
        s.scanning = !s.scanning;
        break;
    case lightbar:
        p.lightbar = !p.lightbar;
        break;
    case lightbar_beat:
        p.lightbar_beat = !p.lightbar_beat;
        break;
    case sign_in:
        ask_keyboard(KeyboardPurpose::sign_in_host, "Music Assistant address", s.account.host);
        break;
    case log_out:
        s.account_command = AccountCommand::log_out;
        s.show_toast("Logging out…");
        break;
    case account_sync:
        p.account_sync = !p.account_sync;
        break;
    case lyrics_reset:
        p.lyrics_offset_ms = 0;
        break;
    case online_lyrics:
        p.online_lyrics = !p.online_lyrics;
        // Back on: a song that had no lyrics is looked up again (LG).
        if (p.online_lyrics && s.lyrics.status == LyricsStatus::missing)
            s.lyrics_retry = true;
        break;
    case retry_lyrics:
        s.lyrics_retry = true;
        break;
    default:
        break;
    }
}

int Panel::shown_delay() const
{
    const auto &c = presentation().companion;
    const int reported = c.static_delay_ms;
    // A value set for another player (this PS5's changed since) is not this one's.
    if (delay_ms_ >= 0 && delay_player_ == c.static_delay_player &&
        (delay_wait_ > 0 || (delay_hold_ > 0 && reported != delay_ms_)))
        return delay_ms_;
    return reported;
}

void Panel::adjust_delay(int direction)
{
    if (!delay_available())
        return;
    // Held, the value speeds up after a second of 10 ms steps: 0..5 s is 500 steps.
    const int step = held_steps_ >= kDelayFastAfter ? kDelayFastStep : kDelayStep;
    const int from = shown_delay();
    const int next = std::clamp((from + direction * step) / kDelayStep * kDelayStep, 0, kDelayMax);
    if (next == from)
        return;
    const auto &c = presentation().companion;
    delay_player_ = c.static_delay_player;
    delay_key_ = c.static_delay_key;
    delay_ms_ = next;
    delay_wait_ = kDelaySaveSeconds;
}

// One static_delay action 600 ms after the last step, as LG speaker.js saves it.
void Panel::update_delay(float dt)
{
    auto &s = presentation();
    delay_hold_ = std::max(0.0f, delay_hold_ - dt);
    if (delay_wait_ <= 0)
        return;
    delay_wait_ -= dt;
    if (delay_wait_ > 0)
        return;
    if (!s.companion.connected)
    {
        delay_hold_ = 0;
        s.show_toast("Not saved: Music Assistant is offline");
        return;
    }
    CompanionAction action;
    action.kind = CompanionActionKind::static_delay;
    action.value = delay_ms_;
    // Saved on the player it was set for, even if this PS5's player changed meanwhile.
    action.text = delay_player_;
    action.option = delay_key_;
    if (send(action, nullptr))
        delay_hold_ = kDelayHoldSeconds;
    else
        delay_wait_ = 0.1f; // another action is in flight: save right after it
}

void Panel::adjust(const SheetRow &row, int direction, ui::Feedback &feedback)
{
    if (!row.adjustable || row.item.disabled)
        return;
    auto &s = presentation();
    auto &p = s.preferences;
    bool changed = true;
    switch (row.item.tag)
    {
    case repeat:
    {
        CompanionAction action;
        action.kind = CompanionActionKind::repeat;
        action.text = kRepeatModes[(repeat_index(s.companion.repeat) + direction + 3) % 3];
        changed = send(action, nullptr);
        break;
    }
    case volume:
        s.command = direction < 0 ? UiCommand::volume_down : UiCommand::volume_up;
        break;
    case sleep:
        changed = step_clamped(sleep_minutes_, direction * 15, 0, 180);
        break;
    case track_card:
        p.track_info = (std::clamp(p.track_info, 0, 2) + direction + 3) % 3;
        break;
    case visual_style:
        p.visual_style = (std::clamp(p.visual_style, 0, 2) + direction + 3) % 3;
        break;
    case overlay_position:
    case overlay_background:
    case overlay_size:
        changed = step_look(p, row.item.tag, direction);
        break;
    case brightness:
        changed = step_clamped(p.brightness, direction * 10, 10, 100);
        break;
    case text_brightness:
        changed = step_clamped(p.text_brightness, direction * 10, 30, 100);
        break;
    case tint_strength:
        changed = step_clamped(p.tint_strength, direction * 25, 25, 100);
        break;
    case beat_sensitivity:
        changed = step_clamped(p.beat_sensitivity, direction * 10, 0, 200);
        break;
    case preset_seconds:
        changed = step_clamped(p.preset_seconds, direction * 15, 15, 600);
        break;
    case minimum_fps:
    {
        constexpr int rates[] = {30, 40, 45, 60};
        p.minimum_fps = step_choice(rates, p.minimum_fps, direction);
        break;
    }
    case fps:
    {
        constexpr int rates[] = {30, 60, 120};
        p.fps = step_choice(rates, p.fps, direction);
        // Only a request: 120 Hz takes effect when Afterglow reopens, and
        // accepted_refresh_hz stays what the display really granted.
        s.requested_refresh_hz = p.fps == 120 ? 120 : 60;
        break;
    }
    case trim:
        changed = step_clamped(p.trim_ms, direction * 25, -2000, 2000);
        break;
    case static_delay:
    {
        const int before = shown_delay();
        adjust_delay(direction);
        changed = shown_delay() != before;
        break;
    }
    case lyrics_offset:
    {
        const int before = p.lyrics_offset_ms;
        p.lyrics_offset_ms = step_lyrics_offset(before, direction);
        changed = p.lyrics_offset_ms != before;
        break;
    }
    default:
        changed = false;
        break;
    }
    if (changed)
        feedback.play(audio::Cue::slider);
}

void Panel::browse_input(const InputFrame &input, ui::Feedback &feedback)
{
    if (!presentation().companion_configured)
        return;
    if (page_ == Page::search && input.is_pressed(Action::west))
    {
        ask_keyboard(KeyboardPurpose::search, "Search Music Assistant", query_);
        feedback.play(audio::Cue::select);
        return;
    }
    if (input.nav == Direction::right && !browse_right_)
    {
        if (sheet_.focused() != nullptr)
        {
            browse_right_ = true;
            feedback.play(audio::Cue::focus);
        }
        return;
    }
    if (input.nav == Direction::left && browse_right_)
    {
        browse_right_ = false;
        feedback.play(audio::Cue::focus);
        return;
    }
    auto &column = browse_right_ ? sheet_ : categories_;
    if (column.handle(input, feedback) == ui::Event::activated && column.focused() != nullptr)
    {
        // A copy: confirming rebuilds the rows the pointer points into.
        const SheetRow row = *column.focused();
        confirm_browse(row);
    }
}

void Panel::preset_input(const InputFrame &input)
{
    auto &s = presentation();
    const SheetRow *row = sheet_.focused();
    if (row == nullptr || row->item.tag < 0 ||
        static_cast<std::size_t>(row->item.tag) >= s.presets.size())
        return;
    const std::string name = s.presets[static_cast<std::size_t>(row->item.tag)];
    if (input.is_pressed(Action::r3))
    {
        s.preferences.toggle_favorite(name);
        s.show_toast(s.preferences.favorite(name) ? "Added to favourite presets"
                                                  : "Removed from favourite presets");
    }
    if (input.is_pressed(Action::west))
    {
        const auto it = s.quality.ratings().find(name);
        const bool keep = it == s.quality.ratings().end() || !it->second.keep;
        s.quality.set_keep(name, keep);
        s.preferences.quality = s.quality.encode();
        s.show_toast(keep ? "Kept, even when slow" : "No longer kept");
    }
}

void Panel::tick(float dt)
{
    update_delay(dt);
    confirmed_left_ = std::max(0.0f, confirmed_left_ - dt);
}

void Panel::update(const InputFrame &input, float dt, ui::Feedback &feedback, bool reduced_motion)
{
    take_keyboard();
    settle_play();
    if (page_ == Page::home)
    {
        home_.set_area(home_area());
        home_.update(input, dt, feedback, reduced_motion);
        return;
    }
    const bool acted = input.pressed != 0 || input.nav != Direction::none;
    if (input.nav == Direction::left || input.nav == Direction::right)
        held_steps_ = input.nav_repeat ? held_steps_ + 1 : 0;
    refresh_ -= dt;
    if (refresh_ <= 0 || acted)
    {
        rebuild();
        refresh_ = 0.5f;
    }
    sheet_.set_reduced_motion(reduced_motion);
    categories_.set_reduced_motion(reduced_motion);
    if (browse_page())
        browse_input(input, feedback);
    else
    {
        if (sheet_.handle(input, feedback) == ui::Event::activated && sheet_.focused() != nullptr)
        {
            const SheetRow row = *sheet_.focused();
            confirm(row);
        }
        if ((input.nav == Direction::left || input.nav == Direction::right) &&
            sheet_.focused() != nullptr)
        {
            const SheetRow row = *sheet_.focused();
            adjust(row, input.nav == Direction::left ? -1 : 1, feedback);
        }
        if (page_ == Page::presets)
            preset_input(input);
    }
    // Show what the press changed on the next frame, not half a second later.
    if (acted)
        refresh_ = 0;
    if (page_ == Page::settings && root_ == Page::settings)
        settings_focus_ = sheet_.focus();
    sheet_.update(dt);
    categories_.update(dt);
}

void Panel::draw_footer(ui::Canvas &canvas, float x) const
{
    const auto &s = presentation();
    const bool browse = browse_page();
    const SheetList &column = browse && !browse_right_ ? categories_ : sheet_;
    const SheetRow *row =
        page_ == Page::home || (browse && !s.companion_configured) ? nullptr : column.focused();
    ui::Hint hints[7];
    int count = 0;
    hints[count++] = {ui::Button::l1, "Sections", ui::Button::r1};
    if (row != nullptr && row->adjustable && !row->item.disabled)
        hints[count++] = {ui::Button::dpad, "Adjust"};
    if (row != nullptr && !row->item.disabled && select_label(*row) != nullptr)
        hints[count++] = {ui::Button::cross, select_label(*row)};
    if (page_ == Page::home)
        hints[count++] = {ui::Button::cross, "Select"};
    if (page_ == Page::search && s.companion_configured)
        hints[count++] = {ui::Button::square, "New search"};
    if (page_ == Page::presets && !s.presets.empty())
    {
        hints[count++] = {ui::Button::right_stick, "Favourite"};
        hints[count++] = {ui::Button::square, "Keep"};
    }
    hints[count++] = {ui::Button::circle, page_ == root_ && trail_.empty() ? "Close" : "Back"};
    ui::HintLayout layout;
    layout.size = 34;
    layout.text_size = style::kTypeTransient;
    layout.cy = kFooterY;
    layout.item_gap = 40;
    ui::draw_hints(canvas.list, canvas.fonts, ui::GlyphStyle::mono(style::kPaper, style::kInk),
                   hints, count, x, false, layout);
}

// LG's sync figure: one timeline, earlier on the left. The music is the fixed
// centre; the picture (MilkDrop and lyrics) moves by the follow-timing trim.
// TV output compensation is not drawn: it lines this PS5's sound up with the
// other speakers, so it is not an offset on this scale (LG 1.4.2).
void Panel::draw_sync_figure(ui::Canvas &canvas, float top) const
{
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const auto &p = presentation().preferences;
    const float left = kLeft + 8, right = kRight - 8, width = right - left;
    const float axis = top + 96;
    list.rounded_rect({left, axis - 1, width, 2}, 0, style::kRule);
    for (int i = 0; i <= 4; ++i)
    {
        const float x = left + width * static_cast<float>(i) / 4;
        list.line(x, axis - 8, x, axis + 8, 2, style::kRule);
    }
    const float music = left + width * 0.5f;
    list.line(music, axis - 24, music, axis + 24, 3, style::kMuted);
    ui::text(list, fonts.regular, "The music", music, axis + 58, style::kTypeMin, style::kMuted,
             gfx::Align::center);
    ui::text(list, fonts.regular, "Earlier", left, axis + 58, style::kTypeMin, style::kMuted);
    ui::text(list, fonts.regular, "Later", right, axis + 58, style::kTypeMin, style::kMuted,
             gfx::Align::right);
    const float picture = left + width * sync_picture_at(p.trim_ms);
    list.line(picture, axis - 30, picture, axis, 4, style::kPaper);
    list.rounded_rect({picture - 7, axis - 7, 14, 14}, 0, style::kPaper);
    const char *label = "Picture & lyrics";
    const float label_width = fonts.semibold.measure(label, style::kTypeLabel);
    const float label_x = std::clamp(picture - label_width / 2, left, right - label_width);
    ui::text(list, fonts.semibold, label, label_x, axis - 44, style::kTypeLabel, style::kPaper);
}

// Scan to save: the playing song's public page, or empty.
ShareLink Panel::share_link() const
{
    const auto &track = presentation().companion.track;
    ShareLink link;
    link.url = track.share_url;
    link.service = track.share_service;
    return link;
}

// Details with a Scan to save code: the facts stop short of it, and the code
// (encoded once per url) stands at the right, level with the first fact.
void Panel::draw_share_details(ui::Canvas &canvas, float rule) const
{
    const auto link = share_link();
    if (link.url != share_url_)
    {
        share_url_ = link.url;
        share_grid_ = qr_grid(link.url);
    }
    draw_facts(canvas, facts(), rule + 12, kDetailsLabelColumn, fact_row(),
               kRight - kShareCodeSize - kShareGap);
    draw_share_block(canvas, link, share_grid_, kRight, rule + 24);
}

gfx::Rect Panel::home_area() const
{
    const float x = home_.covers_screen() ? kBrowseLeft : kLeft;
    const float rule = rule_y();
    return {x, rule + 12, kRight - x, kListBottom - rule - 12};
}

void Panel::draw_home(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const bool full = home_.covers_screen();
    const float x = full ? kBrowseLeft : kLeft;
    const float width = kRight - x;
    list.rounded_rect({full ? 0 : kSheetX, 0, full ? 1920 : 1920 - kSheetX, 1080}, 0, style::kInk);
    ui::text(list, fonts.display, "Home", x, kTitleBaseline, style::kTypeSheetTitle, style::kPaper);
    const std::string line = note();
    if (!line.empty())
        ui::text(list, fonts.regular, fonts.regular.font->fit(line, style::kTypeMin, width), x,
                 kNoteBaseline, style::kTypeMin, style::kMuted);
    const float rule = rule_y();
    list.rounded_rect({x, rule, width, 2}, 0, style::kRule);
    const auto area = home_area();
    home_.draw(canvas, area.x, area.y, area.w, area.h);
    draw_footer(canvas, x);
}

void Panel::draw_sheet(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const auto &s = presentation();
    constexpr float width = kRight - kLeft;
    list.rounded_rect({kSheetX, 0, 1920 - kSheetX, 1080}, 0, style::kInk);
    ui::text(list, fonts.display, fonts.display.font->fit(title(), style::kTypeSheetTitle, width),
             kLeft, kTitleBaseline, style::kTypeSheetTitle, style::kPaper);
    const std::string line = note();
    if (!line.empty())
        ui::text(list, fonts.regular, fonts.regular.font->fit(line, style::kTypeMin, width), kLeft,
                 kNoteBaseline, style::kTypeMin, style::kMuted);
    const float rule = rule_y();
    list.rounded_rect({kLeft, rule, width, 2}, 0, style::kRule);

    if (page_ == Page::details && !share_link().url.empty())
        draw_share_details(canvas, rule);
    else if (page_ == Page::details || page_ == Page::account)
        draw_facts(canvas, facts(), rule + 12, kDetailsLabelColumn, fact_row());
    else if (page_ == Page::status)
        draw_facts(canvas, facts(), rule + 12, kStatusLabelColumn, fact_row());
    else if (page_ == Page::sync)
        draw_sync_figure(canvas, rule + 12);
    else if (page_ == Page::lyrics_timing)
    {
        // Positive offsets show lines later: read the lyrics at an earlier time.
        const double at = s.companion.elapsed - s.preferences.lyrics_offset_ms / 1000.0;
        const int index = current_lyric(lyric_lines_, at);
        if (lyric_lines_.empty())
            // The note above says why (finding, failed, none).
            ui::text(list, fonts.regular, "No timed lyrics to show.", kLeft, rule + 100,
                     style::kTypeLabel, style::kMuted);
        else
        {
            const auto line_at = [&](int i)
            {
                return i >= 0 && static_cast<std::size_t>(i) < lyric_lines_.size()
                           ? lyric_lines_[static_cast<std::size_t>(i)].text
                           : std::string();
            };
            const std::string current = line_at(index);
            ui::text(list, fonts.semibold,
                     fonts.semibold.font->fit(current.empty() ? "…" : current, 64, width), kLeft,
                     rule + 100, 64, style::kPaper);
            ui::text(list, fonts.regular, fonts.regular.font->fit(line_at(index + 1), 36, width),
                     kLeft, rule + 160, 36, style::kMuted);
        }
    }
    sheet_.draw(canvas);
    if (!s.companion.action_error.empty())
        ui::text(list, fonts.regular,
                 fonts.regular.font->fit(s.companion.action_error, style::kTypeMin, width), kLeft,
                 kNoticeBaseline, style::kTypeMin, style::kPaper);
    draw_footer(canvas, kLeft);
}

std::string Panel::browse_heading() const
{
    switch (page_)
    {
    case Page::browse:
        return kCategories[category_].name;
    case Page::search:
        return query_.empty() ? "Search Music Assistant" : "Results for “" + query_ + "”";
    case Page::artist:
    {
        const std::string name = subject_.title.empty() ? "Artist" : subject_.title;
        return name +
               (request_.category == BrowseCategory::artist_albums ? " · Albums" : " · Songs");
    }
    default:
        return subject_.title.empty() ? "Untitled" : subject_.title;
    }
}

void Panel::draw_browse(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const auto &s = presentation();
    constexpr float width = kRight - kBrowseLeft;
    list.rounded_rect({0, 0, 1920, 1080}, 0, style::kInk);
    ui::text(list, fonts.display, title(), kBrowseLeft, kBrowseTitleBaseline,
             style::kTypeBrowseTitle, style::kPaper);
    if (s.companion_configured)
        ui::text(list, fonts.regular, fonts.regular.font->fit(plays_on(s), style::kTypeMin, width),
                 kBrowseLeft, kBrowseNoteBaseline, style::kTypeMin, style::kMuted);
    list.rounded_rect({kBrowseLeft, kBrowseRule, width, 2}, 0, style::kRule);
    if (!s.companion_configured)
    {
        ui::text(list, fonts.display, "Connect Music Assistant", kBrowseLeft, kBrowseRule + 116,
                 style::kTypeSheetTitle, style::kPaper);
        ui::text(list, fonts.regular,
                 fonts.regular.font->fit("Sign in under Settings, Account, or set host and "
                                         "ma_token in afterglow.conf and reopen Afterglow.",
                                         style::kTypeLabel, width),
                 kBrowseLeft, kBrowseRule + 170, style::kTypeLabel, style::kMuted);
        draw_footer(canvas, kBrowseLeft);
        return;
    }
    categories_.draw(canvas);
    ui::text(list, fonts.display,
             fonts.display.font->fit(browse_heading(), kHeadingSize, kRight - kCollectionLeft),
             kCollectionLeft, kHeadingBaseline, kHeadingSize, style::kPaper);
    sheet_.draw(canvas);
    if (!s.companion.action_error.empty())
        ui::text(list, fonts.regular,
                 fonts.regular.font->fit(s.companion.action_error, style::kTypeMin,
                                         kRight - kCollectionLeft),
                 kCollectionLeft, kNoticeBaseline, style::kTypeMin, style::kPaper);
    draw_footer(canvas, kBrowseLeft);
}

void Panel::draw(ui::Canvas &canvas) const
{
    if (page_ == Page::home)
        draw_home(canvas);
    else if (browse_page())
        draw_browse(canvas);
    else
        draw_sheet(canvas);
}
} // namespace hui::afterglow
