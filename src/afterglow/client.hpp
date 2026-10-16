// Afterglow - Native LAN Sendspin player, metadata and controller client.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
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
    std::string status = "Not connected", title, artist, album, group;
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
    void stop(); // Signal only; safe from the UI thread.
    void join(); // Call after stopping the audio output, during app teardown.
    void render(std::int16_t *output, int frames);
    Snapshot snapshot() const;
    void command(Command value);
    void set_output_enabled(bool enabled)
    {
        output_enabled_.store(enabled);
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
    AudioQueue audio_;
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
