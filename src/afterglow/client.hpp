// Afterglow - Native LAN Sendspin player, metadata and controller client.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/controls.hpp"
#include "afterglow/protocol.hpp"
#include <atomic>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

namespace hui::afterglow
{
enum class Command
{
    toggle,
    play,
    pause,
    next,
    previous,
    volume_up,
    volume_down
};
struct Snapshot
{
    bool connected = false, clock_synced = false, playing = false, muted = false;
    int volume = 80;
    double buffered_ms = 0, clock_rtt_ms = 0;
    std::uint64_t underruns = 0, late_frames = 0, reconnects = 0, received_packets = 0,
                  overflows = 0;
    // The current track's length and how far in it is, from the server; 0 if it does not say.
    std::int64_t track_ms = 0, elapsed_ms = 0;
    std::string status = "Not connected", title, artist, album, group;
};

struct OutputState
{
    std::uint64_t epoch = 1;
    // Active includes clock synchronization; muted includes local output disable.
    bool active = false, muted = false, stopping = true;
};
struct RenderStamp
{
    OutputState state;
    std::int64_t presentation_local_us = -1;
};

class Client
{
  public:
    Client() = default;
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    // Numeric IPv4 only: avoids unbounded DNS and keeps teardown predictable.
    bool start(const char *host, std::uint16_t port, const char *client_id, const char *name);
    void stop(); // Invalidate output and signal shutdown; safe from the UI thread.
    void join(); // Call after stopping the audio output, during app teardown.
    void render(std::int16_t *output, int frames);
    // One audio consumer owns render/render_at. State changes and each bounded
    // render share a short-held mutex, independent of network IO and metadata.
    // The stamp describes the PCM and state atomically at render time. An
    // external feeder must check the epoch again before submitting, then flush
    // its own FIFO on changes; this API cannot recall already submitted audio.
    // Intended time is when the sink will present frame zero, on the same
    // monotonic clock as sys::monotonic_us(). Server offset/static delay are
    // applied here. Valid calls require 1..1024 frames and a nonnegative time.
    // Invalid arguments leave output untouched and return an inactive stamp
    // with presentation_local_us=-1. Inactive valid calls render silence and
    // acknowledge queue clears. Mute consumes on schedule without clearing.
    RenderStamp render_at(std::int16_t *output, int frames, std::int64_t intended_local_us);
    OutputState output_state() const;
    Snapshot snapshot() const;
    void command(Command value);
    void set_output_enabled(bool enabled);
    // What this speaker tells the server its volume is when it connects. Call before start().
    void set_start_volume(int volume)
    {
        volume_ = volume;
    }

  private:
    static void *entry(void *self);
    void run();
    bool connect_socket();
    bool set_nonblocking();
    bool handshake();
    bool send_frame(int opcode, const std::uint8_t *data, std::size_t bytes);
    bool send_text(const std::string &text);
    bool write_bytes(const std::uint8_t *data, std::size_t size);
    bool read_frames();
    bool message(int opcode, const std::uint8_t *data, std::size_t size);
    bool text_message(std::string_view text);
    bool send_state();
    void set_status(const char *value);
    bool fail(const char *stage, int error = 0, int detail = 0);
    OutputState output_state_locked() const;
    RenderStamp render_at_locked(std::int16_t *output, int frames, std::int64_t intended_local_us);
    AudioQueue audio_;
    // Never acquire mutex_ or perform socket IO while holding output_mutex_.
    mutable std::mutex output_mutex_;
    std::uint64_t output_epoch_ = 1;
    mutable std::mutex mutex_;
    Snapshot state_;
    std::string host_, id_, name_;
    std::uint16_t port_ = 0;
    pthread_t thread_{};
    bool thread_started_ = false;
    std::atomic<bool> stopping_{true}, active_{false}, synchronized_{false};
    std::atomic<int> volume_{80}, delay_ms_{0};
    std::atomic<bool> muted_{false};
    std::atomic<bool> output_enabled_{true};
    std::atomic<std::int64_t> clock_offset_{0};
    std::array<Command, 32> commands_{};
    std::size_t command_read_ = 0, command_write_ = 0;
    // The volume last sent (mutex_).
    Assumed volume_sent_;
    int socket_ = -1;
    const char *failure_stage_ = "connection";
    int failure_errno_ = 0;
    bool server_hello_ = false, format_ok_ = false, format_error_ = false;
    std::int64_t last_receive_ = 0, time_sent_ = 0, next_time_ = 0, next_state_ = 0;
    std::int64_t best_rtt_ = 10000000, best_at_ = 0;
    std::vector<std::uint8_t> incoming_, fragment_;
    int fragment_opcode_ = 0;
};
} // namespace hui::afterglow
