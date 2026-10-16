// Afterglow - MilkDrop presets through projectM, rendered into a kit canvas.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/milkdrop.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include <algorithm>
#include <projectM-4/projectM.h>
#include <utility>

namespace hui::afterglow
{
namespace
{
// Matches the TV app: two minutes per preset, 2.7 s blends.
constexpr double kPresetSeconds = 120.0;
constexpr double kBlendSeconds = 2.7;
// A run of broken presets (missing textures, shader errors) stops the hunt
// instead of compiling shaders forever.
constexpr int kMaxFailuresInRow = 8;
// A new song resets the colours to neutral, then its artwork decodes: wait for
// them to settle, and never reload more often than this.
constexpr double kColourSettleSeconds = 1.0;
constexpr double kRecolourSeconds = 3.0;
// A cover fade is uploaded about 15 times a second, and always at its end.
constexpr std::int64_t kCoverUploadUs = 66000;
// The audio handed to listen() (Sendspin and the follow relay).
constexpr double kListenRate = 48000.0;

void *load_gl(const char *name, void *)
{
    return reinterpret_cast<void *>(eglGetProcAddress(name));
}

void log_projectm(const char *message, projectm_log_level level, void *)
{
    sys::log("[Afterglow] projectM level=%d %s", static_cast<int>(level), message);
}

double elapsed_ms(std::int64_t since)
{
    return static_cast<double>(sys::monotonic_us() - since) / 1000.0;
}

// projectM leaves its own programs, VAOs and sampler objects bound. The kit
// sets blending and its program per draw but relies on texture parameters,
// so a bound sampler object would silently change how its atlases sample.
void restore_kit_state()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (GLuint unit = 0; unit < 16; ++unit)
        glBindSampler(unit, 0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlendEquation(GL_FUNC_ADD);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}
} // namespace

Milkdrop::~Milkdrop()
{
    release();
}

bool Milkdrop::init(int width, int height, const std::string &preset_dir)
{
    release();
    std::string index;
    if (save::read_file(preset_dir + "/index.tsv", &index, 1u << 20))
    {
        std::size_t start = 0;
        while (start < index.size())
        {
            auto end = index.find('\n', start);
            if (end == std::string::npos)
                end = index.size();
            const std::string line = index.substr(start, end - start);
            const auto tab = line.find('\t');
            if (tab != std::string::npos && tab > 0 && tab + 1 < line.size())
            {
                Preset preset;
                preset.path = preset_dir + "/" + line.substr(0, tab);
                preset.name = line.substr(tab + 1);
                presets_.push_back(preset);
            }
            start = end + 1;
        }
    }
    if (presets_.empty())
    {
        sys::log("[Afterglow] milkdrop no presets in %s", preset_dir.c_str());
        return false;
    }
    if (!canvas_.create(width, height, 1))
    {
        sys::log("[Afterglow] milkdrop canvas %dx%d failed", width, height);
        return false;
    }
    projectm_set_log_level(PROJECTM_LOG_LEVEL_WARN, false);
    projectm_set_log_callback(log_projectm, false, nullptr);
    const auto started = sys::monotonic_us();
    handle_ = projectm_create_with_opengl_load_proc(load_gl, nullptr);
    if (handle_ == nullptr)
    {
        sys::log("[Afterglow] milkdrop projectm_create failed");
        canvas_.destroy();
        restore_kit_state();
        return false;
    }
    projectm_set_window_size(handle_, static_cast<std::size_t>(width),
                             static_cast<std::size_t>(height));
    projectm_set_fps(handle_, 60);
    projectm_set_mesh_size(handle_, 48, 32);
    projectm_set_preset_duration(handle_, kPresetSeconds);
    projectm_set_soft_cut_duration(handle_, kBlendSeconds);
    projectm_set_hard_cut_enabled(handle_, false);
    projectm_set_preset_switch_requested_event_callback(handle_, on_switch_requested, this);
    projectm_set_preset_switch_failed_event_callback(handle_, on_switch_failed, this);
    clouds_ = cover_clouds();
    texture_pixels_ = clouds_;
    projectm_set_texture_load_event_callback(handle_, on_texture_requested, this);
    random_.seed(static_cast<std::uint32_t>(sys::monotonic_us()));
    order_.resize(presets_.size());
    for (std::size_t i = 0; i < order_.size(); ++i)
        order_[i] = i;
    if (!preferences_.sequential)
        std::shuffle(order_.begin(), order_.end(), random_);
    sys::log("[Afterglow] milkdrop ready presets=%zu size=%dx%d create_ms=%.0f", presets_.size(),
             width, height, elapsed_ms(started));
    const auto first = std::find_if(order_.begin(), order_.end(),
                                    [this](std::size_t index) { return eligible(index); });
    if (first != order_.end())
    {
        position_ = static_cast<std::size_t>(first - order_.begin());
        load(*first, false);
    }
    else
    {
        // Keep the initialized instance and index available for an explicit manual
        // selection or changed filters, without loading a quarantined fallback.
        selection_blocked_ = true;
        sys::log("[Afterglow] milkdrop no eligible presets; waiting for selection");
    }
    restore_kit_state();
    return true;
}

void Milkdrop::release()
{
    if (handle_ != nullptr)
    {
        projectm_destroy(handle_);
        handle_ = nullptr;
        restore_kit_state();
    }
    if (cover_texture_ != 0)
    {
        const GLuint texture = cover_texture_;
        glDeleteTextures(1, &texture);
        cover_texture_ = 0;
    }
    fading_ = false;
    cover_schedule_ = CoverSchedule{};
    fade_from_.clear();
    fade_to_.clear();
    texture_pixels_.clear();
    canvas_.destroy();
    presets_.clear();
    order_.clear();
    position_ = 0;
    name_.clear();
    switch_pending_ = false;
    failures_in_row_ = 0;
    timing_first_frame_ = false;
    selection_blocked_ = false;
    scanning_ = false;
}

bool Milkdrop::select(std::size_t index)
{
    if (handle_ == nullptr || index >= presets_.size())
        return false;
    position_ =
        static_cast<std::size_t>(std::find(order_.begin(), order_.end(), index) - order_.begin());
    load(index, false);
    restore_kit_state();
    return !switch_pending_;
}

void Milkdrop::feed(const float *stereo, std::size_t frames)
{
    if (handle_ == nullptr || frames == 0)
        return;
    projectm_pcm_add_float(handle_, stereo, static_cast<unsigned int>(frames), PROJECTM_STEREO);
}

void Milkdrop::render(double frame_time)
{
    if (handle_ == nullptr || name_.empty() || (selection_blocked_ && !scanning_))
        return;
    update_cover();
    const auto started = sys::monotonic_us();
    projectm_set_frame_time(handle_, frame_time);
    projectm_opengl_render_frame_fbo(handle_, canvas_.framebuffer());
    if (timing_first_frame_)
    {
        timing_first_frame_ = false;
        sys::log("[Afterglow] milkdrop first_frame_ms=%.0f since_load_ms=%.0f", elapsed_ms(started),
                 elapsed_ms(loaded_at_));
    }
    // Loading from the callback would re-enter projectM mid-frame.
    if (switch_pending_ && !scanning_ && failures_in_row_ < kMaxFailuresInRow)
    {
        switch_pending_ = false;
        next();
    }
    else if (!scanning_)
    {
        recolour_if_settled();
        change_if_due();
    }
    restore_kit_state();
}

Milkdrop::Colouring Milkdrop::wanted_colouring() const
{
    Colouring out;
    if (!preferences_.tint || (!has_hue(accent_) && !has_hue(secondary_)))
        return out;
    out.accent = accent_;
    out.secondary = secondary_;
    out.strength = static_cast<float>(std::clamp(preferences_.tint_strength, 0, 100)) / 100.0f;
    return out;
}

void Milkdrop::set_album_colours(const Rgb &accent, const Rgb &secondary)
{
    if (accent == accent_ && secondary == secondary_)
        return;
    accent_ = accent;
    secondary_ = secondary;
    colouring_changed_at_ = sys::monotonic_us();
    cover_schedule_.touch(now_ms());
}

double Milkdrop::now_ms() const
{
    return static_cast<double>(sys::monotonic_us()) / 1000.0;
}

void Milkdrop::set_cover(std::vector<std::uint8_t> pixels)
{
    cover_ = std::move(pixels);
    cover_id_ = cover_.empty() ? 0 : ++cover_count_;
    want_cover();
}

void Milkdrop::want_cover()
{
    cover_schedule_.want(preferences_.cover_art && cover_id_ != 0 ? cover_id_ : 0, now_ms());
}

void Milkdrop::set_song(std::string_view key)
{
    if (key == song_)
        return;
    song_ = std::string(key);
    changer_.song(song_, now_ms());
}

void Milkdrop::listen(const float *stereo, std::size_t frames)
{
    changer_.hear(pcm_energy(stereo, frames), static_cast<double>(frames) / kListenRate);
}

// Each song and On big moments go through next(), so the preset list,
// favourites and skip-slow apply as they do for the timer.
void Milkdrop::change_if_due()
{
    if (handle_ == nullptr || preferences_.locked || selection_blocked_)
        return;
    if (changer_.due(preset_change_of(preferences_), now_ms()))
        next();
}

// Made the first time a preset asks for a picture, with whatever the
// picture is by then. GL_LINEAR without mipmaps: projectM's samplers never
// ask for them.
std::uint32_t Milkdrop::cover_texture()
{
    if (cover_texture_ != 0 || texture_pixels_.empty())
        return cover_texture_;
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kCoverSize, kCoverSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 texture_pixels_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound));
    cover_texture_ = texture;
    return cover_texture_;
}

// The fade runs on this (the frame) thread, in place in the one texture: the
// artwork worker has already cropped and scaled the cover (cover_pixels).
void Milkdrop::update_cover()
{
    if (texture_pixels_.empty())
        return;
    const double now = now_ms();
    if (const auto fade = cover_schedule_.tick(now))
    {
        fade_from_ = texture_pixels_;
        fade_to_ = fade->to != 0 && fade->to == cover_id_ ? cover_ : clouds_;
        fading_ = true;
        uploaded_at_ = 0;
    }
    if (!fading_)
        return;
    const float level = cover_schedule_.level(now);
    const auto at = sys::monotonic_us();
    if (level < 1 && at - uploaded_at_ < kCoverUploadUs)
        return;
    uploaded_at_ = at;
    blend_pixels(fade_from_, fade_to_, level, texture_pixels_);
    if (level >= 1)
        fading_ = false;
    if (cover_texture_ == 0)
        return;
    GLint bound = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glBindTexture(GL_TEXTURE_2D, cover_texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kCoverSize, kCoverSize, GL_RGBA, GL_UNSIGNED_BYTE,
                    texture_pixels_.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bound));
}

// Reloads the current preset with a soft cut, so the new colours blend in
// over the usual transition instead of snapping.
void Milkdrop::recolour_if_settled()
{
    if (handle_ == nullptr || name_.empty() || selection_blocked_ || wanted_colouring() == applied_)
        return;
    if (elapsed_ms(colouring_changed_at_) < kColourSettleSeconds * 1000 ||
        elapsed_ms(recoloured_at_) < kRecolourSeconds * 1000)
        return;
    recoloured_at_ = sys::monotonic_us();
    load(current_, true);
}

void Milkdrop::next()
{
    advance(true);
}

void Milkdrop::previous()
{
    advance(false);
}

bool Milkdrop::eligible(std::size_t index) const
{
    const auto &name = presets_[index].name;
    const bool favorite = preferences_.favorite(name);
    return quality_.eligible(name, preferences_.minimum_fps, preferences_.skip_slow && !favorite) &&
           (!preferences_.favorites_only || preferences_.favorites.empty() || favorite);
}

void Milkdrop::advance(bool forward)
{
    if (handle_ == nullptr || order_.empty())
        return;
    auto candidate = position_;
    for (std::size_t i = 0; i < order_.size(); ++i)
    {
        candidate = (candidate + (forward ? 1 : order_.size() - 1)) % order_.size();
        if (!eligible(order_[candidate]))
            continue;
        position_ = candidate;
        load(order_[position_], true);
        restore_kit_state();
        return;
    }
    selection_blocked_ = true;
    switch_pending_ = false;
    sys::log("[Afterglow] milkdrop no eligible presets; keeping current frame");
}

void Milkdrop::configure(const Preferences &preferences)
{
    if (preferences_.sequential != preferences.sequential && !order_.empty())
    {
        const auto current = order_[position_];
        std::sort(order_.begin(), order_.end());
        if (!preferences.sequential)
            std::shuffle(order_.begin(), order_.end(), random_);
        position_ = static_cast<std::size_t>(std::find(order_.begin(), order_.end(), current) -
                                             order_.begin());
    }
    if (preferences_.tint != preferences.tint ||
        preferences_.tint_strength != preferences.tint_strength)
        colouring_changed_at_ = sys::monotonic_us();
    const bool cover_changed = preferences_.cover_art != preferences.cover_art;
    preferences_ = preferences;
    if (cover_changed)
        want_cover();
    quality_ = PresetQuality::decode(preferences.quality);
    if (handle_ == nullptr)
        return;
    selection_blocked_ = std::none_of(order_.begin(), order_.end(),
                                      [this](std::size_t index) { return eligible(index); });
    if (name_.empty() && !selection_blocked_)
    {
        const auto first = std::find_if(order_.begin(), order_.end(),
                                        [this](std::size_t index) { return eligible(index); });
        position_ = static_cast<std::size_t>(first - order_.begin());
        load(*first, false);
        restore_kit_state();
    }
    projectm_set_beat_sensitivity(
        handle_, static_cast<float>(std::clamp(preferences.beat_sensitivity, 0, 200)) / 100.0f);
    // Each song and On big moments are decided here (change_if_due), not by
    // projectM's timer.
    projectm_set_preset_locked(handle_, preferences.locked || preferences.preset_change != 0);
    projectm_set_preset_duration(handle_, preferences.preset_seconds);
    projectm_set_fps(handle_, preferences.fps);
}

std::vector<std::string> Milkdrop::names() const
{
    std::vector<std::string> result;
    for (const auto &preset : presets_)
        result.push_back(preset.name);
    return result;
}

void Milkdrop::load(std::size_t index, bool smooth)
{
    selection_blocked_ = false;
    switch_pending_ = false;
    const Preset &preset = presets_[index];
    std::string data;
    if (!save::read_file(preset.path, &data, 1u << 20))
    {
        sys::log("[Afterglow] milkdrop preset unreadable file=%s", preset.path.c_str());
        ++failures_in_row_;
        switch_pending_ = true;
        // The preset still drawing keeps its name. With none loaded yet the
        // name marks the view ready, so render() runs and moves on to the next.
        if (name_.empty())
            name_ = preset.name;
        return;
    }
    name_ = preset.name;
    // A recolour reloads the same preset; only another one starts the gaps of
    // Each song and On big moments.
    if (index != current_)
        changer_.changed(now_ms());
    current_ = index;
    applied_ = wanted_colouring();
    if (applied_.strength > 0)
        data = recolour_preset(data, applied_.accent, applied_.secondary, applied_.strength);
    const auto started = sys::monotonic_us();
    projectm_load_preset_data(handle_, data.c_str(), smooth);
    loaded_at_ = sys::monotonic_us();
    timing_first_frame_ = true;
    if (!switch_pending_)
        failures_in_row_ = 0;
    sys::log("[Afterglow] milkdrop preset=\"%s\" load_ms=%.0f", name_.c_str(), elapsed_ms(started));
}

void Milkdrop::on_switch_requested(bool, void *self)
{
    auto &view = *static_cast<Milkdrop *>(self);
    view.switch_pending_ = true;
    view.failures_in_row_ = 0;
}

void Milkdrop::on_switch_failed(const char *file, const char *message, void *self)
{
    auto &view = *static_cast<Milkdrop *>(self);
    sys::log("[Afterglow] milkdrop preset failed file=%s reason=%s", file ? file : "?",
             message ? message : "?");
    ++view.failures_in_row_;
    view.switch_pending_ = true;
}

// Every name gets the one cover texture by id; projectM keeps it as a texture
// it does not own, so it never deletes it, and an update shows in every
// preset at once. Built-in textures (main, blur, noise) never reach here.
void Milkdrop::on_texture_requested(const char *name, projectm_texture_load_data *data, void *self)
{
    auto &view = *static_cast<Milkdrop *>(self);
    sys::log("[Afterglow] milkdrop texture=%s not shipped, using the cover texture",
             name ? name : "?");
    data->width = kCoverSize;
    data->height = kCoverSize;
    data->channels = 4;
    data->texture_id = view.cover_texture();
    if (data->texture_id == 0)
        data->data = view.clouds_.data();
}
} // namespace hui::afterglow
