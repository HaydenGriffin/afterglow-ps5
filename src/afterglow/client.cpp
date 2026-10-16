// Afterglow - Native LAN Sendspin player, metadata and controller client.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/client.hpp"
#include "afterglow/version.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hui::afterglow
{
namespace
{
constexpr std::size_t kMaxMessage = kBufferBytes + 9;
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

Client::~Client()
{
    stop();
    join();
}
bool Client::start(const char *host, std::uint16_t port, const char *client_id, const char *name)
{
    if (thread_started_ || !host || !client_id || !name || !port)
        return false;
    in_addr address{};
    if (inet_pton(AF_INET, host, &address) != 1)
    {
        set_status("Use a numeric IPv4 Sendspin host");
        return false;
    }
    host_ = host;
    port_ = port;
    id_ = client_id;
    name_ = name;
    stopping_ = false;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
    {
        stopping_ = true;
        return false;
    }
    const int stack_result = pthread_attr_setstacksize(&attributes, 1024 * 1024);
    const int result =
        stack_result == 0 ? pthread_create(&thread_, &attributes, entry, this) : stack_result;
    pthread_attr_destroy(&attributes);
    thread_started_ = result == 0;
    if (!thread_started_)
    {
        stopping_ = true;
        set_status("Network thread could not start");
    }
    return thread_started_;
}
void Client::stop()
{
    stopping_ = true;
}
void Client::join()
{
    if (thread_started_)
    {
        pthread_join(thread_, nullptr);
        thread_started_ = false;
    }
}
void *Client::entry(void *self)
{
    static_cast<Client *>(self)->run();
    return nullptr;
}
void Client::set_status(const char *value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.status = value;
}
bool Client::fail(const char *stage, int error, int detail)
{
    failure_stage_ = stage;
    failure_errno_ = error;
    if (!stopping_)
        sys::log("[Afterglow] net failure stage=%s errno=%d (%s) detail=%d/0x%08x", stage, error,
                 error ? std::strerror(error) : "none", detail, static_cast<unsigned int>(detail));
    return false;
}
Snapshot Client::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot result = state_;
    result.clock_synced = synchronized_.load();
    result.volume = volume_.load();
    result.muted = muted_.load();
    result.buffered_ms = audio_.buffered_ms();
    result.underruns = audio_.underruns();
    result.late_frames = audio_.late_frames();
    result.overflows = audio_.overflows();
    return result;
}
void Client::render(std::int16_t *output, int frames)
{
    audio_.render(output, frames,
                  sys::monotonic_us() + clock_offset_.load() +
                      static_cast<std::int64_t>(delay_ms_.load()) * 1000,
                  volume_.load(), muted_.load() || !output_enabled_.load(),
                  active_.load() && synchronized_.load());
}
void Client::command(Command value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.connected)
        return;
    // A skip already waiting stands for this press too (LG 1.5.1).
    if (value == Command::next || value == Command::previous)
        for (auto i = command_read_; i != command_write_; i = (i + 1) % commands_.size())
            if (commands_[i] == value)
                return;
    const auto next = (command_write_ + 1) % commands_.size();
    if (next == command_read_)
        return;
    commands_[command_write_] = value;
    command_write_ = next;
}

#ifdef __PROSPERO__
// PS5 titles refuse both F_SETFL and FIONBIO with EACCES (seen on hardware).
// The console's own SO_NBIO option is the switch sandboxed titles may use;
// the read-back is logged so console evidence shows the mode really took.
bool Client::set_nonblocking()
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
bool Client::set_nonblocking()
{
    int enabled = 1;
    const int changed = ioctl(socket_, FIONBIO, &enabled);
    if (changed != 0)
        return fail("ioctl-nonblock", errno, changed);
    return true;
}
#endif

bool Client::connect_socket()
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

bool Client::write_bytes(const std::uint8_t *data, std::size_t size)
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

bool Client::handshake()
{
    std::array<std::uint8_t, 16> nonce{};
    fill_random(nonce.data(), nonce.size());
    const auto key = base64(nonce.data(), nonce.size());
    const auto request =
        "GET /sendspin HTTP/1.1\r\nHost: " + host_ + ":" + std::to_string(port_) +
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

bool Client::send_frame(int opcode, const std::uint8_t *data, std::size_t bytes)
{
    if (bytes > 65535)
        return fail("outgoing-frame-too-large");
    std::vector<std::uint8_t> frame;
    frame.reserve(bytes + 8);
    frame.push_back(static_cast<std::uint8_t>(0x80 | opcode));
    if (bytes < 126)
        frame.push_back(static_cast<std::uint8_t>(0x80 | bytes));
    else
    {
        frame.push_back(0xfe);
        frame.push_back(static_cast<std::uint8_t>(bytes >> 8));
        frame.push_back(static_cast<std::uint8_t>(bytes));
    }
    std::array<std::uint8_t, 4> mask{};
    fill_random(mask.data(), mask.size());
    frame.insert(frame.end(), mask.begin(), mask.end());
    for (std::size_t i = 0; i < bytes; ++i)
        frame.push_back(data[i] ^ mask[i % 4]);
    return write_bytes(frame.data(), frame.size());
}
bool Client::send_text(const std::string &text)
{
    return send_frame(1, reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}

bool Client::read_frames()
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
            std::string reason;
            for (std::size_t i = 2; i < size && reason.size() < 96; ++i)
                reason +=
                    payload[i] >= 32 && payload[i] < 127 ? static_cast<char>(payload[i]) : '.';
            sys::log("[Afterglow] net server close code=%d reason=%s", code, reason.c_str());
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

bool Client::message(int opcode, const std::uint8_t *data, std::size_t size)
{
    if (opcode == 1)
    {
        if (size > 65536)
            return fail("json-size");
        return text_message(std::string_view(reinterpret_cast<const char *>(data), size));
    }
    if (format_ok_ && size && data[0] == 4)
    {
        // A seek can arrive with a full old queue. Give the audio thread one
        // callback to reclaim those slots before admitting the new generation.
        const auto deadline = sys::monotonic_us() + 200000;
        while (audio_.clear_pending() && !stopping_ && sys::monotonic_us() < deadline)
            poll(nullptr, 0, 1);
        if (stopping_)
            return false;
        if (audio_.push(data, size))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++state_.received_packets;
        }
    }
    return true;
}

bool Client::send_state()
{
    if (!server_hello_)
        return true;
    return send_text("{\"type\":\"client/state\",\"payload\":{\"player\":{\"state\":\"" +
                     std::string(format_error_ ? "error" : "synchronized") + "\",\"volume\":" +
                     std::to_string(volume_.load()) + ",\"muted\":" + (muted_ ? "true" : "false") +
                     ",\"static_delay_ms\":" + std::to_string(delay_ms_.load()) +
                     ",\"required_lead_time_ms\":500,\"min_buffer_ms\":500,\"supported_commands\":["
                     "\"set_static_delay\"]}}}");
}

bool Client::text_message(std::string_view text)
{
    Json json(text);
    if (!json.valid())
        return fail("json-parse", 0, static_cast<int>(text.size()));
    const auto type = json.string(json.get(0, "type"));
    const int payload = json.get(0, "payload");
    if (type == "server/hello")
    {
        server_hello_ = true;
        sys::log("[Afterglow] net received server/hello");
        set_status("Connected — select Afterglow in Music Assistant");
        return send_state();
    }
    if (type == "server/time")
    {
        const auto t1 = json.integer(json.get(payload, "client_transmitted"), -1);
        const auto t2 = json.integer(json.get(payload, "server_received"), -1);
        const auto t3 = json.integer(json.get(payload, "server_transmitted"), -1);
        const auto t4 = sys::monotonic_us();
        if (t1 != time_sent_ || t2 < 0 || t3 < t2 || t4 < t1 || t4 - t1 > 2000000 ||
            t2 > INT64_MAX / 4 || t3 > INT64_MAX / 4)
            return true;
        const auto rtt = std::max<std::int64_t>(0, (t4 - t1) - (t3 - t2));
        if (!synchronized_ || rtt <= best_rtt_ + 1000 || t4 - best_at_ > 30000000)
        {
            const auto offset = ((t2 - t1) + (t3 - t4)) / 2;
            const auto old = clock_offset_.load();
            clock_offset_ = synchronized_ ? old + (offset - old) / 8 : offset;
            synchronized_ = true;
            best_rtt_ = rtt;
            best_at_ = t4;
            std::lock_guard<std::mutex> lock(mutex_);
            state_.clock_rtt_ms = static_cast<double>(rtt) / 1000.0;
        }
        time_sent_ = 0;
    }
    else if (type == "stream/start")
    {
        const int player = json.get(payload, "player");
        format_ok_ = json.string(json.get(player, "codec")) == "pcm" &&
                     json.integer(json.get(player, "sample_rate")) == kSampleRate &&
                     json.integer(json.get(player, "channels")) == 2 &&
                     json.integer(json.get(player, "bit_depth")) == 16;
        audio_.clear();
        format_error_ = !format_ok_;
        active_ = format_ok_;
        std::lock_guard<std::mutex> lock(mutex_);
        state_.playing = format_ok_;
        state_.status = format_ok_ ? "Streaming PCM · 48 kHz" : "Unsupported audio format";
    }
    else if (type == "stream/clear" || type == "stream/end")
    {
        const int roles = json.get(payload, "roles");
        if (roles < 0 || json.has_string(roles, "player"))
        {
            audio_.clear();
            if (type == "stream/end")
            {
                active_ = false;
                format_ok_ = false;
                format_error_ = false;
                std::lock_guard<std::mutex> lock(mutex_);
                state_.playing = false;
                state_.status = "Ready for music";
            }
        }
    }
    else if (type == "server/command")
    {
        const int player = json.get(payload, "player");
        const auto command = json.string(json.get(player, "command"));
        if (command == "volume")
            volume_ = static_cast<int>(std::clamp<std::int64_t>(
                json.integer(json.get(player, "volume"), volume_), 0, 100));
        else if (command == "mute")
            muted_ = json.boolean(json.get(player, "mute"), muted_);
        else if (command == "set_static_delay")
            delay_ms_ = static_cast<int>(std::clamp<std::int64_t>(
                json.integer(json.get(player, "static_delay_ms"), delay_ms_), 0, 5000));
        return send_state();
    }
    else if (type == "server/state")
    {
        const int metadata = json.get(payload, "metadata");
        std::lock_guard<std::mutex> lock(mutex_);
        const int title = json.get(metadata, "title"), artist = json.get(metadata, "artist"),
                  album = json.get(metadata, "album");
        if (title >= 0)
            state_.title = json.string(title);
        if (artist >= 0)
            state_.artist = json.string(artist);
        if (album >= 0)
            state_.album = json.string(album);
    }
    else if (type == "group/update")
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int group = json.get(payload, "group_name"),
                  playing = json.get(payload, "playback_state");
        if (group >= 0)
            state_.group = json.string(group);
        if (playing >= 0)
            state_.playing = json.string(playing) == "playing";
    }
    return true;
}

void Client::run()
{
    int retry_ms = 1000;
    unsigned int attempt = 0;
    while (!stopping_)
    {
        failure_stage_ = "connection";
        failure_errno_ = 0;
        sys::log("[Afterglow] net attempt=%u endpoint=%s:%u", ++attempt, host_.c_str(),
                 static_cast<unsigned int>(port_));
        set_status("Connecting to Music Assistant…");
        server_hello_ = false;
        format_ok_ = false;
        format_error_ = false;
        synchronized_ = false;
        active_ = false;
        incoming_.clear();
        fragment_.clear();
        fragment_opcode_ = 0;
        next_time_ = 0;
        next_state_ = 0;
        time_sent_ = 0;
        best_rtt_ = 10000000;
        bool connected = connect_socket();
        if (connected)
        {
            sys::log("[Afterglow] net TCP connected fd=%d", socket_);
            connected = handshake();
        }
        if (connected)
        {
            sys::log("[Afterglow] net websocket upgraded; sending client/hello");
            connected = send_text(
                "{\"type\":\"client/hello\",\"payload\":{\"client_id\":" + json_quote(id_) +
                ",\"name\":" + json_quote(name_) +
                ",\"version\":1,\"supported_roles\":[\"player@v1\",\"controller@v1\",\"metadata@"
                "v1\"],"
                "\"device_info\":{\"product_name\":\"Afterglow "
                "PS5\",\"software_version\":\"" +
                std::string(kVersion) +
                "\"},"
                "\"player@v1_support\":{\"supported_formats\":[{\"codec\":\"pcm\",\"channels\":2,"
                "\"sample_rate\":48000,\"bit_depth\":16}],\"buffer_capacity\":" +
                std::to_string(kBufferBytes) + ",\"supported_commands\":[\"volume\",\"mute\"]}}}");
        }
        if (connected)
        {
            sys::log("[Afterglow] net client/hello sent");
            std::lock_guard<std::mutex> lock(mutex_);
            state_.connected = true;
            state_.status = "Waiting for Sendspin handshake";
            retry_ms = 1000;
        }
        last_receive_ = sys::monotonic_us();
        auto ping_at = last_receive_ + 10000000;
        const auto connected_at = last_receive_;
        while (connected && !stopping_)
        {
            const auto now = sys::monotonic_us();
            if (now - last_receive_ > 30000000 || (!server_hello_ && now - connected_at > 10000000))
            {
                fail(server_hello_ ? "server-idle-timeout" : "server-hello-timeout", ETIMEDOUT);
                break;
            }
            if (server_hello_ && now >= next_time_ && (!time_sent_ || now - time_sent_ >= 2000000))
            {
                time_sent_ = now;
                connected =
                    send_text("{\"type\":\"client/time\",\"payload\":{\"client_transmitted\":" +
                              std::to_string(now) + "}}");
                next_time_ = now + (now - connected_at < 3000000 ? 200000 : 2000000);
            }
            if (connected && server_hello_ && now >= next_state_)
            {
                connected = send_state();
                next_state_ = now + 5000000;
            }
            if (connected && now >= ping_at)
            {
                connected = send_frame(9, nullptr, 0);
                ping_at = now + 10000000;
            }
            std::string outgoing;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (command_read_ != command_write_ && server_hello_)
                {
                    const auto value = commands_[command_read_];
                    command_read_ = (command_read_ + 1) % commands_.size();
                    std::string cmd;
                    switch (value)
                    {
                    case Command::toggle:
                        cmd = state_.playing ? "pause" : "play";
                        break;
                    case Command::next:
                        cmd = "next";
                        break;
                    case Command::previous:
                        cmd = "previous";
                        break;
                    case Command::volume_up:
                    case Command::volume_down:
                        cmd = "volume";
                        break;
                    }
                    outgoing =
                        "{\"type\":\"client/command\",\"payload\":{\"controller\":{\"command\":" +
                        json_quote(cmd);
                    if (cmd == "volume")
                    {
                        // Held presses build on the level sent last, not on a
                        // report that has not caught up yet.
                        const int level =
                            std::clamp(volume_sent_.value_or("", volume_.load(), now) +
                                           (value == Command::volume_up ? 5 : -5),
                                       0, 100);
                        volume_sent_.sent("", level, now);
                        outgoing += ",\"volume\":" + std::to_string(level);
                    }
                    outgoing += "}}}";
                }
            }
            if (connected && !outgoing.empty())
                connected = send_text(outgoing);
            if (!connected)
                break;
            pollfd p{socket_, POLLIN, 0};
            const int ready = poll(&p, 1, 50);
            if (ready > 0)
            {
                // A close frame can be readable together with POLLHUP. Parse it
                // first so a server's protocol rejection is not hidden.
                if (p.revents & POLLIN)
                    connected = read_frames();
                if (connected && ((p.revents & (POLLERR | POLLNVAL)) ||
                                  ((p.revents & POLLHUP) && !(p.revents & POLLIN))))
                    connected = fail("receive-events", 0, p.revents);
            }
            else if (ready < 0 && errno != EINTR)
                connected = fail("receive-poll", errno, ready);
        }
        active_ = false;
        synchronized_ = false;
        audio_.clear();
        if (socket_ >= 0)
        {
            shutdown(socket_, SHUT_RDWR);
            close(socket_);
            socket_ = -1;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.connected = false;
            state_.playing = false;
            command_read_ = command_write_;
            state_.status = stopping_ ? "Disconnected" : "Retrying: " + std::string(failure_stage_);
            if (!stopping_ && failure_errno_ != 0)
                state_.status += " (" + std::to_string(failure_errno_) + ")";
            if (!stopping_)
                ++state_.reconnects;
        }
        const auto retry_at = sys::monotonic_us() + retry_ms * 1000;
        while (!stopping_ && sys::monotonic_us() < retry_at)
            poll(nullptr, 0, 100);
        retry_ms = std::min(15000, retry_ms * 2);
    }
}
} // namespace hui::afterglow
