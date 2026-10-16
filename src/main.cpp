// Afterglow - Native PS5 Music Assistant player and visualizer.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/account.hpp"
#include "afterglow/client.hpp"
#include "afterglow/companion.hpp"
#include "afterglow/dev_requests.hpp"
#include "afterglow/lightbar.hpp"
#include "afterglow/version.hpp"
#include "afterglow/home_assistant.hpp"
#include "afterglow/artwork.hpp"
#include "afterglow/thumbnails.hpp"
#include "afterglow/preference_store.hpp"
#include "afterglow/milkdrop.hpp"
#include "afterglow/night.hpp"
#include "afterglow/online_lyrics.hpp"
#include "afterglow/presentation.hpp"
#include "concepts/concepts.hpp"
#include "core/save_file.hpp"
#include "gfx/canvas.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/keyboard.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

extern "C" void hui_heap_stats(std::size_t *live_bytes, std::size_t *peak_bytes,
                               std::size_t *blocks, std::size_t *failures);

namespace
{
bool font(hui::gfx::Renderer &renderer, const char *name, hui::gfx::Font &face,
          hui::ui::FontRef &ref)
{
    std::string data;
    if (!hui::save::read_file(std::string("/app0/assets/fonts/") + name, &data) || !face.load(data))
        return false;
    ref.font = &face;
    ref.texture = renderer.batch().create_font_texture(face);
    return true;
}
struct Config
{
    std::string host;
    unsigned int port = 8927, ma_port = 8095;
    std::string ma_token, ma_player_id;
    std::string client_id = "afterglow-ps5";
    std::string name = "Afterglow PS5";
    // Dev request path (dev_requests.hpp); empty keeps it off.
    std::string dev_token;
};
Config read_config()
{
    Config config;
    std::string text;
    if (!hui::save::read_file("/app0/afterglow.conf", &text, 16384))
        return config;
    std::size_t start = 0;
    while (start < text.size())
    {
        const auto end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find('=');
        if (eq != std::string::npos)
        {
            const auto key = line.substr(0, eq);
            const auto value = line.substr(eq + 1);
            if (key == "host")
                config.host = value;
            if (key == "name" && value.size() <= 80)
                config.name = value;
            if (key == "client_id" && value.size() <= 80)
                config.client_id = value;
            if (key == "ma_token" && value.size() <= 2048)
                config.ma_token = value;
            if (key == "ma_player_id" && value.size() <= 256)
                config.ma_player_id = value;
            if (key == "dev_requests" && hui::afterglow::valid_dev_token(value))
                config.dev_token = value;
            if (key == "port" || key == "ma_port")
            {
                unsigned int port = 0;
                char extra = 0;
                if (std::sscanf(value.c_str(), "%u%c", &port, &extra) == 1 && port > 0 &&
                    port <= 65535)
                {
                    if (key == "port")
                        config.port = port;
                    else
                        config.ma_port = port;
                }
            }
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return config;
}
// The Account page's sign-in and log-out buttons; the work runs on a thread.
void handle_account_command(hui::afterglow::Presentation &model, hui::afterglow::Account &account,
                            const hui::afterglow::Credentials &credentials,
                            const hui::afterglow::StoredAuth &stored, const Config &config)
{
    using namespace hui::afterglow;
    const auto command = model.account_command;
    model.account_command = AccountCommand::none;
    if (command == AccountCommand::sign_in)
    {
        StoredAuth target;
        const auto error = account_wire::sign_in_target(
            model.sign_in_host, credentials.host, static_cast<std::uint16_t>(config.ma_port),
            model.sign_in_username, model.sign_in_password, config.client_id, target);
        if (!error.empty())
            model.account.status = error;
        else if (account.sign_in(target, model.sign_in_password,
                                 account_wire::replaces_for(stored, target)))
        {
            model.account.busy = true;
            model.account.status = "Signing in…";
        }
        else
            model.account.status = "Still talking to Music Assistant; try again in a moment";
        model.sign_in_password.assign(model.sign_in_password.size(), '\0');
        model.sign_in_password.clear();
    }
    else if (command == AccountCommand::log_out)
    {
        if (credentials.source == AccountState::Source::config)
            model.account.status = "This PS5 uses the token in afterglow.conf, so there is nothing "
                                   "to log out of. Sign in to use your account instead.";
        else if (credentials.source == AccountState::Source::none)
            model.account.status = "Not signed in";
        else if (account.log_out(stored))
        {
            model.account.busy = true;
            model.account.status = "Signing out…";
        }
        else
            model.account.status = "Still talking to Music Assistant; try again in a moment";
    }
}
// Settings mirroring and this PS5's own player (for its static delay), every 100 ms.
void mirror_account(hui::afterglow::Presentation &model, hui::afterglow::Companion &companion,
                    hui::afterglow::PreferenceMirror &mirror, const Config &config)
{
    std::string own_player = config.ma_player_id;
    for (const auto &player : model.companion.players)
        if (own_player.empty() && player.name == config.name)
            own_player = player.id;
    companion.set_own_player(config.client_id, own_player);
    if (!model.companion_configured)
    {
        model.account.sync_status = "Sign in to keep settings on your account";
        return;
    }
    const auto step = mirror.update(model.preferences, model.companion.connected,
                                    companion.account_revision(), companion.account_preferences());
    if (step.restored)
    {
        model.preferences = step.preferences;
        model.show_toast("Settings restored");
    }
    if (!step.save.empty())
        companion.save_account_preferences(step.save);
    model.account.sync_status = mirror.status();
    if (model.companion.account_too_large)
        model.account.sync_status = "Not saved: the account's settings are too large to sync";
}

// The system keyboard: field-specific limits and layouts for each asker.
hui::ps5::KeyboardOptions keyboard_options(const hui::afterglow::KeyboardRequest &request)
{
    using Purpose = hui::afterglow::KeyboardPurpose;
    hui::ps5::KeyboardOptions options;
    options.title = request.title;
    options.initial = request.initial;
    options.secret = request.secret || request.purpose == Purpose::sign_in_password;
    options.max_length = request.purpose == Purpose::sign_in_host ? 253 : 128;
    if (request.purpose == Purpose::sign_in_host)
        options.kind = hui::ps5::KeyboardKind::url;
    if (request.purpose == Purpose::sign_in_password)
        options.max_length = 256;
    return options;
}

// Opens the dialog the UI asked for and hands back its answer once.
void pump_keyboard(hui::ps5::Keyboard &keyboard, hui::afterglow::Presentation &model,
                   hui::afterglow::KeyboardPurpose &asked)
{
    if (model.keyboard_pending && !keyboard.active())
    {
        model.keyboard_pending = false;
        asked = model.keyboard.purpose;
        keyboard.open(keyboard_options(model.keyboard));
    }
    hui::ps5::KeyboardOutcome outcome;
    if (keyboard.poll(outcome))
    {
        model.keyboard_result = {asked, outcome.accepted, std::move(outcome.text)};
        model.keyboard_result_ready = true;
    }
    model.keyboard_open = keyboard.active();
}

// The light bar follows the artwork (and the beat); off hands it back to the system.
void drive_lightbar(hui::ps5::Pad &pad, hui::afterglow::LightbarLimiter &limiter,
                    const hui::afterglow::Presentation &model, std::int64_t now, bool &lit)
{
    if (!model.preferences.lightbar)
    {
        if (lit)
            pad.reset_light_bar();
        limiter.forget();
        lit = false;
        return;
    }
    const auto color = hui::afterglow::lightbar_color(
        {model.artwork_color, model.playing, model.preferences.lightbar_beat, model.beat});
    if (!limiter.due(color, now))
        return;
    pad.set_light_bar(color.r, color.g, color.b);
    limiter.wrote(color, now);
    lit = true;
}

// Dev requests: read from the install folder, answered in the title's storage.
constexpr const char *kDevRoot = "/download0/hui/dev";
constexpr const char *kDevRequest = "/app0/dev/request.txt";
struct DevChannel
{
    std::string token, handled;
    std::int64_t next_poll_us = 0;
    // A screenshot waits for the end of the frame it pictures.
    std::string screenshot, screenshot_id;
    // Screenshot files this run wrote, oldest first (kMaxDevScreenshots kept).
    std::vector<std::string> screenshots;
    // Buttons a press request still has to tap, one every kDevPressFrames.
    std::vector<hui::Action> presses;
    std::size_t next_press = 0;
    int press_wait = 0;
};
// Long enough for a sheet to open before the next tap lands.
constexpr int kDevPressFrames = 12;

void open_dev_channel(DevChannel &dev, const std::string &token)
{
    dev.token = token;
    if (token.empty())
        return;
    hui::save::ensure_directory("/download0/hui");
    hui::save::ensure_directory(kDevRoot);
    hui::save::read_file(std::string(kDevRoot) + "/handled.txt", &dev.handled, 128);
    hui::sys::log("[Afterglow] dev requests on (%s)", kDevRequest);
}

// The request waiting now, once per id; polled about once a second.
std::optional<hui::afterglow::DevRequest> poll_dev(DevChannel &dev, std::int64_t now)
{
    if (dev.token.empty() || now < dev.next_poll_us)
        return std::nullopt;
    dev.next_poll_us = now + 1000000;
    std::string text;
    if (!hui::save::read_file(kDevRequest, &text, 512))
        return std::nullopt;
    auto request = hui::afterglow::parse_dev_request(text, dev.token);
    if (!request || request->id == dev.handled)
        return std::nullopt;
    dev.handled = request->id;
    // Unrecorded, the request could run again after a restart (a quit would
    // loop), so it is refused instead.
    const auto error = hui::save::write_atomic(std::string(kDevRoot) + "/handled.txt", dev.handled);
    if (!error.empty())
    {
        hui::sys::log("[Afterglow] dev request %s refused: handled.txt not saved (%s)",
                      request->id.c_str(), error.c_str());
        hui::save::write_atomic(std::string(kDevRoot) + "/result.txt",
                                request->id + " error handled.txt not saved\n");
        return std::nullopt;
    }
    return request;
}

void dev_answer(hui::afterglow::Presentation &model, const std::string &id,
                const std::string &result)
{
    hui::save::write_atomic(std::string(kDevRoot) + "/result.txt", id + " " + result + "\n");
    model.dev_status = id + " " + result;
    hui::sys::log("[Afterglow] dev request %s %s", id.c_str(), result.c_str());
}

std::string dev_status_text(const hui::afterglow::Presentation &model,
                            const hui::ps5::Display &display, const std::string &id)
{
    hui::afterglow::DevStatus status;
    status.id = id;
    status.version = hui::afterglow::kVersion;
    status.fps = model.fps;
    status.connected = model.connected;
    status.playing = model.playing;
    status.width = display.width();
    status.height = display.height();
    status.refresh_hz = model.accepted_refresh_hz;
    status.display_mode = model.preferences.display_mode;
    status.page = model.page;
    return hui::afterglow::dev_status_json(status);
}

// The frame drawn once more into an off-screen target and read back as a BMP.
bool save_screenshot(hui::gfx::Renderer &renderer, const std::string &path, int width, int height)
{
    hui::gfx::Canvas canvas;
    if (!canvas.create(width, height, 1))
        return false;
    canvas.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    renderer.present(canvas.framebuffer(), width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, canvas.framebuffer());
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    canvas.destroy();
    const auto bmp = hui::afterglow::encode_bmp(width, height, pixels.data());
    return !bmp.empty() &&
           hui::save::write_atomic(
               path, std::string_view(reinterpret_cast<const char *>(bmp.data()), bmp.size()))
               .empty();
}

// A press request as one tap in this frame's input, as the pad would give it.
void press_dev_button(hui::InputFrame &input, hui::Action action)
{
    input.pressed |= hui::action_bit(action);
    switch (action)
    {
    case hui::Action::up:
        input.nav = hui::Direction::up;
        break;
    case hui::Action::down:
        input.nav = hui::Direction::down;
        break;
    case hui::Action::left:
        input.nav = hui::Direction::left;
        break;
    case hui::Action::right:
        input.nav = hui::Direction::right;
        break;
    default:
        break;
    }
}

// The next tap of a press request, when its turn has come.
void pump_dev_presses(DevChannel &dev, hui::InputFrame &input)
{
    if (dev.next_press >= dev.presses.size() || dev.press_wait-- > 0)
        return;
    press_dev_button(input, dev.presses[dev.next_press++]);
    dev.press_wait = kDevPressFrames;
}

// Acts on one dev request. A screenshot is taken later, at the end of the frame.
void handle_dev_request(DevChannel &dev, const hui::afterglow::DevRequest &request,
                        hui::afterglow::Presentation &model, const hui::ps5::Display &display)
{
    using Verb = hui::afterglow::DevRequest::Verb;
    switch (request.verb)
    {
    case Verb::quit:
        dev_answer(model, request.id, "ok quitting");
        model.exit_requested = true;
        break;
    case Verb::status:
    {
        const bool saved = hui::save::write_atomic(std::string(kDevRoot) + "/status.json",
                                                   dev_status_text(model, display, request.id))
                               .empty();
        dev_answer(model, request.id, saved ? "ok status.json" : "error status not saved");
        break;
    }
    case Verb::screenshot:
        dev.screenshot = request.argument;
        dev.screenshot_id = request.id;
        break;
    case Verb::press:
        dev.presses = hui::afterglow::dev_press_actions(request.argument);
        dev.next_press = 0;
        dev.press_wait = 0;
        dev_answer(model, request.id, "ok pressing " + request.argument);
        break;
    }
}

void take_dev_screenshot(DevChannel &dev, hui::gfx::Renderer &renderer,
                         hui::afterglow::Presentation &model, const hui::ps5::Display &display)
{
    if (dev.screenshot.empty())
        return;
    const std::string name = dev.screenshot + ".bmp";
    const bool saved = save_screenshot(renderer, std::string(kDevRoot) + "/" + name,
                                       display.width(), display.height());
    dev_answer(model, dev.screenshot_id, saved ? "ok " + name : "error screenshot not saved");
    if (saved)
        for (const auto &stale : hui::afterglow::keep_dev_screenshot(dev.screenshots, name))
            std::remove((std::string(kDevRoot) + "/" + stale).c_str());
    dev.screenshot.clear();
}

// ha_* lines (home_assistant.hpp); ha_group lines make the file larger than the rest.
hui::afterglow::HomeConfig read_home_config()
{
    std::string text;
    if (!hui::save::read_file("/app0/afterglow.conf", &text, 16384))
        return {};
    return hui::afterglow::home_wire::parse_config(text);
}
void start_home(hui::afterglow::HomeAssistant &home, hui::afterglow::Presentation &model)
{
    const auto config = read_home_config();
    model.home.configured = config.configured();
    if (model.home.configured && !home.start(config))
    {
        model.home.link = hui::afterglow::HomeLink::offline;
        model.home.status = "ha_host must be a numeric IPv4 address";
    }
}
// New states in, presses out; the worker never blocks the frame.
void sync_home(hui::afterglow::HomeAssistant &home, hui::afterglow::Presentation &model)
{
    if (model.home.configured && home.revision() != model.home.revision)
        model.home = home.snapshot();
    for (const auto &command : model.home_commands)
        home.send(command);
    model.home_commands.clear();
}
} // namespace

int main()
{
    using namespace hui;
    sys::log("[Afterglow] starting %s", afterglow::kVersion);
    auto &model = afterglow::presentation();
    std::string saved;
    // No local preferences (a fresh install): restore them from the account.
    const bool restore_preferences =
        !save::read_file("/download0/hui/afterglow.prefs", &saved, 1u << 20);
    if (!restore_preferences)
    {
        const auto decoded = save::decode(save::Kind::prefs, saved);
        if (decoded.ok && decoded.version == 1)
            model.preferences = afterglow::Preferences::decode(decoded.payload);
    }
    model.requested_refresh_hz = model.preferences.fps == 120 ? 120 : 60;
    ps5::Display display;
    if (!display.open(1920, 1080, model.requested_refresh_hz))
    {
        sys::log("[Afterglow] display initialization failed");
        sys::quit();
        return 1;
    }
    gfx::Renderer renderer;
    gfx::Font regular, semibold, display_face, mono;
    ui::Fonts fonts;
    if (!renderer.init() || !font(renderer, "archivo-regular.huifont", regular, fonts.regular) ||
        !font(renderer, "archivo-semibold.huifont", semibold, fonts.semibold) ||
        !font(renderer, "archivo-display.huifont", display_face, fonts.display) ||
        !font(renderer, "dejavu-sans-mono.huifont", mono, fonts.mono))
    {
        sys::log("[Afterglow] renderer or fonts failed");
        renderer.release();
        display.close();
        sys::quit();
        return 1;
    }
    // 1280 x 720 keeps projectM's per-frame passes cheap; the kit scales it to the display.
    auto milkdrop = std::make_unique<afterglow::Milkdrop>();
    const Config config = read_config();
    // Heap allocation avoids putting the bounded audio queue on the title's small entry stack.
    auto client = std::make_unique<afterglow::Client>();
    auto analyzer = std::make_unique<afterglow::Analyzer>();
    auto follow_analyzer = std::make_unique<afterglow::Analyzer>();
    ps5::AudioOut output;
    // A signed-in token (afterglow.auth) wins over afterglow.conf's ma_token.
    afterglow::StoredAuth stored_auth =
        afterglow::account_wire::load_auth(afterglow::account_wire::kAuthPath);
    afterglow::Credentials credentials = afterglow::account_wire::effective_credentials(
        stored_auth, config.host, static_cast<std::uint16_t>(config.ma_port), config.ma_token);
    model.account = afterglow::account_wire::account_state(credentials, stored_auth);
    const std::string account_device = "ps5-" + config.client_id;
    std::string audio_host = config.host.empty() ? credentials.host : config.host;
    model.configured = !audio_host.empty();
    model.quality = afterglow::PresetQuality::decode(model.preferences.quality);
    model.preferences.quality = model.quality.encode();
    milkdrop->configure(model.preferences);
    if (!milkdrop->init(1280, 720, "/app0/assets/presets"))
        milkdrop->release();
    model.presets = milkdrop->names();
    afterglow::PreferenceStore preference_store;
    preference_store.start("/download0/hui/afterglow.prefs");
    auto companion = std::make_unique<afterglow::Companion>();
    companion->restore_pending_favorites(model.preferences.favorite_pending);
    companion->set_account_device(account_device);
    model.companion_configured = !credentials.token.empty();
    if (!model.companion_configured)
        model.preferences.follow = false;
    client->set_output_enabled(!model.preferences.follow);
    if (model.companion_configured)
        companion->start(credentials.host.c_str(), credentials.port, credentials.token.c_str());
    auto home = std::make_unique<afterglow::HomeAssistant>();
    start_home(*home, model);
    auto artwork = std::make_unique<afterglow::Artwork>();
    artwork->start();
    afterglow::OnlineLyrics online_lyrics(afterglow::lrclib_fetch(), afterglow::https_abort);
    online_lyrics.start();
    afterglow::LyricsLookup lyrics_lookup;
    // Sheet rows with artwork ask for 160-pixel covers; GL stays on this thread.
    afterglow::ThumbnailOptions thumb_options;
    thumb_options.origin = "http://" + credentials.host + ":" + std::to_string(credentials.port);
    auto thumbnails = std::make_unique<afterglow::ThumbnailCache>(
        afterglow::fetch_image, afterglow::decode_image, thumb_options);
    if (model.companion_configured)
        thumbnails->start();
    model.thumbnail = [&](const std::string &url) { return thumbnails->texture(url); };
    const auto thumb_upload = [&](int w, int h, const std::uint8_t *rgba)
    { return renderer.batch().create_texture(w, h, rgba); };
    const auto thumb_release = [](std::uint32_t texture) { glDeleteTextures(1, &texture); };
    std::string applied_preferences, applied_player = "\x01", artwork_url;
    std::uint64_t artwork_generation = 0, favorite_revision = 0;
    bool favorites_received = false;
    std::vector<std::string> synced_favorites = model.preferences.favorites;
    float visual_elapsed = 0, analysis_gain = 1, scan_frame_elapsed = 0;
    int scan_phase = 0;
    std::size_t scan_index = 0;
    std::uint64_t scan_signal_samples = 0;
    bool configured_scan = false;
    int configured_refresh = 0;
    model.audio_ok = output.start(*client, *analyzer);
    bool client_started = false;
    if (model.configured && model.audio_ok)
        client_started = client->start(audio_host.c_str(), static_cast<std::uint16_t>(config.port),
                                       config.client_id.c_str(), config.name.c_str());
    afterglow::Account account;
    afterglow::PreferenceMirror mirror(restore_preferences);
    // LG checks the token's expiry 8 s after launch, off the start-up path.
    std::int64_t renew_at = sys::monotonic_us() + 8000000;
    // Tokens a sign-in or renewal superseded, retired once the new one is in use.
    std::vector<std::string> retiring;
    // Sign-in, log-out and renewal swap the token without restarting the app:
    // a fresh Companion is simpler to reason about than resetting a live one.
    const auto restart_companion = [&](const afterglow::Credentials &next)
    {
        const auto pending = companion->pending_favorites();
        companion->stop();
        companion->join();
        companion = std::make_unique<afterglow::Companion>();
        companion->restore_pending_favorites(pending);
        companion->set_account_device(account_device);
        companion->set_relay_enabled(model.preferences.follow);
        const bool moved = next.host != credentials.host || next.port != credentials.port;
        credentials = next;
        model.companion_configured = !credentials.token.empty();
        if (!model.companion_configured)
            model.preferences.follow = false;
        else
            companion->start(credentials.host.c_str(), credentials.port, credentials.token.c_str());
        client->set_output_enabled(!model.preferences.follow);
        model.companion = {};
        applied_player = "\x01";
        favorites_received = false;
        mirror.reset();
        if (moved || !model.companion_configured)
        {
            thumbnails->stop();
            thumbnails->join();
            thumbnails->clear(thumb_release);
            thumb_options.origin =
                "http://" + credentials.host + ":" + std::to_string(credentials.port);
            thumbnails = std::make_unique<afterglow::ThumbnailCache>(
                afterglow::fetch_image, afterglow::decode_image, thumb_options);
            if (model.companion_configured)
                thumbnails->start();
        }
        else if (model.companion_configured)
            thumbnails->start(); // first sign-in to the configured host; no-op when running
        // Without a host in afterglow.conf, Sendspin follows the signed-in server.
        const bool audio_moved = config.host.empty() && client_started &&
                                 !credentials.host.empty() && credentials.host != audio_host;
        if (audio_moved)
        {
            client->stop();
            client->join();
            client_started = false;
        }
        if (!client_started && model.audio_ok && !credentials.host.empty())
        {
            model.configured = true;
            audio_host = config.host.empty() ? credentials.host : config.host;
            client_started =
                client->start(audio_host.c_str(), static_cast<std::uint16_t>(config.port),
                              config.client_id.c_str(), config.name.c_str());
        }
    };
    const auto pump_account = [&](std::int64_t now)
    {
        handle_account_command(model, account, credentials, stored_auth, config);
        afterglow::AccountResult result;
        if (account.take_result(result))
        {
            model.account.busy = false;
            // Success messages name the user; the log keeps only failures.
            if (!result.ok)
                sys::log("[Afterglow] account: %s", result.message.c_str());
            else if (!result.save_error.empty())
                sys::log("[Afterglow] account: token not saved: %s", result.save_error.c_str());
            if (result.job == afterglow::AccountJob::renew && result.ok && result.token_changed)
            {
                // Same server: the open connections take the fresh token for their
                // next login, and only then is the old one retired (LG 1.4.3).
                stored_auth = result.auth;
                credentials.token = stored_auth.token;
                companion->set_token(stored_auth.token);
            }
            else if (result.job == afterglow::AccountJob::log_out ||
                     (result.ok && result.token_changed))
            {
                stored_auth = result.auth;
                restart_companion(afterglow::account_wire::effective_credentials(
                    stored_auth, config.host, static_cast<std::uint16_t>(config.ma_port),
                    config.ma_token));
            }
            retiring = stored_auth.valid() ? std::move(result.retire) : std::vector<std::string>{};
            // Renewal and retiring stay quiet unless the new token could not be
            // saved: then the next launch falls back to the old one, so say so.
            const bool quiet = (result.job == afterglow::AccountJob::renew ||
                                result.job == afterglow::AccountJob::retire) &&
                               result.save_error.empty();
            const auto status = quiet ? model.account.status : result.message;
            const auto sync = model.account.sync_status;
            model.account = afterglow::account_wire::account_state(credentials, stored_auth);
            model.account.status = status;
            model.account.sync_status = sync;
        }
        if (!retiring.empty() && account.retire(stored_auth, retiring))
            retiring.clear();
        if (renew_at && now >= renew_at && !account.busy())
        {
            renew_at = 0;
            if (credentials.source == afterglow::AccountState::Source::signed_in)
                account.renew(stored_auth);
        }
        if (credentials.source == afterglow::AccountState::Source::signed_in &&
            model.companion.token_rejected)
            model.account.status =
                "Music Assistant no longer accepts this PS5's token. Sign in again.";
    };
    ps5::Pad pad;
    pad.open();
    afterglow::LightbarLimiter lightbar_limiter;
    bool lightbar_lit = true;
    const auto ime = ps5::make_ime_backend(pad.user());
    ps5::Keyboard keyboard(*ime);
    afterglow::KeyboardPurpose keyboard_purpose = afterglow::KeyboardPurpose::none;
    DevChannel dev;
    open_dev_channel(dev, config.dev_token);
    InputTracker tracker;
    demo::Catalog catalog;
    Settings settings;
    app::Telemetry telemetry;
    app::Context context{fonts, catalog, telemetry, settings};
    auto screen = concepts::make_afterglow(context);
    app::Frame frame;
    app::Feedback feedback;
    std::array<PadSample, 64> samples{};
    std::uint64_t frames = 0;
    auto previous = sys::monotonic_us();
    auto next_status = previous;
    float fps_time = 0;
    int fps_count = 0;
    while (!model.exit_requested)
    {
        const auto now = sys::monotonic_us();
        const float elapsed = static_cast<float>(now - previous) / 1000000;
        const float dt = std::clamp(elapsed, 0.001f, 0.05f);
        previous = now;
        if (now >= next_status)
        {
            const auto state = client->snapshot();
            model.companion = companion->snapshot();
            afterglow::pump_lyrics(
                model, lyrics_lookup, online_lyrics, [&] { companion->retry_lyrics(); },
                now / 1000);
            if (model.companion.favorites_ready &&
                (!favorites_received || model.companion.favorite_revision != favorite_revision))
            {
                favorites_received = true;
                favorite_revision = model.companion.favorite_revision;
                model.preferences.favorites = model.companion.favorites;
                synced_favorites = model.preferences.favorites;
            }
            model.connected = state.connected;
            model.playing = state.playing;
            if (model.configured)
                model.status = state.status;
            model.title = state.title;
            model.artist = state.artist;
            // Sendspin joins a song's artists with "/", which can't be told
            // from a name like AC/DC; Music Assistant's queue lists them, so
            // its names win while it describes the same song.
            if (model.companion_configured && !model.companion.track.artist.empty() &&
                model.companion.track.title == state.title)
                model.artist = model.companion.track.artist;
            model.album = state.album;
            model.group = state.group;
            model.volume = state.volume;
            model.buffered_ms = state.buffered_ms;
            model.underruns = state.underruns;
            model.late_frames = state.late_frames;
            model.reconnects = state.reconnects;
            model.received_packets = state.received_packets;
            model.output_errors = output.errors();
            if (model.preferences.follow && model.companion_configured)
            {
                model.connected = model.companion.connected;
                model.status = model.companion.status;
                model.playing = model.companion.playing;
                model.title = model.companion.track.title;
                model.artist = model.companion.track.artist;
                model.album = model.companion.track.album;
                model.group = "Following " + model.companion.player_name;
                model.volume = model.companion.volume;
            }
            const std::string next_art = model.idle_showing ? model.idle_artwork
                                         : model.preferences.follow || !applied_player.empty()
                                             ? model.companion.track.artwork
                                             : "";
            if (next_art != artwork_url)
            {
                artwork_url = next_art;
                if (model.artwork_texture)
                    glDeleteTextures(1, &model.artwork_texture);
                model.artwork_texture = 0;
                model.artwork_color = afterglow::Presentation{}.artwork_color;
                model.artwork_secondary = afterglow::Presentation{}.artwork_secondary;
            }
            artwork_generation = artwork->request(next_art);
            model.save_status = preference_store.status();
            mirror_account(model, *companion, mirror, config);
            model.night_hours = afterglow::night_hour(afterglow::local_hour());
            next_status = now + 100000;
        }
        analyzer->set_beat_sensitivity(model.preferences.beat_sensitivity);
        follow_analyzer->set_beat_sensitivity(model.preferences.beat_sensitivity);
        model.spectrum = analyzer->update(dt);
        std::vector<float> pcm = model.preferences.follow
                                     ? companion->take_pcm(model.preferences.trim_ms)
                                     : analyzer->fresh_pcm();
        if (model.preferences.follow && !pcm.empty())
        {
            std::vector<std::int16_t> samples(pcm.size());
            for (std::size_t i = 0; i < pcm.size(); ++i)
                samples[i] = static_cast<std::int16_t>(std::clamp(pcm[i], -1.0f, 1.0f) * 32767);
            // A dedicated analyser is used below; the audio producer owns analyzer.
            follow_analyzer->push(samples.data(), static_cast<int>(samples.size() / 2));
        }
        if (model.preferences.follow)
            model.spectrum = follow_analyzer->update(dt);
        model.beat = model.preferences.follow ? follow_analyzer->beat() : analyzer->beat();
        if (model.preferences.auto_level && !pcm.empty())
        {
            float peak = 0;
            for (float sample : pcm)
                peak = std::max(peak, std::abs(sample));
            const float target = peak > 0.005f ? std::clamp(0.65f / peak, 0.5f, 4.0f) : 1.0f;
            analysis_gain += (target - analysis_gain) * std::min(1.0f, dt * 2);
            for (float &sample : pcm)
                sample = std::clamp(sample * analysis_gain, -1.0f, 1.0f);
        }
        if (!model.scanning)
            milkdrop->feed(pcm.data(), pcm.size() / 2);
        afterglow::ArtworkResult image;
        if (artwork->take_result(image) && image.generation == artwork_generation)
        {
            if (model.artwork_texture)
                glDeleteTextures(1, &model.artwork_texture);
            model.artwork_texture = 0;
            model.artwork_color = image.accent;
            model.artwork_secondary = image.secondary;
            if (!image.rgba.empty())
                model.artwork_texture =
                    renderer.batch().create_texture(image.width, image.height, image.rgba.data());
        }
        milkdrop->set_album_colours(model.artwork_color, model.artwork_secondary);
        thumbnails->pump(thumb_upload, thumb_release);
        const auto count = pad.read(samples);
        auto input = tracker.update(std::span<const PadSample>(samples.data(), count),
                                    static_cast<std::uint64_t>(now));
        pump_keyboard(keyboard, model, keyboard_purpose);
        if (model.keyboard_open)
        {
            // The system keyboard owns the controller: the screen sees nothing held.
            InputFrame blocked;
            blocked.connected = input.connected;
            blocked.focus_lost = true;
            input = blocked;
        }
        drive_lightbar(pad, lightbar_limiter, model, now, lightbar_lit);
        if (const auto request = poll_dev(dev, now))
            handle_dev_request(dev, *request, model, display);
        pump_dev_presses(dev, input);
        feedback.clear();
        screen->update(input, dt, feedback);
        sync_home(*home, model);
        if (model.browse_pending)
        {
            companion->browse(model.browse_request);
            model.browse_pending = false;
        }
        if (std::exchange(model.recent_pending, false))
            companion->load_recent();
        if (model.queue_offset_request >= 0 ||
            model.queue_offset_request == afterglow::kQueueOffsetDefault)
        {
            companion->set_queue_offset(model.queue_offset_request);
            model.queue_offset_request = -1;
        }
        if (model.companion_action_pending)
        {
            companion->action(model.companion_action);
            model.companion_action_pending = false;
        }
        if (model.seek_seconds != 0)
        {
            companion->seek_by(model.seek_seconds);
            model.seek_seconds = 0;
        }
        pump_account(now);
        const auto command = [&](afterglow::Command value)
        {
            if (model.preferences.follow)
                companion->command(value);
            else
                client->command(value);
        };
        switch (model.command)
        {
        case afterglow::UiCommand::toggle:
            command(afterglow::Command::toggle);
            break;
        case afterglow::UiCommand::previous:
            command(afterglow::Command::previous);
            break;
        case afterglow::UiCommand::next:
            command(afterglow::Command::next);
            break;
        case afterglow::UiCommand::volume_up:
            command(afterglow::Command::volume_up);
            break;
        case afterglow::UiCommand::volume_down:
            command(afterglow::Command::volume_down);
            break;
        case afterglow::UiCommand::preset_previous:
            milkdrop->previous();
            break;
        case afterglow::UiCommand::preset_next:
            milkdrop->next();
            break;
        case afterglow::UiCommand::preset_select:
            milkdrop->select(model.selected_preset);
            break;
        case afterglow::UiCommand::none:
            break;
        }
        model.command = afterglow::UiCommand::none;
        if (model.preferences.favorites != synced_favorites)
        {
            for (const auto &name : model.preferences.favorites)
                if (std::find(synced_favorites.begin(), synced_favorites.end(), name) ==
                    synced_favorites.end())
                    companion->set_favorite(name, true);
            for (const auto &name : synced_favorites)
                if (!model.preferences.favorite(name))
                    companion->set_favorite(name, false);
            synced_favorites = model.preferences.favorites;
        }
        model.preferences.favorite_pending = companion->pending_favorites();
        if (!model.scanning && scan_phase != 0)
        {
            model.quality.cancel();
            model.preferences.quality = model.quality.encode();
            model.preferences.scan_cursor = static_cast<int>(scan_index);
            model.scan_status = "Preset scan paused";
            scan_phase = 0;
        }
        if (model.scanning && scan_phase == 0)
        {
            if (model.presets.empty())
                model.scanning = false;
            else
            {
                scan_index = static_cast<std::size_t>(model.preferences.scan_cursor);
                while (scan_index < model.presets.size() &&
                       !model.quality.eligible(model.presets[scan_index], 30, false))
                    ++scan_index;
                if (scan_index >= model.presets.size())
                {
                    model.preferences.scan_cursor = 0;
                    model.scanning = false;
                    model.scan_status = "Preset scan complete";
                }
                else
                {
                    model.quality.begin(model.presets[scan_index]);
                    model.preferences.quality = model.quality.encode();
                    model.preferences.scan_cursor = static_cast<int>(scan_index + 1);
                    model.scan_status = "Saving scan checkpoint";
                    scan_phase = 1;
                }
            }
        }
        else if (model.scanning && scan_phase == 1)
        {
            const auto saved_status = preference_store.status();
            if (saved_status == "Preferences saved")
            {
                if (!milkdrop->select(scan_index))
                {
                    model.quality.quarantine(model.presets[scan_index]);
                    model.preferences.quality = model.quality.encode();
                    model.scanning = false;
                    model.scan_status = "Preset failed; scan stopped";
                    scan_phase = 0;
                }
                else
                {
                    model.scan_status = "Scanning " + std::to_string(scan_index + 1) + " / " +
                                        std::to_string(model.presets.size());
                    scan_signal_samples = 0;
                    scan_phase = 2;
                }
            }
            else if ((saved_status.starts_with("Preferences not saved") ||
                      saved_status.find("unavailable") != std::string::npos))
            {
                model.scanning = false;
                model.quality.cancel();
                model.preferences.quality = model.quality.encode();
                model.preferences.scan_cursor = static_cast<int>(scan_index);
                scan_phase = 0;
                model.scan_status = "Scan stopped: checkpoint could not be saved";
            }
        }
        const std::string preferences = model.preferences.encode();
        if (preferences != applied_preferences || configured_scan != model.scanning ||
            configured_refresh != model.accepted_refresh_hz)
        {
            applied_preferences = preferences;
            auto visual_preferences = model.preferences;
            visual_preferences.fps = std::min(visual_preferences.fps, model.accepted_refresh_hz);
            if (model.scanning)
            {
                visual_preferences.locked = true;
                visual_preferences.fps = 60;
            }
            milkdrop->configure(visual_preferences);
            configured_scan = model.scanning;
            configured_refresh = model.accepted_refresh_hz;
            milkdrop->set_scanning(model.scanning);
            client->set_output_enabled(!model.preferences.follow);
            companion->set_relay_enabled(model.preferences.follow);
            preference_store.submit(preferences);
        }
        std::string desired_player =
            model.preferences.follow ? model.preferences.player : config.ma_player_id;
        if (!model.preferences.follow && desired_player.empty())
            for (const auto &player : model.companion.players)
                if (player.name == config.name)
                {
                    desired_player = player.id;
                    break;
                }
        if (desired_player != applied_player)
        {
            applied_player = desired_player;
            companion->select_player(desired_player);
        }
        visual_elapsed += elapsed;
        scan_frame_elapsed = model.scanning && scan_phase == 2 ? scan_frame_elapsed + elapsed : 0;
        const int visual_fps =
            std::min(model.scanning ? 60 : model.preferences.fps, model.accepted_refresh_hz);
        const float visual_period = 1.0f / static_cast<float>(visual_fps);
        bool rendered_visual = false;
        const float visual_interval = scan_frame_elapsed;
        if (milkdrop->ready() && model.milkdrop_visible &&
            (visual_elapsed + 0.0005f >= visual_period) &&
            ((model.scanning ||
              (!settings.reduced_motion && (!model.preferences.hold_idle || model.playing))) ||
             model.preset != milkdrop->preset_name()))
        {
            if (model.scanning && scan_phase == 2)
            {
                // Repeatable visual-only excitation; never sent to audio output.
                std::array<float, 1600> signal{};
                for (std::size_t i = 0; i < signal.size() / 2; ++i)
                {
                    const double phase = static_cast<double>(scan_signal_samples++);
                    const float value = static_cast<float>(
                        std::sin(phase * 0.0288) * (0.35 + 0.2 * std::sin(phase * 0.00026)) +
                        0.1 * std::sin(phase * 0.0576));
                    signal[i * 2] = signal[i * 2 + 1] = value;
                }
                milkdrop->feed(signal.data(), signal.size() / 2);
            }
            milkdrop->render();
            rendered_visual = true;
            scan_frame_elapsed = 0;
            if (model.scanning && scan_phase == 2 && milkdrop->failed())
            {
                model.quality.quarantine(model.presets[scan_index]);
                model.preferences.quality = model.quality.encode();
                model.scanning = false;
                scan_phase = 0;
                model.scan_status = "Preset failed; scan stopped";
            }
            visual_elapsed = std::fmod(std::max(visual_elapsed, visual_period), visual_period);
        }
        model.milkdrop_texture = milkdrop->ready() ? milkdrop->texture() : 0;
        model.preset = milkdrop->preset_name();
        model.preset_count = milkdrop->preset_count();
        frame.reset();
        screen->draw(frame);
        renderer.begin();
        renderer.backdrop(frame.backdrop);
        renderer.draw(frame.scene);
        renderer.draw(frame.overlay);
        renderer.present(0, display.width(), display.height());
        take_dev_screenshot(dev, renderer, model, display);
        if (!display.swap())
            break;
        model.accepted_refresh_hz = display.refresh_hz();
        model.refresh_confirmed = display.refresh_confirmed();
        if (frames++ == 0)
        {
            sys::log("[Afterglow] display requested=%dHz accepted=%dHz confirmed=%d",
                     model.requested_refresh_hz, model.accepted_refresh_hz,
                     model.refresh_confirmed);
            sys::hide_splash_screen();
        }
        if (model.scanning && scan_phase == 2 && rendered_visual &&
            model.quality.observe(visual_interval))
        {
            model.preferences.quality = model.quality.encode();
            scan_phase = 0;
            if (model.preferences.scan_cursor >= static_cast<int>(model.presets.size()))
            {
                model.preferences.scan_cursor = 0;
                model.scanning = false;
                model.scan_status = "Preset scan complete";
            }
        }
        fps_time += elapsed;
        ++fps_count;
        if (fps_time >= 1)
        {
            model.fps = static_cast<float>(fps_count) / fps_time;
            fps_count = 0;
            fps_time = 0;
        }
        if (frames % 600 == 0)
        {
            sys::log("[Afterglow] connected=%d playing=%d buffer=%.0fms underruns=%llu late=%llu "
                     "reconnects=%llu output_errors=%llu fps=%.1f",
                     model.connected ? 1 : 0, model.playing ? 1 : 0, model.buffered_ms,
                     static_cast<unsigned long long>(model.underruns),
                     static_cast<unsigned long long>(model.late_frames),
                     static_cast<unsigned long long>(model.reconnects),
                     static_cast<unsigned long long>(model.output_errors), model.fps);
            std::size_t live = 0, peak = 0, blocks = 0, failures = 0;
            hui_heap_stats(&live, &peak, &blocks, &failures);
            sys::log("[Afterglow] heap live=%zu peak=%zu blocks=%zu failures=%zu", live, peak,
                     blocks, failures);
        }
    }
    model.quality.cancel();
    model.preferences.quality = model.quality.encode();
    model.preferences.favorite_pending = companion->pending_favorites();
    preference_store.submit(model.preferences.encode());
    preference_store.stop();
    account.stop();
    keyboard.abort();
    companion->stop();
    home->stop();
    artwork->stop();
    thumbnails->stop();
    online_lyrics.stop();
    model.thumbnail = nullptr;
    thumbnails->clear(thumb_release);
    milkdrop->release();
    if (model.artwork_texture)
        glDeleteTextures(1, &model.artwork_texture);
    client->stop();
    output.stop();
    client->join();
    companion->join();
    account.join();
    home->join();
    artwork->join();
    thumbnails->join();
    online_lyrics.join();
    preference_store.join();
    pad.reset_light_bar();
    pad.close();
    renderer.release();
    display.close();
    sys::quit();
    return 0;
}
