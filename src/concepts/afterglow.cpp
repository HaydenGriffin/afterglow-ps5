// ps5-homebrew-ui - Afterglow: real music, an audio-reactive canvas, quiet controls.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/concepts.hpp"
#include "afterglow/lyric_overlay.hpp"
#include "afterglow/lyrics.hpp"
#include "afterglow/panel.hpp"
#include "afterglow/progress_clock.hpp"
#include "afterglow/presentation.hpp"
#include "afterglow/programme_scene.hpp"
#include "afterglow/scenes.hpp"
#include "afterglow/style.hpp"
#include "ui/glyphs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

namespace hui::concepts
{
namespace
{
using afterglow::Panel;
using afterglow::Tile;
using gfx::Color;
using namespace afterglow::style;
const auto kAmber = Color::rgb(0xeeb875);
constexpr const char *kModes[] = {"Visuals only", "Visuals + track", "Album", "Lyrics"};
constexpr const char *kLayers[] = {"Lyrics off", "Lyrics: one line", "Lyrics: stage"};
// LG shows the track card for nine seconds and hides its controls after nine.
constexpr float kCardSeconds = 9.0f;
constexpr float kControlsIdleSeconds = 9.0f;
constexpr float kHintSeconds = 6.0f;
constexpr float kCloseHoldSeconds = 1.5f;
constexpr const char *kNotes[] = {"Live Sendspin audio", "MilkDrop driven by the music you hear",
                                  "Triangle brings the lyrics in"};
constexpr app::TourStep kTour[] = {
    {0.8f, 0, Direction::none, "track"},
    {0.1f, action_bit(Action::west)},
    {0.9f, 0, Direction::none, "album"},
    {0.1f, action_bit(Action::west)},
    {0.9f, 0, Direction::none, "lyrics"},
    {0.1f, action_bit(Action::west)},
    {0.8f, 0, Direction::none, "visuals-only"},
    {0.1f, action_bit(Action::west)},
    {0.1f, action_bit(Action::north)},
    {9.6f, 0, Direction::none, "lyric-line"},
    {0.1f, action_bit(Action::north)},
    {0.9f, 0, Direction::none, "stage"},
    {0.1f, action_bit(Action::north)},
    {0.1f, action_bit(Action::menu)},
    {0.8f, 0, Direction::none, "controls"},
    {0.1f, 0, Direction::up},
    {0.6f, 0, Direction::none, "controls-sections"},
    {0.1f, action_bit(Action::page_next)},
    {0.8f, 0, Direction::none, "queue"},
    {0.1f, action_bit(Action::page_next)},
    {0.8f, 0, Direction::none, "settings"},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.1f, 0, Direction::right},
    {0.8f, 0, Direction::none, "lyrics-settings"},
    {0.1f, 0, Direction::left},
    {0.1f, action_bit(Action::back)},
    {0.1f, action_bit(Action::page_prev)},
    {0.1f, action_bit(Action::page_prev)},
    {0.1f, action_bit(Action::page_prev)},
    {0.8f, 0, Direction::none, "browse"},
    {0.1f, action_bit(Action::menu)},
    {0.1f, action_bit(Action::menu)},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "display"},
    // Down to the last row: the rows above now show "More" at the top.
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.8f, 0, Direction::none, "display-end"},
    {0.1f, action_bit(Action::page_next)},
    {0.1f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "players"},
    {0.1f, action_bit(Action::back)},
    {0.1f, action_bit(Action::page_next)},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "sync"},
    {0.1f, action_bit(Action::back)},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "account"},
    {0.1f, action_bit(Action::back)},
    {0.1f, action_bit(Action::page_next)},
    {0.8f, 0, Direction::none, "home"},
    {0.1f, action_bit(Action::page_next)},
    {0.1f, 0, Direction::up},
    {0.1f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "search"},
    {0.1f, action_bit(Action::confirm)},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "artist"},
    {0.1f, action_bit(Action::confirm)},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.6f, 0, Direction::none, "browse-added"},
    // Playlists: Save the queue, then Add to a playlist from Queue, Details
    // and Browse (host/snapshot_main.cpp answers Music Assistant's calls).
    {0.1f, action_bit(Action::page_next)},
    {0.1f, action_bit(Action::page_next)},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.6f, 0, Direction::none, "queue-save"},
    {0.1f, action_bit(Action::confirm)},
    {0.4f, 0, Direction::none, "queue-saved"},
    {0.1f, 0, Direction::up},
    {0.1f, 0, Direction::up},
    {0.1f, action_bit(Action::confirm)},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.6f, 0, Direction::none, "queue-picker"},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.4f, 0, Direction::none, "queue-added"},
    {0.1f, action_bit(Action::menu)},
    {0.1f, action_bit(Action::menu)},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, action_bit(Action::confirm)},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.6f, 0, Direction::none, "details-picker"},
    {0.1f, action_bit(Action::confirm)},
    {0.4f, 0, Direction::none, "details-added"},
    {0.1f, action_bit(Action::menu)},
    {0.1f, action_bit(Action::menu)},
    {0.1f, action_bit(Action::page_prev)},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.1f, 0, Direction::down},
    {0.6f, 0, Direction::none, "browse-song-actions"},
    {0.1f, action_bit(Action::confirm)},
    {0.6f, 0, Direction::none, "browse-picker"},
    {0.1f, 0, Direction::down},
    {0.1f, action_bit(Action::confirm)},
    {0.4f, 0, Direction::none, "browse-playlist-added"},
    // The idle poster: the preview pauses on Cross, ends the queue on R1 while
    // paused and starts what Cross plays (host/snapshot_main.cpp).
    {0.1f, action_bit(Action::menu)},
    {0.1f, action_bit(Action::confirm)},
    {30.6f, 0, Direction::none, "idle-paused"},
    {0.1f, 0, Direction::right},
    {0.8f, 0, Direction::none, "idle-recent"},
    // Tonight's programme: its tile before Browse, the page, Keep it.
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.1f, 0, Direction::right},
    {0.6f, 0, Direction::none, "idle-programme"},
    {0.1f, action_bit(Action::confirm)},
    {0.6f, 0, Direction::none, "programme"},
    {0.1f, action_bit(Action::confirm)},
    {0.4f, 0, Direction::none, "programme-kept"},
    {0.1f, action_bit(Action::back)},
    {0.1f, 0, Direction::left},
    {0.1f, 0, Direction::left},
    {0.1f, 0, Direction::left},
    {0.1f, 0, Direction::left},
    {0.1f, 0, Direction::left},
    {0.1f, 0, Direction::left},
    {180.4f, 0, Direction::none, "idle-rest"},
    {0.1f, action_bit(Action::page_next)},
    {1.2f, 0, Direction::none, "idle-nothing"},
    {0.1f, action_bit(Action::confirm)},
    {0.8f, 0, Direction::none, "idle-played"},
};

class Afterglow final : public app::Concept
{
  public:
    explicit Afterglow(app::Context &context) : context_(context)
    {
    }
    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo info{"afterglow",
                                           "Afterglow",
                                           "Music for the room. Light for the moment.",
                                           "src/concepts/afterglow.cpp",
                                           audio::SoundSet::glass,
                                           kAmber,
                                           kNotes};
        return info;
    }

    void update(const InputFrame &input, float dt, app::Feedback &feedback) override
    {
        auto &state = afterglow::presentation();
        auto &p = state.preferences;
        context_.settings.reduced_motion = p.reduce_motion;
        clock_.reduced_motion = p.reduce_motion;
        if (!p.reduce_motion)
            clock_.time += dt;
        state.toast_age += dt;
        seek_age_ += dt;
        follow_position(state, dt);
        follow_lyrics(state);
        if (state.title != shown_track_)
        {
            shown_track_ = state.title;
            clock_.track_age = 0;
            hint_age_ = 0;
        }
        clock_.track_age += dt;
        clock_.lyric_age += dt;
        const bool touched = input.pressed != 0 || input.nav != Direction::none;
        hint_age_ = touched ? 0 : hint_age_ + dt;
        announce_changes(state, dt);

        if (state.scanning && !was_scanning_)
            close_all();
        was_scanning_ = state.scanning;
        if (state.scanning &&
            (input.is_pressed(Action::back) || input.is_pressed(Action::menu) ||
             input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next) ||
             input.is_pressed(Action::north) || input.is_pressed(Action::west)))
        {
            state.scanning = false;
            state.show_toast("Preset scan stopped");
        }

        close_hold_ = input.is_held(Action::back) ? close_hold_ + dt : 0;
        if (close_hold_ > kCloseHoldSeconds)
            state.exit_requested = true;

        // A delay step taken just before the sheet closed still saves.
        panel_.tick(dt);
        if (sheet_open_)
            update_sheet(input, dt, feedback);
        else if (transport_open_)
            update_transport(input, dt, feedback);
        else if (!idle_input(input, feedback))
            update_listening(input, feedback);
        if (!sheet_open_)
            update_shortcuts(input, feedback);
        state.page = sheet_open_ ? panel_.page_name() : "";

        update_layers(dt);
        transport_.open =
            approach(transport_.open, transport_open_ && !sheet_open_ ? 1.0f : 0.0f, dt, 16.0f);
        update_idle(state, dt);
        const int mode = p.display_mode;
        const bool visuals = (mode < 2 || p.poster_visuals) && !idle_.showing();
        state.milkdrop_visible = state.scanning || (p.visual_style == 0 && visuals && !sheet_open_);
    }

    void draw(app::Frame &frame) const override
    {
        const auto &state = afterglow::presentation();
        const auto &p = state.preferences;
        auto &list = frame.scene;
        const auto &fonts = context_.fonts;
        const int mode = p.display_mode;
        list.rounded_rect({0, 0, 1920, 1080}, 0, kInk);
        const afterglow::Scene scene{list, fonts, state, clock_, view_};

        const bool idle = idle_.showing() && !sheet_open_;
        const bool visuals = (mode < 2 || p.poster_visuals) && !sheet_open_ && !idle;
        if (visuals || state.scanning)
        {
            // Album colour is applied inside the preset (Milkdrop recolours its slots).
            if (p.visual_style == 0 && state.milkdrop_texture != 0)
                list.image(state.milkdrop_texture, {0, 0, 1920, 1080}, gfx::kCanvasUv,
                           Color::rgb(0xffffff));
            else
                afterglow::draw_visual_style(scene, p.visual_style == 0 ? 1 : p.visual_style);
            list.rounded_rect({0, 0, 1920, 1080}, 0,
                              kInk.with_alpha(1.0f - static_cast<float>(p.brightness) / 100.0f));
        }
        if (state.scanning)
        {
            list.rounded_rect({kSafeLeft, 950, kSafeRight - kSafeLeft, 72}, 0,
                              kInk.with_alpha(0.9f));
            ui::text(list, fonts.regular, state.scan_status, kSafeLeft + 32, 996, kTypeMin, kPaper);
            const ui::Hint stop[] = {{ui::Button::circle, "Stop"}};
            draw_hints(list, stop, 1, 986);
            afterglow::draw_toast(scene, false);
            return;
        }

        // Text dims on its own; the picture keeps its brightness.
        const float drift = p.oled_drift ? std::sin(clock_.time * 0.025f) * 6.0f : 0;
        list.push_transform(1, 0, 0, drift, 0);
        list.push_opacity(static_cast<float>(p.text_brightness) / 100.0f);
        if (idle && state.programme.is_open())
            afterglow::draw_programme(scene, programme_page_, idle_view_.level);
        else if (idle)
            afterglow::draw_idle_poster(scene, idle_view_);
        else if (sheet_open_ && !panel_.covers_screen())
            afterglow::draw_slab(scene, 1);
        else if (!sheet_open_ && mode >= 2)
            afterglow::draw_poster(scene, mode == 3, p.poster_visuals ? 0.86f : 1.0f);
        else if (!sheet_open_)
        {
            afterglow::draw_track_card(scene);
            if (p.lyrics_layer == 1)
                afterglow::draw_lyric_line(scene);
            if (p.lyrics_layer == 2)
                afterglow::draw_stage(scene);
        }
        const float hint_alpha =
            std::clamp((kHintSeconds - hint_age_) / 0.6f, 0.0f, 1.0f) * (1 - transport_.open);
        if (!sheet_open_ && !idle && hint_alpha > 0)
        {
            ui::Hint hints[4];
            int count = 0;
            if (mode == 3 && !view_.timed && view_.pages > 1)
                hints[count++] = {ui::Button::dpad, "Lyrics"};
            else if (mode < 2 && p.visual_style == 0 && state.milkdrop_texture != 0)
                hints[count++] = {ui::Button::l2, "Preset", ui::Button::r2};
            hints[count++] = {ui::Button::triangle, "Lyrics"};
            hints[count++] = {ui::Button::square, "Display"};
            hints[count++] = {ui::Button::options, "Controls"};
            list.push_opacity(hint_alpha);
            if (mode < 2)
            {
                const ui::HintLayout layout = hint_layout(1000);
                const float width = ui::measure_hints(fonts, hints, count, layout);
                list.rounded_rect({kSafeRight - width - 20, 978, width + 20, 44}, 0,
                                  kInk.with_alpha(0.78f));
            }
            draw_hints(list, hints, count, 1000);
            list.pop_opacity();
        }
        list.pop_opacity();
        list.pop_transform();

        if (!sheet_open_)
            afterglow::draw_transport(scene, transport_);
        if (sheet_open_)
        {
            ui::Canvas canvas{list, fonts, 0, clock_.time};
            panel_.draw(canvas);
        }
        afterglow::draw_toast(scene, sheet_open_);
        draw_close_hold(list);
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    // ---- lyrics -------------------------------------------------------------
    // Progress bars and lyric underlines draw from this every frame.
    void follow_position(const afterglow::Presentation &state, float dt)
    {
        const auto &c = state.companion;
        clock_.position =
            progress_.update(c.elapsed, c.playing, c.track.uri + "\n" + c.track.queue_item_id,
                             c.track.duration, dt, state.preferences.reduce_motion);
    }
    void follow_lyrics(afterglow::Presentation &state)
    {
        const auto &track = state.companion.track;
        const bool timed = !track.lrc_lyrics.empty();
        const std::string &text = timed ? track.lrc_lyrics : track.lyrics;
        if (text != lyrics_text_ || track.uri != lyrics_track_ || timed != view_.timed)
        {
            lyrics_text_ = text;
            lyrics_track_ = track.uri;
            lyrics_ = afterglow::parse_lyrics(text, timed);
            view_.timed = timed;
            view_.page = 0;
            paginate();
            lyrics_announced_ = false;
        }
        view_.lines = &lyrics_;
        view_.karaoke = state.preferences.karaoke;
        view_.now = clock_.position - state.preferences.lyrics_offset_ms / 1000.0;
        const int active = timed ? afterglow::current_lyric(lyrics_, view_.now) : -1;
        if (active != view_.active)
        {
            view_.active = active;
            clock_.lyric_age = 0;
        }
        // The lookup says when it is still asking (Music Assistant, then LRCLIB).
        view_.loading = lyrics_.empty() && state.lyrics.status == afterglow::LyricsStatus::loading;
        view_.failed = lyrics_.empty() && state.lyrics.status == afterglow::LyricsStatus::error;
        view_.retrying = view_.failed && state.lyrics.retry_at_ms != 0;
        const int page = std::clamp(view_.page, 0, std::max(0, view_.pages - 1));
        view_.page = page;
        const auto starts = static_cast<int>(page_starts_.size());
        view_.first = page < starts ? page_starts_[static_cast<std::size_t>(page)] : 0;
        view_.count = (page + 1 < starts ? page_starts_[static_cast<std::size_t>(page + 1)]
                                         : static_cast<int>(lyrics_.size())) -
                      view_.first;
    }
    // Untimed lyrics page by measured height, so long lines never overflow.
    void paginate()
    {
        page_starts_.assign(1, 0);
        const auto &font = context_.fonts.semibold;
        float y = 0;
        for (std::size_t i = 0; i < lyrics_.size(); ++i)
        {
            const auto rows =
                font.font == nullptr
                    ? std::size_t{1}
                    : ui::paragraph_lines(font, lyrics_[i].text, 44, 1144, 2).lines.size();
            const float height = static_cast<float>(rows) * 52 + 10;
            if (y + height > 360 && y > 0)
            {
                page_starts_.push_back(static_cast<int>(i));
                y = 0;
            }
            y += height;
        }
        view_.pages = static_cast<int>(page_starts_.size());
    }

    // ---- the idle poster -----------------------------------------------------
    // Timers and the strip; the view draw() needs is prepared here.
    void update_idle(afterglow::Presentation &state, float dt)
    {
        const auto &c = state.companion;
        const bool playing = state.playing || c.playing;
        const bool paused = !playing && c.queue_state == "paused" && !c.track.title.empty();
        afterglow::IdlePoster::Inputs in;
        in.playing = playing;
        in.paused = paused;
        in.allowed = state.companion_configured && c.connected && !sheet_open_ &&
                     !transport_open_ && !state.scanning && !state.keyboard_open;
        in.recent = c.recent_state;
        in.tiles = static_cast<int>(idle_tiles_.size());
        idle_.set_reduced_motion(state.preferences.reduce_motion);
        const bool was_showing = idle_.showing();
        if (idle_.update(dt, in))
            state.recent_pending = true;
        state.idle_showing = idle_.showing();
        if (!idle_.showing())
        {
            idle_tiles_.clear();
            state.idle_artwork.clear();
            state.programme.close();
            return;
        }
        const auto offer = programme_offer(state, !was_showing);
        idle_tiles_ = afterglow::idle_tiles(paused ? &c.track : nullptr, c.recent, c.recent_state,
                                            offer ? &*offer : nullptr);
        const auto &tile = idle_tiles_[static_cast<std::size_t>(
            std::clamp(idle_.focus(), 0, static_cast<int>(idle_tiles_.size()) - 1))];
        const bool has_recent =
            std::any_of(idle_tiles_.begin(), idle_tiles_.end(),
                        [](const auto &t) { return t.kind == afterglow::IdleTileKind::play; });
        idle_view_.tiles = &idle_tiles_;
        idle_view_.focus = idle_.focus();
        idle_view_.text = afterglow::idle_text(tile, c.recent_state, has_recent);
        idle_view_.where = c.target_name.empty() ? "Music Assistant" : c.target_name;
        idle_view_.status = paused ? "Paused" : "Nothing playing";
        idle_view_.note = idle_.note();
        idle_view_.level = idle_.level();
        state.idle_artwork = tile.item.artwork;
    }
    // Tonight's programme (programme.hpp): read when the poster appears,
    // offered on the strip, its page prepared while it is open.
    std::optional<afterglow::IdleOffer> programme_offer(afterglow::Presentation &state,
                                                        bool appeared)
    {
        const auto now = afterglow::local_time();
        auto &programme = state.programme;
        if (appeared)
            programme.refresh(state.companion.queue_id, state.companion.target_name, now);
        if (programme.is_open())
            programme_page_ = programme.page(now, state.playlists);
        return programme.tile(now);
    }
    // The open page takes Cross, Circle and the D-pad; the first key after
    // the dim register only wakes it, as on the poster.
    bool programme_input(const InputFrame &input, app::Feedback &feedback)
    {
        auto &state = afterglow::presentation();
        const bool woke = idle_.resting();
        auto act = afterglow::IdleAct::none;
        idle_.press(afterglow::IdleKey::other, idle_tiles_, act);
        auto key = afterglow::ProgrammeKey::back;
        if (input.is_pressed(Action::confirm))
            key = afterglow::ProgrammeKey::confirm;
        else if (input.nav == Direction::up || input.nav == Direction::left)
            key = afterglow::ProgrammeKey::up;
        else if (input.nav == Direction::down || input.nav == Direction::right)
            key = afterglow::ProgrammeKey::down;
        else if (!input.is_pressed(Action::back))
            return false;
        if (woke)
            return true;
        switch (state.programme.press(key))
        {
        case afterglow::ProgrammeAct::moved:
            feedback.play(audio::Cue::focus);
            break;
        case afterglow::ProgrammeAct::refused:
            if (!input.nav_repeat)
                feedback.play(audio::Cue::error);
            break;
        case afterglow::ProgrammeAct::keep:
            state.programme.keep(state.playlists, afterglow::local_time());
            feedback.play(audio::Cue::select);
            break;
        case afterglow::ProgrammeAct::closed:
            feedback.play(audio::Cue::back);
            break;
        }
        return true;
    }
    // While the idle poster shows it takes Cross, left and right; any key
    // wakes it, and every other key keeps its usual meaning.
    bool idle_input(const InputFrame &input, app::Feedback &feedback)
    {
        if (!idle_.showing() || (input.pressed == 0 && input.nav == Direction::none))
            return false;
        if (afterglow::presentation().programme.is_open())
            return programme_input(input, feedback);
        auto key = afterglow::IdleKey::other;
        if (input.is_pressed(Action::confirm))
            key = afterglow::IdleKey::confirm;
        else if (input.nav == Direction::left)
            key = afterglow::IdleKey::left;
        else if (input.nav == Direction::right)
            key = afterglow::IdleKey::right;
        const int before = idle_.focus();
        const bool woke = idle_.resting();
        auto act = afterglow::IdleAct::none;
        if (!idle_.press(key, idle_tiles_, act))
            return false;
        if (woke)
            return true;
        if (key == afterglow::IdleKey::left || key == afterglow::IdleKey::right)
        {
            if (idle_.focus() != before)
                feedback.play(audio::Cue::focus);
            else if (!input.nav_repeat)
                feedback.play(audio::Cue::error);
            return true;
        }
        idle_act(act, before);
        feedback.play(audio::Cue::select);
        return true;
    }
    void idle_act(afterglow::IdleAct act, int focus)
    {
        auto &state = afterglow::presentation();
        switch (act)
        {
        case afterglow::IdleAct::resume:
            state.command = afterglow::UiCommand::toggle;
            idle_.starting("Resuming…");
            break;
        case afterglow::IdleAct::play:
            if (play_now(idle_tiles_[static_cast<std::size_t>(focus)].item.uri))
                idle_.starting("Starting…");
            else
                idle_.failed();
            break;
        case afterglow::IdleAct::browse:
            open_sheet(Panel::Page::browse, 0);
            break;
        case afterglow::IdleAct::retry:
            state.recent_pending = true;
            break;
        case afterglow::IdleAct::programme:
            state.programme.open();
            programme_page_ = state.programme.page(afterglow::local_time(), state.playlists);
            break;
        case afterglow::IdleAct::none:
            break;
        }
    }
    // Replaces the target's queue with an item, as Browse's Play now does.
    static bool play_now(const std::string &uri)
    {
        auto &state = afterglow::presentation();
        if (uri.empty() || !state.companion.connected || state.companion.action_busy ||
            state.companion_action_pending)
            return false;
        afterglow::CompanionAction action;
        action.kind = afterglow::CompanionActionKind::play_media;
        action.text = uri;
        action.option = "replace";
        state.companion_action = std::move(action);
        state.companion_action_pending = true;
        return true;
    }

    // ---- what is on screen ---------------------------------------------------
    void update_layers(float dt)
    {
        const auto &state = afterglow::presentation();
        const auto &p = state.preferences;
        const int mode = p.display_mode;
        const bool idle = state.title.empty();
        const bool singable = view_.timed && !lyrics_.empty() && mode < 2 && !sheet_open_;
        const bool early = clock_.track_age < kCardSeconds && p.track_info != 2;
        // The track card hands over to the lyric line after nine seconds, as on LG.
        const bool line = singable && p.lyrics_layer == 1 && !early;
        const bool stage = singable && p.lyrics_layer == 2;
        const bool card = mode == 1 && !sheet_open_ && !line &&
                          (idle || p.track_info == 0 || (p.track_info == 1 && early));
        const float fade = 1.0f / kFadeSeconds;
        clock_.card = std::clamp(clock_.card + (card ? fade : -fade) * dt, 0.0f, 1.0f);
        clock_.lyric = std::clamp(clock_.lyric + ((line || stage) ? fade : -fade) * dt, 0.0f, 1.0f);
        if (line && context_.fonts.display.font != nullptr)
        {
            const float target = afterglow::lyric_line_width(
                context_.fonts, view_, afterglow::overlay_scale(p.line_look.size));
            clock_.lyric_width = clock_.lyric_width <= 0 || p.reduce_motion
                                     ? target
                                     : approach(clock_.lyric_width, target, dt, 14.0f);
        }
    }

    // Toasts for changes the user asked for but cannot see directly.
    void announce_changes(afterglow::Presentation &state, float dt)
    {
        volume_wait_ = std::max(0.0f, volume_wait_ - dt);
        preset_wait_ = std::max(0.0f, preset_wait_ - dt);
        if (volume_wait_ > 0 && state.volume != volume_seen_)
        {
            state.show_toast("Volume " + std::to_string(state.volume) + "%");
            volume_wait_ = 0;
        }
        volume_seen_ = state.volume;
        if (preset_wait_ > 0 && state.preset != preset_seen_ && !state.preset.empty())
        {
            state.show_toast(state.preset);
            preset_wait_ = 0;
        }
        preset_seen_ = state.preset;
        const auto &p = state.preferences;
        const bool wants_timed = p.display_mode < 2 && p.lyrics_layer > 0;
        if (wants_timed && !lyrics_announced_ && clock_.track_age > 4 && !state.title.empty() &&
            !view_.timed && !view_.loading)
        {
            state.show_toast(view_.failed      ? "Lyrics couldn’t load"
                             : lyrics_.empty() ? "No lyrics for this song"
                                               : "Untimed lyrics: press Square for Lyrics");
            lyrics_announced_ = true;
        }
    }

    // ---- input ---------------------------------------------------------------
    void update_listening(const InputFrame &input, app::Feedback &feedback)
    {
        auto &state = afterglow::presentation();
        auto &p = state.preferences;
        if (input.is_pressed(Action::menu))
        {
            open_transport();
            feedback.play(audio::Cue::open);
            return;
        }
        if (input.is_pressed(Action::confirm))
            state.command = afterglow::UiCommand::toggle;
        if (input.is_pressed(Action::page_next))
            state.command = afterglow::UiCommand::next;
        if (input.is_pressed(Action::page_prev))
            state.command = afterglow::UiCommand::previous;
        if (input.nav == Direction::left || input.nav == Direction::right)
            seek(input.nav == Direction::left ? -10 : 10);
        const bool paging = p.display_mode == 3 && !view_.timed && view_.pages > 1;
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int step = input.nav == Direction::down ? 1 : -1;
            if (paging)
                view_.page = std::clamp(view_.page + step, 0, view_.pages - 1);
            else
            {
                state.command =
                    step < 0 ? afterglow::UiCommand::volume_up : afterglow::UiCommand::volume_down;
                volume_wait_ = 2;
            }
        }
        const bool lyric_poster = p.display_mode == 3;
        if (input.is_pressed(Action::jump_next) || input.is_pressed(Action::jump_prev))
        {
            const int step = input.is_pressed(Action::jump_next) ? 1 : -1;
            if (lyric_poster && view_.timed)
            {
                // On the lyric poster L2/R2 nudge the timing while the words are
                // in view, in the Lyrics timing row's steps.
                p.lyrics_offset_ms = afterglow::step_lyrics_offset(p.lyrics_offset_ms, step);
                state.show_toast(p.lyrics_offset_ms == 0
                                     ? "Lyrics with the music"
                                     : "Lyrics " +
                                           afterglow::lyrics_timing_label(p.lyrics_offset_ms));
            }
            else if (p.visual_style == 0 && state.milkdrop_texture != 0 && !lyric_poster)
            {
                state.command = step > 0 ? afterglow::UiCommand::preset_next
                                         : afterglow::UiCommand::preset_previous;
                preset_wait_ = 2;
            }
        }
        if (input.is_pressed(Action::l3))
        {
            p.locked = !p.locked;
            state.show_toast(p.locked ? "Preset locked" : "Presets change again");
        }
        if (input.is_pressed(Action::r3) && !state.preset.empty())
        {
            p.toggle_favorite(state.preset);
            state.show_toast(p.favorite(state.preset) ? "Favourite preset"
                                                      : "Removed from favourite presets");
        }
    }

    // Square and Triangle work whenever no sheet holds the focus.
    void update_shortcuts(const InputFrame &input, app::Feedback &feedback)
    {
        auto &state = afterglow::presentation();
        auto &p = state.preferences;
        if (input.is_pressed(Action::west))
        {
            p.display_mode = (p.display_mode + 1) % 4;
            state.show_toast(kModes[p.display_mode]);
            feedback.play(audio::Cue::select);
        }
        if (input.is_pressed(Action::north))
        {
            if (p.display_mode >= 2)
            {
                p.display_mode = p.display_mode == 3 ? 2 : 3;
                state.show_toast(kModes[p.display_mode]);
            }
            else
            {
                p.lyrics_layer = (p.lyrics_layer + 1) % 3;
                state.show_toast(p.lyrics_layer > 0 && !view_.timed
                                     ? (lyrics_.empty() ? "No timed lyrics for this song"
                                                        : "These lyrics are untimed")
                                     : kLayers[p.lyrics_layer]);
                lyrics_announced_ = true;
            }
            feedback.play(audio::Cue::select);
        }
    }

    void update_transport(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        const bool touched = input.pressed != 0 || input.nav != Direction::none;
        controls_idle_ = touched ? 0 : controls_idle_ + dt;
        if (controls_idle_ > kControlsIdleSeconds || input.is_pressed(Action::menu) ||
            input.is_pressed(Action::back))
        {
            transport_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
        {
            step_section(input.is_pressed(Action::page_next) ? 1 : -1, feedback);
            return;
        }
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int step = input.nav == Direction::right ? 1 : -1;
            int &focus = transport_.row == 1 ? transport_.tile : transport_.focus_section;
            const int limit = transport_.row == 1
                                  ? static_cast<int>(Tile::count)
                                  : afterglow::section_count(afterglow::presentation());
            if (focus + step >= 0 && focus + step < limit)
            {
                focus += step;
                feedback.play(audio::Cue::focus);
            }
            else if (!input.nav_repeat)
                feedback.play(audio::Cue::error);
        }
        if (input.nav == Direction::up && transport_.row == 1)
        {
            transport_.row = 0;
            transport_.focus_section = transport_.section;
            feedback.play(audio::Cue::focus);
        }
        if (input.nav == Direction::down && transport_.row == 0)
        {
            transport_.row = 1;
            feedback.play(audio::Cue::focus);
        }
        if (input.is_pressed(Action::confirm))
        {
            if (transport_.row == 1)
                activate_tile(static_cast<Tile>(transport_.tile), feedback);
            else
                open_section(transport_.focus_section, feedback);
        }
    }

    void update_sheet(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        if (input.is_pressed(Action::menu))
        {
            close_all();
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
        {
            step_section(input.is_pressed(Action::page_next) ? 1 : -1, feedback);
            return;
        }
        if (input.is_pressed(Action::back))
        {
            if (!panel_.back())
            {
                // Out of a sheet lands back on the transport, as on LG.
                sheet_open_ = false;
                transport_open_ = true;
                controls_idle_ = 0;
                transport_.section = 1;
            }
            feedback.play(audio::Cue::back);
            return;
        }
        panel_.update(input, dt, feedback, context_.settings.reduced_motion);
        // Play now started the music: back to Now playing, as on LG.
        if (panel_.take_leave())
            close_all();
    }

    void activate_tile(Tile tile, app::Feedback &feedback)
    {
        auto &state = afterglow::presentation();
        controls_idle_ = 0;
        switch (tile)
        {
        case Tile::previous:
            state.command = afterglow::UiCommand::previous;
            break;
        case Tile::play_pause:
            state.command = afterglow::UiCommand::toggle;
            break;
        case Tile::next:
            state.command = afterglow::UiCommand::next;
            break;
        case Tile::back10:
            seek(-10);
            break;
        case Tile::ahead10:
            seek(10);
            break;
        case Tile::favourite:
        {
            const bool adding = !state.companion.track.favorite;
            if (send_action(afterglow::CompanionActionKind::favorite, adding))
                state.show_toast(adding ? "Added to music favourites"
                                        : "Removed from music favourites");
            break;
        }
        case Tile::details:
            open_sheet(Panel::Page::details, 1);
            break;
        case Tile::display:
            open_sheet(Panel::Page::display, 1);
            break;
        case Tile::count:
            break;
        }
        feedback.play(audio::Cue::select);
    }

    // L1/R1 walk the sections, wrapping; Home is there only once it is set up.
    void step_section(int step, app::Feedback &feedback)
    {
        const int count = afterglow::section_count(afterglow::presentation());
        open_section((std::min(transport_.section, count - 1) + step + count) % count, feedback);
    }
    void open_section(int section, app::Feedback &feedback)
    {
        constexpr Panel::Page kPages[] = {Panel::Page::browse, Panel::Page::queue,
                                          Panel::Page::queue, Panel::Page::settings,
                                          Panel::Page::home};
        transport_.section = section;
        transport_.focus_section = section;
        if (section == 1)
        {
            sheet_open_ = false;
            transport_open_ = true;
            transport_.row = 1;
            controls_idle_ = 0;
        }
        else
            open_sheet(kPages[section], section);
        feedback.play(audio::Cue::select);
    }
    void open_sheet(Panel::Page page, int section)
    {
        transport_.section = section;
        panel_.open(page);
        sheet_open_ = true;
    }
    void open_transport()
    {
        transport_open_ = true;
        transport_.row = 1;
        transport_.tile = static_cast<int>(Tile::play_pause);
        transport_.section = 1;
        transport_.focus_section = 1;
        controls_idle_ = 0;
    }
    void close_all()
    {
        sheet_open_ = false;
        transport_open_ = false;
    }

    void seek(int seconds)
    {
        auto &state = afterglow::presentation();
        const auto &track = state.companion.track;
        if (track.duration <= 0)
        {
            state.show_toast(state.title.empty() ? "Nothing playing" : "This stream can't seek");
            return;
        }
        if (!state.companion.connected)
        {
            state.show_toast("Needs Music Assistant");
            return;
        }
        // Quick presses add up (Companion::seek_by); the toast names where they
        // land, counted from the last one while the report catches up.
        const bool recent = seek_age_ < 4 && seek_track_ == track.uri;
        const double from = recent ? seek_target_ + (state.companion.playing ? seek_age_ : 0)
                                   : state.companion.elapsed;
        const int target =
            std::clamp(static_cast<int>(std::lround(from)) + seconds, 0, track.duration);
        state.seek_seconds += seconds;
        seek_target_ = target;
        seek_track_ = track.uri;
        seek_age_ = 0;
        char text[48];
        std::snprintf(text, sizeof(text), "%s 10 s · %d:%02d", seconds < 0 ? "Back" : "Ahead",
                      target / 60, target % 60);
        state.show_toast(text);
    }
    // Sends one action to Music Assistant; false (with a toast) when it can't go.
    bool send_action(afterglow::CompanionActionKind kind, bool enabled = false, int value = 0)
    {
        auto &state = afterglow::presentation();
        if (!state.companion.connected)
        {
            state.show_toast("Needs Music Assistant");
            return false;
        }
        if (state.companion.action_busy || state.companion_action_pending)
            return false;
        afterglow::CompanionAction action;
        action.kind = kind;
        action.enabled = enabled;
        action.value = value;
        action.text = state.companion.track.uri;
        state.companion_action = std::move(action);
        state.companion_action_pending = true;
        return true;
    }

    // ---- small drawings ------------------------------------------------------
    static ui::HintLayout hint_layout(float cy)
    {
        ui::HintLayout layout;
        layout.size = 30;
        layout.text_size = kTypeTransient;
        layout.cy = cy;
        layout.item_gap = 32;
        return layout;
    }
    void draw_hints(gfx::DrawList &list, const ui::Hint *hints, int count, float cy) const
    {
        ui::draw_hints(list, context_.fonts, ui::GlyphStyle::mono(kPaper, kInk), hints, count,
                       kSafeRight - 10, true, hint_layout(cy));
    }
    // Holding Circle shows how close it is to closing, so a long press never surprises.
    void draw_close_hold(gfx::DrawList &list) const
    {
        if (close_hold_ < 0.25f)
            return;
        const auto &fonts = context_.fonts;
        const float progress = std::clamp(close_hold_ / kCloseHoldSeconds, 0.0f, 1.0f);
        const gfx::Rect plate{960 - 220, 470, 440, 140};
        list.rounded_rect(plate, 0, kInk.with_alpha(0.94f));
        ui::text(list, fonts.semibold, "Keep holding to close", 960, 530, kTypeBody, kPaper,
                 gfx::Align::center);
        list.line(plate.x + 40, 572, plate.x + plate.w - 40, 572, 4, kRule);
        list.line(plate.x + 40, 572, plate.x + 40 + (plate.w - 80) * progress, 572, 4, kPaper);
    }

    app::Context &context_;
    Panel panel_;
    afterglow::IdlePoster idle_;
    std::vector<afterglow::IdleTile> idle_tiles_;
    afterglow::IdleView idle_view_;
    afterglow::ProgrammePage programme_page_;
    afterglow::SceneClock clock_;
    afterglow::ProgressClock progress_;
    afterglow::LyricView view_;
    afterglow::Transport transport_;
    std::vector<afterglow::LyricLine> lyrics_;
    std::vector<int> page_starts_{0};
    std::string lyrics_text_, lyrics_track_, shown_track_, preset_seen_;
    bool lyrics_announced_ = false;
    bool sheet_open_ = false;
    bool transport_open_ = false;
    bool was_scanning_ = false;
    float controls_idle_ = 0;
    float hint_age_ = 0;
    float close_hold_ = 0;
    float volume_wait_ = 0;
    float preset_wait_ = 0;
    int volume_seen_ = -1;
    // The last seek's target, for the toast while quick presses add up.
    int seek_target_ = 0;
    float seek_age_ = 99;
    std::string seek_track_;
};
} // namespace

std::unique_ptr<app::Concept> make_afterglow(app::Context &context)
{
    return std::make_unique<Afterglow>(context);
}
} // namespace hui::concepts
