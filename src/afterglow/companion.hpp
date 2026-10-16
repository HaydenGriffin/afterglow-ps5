// Afterglow - Music Assistant metadata and synchronized speaker visualizations.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/client.hpp"
#include <memory>
#include <map>

namespace hui::afterglow
{
struct CompanionPlayer
{
    CompanionPlayer() = default;
    CompanionPlayer(std::string player_id, std::string player_name)
        : id(std::move(player_id)), name(std::move(player_name))
    {
    }
    std::string id, name;
    // Group editing: current members (for a group/leader) and who this player may join.
    std::string type;
    std::vector<std::string> group_members, can_group_with;
    std::string synced_to;
    bool available = true;
};
struct CompanionTrack
{
    std::string title, artist, album, year, label, uri, artwork, lyrics, lrc_lyrics;
    std::string item_id, provider, media_type, queue_item_id, format;
    bool favorite = false;
    int duration = 0;
    // Stream details from the queue item; zero when the provider does not say.
    std::string content_type;
    int sample_rate = 0, bit_depth = 0, bit_rate = 0;
    // LG-style grade and badge: "Hi-res" / "Lossless" / "Compressed" / "Low quality",
    // badge e.g. "Lossless · FLAC 44.1 kHz / 16-bit". Empty when unknown.
    std::string quality, quality_badge;
    // Podcast episode / audiobook chapter line, or artist item count.
    std::string subtitle;
};
enum class BrowseCategory
{
    recent,
    favorite_playlists,
    favorite_albums,
    favorite_tracks,
    favorite_artists,
    radio,
    playlists,
    albums,
    tracks,
    podcasts,
    audiobooks,
    // Below here are drill-downs or queries, not category-column entries.
    search,          // query = text, results of every media type
    artist_albums,   // item_id/provider of an artist
    artist_tracks,   // item_id/provider of an artist
    podcast_episodes // item_id/provider of a podcast
};
struct BrowseRequest
{
    BrowseCategory category = BrowseCategory::recent;
    std::string item_id, provider, media_type;
    std::string query;
    // Pagination: offset of the first item; results append when offset > 0.
    int offset = 0;
};
enum class CompanionActionKind
{
    seek,
    shuffle,
    repeat,
    autoplay,
    crossfade,
    sleep,
    favorite,
    play_media,
    radio,
    play_index,
    delete_item,
    move_item,
    // value = milliseconds 0..5000 for this PS5's own Music Assistant player
    // (config/players/save sendspin_static_delay).
    static_delay,
    // text = player id to add to / remove from the group led by option.
    group_add,
    group_remove
};
struct CompanionAction
{
    CompanionActionKind kind = CompanionActionKind::seek;
    int value = 0;
    std::string text, option;
    bool enabled = false;
};
struct CompanionSnapshot
{
    bool connected = false, streaming = false, playing = false;
    std::string status, player_id, player_name, queue_id;
    std::vector<CompanionPlayer> players;
    std::vector<std::string> favorites;
    bool favorites_ready = false;
    std::uint64_t favorite_revision = 0;
    CompanionTrack track;
    std::vector<CompanionTrack> upcoming;
    std::vector<CompanionTrack> browse_items;
    bool browse_loading = false, action_busy = false;
    std::string browse_error, action_error, action_status;
    std::uint64_t browse_revision = 0;
    // True when the last page was full: Browse offers "More".
    bool browse_has_more = false;
    int browse_offset = 0;
    // Sticky: Music Assistant refused the token. Survives disconnects; the
    // worker stops reconnecting until the next start() (a fresh sign-in).
    bool token_rejected = false;
    // The account preferences grew past what one save can carry
    // (companion_wire::kMaxAccountSave); saves wait until they shrink.
    bool account_too_large = false;
    // Queue window: upcoming holds up to 50 items from queue_offset; total is the queue length.
    int queue_offset = 0, queue_total = 0;
    // Index of the playing item in the queue; up next starts at queue_index + 1.
    int queue_index = -1;
    // This PS5's own player's sendspin_static_delay; -1 until read.
    int static_delay_ms = -1;
    std::string own_player_id;
    // Where the delay setting was found: the player whose config holds it and
    // the exact value key (a universal player prefixes it with its protocol id).
    std::string static_delay_player, static_delay_key;
    bool shuffle = false, autoplay = false, crossfade = false;
    std::string repeat = "off";
    int sleep_seconds = 0;
    int volume = 100;
    double elapsed = 0;
    double clock_rtt_ms = 0;
    std::size_t buffered_frames = 0;
};

namespace companion_wire
{
// Bounded decoders also used by fixture tests; malformed frames are discarded.
struct Wave
{
    std::int64_t time = 0;
    std::array<std::uint8_t, 1024> samples{};
};
class WaveResampler
{
  public:
    void reset();
    void append(const Wave &wave, std::vector<float> &stereo);
    int source_rate() const
    {
        return source_rate_;
    }

  private:
    std::int64_t previous_time_ = 0;
    int source_rate_ = 48000;
};
bool decode_wave(const std::uint8_t *data, std::size_t size, Wave &out);
CompanionTrack decode_track(std::string_view item);
struct Request
{
    std::string command, args, error;
};
Request browse_request(const BrowseRequest &request);
Request action_request(const CompanionAction &action, const CompanionSnapshot &target);
std::vector<CompanionTrack> decode_items(std::string_view items, std::size_t limit);
// Browse paging: library lists page on the server (offset/limit); whole-list
// commands (album/playlist/artist tracks, episodes, recent) are skipped and cut here.
struct BrowsePage
{
    std::size_t skip = 0, limit = 60, received = 0;
    bool server_paged = false, more = false;
    std::vector<CompanionTrack> items;
};
BrowsePage browse_page(const BrowseRequest &request);
// Call once per reply chunk (partial results append); search flattens its sections.
void decode_browse(std::string_view result, BrowseCategory category, BrowsePage &page);
// A library artist with nothing in the library is retried on its first provider
// mapping (artist = music/artists/get result). Empty command when there is none.
Request artist_fallback(const BrowseRequest &request, std::string_view artist);
// Browse loads at most this far into a list: later offsets are refused and
// "More" is no longer offered once this many items are loaded.
constexpr int kMaxBrowseOffset = 2000;
bool browse_has_more(bool page_more, std::size_t loaded);
constexpr int kQueueWindow = 50;
Request queue_items_request(const std::string &queue_id, int offset);
std::vector<CompanionPlayer> decode_players(std::string_view players);
using FavoriteChanges = std::map<std::string, std::pair<bool, std::int64_t>>;
std::string merge_favorites(std::string_view preferences, const FavoriteChanges &changes,
                            std::int64_t now_ms);
// What auth/user/update writes: favourites are merged only when some changed
// (an unchanged list keeps the order another client wrote) and this device's
// object only when device_json is set. "" when there is nothing to save.
std::string account_save(std::string_view preferences, const FavoriteChanges &changes,
                         std::string_view device_id, std::string_view device_json,
                         std::int64_t now_ms);
// Largest auth/user/update args the API socket sends in one frame (it allows
// ma_socket::kMaxOutgoing); larger saves are refused rather than retried.
constexpr std::size_t kMaxAccountSave = 255 * 1024;
// This device's object under vizman.devices, or "" when there is none.
std::string device_preferences(std::string_view preferences, std::string_view device_id);
// Replaces only vizman.devices[device_id]; every other key is kept verbatim.
std::string merge_device_preferences(std::string_view preferences, std::string_view device_id,
                                     std::string_view device_json);
// config/players/get result: finds sendspin_static_delay (value, else default).
bool decode_static_delay(std::string_view config, std::string &key, int &milliseconds);
Request static_delay_request(std::string_view player_id, std::string_view key, int milliseconds);
} // namespace companion_wire

class Companion
{
  public:
    Companion();
    ~Companion();
    Companion(const Companion &) = delete;
    Companion &operator=(const Companion &) = delete;
    bool start(const char *host, std::uint16_t port, const char *token);
    void stop();
    void join();
    CompanionSnapshot snapshot() const;
    // Empty chooses the playing queue automatically; otherwise follows this speaker.
    void select_player(const std::string &id);
    void set_relay_enabled(bool enabled);
    void set_favorite(const std::string &preset, bool favorite);
    // One-line JSON journal, including unacknowledged changes. Persist locally.
    std::string pending_favorites() const;
    void restore_pending_favorites(std::string_view journal);
    void command(Command command);
    void browse(const BrowseRequest &request);
    // Captures the current target when enqueued; never retargets a queued edit to another room.
    bool action(const CompanionAction &action);
    // Moves the queue window (Queue sheet paging); clamped to the queue. Negative
    // returns to the default (items after the current one). Resets when the
    // followed queue changes.
    void set_queue_offset(int offset);
    // This PS5's own players: its Sendspin player id (client_id) and the Music
    // Assistant player that represents it (ma_player_id or name match). Their
    // config is where the static delay lives.
    void set_own_player(const std::string &sendspin_id, const std::string &player_id);
    // Account mirroring: this device's preferences JSON object, stored in the
    // Music Assistant user preferences beside the LG app's (vizman.devices[id]).
    void set_account_device(const std::string &device_id);
    void save_account_preferences(const std::string &preferences_json);
    // Last fetched copy for this device ("" until read); revision bumps on change.
    std::string account_preferences() const;
    std::uint64_t account_revision() const;
    // UI consumer only. Returns each due waveform once, normalized stereo PCM.
    std::vector<float> take_pcm(int trim_ms = 0);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hui::afterglow
