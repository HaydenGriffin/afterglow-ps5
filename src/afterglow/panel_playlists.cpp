// Afterglow - The sheets' playlist rows: Save as a playlist, Add to a playlist and its picker.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/panel.hpp"

// LG player-view.js / browse-view.js (spec: .lanes/_brief/R2-playlists.md).
// The work is presentation().playlists'; these rows start it and read back
// what it did, keyed by the row that started it ("queue-save", "queue:<uri>",
// "details:<uri>", "browse:<uri>"), so the song's own row confirms as Play
// next does.
namespace hui::afterglow
{
std::string Panel::playlist_key(const CompanionTrack &song, Page from) const
{
    const char *where = from == Page::queue_actions  ? "queue:"
                        : from == Page::item_actions ? "browse:"
                                                     : "details:";
    return where + song.uri;
}

void Panel::mark_playlist(SheetRow &row, const std::string &key, bool subtitle)
{
    const auto &lists = presentation().playlists;
    const auto shown = lists.confirmation(key, lists.clock());
    if (!shown)
        return;
    (subtitle ? row.item.subtitle : row.item.value) = shown->text;
    row.chosen = shown->tick;
}

SheetRow Panel::save_playlist_row() const
{
    const auto &s = presentation();
    const auto now = local_time();
    SheetRow row;
    row.item.title = "Save as a playlist";
    row.item.tag = kSavePlaylistTag;
    // Music Assistant saves the whole queue, played and to come.
    row.item.value = "As “" + playlist_wire::playlist_name("Queue", now.day, now.month) + "”";
    row.item.disabled = !s.companion.connected || s.companion.queue_total <= 0 ||
                        s.companion.queue_id.empty() || s.playlists.busy();
    mark_playlist(row, "queue-save", false);
    return row;
}

SheetRow Panel::add_playlist_row(const CompanionTrack &song, bool ready) const
{
    SheetRow row;
    row.item.title = "Add to a playlist";
    row.item.tag = kAddPlaylistTag;
    row.item.chevron = true;
    row.item.disabled = !ready || song.uri.empty() || song.media_type != "track";
    // The song's own row in Browse and Queue confirms; Details has only this one.
    if (page_ == Page::details)
        mark_playlist(row, playlist_key(song, Page::details), false);
    return row;
}

std::vector<SheetRow> Panel::picker_rows() const
{
    const auto &lists = presentation().playlists;
    std::vector<SheetRow> rows;
    int index = 0;
    for (const auto &picked : lists.picker_rows(local_time()))
    {
        SheetRow row;
        row.item.title = picked.label;
        row.item.value = picked.detail;
        row.item.tag = picked.kind == PickerRow::Kind::create  ? kNewPlaylistTag
                       : picked.kind == PickerRow::Kind::retry ? kRetryPlaylistsTag
                       : picked.kind == PickerRow::Kind::playlist
                           ? index++
                           : kNewPlaylistTag - 100; // a status line, never acted on
        row.item.disabled =
            picked.disabled || (picked.kind != PickerRow::Kind::retry && lists.busy());
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Panel::playlists_moved()
{
    const auto &lists = presentation().playlists;
    const auto &job = lists.job();
    const bool shown = lists.confirmation(job.key, lists.clock()).has_value();
    auto seen = job.key + '\n' + job.text + (job.working ? "1" : "0") + (shown ? "1" : "0") +
                std::to_string(static_cast<int>(lists.list_state())) + ':' +
                std::to_string(lists.list().size());
    if (seen == playlists_seen_)
        return false;
    playlists_seen_ = std::move(seen);
    return true;
}

void Panel::open_picker(const CompanionTrack &song)
{
    adding_song_ = song;
    adding_from_ = page_;
    adding_focus_ = sheet_.focus();
    if (page_ == Page::item_actions)
        trail_.push_back({page_, request_, subject_, sheet_.focus()});
    else
        parent_focus_ = page_ == Page::queue_actions ? parent_focus_ : sheet_.focus();
    presentation().playlists.load_editable();
    navigate(Page::playlist);
}

void Panel::close_picker()
{
    if (adding_from_ == Page::item_actions)
    {
        // Back from the picker is the song's actions; a pick goes on to the list.
        pop_browse();
        return;
    }
    navigate(adding_from_, adding_focus_);
}

bool Panel::confirm_playlist(const SheetRow &row)
{
    auto &s = presentation();
    auto &lists = s.playlists;
    const int tag = row.item.tag;
    if (tag == kSavePlaylistTag)
    {
        lists.save_queue(s.companion.queue_id, s.companion.queue_total, local_time());
        return true;
    }
    if (tag == kAddPlaylistTag)
    {
        open_picker(page_ == Page::queue_actions  ? selected_track_
                    : page_ == Page::item_actions ? subject_
                                                  : s.companion.track);
        return true;
    }
    if (page_ != Page::playlist)
        return false;
    if (tag == kRetryPlaylistsTag)
    {
        lists.load_editable();
        return true;
    }
    const auto key = playlist_key(adding_song_, adding_from_);
    bool started = false;
    if (tag == kNewPlaylistTag)
        started = lists.add_to_new({adding_song_.uri}, key, local_time());
    else if (tag >= 0 && static_cast<std::size_t>(tag) < lists.list().size())
        started =
            lists.add_to(lists.list()[static_cast<std::size_t>(tag)], {adding_song_.uri}, key);
    if (!started)
        return true;
    // Back where the song is, as Play next goes back to the list.
    if (adding_from_ == Page::item_actions)
    {
        pop_browse();
        if (!trail_.empty())
            pop_browse();
    }
    else if (adding_from_ == Page::queue_actions)
        navigate(Page::queue, parent_focus_);
    else
        navigate(adding_from_, adding_focus_);
    return true;
}
} // namespace hui::afterglow
