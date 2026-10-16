// ps5-homebrew-ui - Headless host renderer: runs the tour and writes PNG files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: hui_snapshots <assets dir> <output dir> [design id|all] [width height]
//
// Runs the same shell, designs and tour script as the console build, through
// Mesa's surfaceless EGL (llvmpipe), with a fixed 60 Hz clock. Only the frames
// the tour marks are rendered, so a full run takes seconds. With
// HUI_STRIP=<from>,<to> it instead writes every frame of the switch between
// two designs (for reviewing the transition).
//
// HUI_REEL=<dir> records instead of photographing: every third simulated frame
// is piped to ffmpeg, which writes animated WebP clips into <dir>.
//   HUI_REEL_SPLIT=1   one clip per tour picture (the Theme Lab: one per theme)
//   otherwise          one clip per design, its whole tour
//   HUI_REEL=<dir> HUI_REEL_SWITCH=1   one clip of L1/R1 cycling through all designs

#include "app/shell.hpp"
#include "afterglow/presentation.hpp"
#include "afterglow/home_assistant.hpp"
#include "afterglow/milkdrop.hpp"
#include "app/tour.hpp"
#include "core/save_file.hpp"
#include "demo/catalog.hpp"
#include "gfx/gl_program.hpp"
#include "gfx/renderer.hpp"
#include "manifest.hpp"
#include "milkdrop_capture.hpp"
#include "ui/fonts.hpp"
extern "C" unsigned char *stbi_load_from_memory(const unsigned char *, int, int *, int *, int *,
                                                int);
extern "C" void stbi_image_free(void *);

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>

// Upstream zero-initializes its C structs; keep vendor diagnostics local.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"
#pragma clang diagnostic pop

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

bool open_context()
{
    auto get_platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        get_platform_display != nullptr
            ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
            : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API))
        return false;
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                         4,
                                         EGL_CONTEXT_MINOR_VERSION,
                                         5,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                         EGL_NONE};
    EGLContext context =
        eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, context_attributes);
    return context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

bool load_font(hui::gfx::Renderer &renderer, const std::string &path, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        std::fprintf(stderr, "cannot load font %s\n", path.c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// Streams raw frames to ffmpeg and gives the finished clip its name on close
// (the name is only known once the tour reaches the picture that ends it).
class Recorder
{
  public:
    Recorder(std::string directory, int width, int height, int fps)
        : directory_(std::move(directory)), width_(width), height_(height), fps_(fps)
    {
    }
    ~Recorder()
    {
        // Frames after the last picture belong to no clip.
        if (pipe_ != nullptr)
        {
            pclose(pipe_);
            std::remove((directory_ + "/.recording.webp").c_str());
        }
    }
    void frame(const unsigned char *rgba)
    {
        if (pipe_ == nullptr)
        {
            char command[512];
            std::snprintf(command, sizeof(command),
                          "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s %dx%d -r %d -i - "
                          "-vf vflip -c:v libwebp_anim -lossless 0 -q:v 62 -compression_level 5 "
                          "-loop 0 '%s/.recording.webp'",
                          width_, height_, fps_, directory_.c_str());
            pipe_ = popen(command, "w");
            frames_ = 0;
        }
        if (pipe_ != nullptr)
        {
            std::fwrite(rgba, 1, static_cast<std::size_t>(width_) * height_ * 4, pipe_);
            ++frames_;
        }
    }
    void close(const std::string &name)
    {
        if (pipe_ == nullptr)
            return;
        pclose(pipe_);
        pipe_ = nullptr;
        const std::string from = directory_ + "/.recording.webp";
        const std::string to = directory_ + "/" + name + ".webp";
        std::rename(from.c_str(), to.c_str());
        std::fprintf(stderr, "recorded %s: %d frames\n", to.c_str(), frames_);
    }

  private:
    std::string directory_;
    int width_;
    int height_;
    int fps_;
    FILE *pipe_ = nullptr;
    int frames_ = 0;
};

// Illustrative Music Assistant answers for the Afterglow tour: what main.cpp
// and the companion would do with a request, so search and drill-down pages
// can be photographed. Invented titles; no live server is contacted.
hui::afterglow::CompanionTrack preview_item(const char *title, const char *subtitle,
                                            const char *type, int id)
{
    hui::afterglow::CompanionTrack item;
    item.title = title;
    item.subtitle = subtitle;
    item.media_type = type;
    item.item_id = std::to_string(id);
    item.provider = "library";
    item.uri = std::string("library://") + type + "/" + item.item_id;
    return item;
}
void answer_afterglow_preview()
{
    using hui::afterglow::BrowseCategory;
    using hui::afterglow::KeyboardPurpose;
    auto &model = hui::afterglow::presentation();
    model.companion_action_pending = false;
    model.queue_offset_request = -1;
    if (model.keyboard_pending && model.keyboard.purpose == KeyboardPurpose::search)
    {
        model.keyboard_pending = false;
        model.keyboard_result.purpose = KeyboardPurpose::search;
        model.keyboard_result.accepted = true;
        model.keyboard_result.text = "light";
        model.keyboard_result_ready = true;
    }
    if (!model.browse_pending)
        return;
    model.browse_pending = false;
    auto &items = model.companion.browse_items;
    items.clear();
    switch (model.browse_request.category)
    {
    case BrowseCategory::search:
        items = {preview_item("Lightkeeper", "34 albums", "artist", 1),
                 preview_item("Low Light Choir", "6 albums", "artist", 2),
                 preview_item("Light Across the Ceiling", "Lightkeeper", "album", 3),
                 preview_item("Morning Light", "Low Light Choir", "album", 4),
                 preview_item("First Light", "Lightkeeper", "track", 5),
                 preview_item("Lighthouse", "Harbour Lamps", "track", 6),
                 preview_item("Night Light Radio", "Radio station", "radio", 7),
                 preview_item("Light Reading", "Paper Lantern · 120 episodes", "podcast", 8)};
        break;
    case BrowseCategory::artist_albums:
        items = {preview_item("Light Across the Ceiling", "Lightkeeper · 2024", "album", 9),
                 preview_item("Slow Current", "Lightkeeper · 2021", "album", 10),
                 preview_item("Harbour Songs", "Lightkeeper · 2018", "album", 11)};
        break;
    default:
        items = {preview_item("Evening Light", "Afterglow preview", "album", 12),
                 preview_item("Still Water", "Afterglow preview", "playlist", 13),
                 preview_item("The Long Way Home", "Afterglow preview", "track", 14)};
        model.companion.browse_has_more = true;
        return;
    }
    model.companion.browse_has_more = false;
}

// An illustrative Home glance for previews; AFTERGLOW_HOME=connecting, offline,
// auth or unset shows that state instead.
void preview_home(hui::afterglow::HomeSnapshot &home)
{
    namespace wire = hui::afterglow::home_wire;
    const char *mode = std::getenv("AFTERGLOW_HOME");
    const std::string state = mode != nullptr ? mode : "connected";
    if (state == "unset")
        return;
    const auto config = wire::parse_config(
        "ha_group=Living room:sensor.room_temperature=Temperature,sensor.room_humidity=Humidity,"
        "light.ceiling=Ceiling light,light.lamp=Reading lamp,switch.fairy_lights=Fairy lights,"
        "media_player.speakers=Music,scene.film_night=Film night\n"
        "ha_group=Outside:weather.home=Weather,sensor.outdoor_air=Air quality\n"
        "ha_group=Heating:climate.radiator=Radiator,sensor.hot_water=Hot water\n"
        "ha_group=People:person.alex=Alex,person.sam=Sam\n");
    wire::StateTable table;
    if (state != "connecting" && state != "auth")
        table.apply(R"({"a":{
"sensor.room_temperature":{"s":"21.34","a":{"unit_of_measurement":"\u00b0C"}},
"sensor.room_humidity":{"s":"48.6","a":{"unit_of_measurement":"%"}},
"light.ceiling":{"s":"on","a":{}},"light.lamp":{"s":"off","a":{}},
"switch.fairy_lights":{"s":"unavailable","a":{}},
"media_player.speakers":{"s":"playing","a":{"media_title":"Slow Current","media_artist":"Afterglow preview"}},
"scene.film_night":{"s":"2026-10-01T20:00:00+00:00","a":{}},
"weather.home":{"s":"partlycloudy","a":{"temperature":14.2,"temperature_unit":"\u00b0C"}},
"sensor.outdoor_air":{"s":"good","a":{}},
"climate.radiator":{"s":"heat","a":{"hvac_action":"heating","current_temperature":20.5}},
"sensor.hot_water":{"s":"idle","a":{}},
"person.alex":{"s":"home","a":{}},"person.sam":{"s":"not_home","a":{}}}})");
    home.configured = true;
    home.groups = wire::build_groups(config, table);
    home.link = state == "offline"      ? hui::afterglow::HomeLink::offline
                : state == "auth"       ? hui::afterglow::HomeLink::auth_failed
                : state == "connecting" ? hui::afterglow::HomeLink::connecting
                                        : hui::afterglow::HomeLink::connected;
    home.connected = home.link == hui::afterglow::HomeLink::connected;
    home.status = state == "offline" ? "Home Assistant is offline · retrying in 8 s" : "Preview";
    home.revision = 1;
}
} // namespace

int main(int argc, char **argv)
{
    if (std::getenv("AFTERGLOW_PREVIEW") != nullptr)
    {
        auto &model = hui::afterglow::presentation();
        model.configured = true;
        model.connected = true;
        model.playing = true;
        model.preview = true;
        model.title = "Slow Current";
        model.artist = "Afterglow preview";
        model.album = "Illustrative track";
        model.status = "Preview";
        model.group = "Afterglow PS5";
        model.volume = 60;
        model.companion_configured = true;
        model.companion.status = "Preview";
        model.companion.connected = true;
        model.companion.elapsed = 39;
        model.companion.track.duration = 240;
        model.companion.track.lrc_lyrics = "[00:00]A little room to breathe\n[00:30]The light "
                                           "across the ceiling\n[01:00]The music underneath";
        model.artwork_color = {0.16f, 0.25f, 0.72f};
        model.artwork_secondary = {0.07f, 0.11f, 0.32f};
        model.companion.track.quality_badge = "Lossless · FLAC 44.1 kHz / 16-bit";
        if (const char *lyrics = std::getenv("AFTERGLOW_LYRICS"))
        {
            // karaoke: enhanced LRC word timings on the current line.
            if (std::string_view(lyrics) == "karaoke")
                model.companion.track.lrc_lyrics =
                    "[00:00]A little room to breathe\n[00:30]<00:30.00>The <00:32.00>light "
                    "<00:36.00>across <00:41.00>the <00:43.00>ceiling\n[01:00]The music underneath";
            else if (std::string_view(lyrics) != "timed")
                model.companion.track.lrc_lyrics.clear();
        }
        // AFTERGLOW_NIGHT=1: photograph the night register whatever the hour.
        model.night_hours = std::getenv("AFTERGLOW_NIGHT") != nullptr;
        // AFTERGLOW_BEAT=<0..1>: hold the beat pulse for halo/spectrum captures.
        if (const char *beat = std::getenv("AFTERGLOW_BEAT"))
            model.beat = static_cast<float>(std::atof(beat));
        // AFTERGLOW_VISUAL=1|2: halo or spectrum instead of MilkDrop.
        if (const char *visual = std::getenv("AFTERGLOW_VISUAL"))
            model.preferences.visual_style = std::atoi(visual);
        model.companion.players = {{"preview-room", "Listening room"},
                                   {"preview-kitchen", "Kitchen"}};
        model.companion.track.title = model.title;
        model.companion.track.artist = model.artist;
        model.companion.track.album = model.album;
        model.companion.track.lyrics = "Illustrative lyric preview\nThe room grows quiet\nLight "
                                       "moves across the ceiling\nA slow current carries us home";
        if (const char *lyrics = std::getenv("AFTERGLOW_LYRICS"))
        {
            // missing / failed: what the lyrics lookup found (Settings → Lyrics
            // then offers Retry lyrics).
            const std::string_view kind(lyrics);
            if (kind == "none" || kind == "missing" || kind == "failed")
                model.companion.track.lyrics.clear();
            if (kind == "missing")
                model.lyrics.status = hui::afterglow::LyricsStatus::missing;
            if (kind == "failed")
                model.lyrics.status = hui::afterglow::LyricsStatus::error;
        }
        hui::afterglow::CompanionTrack next;
        next.title = "Evening Light";
        next.artist = "Afterglow preview";
        model.companion.upcoming.push_back(next);
        next.title = "Still Water";
        model.companion.upcoming.push_back(next);
        next.title = "The Long Way Home";
        model.companion.upcoming.push_back(next);
        preview_home(model.home);
        for (std::size_t i = 0; i < model.spectrum.size(); ++i)
            model.spectrum[i] = 0.25f + 0.65f * static_cast<float>((i * 7 + 11) % 17) / 17;
        model.companion.static_delay_ms = 120;
        model.account.source = hui::afterglow::AccountState::Source::config;
        model.account.host = "music.local";
        model.home.configured = true;
    }
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <assets dir> <output dir> [design id|all] [width height]\n",
                     argv[0]);
        return 2;
    }
    const std::string assets = argv[1];
    const std::string output = argv[2];
    const std::string only = argc > 3 && std::string(argv[3]) != "all" ? argv[3] : "";
    const int width = argc > 5 ? std::atoi(argv[4]) : 1920;
    const int height = argc > 5 ? std::atoi(argv[5]) : 1080;

    if (!open_context())
    {
        std::fprintf(stderr, "no surfaceless EGL OpenGL 4.5 context\n");
        return 1;
    }
    std::fprintf(stderr, "GL %s / %s\n", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
                 reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    hui::gfx::set_glsl_prefix("#version 450 core\n");

    hui::gfx::Renderer renderer;
    hui::gfx::Font regular;
    hui::gfx::Font semibold;
    hui::gfx::Font display;
    hui::gfx::Font mono;
    hui::gfx::Font pixel;
    hui::gfx::Font hand;
    hui::ui::Fonts fonts;
    // The console loads Archivo for Afterglow whatever its state, so captures must too.
    const bool afterglow_fonts = std::getenv("AFTERGLOW_PREVIEW") != nullptr || only == "afterglow";
    if (!renderer.init() ||
        !load_font(renderer,
                   assets + (afterglow_fonts ? "/fonts/archivo-regular.huifont"
                                             : "/fonts/inter-regular.huifont"),
                   &regular, &fonts.regular) ||
        !load_font(renderer,
                   assets + (afterglow_fonts ? "/fonts/archivo-semibold.huifont"
                                             : "/fonts/inter-semibold.huifont"),
                   &semibold, &fonts.semibold) ||
        !load_font(renderer,
                   assets + (afterglow_fonts ? "/fonts/archivo-display.huifont"
                                             : "/fonts/montserrat-medium.huifont"),
                   &display, &fonts.display) ||
        !load_font(renderer, assets + "/fonts/dejavu-sans-mono.huifont", &mono, &fonts.mono) ||
        !load_font(renderer, assets + "/fonts/press-start-2p.huifont", &pixel, &fonts.pixel) ||
        !load_font(renderer, assets + "/fonts/patrick-hand.huifont", &hand, &fonts.hand))
        return 1;
    hui::demo::Catalog catalog;
    if (!catalog.build_covers(renderer, fonts))
    {
        std::fprintf(stderr, "cover art failed\n");
        return 1;
    }

    // AFTERGLOW_MILKDROP=<count>: one picture per MilkDrop preset, then stop.
    if (const char *count = std::getenv("AFTERGLOW_MILKDROP"))
        return hui::host::capture_milkdrop(assets + "/presets", output, std::atoi(count));

    // Compose the actual off-screen visualiser texture through the normal kit.
    hui::afterglow::Milkdrop milkdrop_preview;
    if (std::getenv("AFTERGLOW_PREVIEW") != nullptr && only == "afterglow")
    {
        // AFTERGLOW_TINT=<25..100>: album colour recolouring of the preset's slots.
        if (const char *tint = std::getenv("AFTERGLOW_TINT"))
        {
            auto &model = hui::afterglow::presentation();
            model.preferences.tint = true;
            model.preferences.tint_strength = std::atoi(tint);
            milkdrop_preview.configure(model.preferences);
            milkdrop_preview.set_album_colours(model.artwork_color, model.artwork_secondary);
        }
        if (!milkdrop_preview.init(1280, 720, assets + "/presets") || !milkdrop_preview.select(0))
            return 1;
        std::size_t sample = 0;
        hui::host::advance_milkdrop(milkdrop_preview, sample, 90);
        auto &model = hui::afterglow::presentation();
        model.milkdrop_texture = milkdrop_preview.texture();
        model.preset = milkdrop_preview.preset_name();
        model.preset_count = milkdrop_preview.preset_count();
        model.presets = milkdrop_preview.names();
        model.artwork_texture =
            milkdrop_preview.texture(); // Illustrative cover for layout validation.
        if (const char *cover = std::getenv("AFTERGLOW_COVER"))
        {
            int w = 0, h = 0, channels = 0;
            std::string bytes;
            hui::save::read_file(cover, &bytes, 8u << 20);
            auto *pixels =
                stbi_load_from_memory(reinterpret_cast<const unsigned char *>(bytes.data()),
                                      static_cast<int>(bytes.size()), &w, &h, &channels, 4);
            if (pixels)
            {
                model.artwork_texture = renderer.batch().create_texture(w, h, pixels);
                stbi_image_free(pixels);
            }
        }
    }

    // HUI_ICON=<file.png>: draw the 512x512 launcher icon with the kit and stop.
    if (const char *icon_path = std::getenv("HUI_ICON"))
    {
        constexpr int kIcon = 512;
        GLuint target = 0;
        GLuint storage = 0;
        glGenFramebuffers(1, &target);
        glGenRenderbuffers(1, &storage);
        glBindRenderbuffer(GL_RENDERBUFFER, storage);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kIcon, kIcon);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, storage);
        using hui::gfx::Color;
        hui::gfx::BackdropSpec spec;
        spec.mode = hui::gfx::BackdropMode::aurora;
        spec.colors[0] = Color::rgb(0x0b0e20);
        spec.colors[1] = Color::rgb(0x1b1450);
        spec.colors[2] = Color::rgb(0x3b6cff);
        spec.colors[3] = Color::rgb(0x7cf0c8);
        spec.time = 42.0f;
        hui::gfx::DrawList list;
        // Three cards fanned out, the front one focused: the app in one glance.
        list.rotated_rect({118, 150, 230, 150}, 26, -0.28f, Color::rgb(0xffffff, 0.16f));
        list.rotated_rect({150, 136, 230, 150}, 26, -0.12f, Color::rgb(0xffffff, 0.3f));
        list.shadow({170, 150, 250, 160}, 30, 40, Color::rgb(0x000000, 0.5f));
        list.glow({166, 128, 250, 160}, 30, 26, Color::rgb(0x7cf0c8, 0.55f));
        list.gradient_rect({166, 128, 250, 160}, 30, Color::rgb(0xf4f7ff), Color::rgb(0xcfd8ff));
        list.bordered_rect({160, 122, 262, 172}, 34, Color::rgb(0x000000, 0.0f), 5,
                           Color::rgb(0x7cf0c8));
        list.rounded_rect({196, 164, 96, 18}, 9, Color::rgb(0x1b1450));
        list.rounded_rect({196, 198, 160, 12}, 6, Color::rgb(0x1b1450, 0.45f));
        list.rounded_rect({196, 222, 124, 12}, 6, Color::rgb(0x1b1450, 0.45f));
        list.arc(366, 176, 30, 9, 0.0f, 4.4f, Color::rgb(0x3b6cff));
        hui::ui::text(list, fonts.display, "UI Lab", 256, 402, 82, Color::rgb(0xffffff),
                      hui::gfx::Align::center);
        hui::ui::text(list, fonts.semibold, "HOMEBREW", 256, 446, 24, Color::rgb(0x7cf0c8),
                      hui::gfx::Align::center, 9.0f);
        renderer.begin();
        renderer.backdrop(spec);
        renderer.draw(list);
        renderer.present(target, kIcon, kIcon, 512.0f, 512.0f);
        std::vector<unsigned char> icon(static_cast<std::size_t>(kIcon * kIcon * 4));
        glReadPixels(0, 0, kIcon, kIcon, GL_RGBA, GL_UNSIGNED_BYTE, icon.data());
        for (std::size_t i = 3; i < icon.size(); i += 4)
            icon[i] = 255;
        stbi_flip_vertically_on_write(1);
        return stbi_write_png(icon_path, kIcon, kIcon, 4, icon.data(), kIcon * 4) != 0 ? 0 : 1;
    }

    GLuint framebuffer = 0;
    GLuint color = 0;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &color);
    glBindRenderbuffer(GL_RENDERBUFFER, color);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return 1;

    // Settings are not read from (or written to) a real save: a scratch folder.
    const std::string data_root = output + "/.data";
    hui::save::ensure_directory(output);
    hui::save::ensure_directory(data_root);
    std::remove((data_root + "/settings.bin").c_str());
    hui::app::Shell shell(fonts, catalog, data_root, renderer.glass_texture());
    shell.set_version("host");

    constexpr float kDt = 1.0f / 60.0f;
    // HUI_MANIFEST=<file.json>: describe every design and theme (host/manifest.cpp).
    if (const char *manifest = std::getenv("HUI_MANIFEST"))
        return hui::host::write_manifest(manifest, shell) ? 0 : 1;

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    stbi_flip_vertically_on_write(1);
    bool ok = true;
    const auto render = [&](const std::string &name)
    {
        shell.compose(renderer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.present(framebuffer, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // The PNG is opaque whatever the blending left in the alpha channel.
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255;
        const std::string path = output + "/" + name + ".png";
        ok = stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0 && ok;
        std::fprintf(stderr, "wrote %s: %zu shapes, %zu draw calls, GL error 0x%x\n", path.c_str(),
                     renderer.last_instances(), renderer.last_draw_calls(), glGetError());
    };

    if (const char *strip = std::getenv("HUI_STRIP"))
    {
        int from = 0;
        int to = 1;
        std::sscanf(strip, "%d,%d", &from, &to);
        hui::InputFrame idle;
        idle.connected = true;
        shell.show(static_cast<std::size_t>(from), false);
        for (int frame = 0; frame < 120; ++frame)
            shell.update(idle, kDt);
        shell.show(static_cast<std::size_t>(to), true);
        for (int frame = 0; frame < 36; ++frame)
        {
            shell.update(idle, kDt);
            char name[32];
            std::snprintf(name, sizeof(name), "strip-%02d", frame);
            render(name);
        }
        return ok ? 0 : 1;
    }

    if (const char *reel = std::getenv("HUI_REEL"))
    {
        constexpr int kEvery = 3; // 60 Hz simulated, 20 frames per second recorded
        hui::save::ensure_directory(reel);
        Recorder recorder(reel, width, height, 60 / kEvery);
        const auto record = [&]()
        {
            shell.compose(renderer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(framebuffer, width, height);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            recorder.frame(pixels.data());
        };
        hui::InputFrame idle;
        idle.connected = true;
        long count = 0;
        if (std::getenv("HUI_REEL_SWITCH") != nullptr)
        {
            // R1 through every design, then the info panel: the app in one clip.
            shell.show(0, false);
            for (std::size_t i = 0; i <= shell.concept_count(); ++i)
            {
                for (int frame = 0; frame < 66; ++frame, ++count)
                {
                    hui::InputFrame input = idle;
                    if (frame == 0 && i > 0)
                        input.pressed = hui::action_bit(hui::Action::page_next);
                    shell.update(input, kDt);
                    if (count % kEvery == 0)
                        record();
                }
            }
            recorder.close("switcher");
            return 0;
        }
        const bool split = std::getenv("HUI_REEL_SPLIT") != nullptr;
        hui::app::Tour tour(shell, only);
        std::size_t design = shell.current();
        char name[96];
        while (!tour.finished() && count < 60 * 60 * 60)
        {
            const hui::InputFrame input = tour.step(kDt);
            if (!split && shell.current() != design)
            {
                std::snprintf(name, sizeof(name), "%s", shell.concept_at(design).info().id);
                recorder.close(name);
                design = shell.current();
            }
            shell.update(input, kDt);
            if (count % kEvery == 0)
                record();
            ++count;
            if (!tour.capture().empty())
            {
                if (split)
                    recorder.close(tour.capture());
                tour.capture_done();
            }
        }
        if (!split)
            recorder.close(shell.concept_at(design).info().id);
        return 0;
    }

    hui::app::Tour tour(shell, only);
    if (tour.finished())
    {
        std::fprintf(stderr, "no design named '%s'\n", only.c_str());
        return 2;
    }
    long frames = 0;
    while (!tour.finished() && frames < 60 * 60 * 60)
    {
        const hui::InputFrame input = tour.step(kDt);
        // Nothing is presented here, so the numbers a console would measure
        // are stood in for: 60 Hz with a little jitter and a rare long frame.
        hui::app::Telemetry &telemetry = shell.telemetry();
        const float jitter = static_cast<float>((frames * 7919) % 97) / 97.0f;
        telemetry.push(16.3f + jitter * 0.8f + (frames % 211 == 0 ? 5.5f : 0.0f));
        telemetry.fps = 59.9f;
        telemetry.average_ms = 16.7f;
        telemetry.voices = static_cast<int>(frames / 40 % 4);
        telemetry.draw_calls = renderer.last_draw_calls();
        telemetry.instances = renderer.last_instances();
        shell.update(input, kDt);
        if (std::getenv("AFTERGLOW_PREVIEW") != nullptr)
            answer_afterglow_preview();
        ++frames;
        if (!tour.capture().empty())
        {
            render(tour.capture());
            tour.capture_done();
        }
        else
        {
            shell.rehearse(); // as on the console, every frame is drawn
        }
    }
    std::fprintf(stderr, "tour: %ld frames simulated\n", frames);
    return ok ? 0 : 1;
}
