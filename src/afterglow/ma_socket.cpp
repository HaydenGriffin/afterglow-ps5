// Afterglow - Minimal WebSocket client for Music Assistant's API and relay sockets.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/ma_socket.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hui::afterglow::ma_socket
{
namespace
{
// arc4random_buf links on PS5 but only libScePosixForWebKit exports it, which
// is a null import in a native title (it crashed on hardware). WebSocket keys
// and masks need variety, not secrecy, so splitmix64 over the clock is enough.
void fill_random(std::uint8_t *data, std::size_t size)
{
    static std::atomic<std::uint64_t> counter{0};
    for (std::size_t i = 0; i < size; i += 8)
    {
        std::uint64_t z = counter.fetch_add(0x9e3779b97f4a7c15ULL) +
                          static_cast<std::uint64_t>(sys::monotonic_us());
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        for (std::size_t j = 0; j < 8 && i + j < size; ++j)
            data[i + j] = static_cast<std::uint8_t>(z >> (j * 8));
    }
}
std::string base64(const std::uint8_t *data, std::size_t size)
{
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (std::size_t i = 0; i < size; i += 3)
    {
        const auto word = (static_cast<std::uint32_t>(data[i]) << 16) |
                          (i + 1 < size ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0U) |
                          (i + 2 < size ? static_cast<std::uint32_t>(data[i + 2]) : 0U);
        result += alphabet[(word >> 18) & 63];
        result += alphabet[(word >> 12) & 63];
        result += i + 1 < size ? alphabet[(word >> 6) & 63] : '=';
        result += i + 2 < size ? alphabet[word & 63] : '=';
    }
    return result;
}
// SHA-1 is required by the RFC6455 handshake, not used for authentication.
std::string websocket_accept(const std::string &key)
{
    const std::string input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const std::uint64_t bits = bytes.size() * 8;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56)
        bytes.push_back(0);
    for (int n = 7; n >= 0; --n)
        bytes.push_back(static_cast<std::uint8_t>(bits >> (n * 8)));
    std::array<std::uint32_t, 5> hash{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    auto rotate = [](std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (std::size_t block = 0; block < bytes.size(); block += 64)
    {
        std::array<std::uint32_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i)
            for (std::size_t j = 0; j < 4; ++j)
                w[i] = (w[i] << 8) | bytes[block + i * 4 + j];
        for (std::size_t i = 16; i < 80; ++i)
            w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        auto a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4];
        for (std::size_t i = 0; i < 80; ++i)
        {
            std::uint32_t f = 0, k = 0;
            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5a827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            const auto t = rotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotate(b, 30);
            b = a;
            a = t;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
    }
    std::array<std::uint8_t, 20> digest{};
    for (std::size_t i = 0; i < 20; ++i)
        digest[i] = static_cast<std::uint8_t>(hash[i / 4] >> (24 - (i % 4) * 8));
    return base64(digest.data(), digest.size());
}
} // namespace

void Socket::close()
{
    if (socket_ >= 0)
        ::close(socket_);
    socket_ = -1;
    incoming_.clear();
    fragment_.clear();
    fragment_opcode_ = 0;
}
bool Socket::open(const std::string &host, std::uint16_t port, const std::string &path)
{
    close();
    host_ = host;
    port_ = port;
    path_ = path;
    if (connect_socket() && handshake())
    {
        last_receive_ = sys::monotonic_us();
        return true;
    }
    close();
    return false;
}
bool Socket::tick()
{
    if (socket_ < 0)
        return false;
    pollfd p{socket_, POLLIN, 0};
    int n = poll(&p, 1, 0);
    if (n < 0)
        return errno == EINTR;
    if (n && (p.revents & POLLIN))
        return read_frames();
    return !(p.revents & (POLLERR | POLLHUP | POLLNVAL));
}
#ifdef __PROSPERO__
// PS5 titles refuse both F_SETFL and FIONBIO with EACCES (seen on hardware).
// The console's own SO_NBIO option is the switch sandboxed titles may use;
// the read-back is logged so console evidence shows the mode really took.
bool Socket::set_nonblocking()
{
    constexpr int kConsoleSoNbio = 0x1200;
    const int enabled = 1;
    const int changed = setsockopt(socket_, SOL_SOCKET, kConsoleSoNbio, &enabled, sizeof(enabled));
    if (changed != 0)
        return fail("so-nbio-set", errno, changed);
    int readback = -1;
    socklen_t size = sizeof(readback);
    const int read = getsockopt(socket_, SOL_SOCKET, kConsoleSoNbio, &readback, &size);
    const int read_errno = read != 0 ? errno : 0;
    sys::log("[Afterglow] net nonblocking via=so-nbio get=%d errno=%d value=%d", read, read_errno,
             readback);
    if (read == 0 && readback == 0)
        return fail("so-nbio-readback", 0, readback);
    return true;
}
#else
// Hosts use the BSD ioctl, which keeps working when a test shim denies
// F_SETFL the way the console does.
bool Socket::set_nonblocking()
{
    int enabled = 1;
    const int changed = ioctl(socket_, FIONBIO, &enabled);
    if (changed != 0)
        return fail("ioctl-nonblock", errno, changed);
    return true;
}
#endif

bool Socket::connect_socket()
{
    socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0)
        return fail("socket", errno, socket_);
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    setsockopt(socket_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
    if (!set_nonblocking())
        return false;
    sockaddr_in address{};
#if defined(__FreeBSD__) || defined(__APPLE__)
    address.sin_len = sizeof(address);
#endif
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1)
        return fail("address");
    const int connected =
        ::connect(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (connected == 0)
        return true;
    const int connect_error = errno;
    if (connect_error != EINPROGRESS)
        return fail("connect", connect_error, connected);
    const auto deadline = sys::monotonic_us() + 3000000;
    while (!stopping_ && sys::monotonic_us() < deadline)
    {
        pollfd p{socket_, POLLOUT, 0};
        const int ready = poll(&p, 1, 100);
        if (ready < 0 && errno != EINTR)
            return fail("connect-poll", errno, ready);
        if (ready > 0)
        {
            int error = 0;
            socklen_t size = sizeof(error);
            const int result = getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &size);
            if (result != 0)
                return fail("connect-so-error", errno, result);
            if (error != 0)
                return fail("connect-completion", error, p.revents);
            if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
                return fail("connect-events", 0, p.revents);
            return true;
        }
    }
    return fail("connect-timeout", ETIMEDOUT);
}

bool Socket::write_bytes(const std::uint8_t *data, std::size_t size)
{
    const auto deadline = sys::monotonic_us() + 2000000;
    std::size_t position = 0;
    while (position < size && !stopping_ && sys::monotonic_us() < deadline)
    {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto count = send(socket_, data + position, size - position, flags);
        if (count > 0)
            position += static_cast<std::size_t>(count);
        else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        {
            pollfd p{socket_, POLLOUT, 0};
            poll(&p, 1, 100);
        }
        else
            return fail("send", count < 0 ? errno : 0, static_cast<int>(count));
    }
    return position == size || fail("send-timeout", ETIMEDOUT, static_cast<int>(position));
}

bool Socket::handshake()
{
    std::array<std::uint8_t, 16> nonce{};
    fill_random(nonce.data(), nonce.size());
    const auto key = base64(nonce.data(), nonce.size());
    const auto request =
        "GET " + path_ + " HTTP/1.1\r\nHost: " + host_ + ":" + std::to_string(port_) +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
        "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!write_bytes(reinterpret_cast<const std::uint8_t *>(request.data()), request.size()))
        return false;
    std::string response;
    const auto deadline = sys::monotonic_us() + 3000000;
    while (!stopping_ && sys::monotonic_us() < deadline && response.size() < 8192)
    {
        pollfd p{socket_, POLLIN, 0};
        const int ready = poll(&p, 1, 100);
        if (ready < 0 && errno != EINTR)
            return fail("upgrade-poll", errno, ready);
        if (ready <= 0)
            continue;
        // Read only the HTTP header; coalesced WebSocket bytes remain in the socket.
        char c = 0;
        const auto count = recv(socket_, &c, 1, 0);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        if (count <= 0)
            return fail("upgrade-recv", count < 0 ? errno : 0, static_cast<int>(count));
        response += c;
        if (response.ends_with("\r\n\r\n"))
            break;
    }
    if (!response.starts_with("HTTP/1.1 101 ") || !response.ends_with("\r\n\r\n"))
    {
        const auto line_end = response.find("\r\n");
        const auto first_line = response.substr(0, std::min<std::size_t>(line_end, 120));
        sys::log("[Afterglow] net HTTP response=%s bytes=%zu", first_line.c_str(), response.size());
        return fail("upgrade-response", response.empty() ? ETIMEDOUT : 0);
    }
    bool upgrade = false, connection = false, accept = false;
    std::size_t start = response.find("\r\n") + 2;
    while (start < response.size())
    {
        const auto end = response.find("\r\n", start);
        const auto colon = response.find(':', start);
        if (end == std::string::npos)
            break;
        if (colon < end)
        {
            std::string name = response.substr(start, colon - start);
            std::string value = response.substr(colon + 1, end - colon - 1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.erase(0, 1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            for (char &v : name)
                v = static_cast<char>(std::tolower(static_cast<unsigned char>(v)));
            if (name == "sec-websocket-accept")
                accept = value == websocket_accept(key);
            for (char &v : value)
                v = static_cast<char>(std::tolower(static_cast<unsigned char>(v)));
            if (name == "upgrade")
                upgrade = value == "websocket";
            if (name == "connection")
                connection = value.find("upgrade") != std::string::npos;
        }
        start = end + 2;
    }
    if (!upgrade || !connection || !accept)
    {
        sys::log("[Afterglow] net upgrade flags websocket=%d connection=%d accept=%d", upgrade,
                 connection, accept);
        return fail("upgrade-validation");
    }
    return true;
}

std::vector<std::uint8_t> encode_frame(int opcode, const std::uint8_t *data, std::size_t bytes,
                                       const std::array<std::uint8_t, 4> &mask)
{
    std::vector<std::uint8_t> frame;
    if (bytes > kMaxOutgoing)
        return frame;
    frame.reserve(bytes + 14);
    frame.push_back(static_cast<std::uint8_t>(0x80 | opcode));
    if (bytes < 126)
        frame.push_back(static_cast<std::uint8_t>(0x80 | bytes));
    else if (bytes <= 65535)
    {
        frame.push_back(0xfe);
        frame.push_back(static_cast<std::uint8_t>(bytes >> 8));
        frame.push_back(static_cast<std::uint8_t>(bytes));
    }
    else
    {
        frame.push_back(0xff);
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(bytes) >> shift));
    }
    frame.insert(frame.end(), mask.begin(), mask.end());
    for (std::size_t i = 0; i < bytes; ++i)
        frame.push_back(data[i] ^ mask[i % 4]);
    return frame;
}

bool Socket::send_frame(int opcode, const std::uint8_t *data, std::size_t bytes)
{
    std::array<std::uint8_t, 4> mask{};
    fill_random(mask.data(), mask.size());
    const auto frame = encode_frame(opcode, data, bytes, mask);
    if (frame.empty())
        return fail("outgoing-frame-too-large");
    return write_bytes(frame.data(), frame.size());
}
bool Socket::send_text(const std::string &text)
{
    return send_frame(1, reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}

bool Socket::read_frames()
{
    std::array<std::uint8_t, 32768> buffer{};
    const auto count = recv(socket_, buffer.data(), buffer.size(), 0);
    if (count <= 0)
    {
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return true;
        return fail("receive", count < 0 ? errno : 0, static_cast<int>(count));
    }
    last_receive_ = sys::monotonic_us();
    incoming_.insert(incoming_.end(), buffer.begin(), buffer.begin() + count);
    if (incoming_.size() > kMaxMessage + 32768)
        return fail("receive-buffer-limit");
    std::size_t position = 0;
    while (incoming_.size() - position >= 2)
    {
        const auto *frame = incoming_.data() + position;
        const bool fin = (frame[0] & 0x80) != 0;
        const int opcode = frame[0] & 15;
        if ((frame[0] & 0x70) != 0 || (frame[1] & 0x80) != 0)
            return fail("frame-flags", 0, frame[0] * 256 + frame[1]);
        std::uint64_t length = frame[1] & 127;
        std::size_t header = 2;
        if (length == 126)
        {
            if (incoming_.size() - position < 4)
                break;
            length = (static_cast<std::uint64_t>(frame[2]) << 8) | frame[3];
            header = 4;
            if (length < 126)
                return fail("frame-length16");
        }
        else if (length == 127)
        {
            if (incoming_.size() - position < 10)
                break;
            length = 0;
            for (int i = 2; i < 10; ++i)
                length = (length << 8) | frame[i];
            header = 10;
            if (length < 65536)
                return fail("frame-length64");
        }
        if (length > kMaxMessage || (opcode >= 8 && (!fin || length > 125)))
            return fail("frame-limit", 0, opcode);
        if (incoming_.size() - position < header + length)
            break;
        const auto *payload = frame + header;
        const auto size = static_cast<std::size_t>(length);
        if (opcode == 8)
        {
            const int code = size >= 2 ? payload[0] * 256 + payload[1] : 1005;
            sys::log("[Afterglow] companion websocket closed code=%d", code);
            send_frame(8, payload, size);
            return fail("server-close", 0, code);
        }
        if (opcode == 9)
        {
            if (!send_frame(10, payload, size))
                return false;
        }
        else if (opcode == 10)
        { /* Pong confirms the connection is alive. */
        }
        else if (opcode == 0)
        {
            if (fragment_opcode_ == 0 || fragment_.size() + size > kMaxMessage)
                return fail("frame-continuation");
            fragment_.insert(fragment_.end(), payload, payload + size);
            if (fin)
            {
                if (!message(fragment_opcode_, fragment_.data(), fragment_.size()))
                    return false;
                fragment_.clear();
                fragment_opcode_ = 0;
            }
        }
        else if (opcode == 1 || opcode == 2)
        {
            if (fragment_opcode_ != 0)
                return fail("frame-interleaved-data");
            if (fin)
            {
                if (!message(opcode, payload, size))
                    return false;
            }
            else
            {
                fragment_opcode_ = opcode;
                fragment_.assign(payload, payload + size);
            }
        }
        else
            return fail("frame-opcode", 0, opcode);
        position += header + size;
    }
    if (position)
        incoming_.erase(incoming_.begin(),
                        incoming_.begin() + static_cast<std::ptrdiff_t>(position));
    return true;
}
} // namespace hui::afterglow::ma_socket
