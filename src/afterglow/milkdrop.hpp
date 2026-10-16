// Afterglow - MilkDrop presets through projectM, rendered into a kit canvas.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/canvas.hpp"
#include "afterglow/cover_texture.hpp"
#include "afterglow/preferences.hpp"
#include "afterglow/preset_change.hpp"
#include "afterglow/preset_colour.hpp"
#include "afterglow/preset_quality.hpp"
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

struct projectm;
struct projectm_texture_load_data;

namespace hui::afterglow
{
// Owns one projectM instance. projectM draws into its own canvas before the
// kit composes the frame, and the Afterglow screen shows canvas texture() as
// an image, so the kit stays the only thing that draws to the display.
// Needs the GL context current for every call; never used by unit tests.
class Milkdrop
{
  public:
    Milkdrop() = default;
    Milkdrop(const Milkdrop &) = delete;
    Milkdrop &operator=(const Milkdrop &) = delete;
    ~Milkdrop();

    // Reads preset_dir/index.tsv ("file<TAB>name" per line; /app0 cannot be
    // listed on the console) and loads the first preset of a shuffled order.
    bool init(int width, int height, const std::string &preset_dir);
    void release();

    // Interleaved stereo floats of the audio being heard, oldest first.
    void feed(const float *stereo, std::size_t frames);
    // Renders one frame into the canvas, then resets the GL state the kit expects.
    void render(double frame_time = -1.0);
    // Hard-cut selection in index order for offline validation.
    bool select(std::size_t index);

    void next();
    void previous();
    void configure(const Preferences &preferences);
    // The artwork's two colours; with album colour on, the current preset is
    // reloaded recoloured once they have settled (see render()).
    void set_album_colours(const Rgb &accent, const Rgb &secondary);
    std::vector<std::string> names() const;
    // The song's cover as cover_pixels() made it (empty: none): the picture
    // presets sample while "Cover art in the visuals" is on.
    void set_cover(std::vector<std::uint8_t> pixels);
    // The song playing (song_key; empty while nothing plays), for Each song.
    void set_song(std::string_view key);
    // The audio before Auto level, for On big moments.
    void listen(const float *stereo, std::size_t frames);
    void set_scanning(bool value)
    {
        scanning_ = value;
    }
    bool failed() const
    {
        return failures_in_row_ > 0;
    }

    bool ready() const
    {
        return handle_ != nullptr && !name_.empty();
    }
    std::uint32_t texture() const
    {
        return canvas_.texture();
    }
    const std::string &preset_name() const
    {
        return name_;
    }
    std::size_t preset_count() const
    {
        return presets_.size();
    }

  private:
    struct Preset
    {
        std::string path;
        std::string name;
    };
    static void on_switch_requested(bool hard_cut, void *self);
    static void on_switch_failed(const char *file, const char *message, void *self);
    static void on_texture_requested(const char *name, projectm_texture_load_data *data,
                                     void *self);
    void load(std::size_t index, bool smooth);
    bool eligible(std::size_t index) const;
    void advance(bool forward);
    // The colouring a load would apply now; all zero strength when off.
    struct Colouring
    {
        Rgb accent{}, secondary{};
        float strength = 0;
        bool operator==(const Colouring &) const = default;
    };
    Colouring wanted_colouring() const;
    void recolour_if_settled();
    void want_cover();
    void update_cover();
    std::uint32_t cover_texture();
    double now_ms() const;
    void change_if_due();

    projectm *handle_ = nullptr;
    gfx::Canvas canvas_;
    std::vector<Preset> presets_;
    std::vector<std::size_t> order_;
    std::size_t position_ = 0;
    std::string name_;
    std::mt19937 random_;
    bool switch_pending_ = false;
    int failures_in_row_ = 0;
    std::int64_t loaded_at_ = 0;
    bool timing_first_frame_ = false;
    Preferences preferences_;
    PresetQuality quality_;
    bool scanning_ = false;
    bool selection_blocked_ = false;
    std::size_t current_ = 0;
    Rgb accent_{}, secondary_{};
    // What the current preset was loaded with, when the wish last changed and
    // when a recolour last reloaded the preset.
    Colouring applied_;
    std::int64_t colouring_changed_at_ = 0, recoloured_at_ = 0;
    // Every texture a preset names that is not shipped is this one texture,
    // owned here and handed to projectM by id (projectM does not delete it):
    // the song's cover, or the generated clouds, faded from one to the other.
    std::uint32_t cover_texture_ = 0;
    std::vector<std::uint8_t> clouds_, cover_, fade_from_, fade_to_, texture_pixels_;
    std::uint64_t cover_id_ = 0, cover_count_ = 0;
    CoverSchedule cover_schedule_;
    bool fading_ = false;
    std::int64_t uploaded_at_ = 0;
    PresetChanger changer_;
    std::string song_;
};
} // namespace hui::afterglow
