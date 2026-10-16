// Afterglow - LG-style sheets: Browse, Queue, Details, Display and Settings.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/presentation.hpp"
#include "afterglow/home_sheet.hpp"
#include "afterglow/lyrics.hpp"
#include "afterglow/share_link.hpp"
#include "afterglow/sheet_list.hpp"

namespace hui::afterglow
{
// Where the sync figure puts "Picture & lyrics" for a follow-timing trim:
// 0..1 across a +/-600 ms scale, "The music" fixed at 0.5 (LG sync-figure.js).
float sync_picture_at(int trim_ms);

// Queue paging. queue_offset is an absolute queue index; Up next counts from
// the song after the playing one, so these are relative to queue_index + 1.
int up_next_start(const CompanionSnapshot &c);
int up_next_first(const CompanionSnapshot &c);
// Songs after the shown window ("Later songs"); 0 when the window reaches the end.
int later_songs(const CompanionSnapshot &c);

// A Browse row's second line, as LG's browse-view.js detailOf writes it: what
// kind of item it is, who made it, then an album's year or a song's length.
std::string browse_detail(const CompanionTrack &item);

// The sheets the controls overlay opens, drawn the way the LG app draws them:
// a 65-pixel title, hairline-ruled rows, drawn ticks for the current choice.
// Browse covers the screen; every other sheet sits right of the artwork slab
// (x >= style::kSlabWidth), which the screen draws.
//
// The overlay owns L1/R1 (sections), Options (close) and Circle: it calls
// back() and closes the sheet when back() returns false. update() consumes
// the D-pad, Cross, Square and R3. Rows carry what they do in their tag, so
// reordering a page never changes what a row does.
class Panel
{
  public:
    enum class Page
    {
        browse,        // categories left, the chosen collection right (full screen)
        collection,    // an album's or playlist's songs (full screen)
        item_actions,  // play now / next / add / radio / show songs (full screen)
        queue,         // playing on, up next, playback
        queue_actions, // one queued song: play, next, move, remove
        details,       // the back of the poster: facts, then actions
        display,       // four display modes and the lyrics layer, with ticks
        settings,      // source, visuals & presets, lyrics timing, status
        source,        // play here or follow a speaker
        visuals,       // brightness, preset timer/order/lock, performance
        presets,       // every preset, favourite and keep
        lyrics_timing, // current and next line live while the offset changes
        status,        // connection, buffer, frame rate
        // Added by the all-features round (UI lane owns their rows):
        search,  // keyboard query, then results like a collection (full screen)
        artist,  // an artist's albums and songs (full screen)
        players, // Playing on: speakers, and group editing for the chosen one
        sync,    // TV output compensation (static delay) with the sync figure
        account, // sign in / log out / settings sync
        home,    // Home Assistant glance, drawn by HomeSheet
        setup,   // Check my setup: pass/fail rows with next steps, Check again
        // Added by round 2 (lyrics lane, song_lyrics.hpp):
        lyrics_choice, // Choose other lyrics: LRCLIB's versions of the song
        lyrics_lines,  // Go to a line: the timed lines, picking one seeks
        // Added by feature round 2:
        chapters, // Details → Chapters: an audiobook's or episode's chapters
        // Added by the playlists round (panel_playlists.cpp):
        playlist, // Add to a playlist: New playlist and the playlists a song can go into
    };

    Panel();
    // Shows a page's top level with the focus on its first row.
    void open(Page page);
    void update(const InputFrame &input, float dt, ui::Feedback &feedback, bool reduced_motion);
    // Every frame, open or closed: work that must outlive the sheet (the
    // debounced TV output compensation save) runs here.
    void tick(float dt);
    void draw(ui::Canvas &canvas) const;
    // Goes up one level; false at the page open() showed.
    bool back();
    Page page() const
    {
        return page_;
    }
    // A short lower-case name for the page ("queue", "search"), for dev status.
    const char *page_name() const;
    // Browse pages cover the whole screen; the others leave the artwork slab.
    bool covers_screen() const;
    // The rows of the right column as last built (a sheet's rows, or Browse's list or actions).
    const std::vector<SheetRow> &rows() const
    {
        return sheet_.rows();
    }
    // How many times the rows have been built: a focus move builds none, so
    // holding a direction through 2000 Browse rows stays cheap.
    int rebuilds() const
    {
        return rebuilds_;
    }
    // True once when Play now or Start radio has started the music: the
    // screen closes the controls and returns to Now playing, as on LG.
    bool take_leave();

  private:
    // A browse page the focus left by going deeper, restored by back().
    struct BrowseFrame
    {
        Page page = Page::browse;
        BrowseRequest request;
        CompanionTrack subject;
        int focus = 0;
    };

    void rebuild();
    void rebuild_browse();
    std::vector<SheetRow> sheet_rows() const;
    std::vector<SheetRow> queue_rows() const;
    std::vector<SheetRow> players_rows() const;
    std::vector<SheetRow> visuals_rows() const;
    std::vector<SheetRow> account_rows() const;
    std::vector<SheetRow> category_rows() const;
    std::vector<SheetRow> list_rows() const;
    std::vector<SheetRow> action_rows() const;
    std::string title() const;
    std::string note() const;
    std::string browse_heading() const;
    std::vector<std::pair<std::string, std::string>> facts() const;
    float fact_row() const;
    float rule_y() const;
    float list_top() const;
    void navigate(Page page, int focus = 0);
    void confirm(const SheetRow &row);
    bool confirm_list(const SheetRow &row);
    // The song lyrics rows (song_lyrics.hpp); false for any other row.
    bool confirm_song_lyrics_row(int tag);
    void confirm_browse(const SheetRow &row);
    void confirm_group(int tag);
    void choose_speaker(const CompanionPlayer &player);
    void adjust(const SheetRow &row, int direction, ui::Feedback &feedback);
    void adjust_delay(int direction);
    void browse_input(const InputFrame &input, ui::Feedback &feedback);
    void preset_input(const InputFrame &input);
    void request_category(int category);
    // Browse drill-downs: each pushes the page it leaves onto trail_.
    void open_search();
    void open_actions(const CompanionTrack &item);
    void open_list(Page page, BrowseCategory category);
    void load_more();
    void pop_browse();
    // Details' Go to rows: Browse on an album's, artist's or podcast's page;
    // Back from it returns to Details.
    void go_to(const CompanionTrack &item);
    void load(const BrowseRequest &request);
    // The system keyboard: ask once, answer when main.cpp fills the result.
    void ask_keyboard(KeyboardPurpose purpose, std::string title, std::string initial,
                      bool secret = false);
    void take_keyboard();
    void answer_keyboard(const KeyboardResult &result);
    void update_delay(float dt);
    // Browse's Play now / next / add / radio, answered once Music Assistant settles it.
    void play(int tag);
    void settle_play();
    int shown_delay() const;
    bool delay_available() const;
    bool send(CompanionAction action, const char *toast);
    bool browse_page() const;
    void draw_sheet(ui::Canvas &canvas) const;
    void draw_browse(ui::Canvas &canvas) const;
    void draw_home(ui::Canvas &canvas) const;
    void draw_sync_figure(ui::Canvas &canvas, float top) const;
    void draw_footer(ui::Canvas &canvas, float x) const;
    gfx::Rect home_area() const;

    // The right column: every sheet's rows, or Browse's collection and actions.
    SheetList sheet_;
    // Browse's category column.
    SheetList categories_;
    HomeSheet home_;
    Page page_ = Page::queue;
    Page root_ = Page::queue;
    // The row back() puts the focus on when it returns to the parent page.
    int parent_focus_ = 0;
    // Settings' Lyrics row, kept while Choose lyrics (a page below Lyrics)
    // holds parent_focus_, so two Backs land on Lyrics again.
    int lyrics_parent_focus_ = 0;
    // An index in kCategories; Browse opens on Recently played.
    int category_ = 1;
    bool browse_right_ = false;
    // Browse: the request the current list shows, the item whose page this is
    // (album, artist, podcast or the song whose actions show) and the way back.
    BrowseRequest request_;
    BrowseRequest loaded_;
    CompanionTrack subject_;
    std::vector<BrowseFrame> trail_;
    bool search_root_ = false;
    std::string query_;
    // "More": results append after append_from_ items.
    bool appending_ = false;
    std::size_t append_from_ = 0;
    // The focus to restore once a list that back() reloads has arrived.
    int pending_focus_ = -1;
    CompanionTrack selected_track_;
    int selected_queue_index_ = 0;
    int sleep_minutes_ = 30;
    float refresh_ = 0;
    int rebuilds_ = 0;
    // Keyboard round trip and the sign-in fields collected so far.
    KeyboardPurpose keyboard_wait_ = KeyboardPurpose::none;
    std::string sign_in_host_, sign_in_username_;
    // TV output compensation: the value being set, saved 600 ms after the last
    // step (LG speaker.js), then shown until Music Assistant reports it.
    int delay_ms_ = -1;
    float delay_wait_ = 0, delay_hold_ = 0;
    // The player and value key delay_ms_ was set for.
    std::string delay_player_, delay_key_;
    int held_steps_ = 0;
    // Lyrics timing previews the lines live; parsed once per song, not per frame.
    std::string lyric_source_;
    std::vector<LyricLine> lyric_lines_;
    // A Browse play waiting for Music Assistant (its action id and row), the
    // item row confirming Play next / Add to the queue for a moment, and the
    // action row that failed.
    std::uint64_t play_id_ = 0, play_ids_ = 0;
    int play_tag_ = 0, failed_tag_ = 0;
    CompanionTrack play_item_;
    std::string confirmed_uri_, confirmed_text_;
    float confirmed_left_ = 0;
    bool leave_ = false;
    // Settings opens on the row it was left at (LG 1.4.3).
    int settings_focus_ = 0;
    // Scan to save on Details: the code is encoded once per url.
    ShareLink share_link() const;
    void draw_share_details(ui::Canvas &canvas, float rule) const;
    mutable std::string share_url_;
    mutable QrGrid share_grid_;

    // Playlists (panel_playlists.cpp): save the queue, add a song through the
    // picker. Rows confirm from presentation().playlists by a key naming them.
    static constexpr int kSavePlaylistTag = -1000, kAddPlaylistTag = -1001, kNewPlaylistTag = -1002,
                         kRetryPlaylistsTag = -1003;
    SheetRow save_playlist_row() const;
    SheetRow add_playlist_row(const CompanionTrack &song, bool ready) const;
    // A row reads its key's confirmation: in the value, or the subtitle (Browse).
    static void mark_playlist(SheetRow &row, const std::string &key, bool subtitle);
    std::vector<SheetRow> picker_rows() const;
    void open_picker(const CompanionTrack &song);
    // True when the row was a playlist row and has been acted on.
    bool confirm_playlist(const SheetRow &row);
    void close_picker();
    std::string playlist_key(const CompanionTrack &song, Page from) const;
    // True once when a job or the picker's list moved on, or a confirmation
    // expired: the rows are rebuilt then, not at the next half-second refresh.
    bool playlists_moved();
    CompanionTrack adding_song_;
    Page adding_from_ = Page::details;
    int adding_focus_ = 0;
    std::string playlists_seen_;
};
} // namespace hui::afterglow
