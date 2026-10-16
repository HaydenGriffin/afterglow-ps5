// ps5-homebrew-ui - Afterglow playback presentation and output-audio analysis.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/stream_ring.hpp"
#include "afterglow/companion.hpp"
#include "afterglow/preferences.hpp"
#include "afterglow/preset_quality.hpp"
#include "afterglow/home_model.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::afterglow
{
// The system on-screen keyboard (sceImeDialog). The UI asks; main.cpp runs the
// dialog and answers once. purpose tells the asker which field it was for.
enum class KeyboardPurpose
{
    none,
    search,
    sign_in_host,
    sign_in_username,
    sign_in_password
};
struct KeyboardRequest
{
    KeyboardPurpose purpose = KeyboardPurpose::none;
    std::string title, initial;
    bool secret = false;
};
struct KeyboardResult
{
    KeyboardPurpose purpose = KeyboardPurpose::none;
    bool accepted = false;
    std::string text;
};
// Music Assistant sign-in from the console, replacing the hand-written token.
enum class AccountCommand
{
    none,
    sign_in,
    log_out
};
struct AccountState
{
    // Token origin: none, afterglow.conf (fixed; cannot log out), or signed in here.
    enum class Source
    {
        none,
        config,
        signed_in
    } source = Source::none;
    std::string host, username;
    // "Signing in…", "Signed in as …", errors; empty when idle.
    std::string status;
    bool busy = false;
    // Settings mirroring to the Music Assistant account.
    std::string sync_status;
};

enum class UiCommand
{
    none,
    toggle,
    previous,
    next,
    volume_up,
    volume_down,
    preset_previous,
    preset_next,
    preset_select
};

// queue_offset_request value that puts the Queue sheet back on its default
// window: the songs after the playing one, following it as songs change.
constexpr int kQueueOffsetDefault = -2;
struct Presentation
{
    bool configured = false;
    bool connected = false;
    bool playing = false;
    bool audio_ok = true;
    bool preview = false;
    bool exit_requested = false;
    std::string status = "Add your Music Assistant server";
    std::string title;
    std::string artist;
    std::string album;
    std::string group;
    int volume = 100;
    double buffered_ms = 0;
    std::uint64_t underruns = 0;
    std::uint64_t late_frames = 0;
    std::uint64_t reconnects = 0;
    std::uint64_t received_packets = 0;
    std::uint64_t output_errors = 0;
    float fps = 0;
    int accepted_refresh_hz = 60, requested_refresh_hz = 60;
    bool refresh_confirmed = false;
    std::array<float, 32> spectrum{};
    // Local time is in the night register's hours (22:00-06:00); main.cpp
    // keeps it current, host previews set it. Gated by preferences.night_register.
    bool night_hours = false;
    // GL texture holding the latest MilkDrop frame; 0 when projectM is unavailable.
    std::uint32_t milkdrop_texture = 0;
    std::string preset;
    std::size_t preset_count = 0;
    // Set by the screen: projectM renders only while its picture is shown.
    bool milkdrop_visible = false;
    Preferences preferences;
    CompanionSnapshot companion;
    CompanionAction companion_action;
    BrowseRequest browse_request;
    bool companion_action_pending = false, browse_pending = false;
    // Queue sheet paging, pumped by main.cpp into Companion::set_queue_offset:
    // the first queue index to show, kQueueOffsetDefault (-2) for the default
    // window after the playing song, or -1 when nothing is asked.
    int queue_offset_request = -1;
    bool companion_configured = false;
    std::vector<std::string> presets;
    std::size_t selected_preset = 0;
    std::uint32_t artwork_texture = 0;
    // Field colours from the artwork; neutral greys until an image decodes.
    std::array<float, 3> artwork_color{0.243f, 0.259f, 0.282f};
    std::array<float, 3> artwork_secondary{0.149f, 0.161f, 0.180f};
    // A short notice that names what a blind action did (LG's toast).
    std::string toast;
    float toast_age = 0;
    void show_toast(std::string text)
    {
        toast = std::move(text);
        toast_age = 0;
    }
    // GL texture for an item's artwork URL, or 0 while it loads or when none.
    // main.cpp installs the thumbnail cache; tests leave it empty.
    std::function<std::uint32_t(const std::string &url)> thumbnail;
    std::string save_status;
    PresetQuality quality;
    bool scanning = false;
    std::string scan_status;
    UiCommand command = UiCommand::none;
    // On-screen keyboard: the UI sets keyboard_pending + keyboard; main.cpp
    // clears keyboard_pending when the dialog opens and fills keyboard_result.
    bool keyboard_pending = false, keyboard_result_ready = false;
    KeyboardRequest keyboard;
    KeyboardResult keyboard_result;
    // True while the system keyboard is on screen; main.cpp then hands the
    // screen no input (focus_lost) and the UI should not open anything new.
    bool keyboard_open = false;
    // Sign-in: the UI fills sign_in_* and sets account_command; main.cpp acts.
    AccountCommand account_command = AccountCommand::none;
    std::string sign_in_host, sign_in_username, sign_in_password;
    AccountState account;
    // 0..1 beat pulse from the output-audio analyser, decaying; drives the
    // light bar and halo/spectrum accents.
    float beat = 0;
    // Home Assistant glance (home_model.hpp). commands are drained by main.cpp.
    HomeSnapshot home;
    std::vector<HomeCommand> home_commands;
    // Dev request path (dev_requests token in afterglow.conf): last request result.
    std::string dev_status;
    // The open sheet's page name ("" when none), reported by the dev status request.
    std::string page;
    // Idle poster (idle.hpp): the screen asks for recent music (main.cpp calls
    // Companion::load_recent) and, while it shows, names the cover to load
    // instead of the song's ("" for none).
    bool recent_pending = false;
    bool idle_showing = false;
    std::string idle_artwork;
};

// Only the UI thread accesses this model. Audio reaches it through Analyzer.
Presentation &presentation();

class Analyzer
{
  public:
    // Single audio producer; drops analysis data if the screen falls behind.
    void push(const std::int16_t *samples, int frames);
    // Single screen consumer, at most 30 analyses/second. Never blocks audio.
    std::array<float, 32> update(float dt);
    // Interleaved stereo read by the last update(); empty between analyses.
    const std::vector<float> &fresh_pcm() const
    {
        return fresh_;
    }
    // 0..1 pulse on low-band onsets: set at once, gone about 250 ms later.
    float beat() const
    {
        return beat_;
    }
    // Percent, as preferences.beat_sensitivity: 100 is the default, 0 never fires.
    void set_beat_sensitivity(int percent)
    {
        sensitivity_ = static_cast<float>(percent) / 100.0f;
    }

  private:
    void detect_beat(float low, float interval);
    audio::StreamRing ring_{8192};
    std::array<float, 2048> window_{};
    std::array<float, 32> levels_{};
    std::vector<float> fresh_;
    float elapsed_ = 0;
    float beat_ = 0, sensitivity_ = 1;
    // Low-band level: its slow average and the last analysis, for onsets.
    float low_average_ = 0, low_previous_ = 0, since_beat_ = 1;
};
} // namespace hui::afterglow
