// Afterglow - Minimal WebSocket client for Music Assistant's API and relay sockets.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::afterglow::ma_socket
{
constexpr std::size_t kMaxMessage = 2 * 1024 * 1024;
// Largest message sent: account preferences are the biggest (auth/user/update).
constexpr std::size_t kMaxOutgoing = 256 * 1024;

// One masked client frame (RFC 6455 5.2), with a 16- or 64-bit length as the
// size needs. Empty when bytes exceeds kMaxOutgoing.
std::vector<std::uint8_t> encode_frame(int opcode, const std::uint8_t *data, std::size_t bytes,
                                       const std::array<std::uint8_t, 4> &mask);

// One non-blocking client connection (numeric IPv4 only). Shared by the
// Companion's long-lived sockets and the short sign-in sessions so the
// console-specific socket fixes (SO_NBIO, no arc4random) live in one place.
class Socket
{
  public:
    explicit Socket(std::atomic<bool> &stop) : stopping_(stop)
    {
    }
    ~Socket()
    {
        close();
    }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    void close();
    bool open(const std::string &host, std::uint16_t port, const std::string &path);
    // Reads whatever has arrived without waiting; false when the connection is gone.
    bool tick();
    bool send_text(const std::string &text);
    std::function<bool(int, const std::uint8_t *, std::size_t)> handler;
    std::int64_t last_receive_ = 0;
    // The upgrade response's Date header ("" when none): Check my setup
    // compares the console's clock with it.
    std::string server_date;

  private:
    bool fail(const char *, int = 0, int = 0)
    {
        return false;
    }
    bool message(int opcode, const std::uint8_t *data, std::size_t size)
    {
        return handler ? handler(opcode, data, size) : true;
    }
    bool set_nonblocking();
    bool connect_socket();
    bool handshake();
    bool write_bytes(const std::uint8_t *, std::size_t);
    bool send_frame(int, const std::uint8_t *, std::size_t);
    bool read_frames();
    std::atomic<bool> &stopping_;
    std::string host_, path_;
    std::uint16_t port_ = 0;
    int socket_ = -1, fragment_opcode_ = 0;
    std::vector<std::uint8_t> incoming_, fragment_;
};
} // namespace hui::afterglow::ma_socket
