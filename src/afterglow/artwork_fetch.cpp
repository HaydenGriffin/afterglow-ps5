// Afterglow - HTTP image download and JPEG/PNG decoding for the artwork and thumbnail workers.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/artwork.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

// The pinned projectM archive already supplies stb_image. Keep one implementation
// and use its stable C API without pulling the third-party implementation here.
extern "C" unsigned char *stbi_load_from_memory(const unsigned char *, int, int *, int *, int *,
                                                int);
extern "C" int stbi_info_from_memory(const unsigned char *, int, int *, int *, int *);
extern "C" void stbi_image_free(void *);

namespace hui::afterglow
{
namespace
{
constexpr std::size_t kBodyLimit = 8 * 1024 * 1024, kHeaderLimit = 16384;
struct Socket
{
    int fd = -1;
    ~Socket()
    {
        if (fd >= 0)
            close(fd);
    }
};
bool number(std::string_view text, std::size_t &value, int base = 10)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
bool unpack(const std::vector<std::uint8_t> &wire, std::vector<std::uint8_t> &body)
{
    const std::string_view response(reinterpret_cast<const char *>(wire.data()), wire.size());
    const auto boundary = response.find("\r\n\r\n");
    if (boundary == std::string_view::npos || boundary > kHeaderLimit ||
        !(response.starts_with("HTTP/1.1 200 ") || response.starts_with("HTTP/1.0 200 ")))
        return false;
    std::string header(response.substr(0, boundary));
    std::transform(header.begin(), header.end(), header.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto payload = response.substr(boundary + 4);
    if (header.find("transfer-encoding: chunked") != std::string::npos)
    {
        while (!payload.empty())
        {
            const auto end = payload.find("\r\n");
            if (end == std::string_view::npos)
                return false;
            auto size_text = payload.substr(0, end);
            size_text = size_text.substr(0, size_text.find(';'));
            std::size_t count = 0;
            if (!number(size_text, count, 16) || count > kBodyLimit - body.size())
                return false;
            payload.remove_prefix(end + 2);
            if (count == 0)
                return true;
            if (payload.size() < count + 2 || payload.substr(count, 2) != "\r\n")
                return false;
            body.insert(body.end(), payload.begin(), payload.begin() + count);
            payload.remove_prefix(count + 2);
        }
        return false;
    }
    const auto length_at = header.find("\r\ncontent-length:");
    if (length_at != std::string::npos)
    {
        auto length = std::string_view(header).substr(length_at + 17);
        length = length.substr(0, length.find("\r\n"));
        while (!length.empty() && length.front() == ' ')
            length.remove_prefix(1);
        std::size_t expected = 0;
        if (!number(length, expected) || expected != payload.size())
            return false;
    }
    if (payload.size() > kBodyLimit)
        return false;
    body.assign(payload.begin(), payload.end());
    return true;
}
} // namespace

bool fetch_image(const std::string &url, const std::function<bool()> &cancelled,
                 std::vector<std::uint8_t> &body)
{
    if (!url.starts_with("http://") || url.size() > 8192 ||
        url.find_first_of("\r\n ") != std::string::npos)
        return false;
    const auto slash = url.find('/', 7);
    if (slash == std::string::npos)
        return false;
    const auto authority = url.substr(7, slash - 7);
    const auto colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    std::size_t port = 80;
    if (colon != std::string::npos && !number(std::string_view(authority).substr(colon + 1), port))
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (port == 0 || port > 65535 || inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
        return false;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    Socket socket;
    socket.fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket.fd < 0)
        return false;
    int enabled = 1;
#ifdef SO_NOSIGPIPE
    if (setsockopt(socket.fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0)
        return false;
#endif
#ifdef __PROSPERO__
    if (setsockopt(socket.fd, SOL_SOCKET, 0x1200, &enabled, sizeof(enabled)) != 0)
#else
    if (ioctl(socket.fd, FIONBIO, &enabled) != 0)
#endif
        return false;
    const auto deadline = sys::monotonic_us() + 8000000;
    auto wait = [&](short event)
    {
        while (!cancelled() && sys::monotonic_us() < deadline)
        {
            pollfd fd{socket.fd, event, 0};
            const int result = poll(&fd, 1, 50);
            if (result > 0)
                return (fd.revents & (event | POLLHUP)) != 0;
            if (result < 0 && errno != EINTR)
                return false;
        }
        return false;
    };
    if (connect(socket.fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 &&
        errno != EINPROGRESS)
        return false;
    if (!wait(POLLOUT))
        return false;
    int error = 0;
    socklen_t error_size = sizeof(error);
    if (getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &error_size) != 0 || error != 0)
        return false;
    const std::string request = "GET " + url.substr(slash) + " HTTP/1.1\r\nHost: " + authority +
                                "\r\nAccept: image/jpeg, image/png\r\nConnection: close\r\n\r\n";
    std::size_t sent = 0;
    while (sent < request.size() && wait(POLLOUT))
    {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const auto count = send(socket.fd, request.data() + sent, request.size() - sent, flags);
        if (count > 0)
            sent += static_cast<std::size_t>(count);
        else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
            return false;
    }
    if (sent != request.size())
        return false;
    std::vector<std::uint8_t> wire;
    std::array<std::uint8_t, 16384> buffer{};
    while (wait(POLLIN))
    {
        const auto count = recv(socket.fd, buffer.data(), buffer.size(), 0);
        if (count == 0)
            return unpack(wire, body);
        if (count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            return false;
        }
        if (wire.size() + static_cast<std::size_t>(count) > kBodyLimit + kHeaderLimit)
            return false;
        wire.insert(wire.end(), buffer.begin(), buffer.begin() + count);
    }
    return false;
}

bool decode_image(const std::vector<std::uint8_t> &body, std::vector<std::uint8_t> &rgba,
                  int &width, int &height)
{
    // Reject unsupported formats and oversized allocations before invoking the decoder.
    const bool jpeg = body.size() >= 3 && body[0] == 0xff && body[1] == 0xd8 && body[2] == 0xff;
    const unsigned char png_header[]{137, 80, 78, 71, 13, 10, 26, 10};
    const bool png = body.size() >= 8 && std::memcmp(body.data(), png_header, 8) == 0;
    int channels = 0;
    const int size = static_cast<int>(body.size());
    if ((!jpeg && !png) || !stbi_info_from_memory(body.data(), size, &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > 2048 || height > 2048)
        return false;
    auto *pixels = stbi_load_from_memory(body.data(), size, &width, &height, &channels, 4);
    if (!pixels)
        return false;
    rgba.assign(pixels, pixels + static_cast<std::size_t>(width * height) * 4);
    stbi_image_free(pixels);
    return true;
}
} // namespace hui::afterglow
