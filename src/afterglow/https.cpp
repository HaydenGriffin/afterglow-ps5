// Afterglow - One-shot HTTPS GET with verified certificates (BearSSL).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/https.hpp"
#include "afterglow/http_response.hpp"
#include "afterglow/online_lyrics.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include <bearssl.h>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#ifdef __PROSPERO__
// getaddrinfo links on the console but only libScePosixForWebKit exports it,
// a null import in a native title; names go through the system resolver.
extern "C"
{
    int sceNetPoolCreate(const char *name, int size, int flags);
    int sceNetPoolDestroy(int pool);
    int sceNetResolverCreate(const char *name, int pool, int flags);
    int sceNetResolverStartNtoa(int resolver, const char *hostname, struct in_addr *address,
                                int timeout_us, int retries, int flags);
    int sceNetResolverAbort(int resolver, int flags);
    int sceNetResolverDestroy(int resolver);
    const char *sceKernelGetFsSandboxRandomWord(void);
}
#endif

namespace hui::afterglow
{
namespace
{
// ---- Trust anchors ---------------------------------------------------------

// The CA list decoded once into BearSSL trust anchors, kept for the process.
struct Anchors
{
    std::vector<br_x509_trust_anchor> list;
    std::deque<std::vector<unsigned char>> bytes; // what the anchors point into
    std::string source;
    bool loaded = false;
};
std::mutex anchors_mutex;
Anchors anchors;

unsigned char *keep(Anchors &into, const unsigned char *data, std::size_t size)
{
    into.bytes.emplace_back(data, data + size);
    return into.bytes.back().data();
}
void append_bytes(void *context, const void *data, std::size_t size)
{
    auto &out = *static_cast<std::vector<unsigned char> *>(context);
    const auto *bytes = static_cast<const unsigned char *>(data);
    out.insert(out.end(), bytes, bytes + size);
}
// One DER certificate as an anchor: its subject name and public key. Keys
// BearSSL cannot use (DSA, odd curves) are left out.
void add_certificate(Anchors &into, const std::vector<unsigned char> &der)
{
    std::vector<unsigned char> dn;
    br_x509_decoder_context decoder;
    br_x509_decoder_init(&decoder, append_bytes, &dn);
    br_x509_decoder_push(&decoder, der.data(), der.size());
    const br_x509_pkey *key = br_x509_decoder_get_pkey(&decoder);
    if (key == nullptr || dn.empty())
        return;
    br_x509_trust_anchor anchor{};
    anchor.dn.data = keep(into, dn.data(), dn.size());
    anchor.dn.len = dn.size();
    anchor.flags = br_x509_decoder_isCA(&decoder) ? BR_X509_TA_CA : 0;
    anchor.pkey.key_type = key->key_type;
    if (key->key_type == BR_KEYTYPE_RSA)
    {
        anchor.pkey.key.rsa.n = keep(into, key->key.rsa.n, key->key.rsa.nlen);
        anchor.pkey.key.rsa.nlen = key->key.rsa.nlen;
        anchor.pkey.key.rsa.e = keep(into, key->key.rsa.e, key->key.rsa.elen);
        anchor.pkey.key.rsa.elen = key->key.rsa.elen;
    }
    else if (key->key_type == BR_KEYTYPE_EC)
    {
        anchor.pkey.key.ec.curve = key->key.ec.curve;
        anchor.pkey.key.ec.q = keep(into, key->key.ec.q, key->key.ec.qlen);
        anchor.pkey.key.ec.qlen = key->key.ec.qlen;
    }
    else
        return;
    into.list.push_back(anchor);
}
void add_pem(Anchors &into, std::string text)
{
    // The decoder ends an object on the newline after its END line.
    text += '\n';
    br_pem_decoder_context pem;
    br_pem_decoder_init(&pem);
    std::vector<unsigned char> der;
    bool certificate = false;
    const auto *data = reinterpret_cast<const unsigned char *>(text.data());
    std::size_t left = text.size();
    while (left > 0)
    {
        const std::size_t used = br_pem_decoder_push(&pem, data, left);
        data += used;
        left -= used;
        switch (br_pem_decoder_event(&pem))
        {
        case BR_PEM_BEGIN_OBJ:
        {
            const std::string_view name = br_pem_decoder_name(&pem);
            certificate = name == "CERTIFICATE" || name == "X509 CERTIFICATE" ||
                          name == "TRUSTED CERTIFICATE";
            der.clear();
            br_pem_decoder_setdest(&pem, append_bytes, &der);
            break;
        }
        case BR_PEM_END_OBJ:
            if (certificate)
                add_certificate(into, der);
            certificate = false;
            break;
        case BR_PEM_ERROR:
            return;
        default:
            break;
        }
    }
}
// A list of DER certificates one after another.
void add_der_list(Anchors &into, std::string_view data)
{
    while (data.size() >= 4 && static_cast<unsigned char>(data[0]) == 0x30)
    {
        std::size_t length = static_cast<unsigned char>(data[1]), header = 2;
        if (length & 0x80)
        {
            const std::size_t count = length & 0x7F;
            if (count == 0 || count > 3 || data.size() < 2 + count)
                return;
            length = 0;
            for (std::size_t i = 0; i < count; ++i)
                length = (length << 8) | static_cast<unsigned char>(data[2 + i]);
            header += count;
        }
        if (data.size() < header + length)
            return;
        const auto *bytes = reinterpret_cast<const unsigned char *>(data.data());
        add_certificate(into, std::vector<unsigned char>(bytes, bytes + header + length));
        data.remove_prefix(header + length);
    }
}
std::vector<std::string> ca_files()
{
#ifdef __PROSPERO__
    std::vector<std::string> files{"/system/common/cert/CA_LIST.cer"};
    // A sandboxed title may see the system folder under its random word.
    const char *word = sceKernelGetFsSandboxRandomWord();
    if (word != nullptr && *word != '\0')
        files.push_back(std::string("/") + word + "/common/cert/CA_LIST.cer");
    return files;
#else
    std::vector<std::string> files;
    if (const char *chosen = std::getenv("SSL_CERT_FILE"))
        files.emplace_back(chosen);
    for (const char *path :
         {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
          "/etc/ssl/cert.pem", "/etc/ssl/ca-bundle.pem", "/usr/local/etc/openssl/cert.pem"})
        files.emplace_back(path);
    return files;
#endif
}
// Loaded on first use, on the worker; a missing list is looked for again
// next time rather than remembered.
const Anchors &trust_anchors()
{
    if (anchors.loaded)
        return anchors;
    for (const auto &file : ca_files())
    {
        std::string text;
        if (!save::read_file(file, &text, 8u << 20))
            continue;
        Anchors found;
        if (text.find("-----BEGIN") != std::string::npos)
            add_pem(found, std::move(text));
        else
            add_der_list(found, text);
        if (found.list.empty())
            continue;
        found.source = file;
        found.loaded = true;
        anchors = std::move(found);
        sys::log("[Afterglow] https: %zu trusted certificates from %s", anchors.list.size(),
                 anchors.source.c_str());
        break;
    }
    return anchors;
}

// ---- Name lookup and connection ---------------------------------------------

#ifdef __PROSPERO__
std::mutex resolver_mutex;
int active_resolver = -1;
#endif

std::vector<sockaddr_storage> resolve(const std::string &host, int port, std::string &error)
{
    std::vector<sockaddr_storage> out;
#ifdef __PROSPERO__
    const int pool = sceNetPoolCreate("afterglow_dns", 16 * 1024, 0);
    if (pool < 0)
    {
        error = "dns pool";
        return out;
    }
    int resolver = -1;
    {
        std::lock_guard<std::mutex> lock(resolver_mutex);
        resolver = active_resolver = sceNetResolverCreate("afterglow_dns", pool, 0);
    }
    in_addr address{};
    // 3 s a try, one retry: a slow DNS server does not hold a lookup for long.
    const bool found = resolver >= 0 && sceNetResolverStartNtoa(resolver, host.c_str(), &address,
                                                                3000000, 1, 0) >= 0;
    {
        std::lock_guard<std::mutex> lock(resolver_mutex);
        active_resolver = -1;
        if (resolver >= 0)
            sceNetResolverDestroy(resolver);
    }
    sceNetPoolDestroy(pool);
    if (!found)
    {
        error = "dns";
        return out;
    }
    sockaddr_storage storage{};
    auto *in = reinterpret_cast<sockaddr_in *>(&storage);
    in->sin_len = sizeof(sockaddr_in);
    in->sin_family = AF_INET;
    in->sin_port = htons(static_cast<std::uint16_t>(port));
    in->sin_addr = address;
    out.push_back(storage);
#else
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *list = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0 ||
        list == nullptr)
    {
        error = "dns";
        return out;
    }
    for (const addrinfo *entry = list; entry != nullptr && out.size() < 8; entry = entry->ai_next)
        if (entry->ai_addrlen <= sizeof(sockaddr_storage))
        {
            sockaddr_storage storage{};
            std::memcpy(&storage, entry->ai_addr, entry->ai_addrlen);
            out.push_back(storage);
        }
    freeaddrinfo(list);
#endif
    return out;
}

struct Connection
{
    int fd = -1;
    std::int64_t deadline = 0;
    const std::function<bool()> *cancelled = nullptr;
    const char *error = nullptr;
    ~Connection()
    {
        if (fd >= 0)
            ::close(fd);
    }
    // Waits for the socket in 50 ms slices so a cancel or the deadline ends it.
    bool wait(short event)
    {
        while (sys::monotonic_us() < deadline)
        {
            if (cancelled != nullptr && *cancelled && (*cancelled)())
            {
                error = "cancelled";
                return false;
            }
            pollfd p{fd, event, 0};
            const int ready = poll(&p, 1, 50);
            if (ready > 0)
                return (p.revents & (event | POLLHUP | POLLERR)) != 0;
            if (ready < 0 && errno != EINTR)
            {
                error = "poll";
                return false;
            }
        }
        error = "timeout";
        return false;
    }
};

bool nonblocking(int fd)
{
    int enabled = 1;
#ifdef __PROSPERO__
    // PS5 titles are refused F_SETFL and FIONBIO; SO_NBIO is the console's switch.
    return setsockopt(fd, SOL_SOCKET, 0x1200, &enabled, sizeof(enabled)) == 0;
#else
    return ioctl(fd, FIONBIO, &enabled) == 0;
#endif
}

bool connect_to(Connection &c, const sockaddr_storage &address)
{
    const socklen_t size =
        address.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    c.fd = ::socket(address.ss_family, SOCK_STREAM, 0);
    if (c.fd < 0)
        return false;
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    setsockopt(c.fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
    if (!nonblocking(c.fd))
    {
        c.error = "nonblocking";
        return false;
    }
    if (::connect(c.fd, reinterpret_cast<const sockaddr *>(&address), size) != 0 &&
        errno != EINPROGRESS)
    {
        c.error = "connect";
        return false;
    }
    if (!c.wait(POLLOUT))
        return false;
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0)
    {
        c.error = "connect";
        return false;
    }
    return true;
}

// BearSSL's transport callbacks: -1 ends the exchange (error, EOF or deadline).
int read_socket(void *context, unsigned char *data, std::size_t size)
{
    auto &c = *static_cast<Connection *>(context);
    for (;;)
    {
        if (!c.wait(POLLIN))
            return -1;
        const auto count = ::recv(c.fd, data, size, 0);
        if (count > 0)
            return static_cast<int>(count);
        if (count == 0)
            return -1;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return -1;
    }
}
int write_socket(void *context, const unsigned char *data, std::size_t size)
{
    auto &c = *static_cast<Connection *>(context);
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    for (;;)
    {
        if (!c.wait(POLLOUT))
            return -1;
        const auto count = ::send(c.fd, data, size, flags);
        if (count > 0)
            return static_cast<int>(count);
        if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
            return -1;
    }
}

// The engine, its certificate checker and the full-duplex record buffer:
// about 40 KiB, so it lives on the heap rather than the worker's stack.
struct Session
{
    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    std::array<unsigned char, BR_SSL_BUFSIZE_BIDI> buffer;
};

std::string tls_error(int code)
{
    switch (code)
    {
    case BR_ERR_X509_NOT_TRUSTED:
        return "certificate not trusted";
    case BR_ERR_X509_BAD_SERVER_NAME:
        return "certificate for another name";
    case BR_ERR_X509_EXPIRED:
        return "certificate expired or clock wrong";
    case BR_ERR_NO_RANDOM:
        return "no random source";
    default:
        return "tls " + std::to_string(code);
    }
}
} // namespace

HttpsResult https_get(const std::string &host, const std::string &path,
                      const std::string &user_agent, std::size_t max_body, int timeout_ms,
                      const std::function<bool()> &cancelled, int port)
{
    HttpsResult result;
    const auto started = sys::monotonic_us();
    std::lock_guard<std::mutex> lock(anchors_mutex);
    const auto &trusted = trust_anchors();
    if (trusted.list.empty())
    {
        result.error = "no CA list";
        return result;
    }
    if (port <= 0 || port > 65535)
    {
        result.error = "port";
        return result;
    }
    const auto addresses = resolve(host, port, result.error);
    if (addresses.empty())
        return result;
    Connection connection;
    connection.deadline = started + static_cast<std::int64_t>(timeout_ms) * 1000;
    connection.cancelled = &cancelled;
    bool connected = false;
    for (const auto &address : addresses)
    {
        if (connection.fd >= 0)
            ::close(connection.fd);
        connection.fd = -1;
        if (connect_to(connection, address))
        {
            connected = true;
            break;
        }
        if (connection.error != nullptr && (std::strcmp(connection.error, "cancelled") == 0 ||
                                            std::strcmp(connection.error, "timeout") == 0))
            break;
    }
    if (!connected)
    {
        result.error = connection.error != nullptr ? connection.error : "connect";
        return result;
    }

    auto session = std::make_unique<Session>();
    br_ssl_client_init_full(&session->client, &session->x509, trusted.list.data(),
                            trusted.list.size());
    br_ssl_engine_set_versions(&session->client.eng, BR_TLS12, BR_TLS12);
    // Certificate dates are checked against the wall clock: days since
    // 1 January of year 0 (proleptic Gregorian) and seconds into the day.
    timeval now{};
    gettimeofday(&now, nullptr);
    const auto unix_seconds = static_cast<std::uint64_t>(now.tv_sec);
    br_x509_minimal_set_time(&session->x509,
                             static_cast<std::uint32_t>(unix_seconds / 86400 + 719528),
                             static_cast<std::uint32_t>(unix_seconds % 86400));
    br_ssl_engine_set_buffer(&session->client.eng, session->buffer.data(), session->buffer.size(),
                             1);
    if (!br_ssl_client_reset(&session->client, host.c_str(), 0))
    {
        result.error = tls_error(br_ssl_engine_last_error(&session->client.eng));
        return result;
    }
    br_sslio_init(&session->io, &session->client.eng, read_socket, &connection, write_socket,
                  &connection);
    const auto request =
        http::get_request(port == 443 ? host : host + ":" + std::to_string(port), path, user_agent);
    if (br_sslio_write_all(&session->io, request.data(), request.size()) != 0 ||
        br_sslio_flush(&session->io) != 0)
    {
        const int code = br_ssl_engine_last_error(&session->client.eng);
        result.error = code != BR_ERR_OK  ? tls_error(code)
                       : connection.error ? connection.error
                                          : "send";
        return result;
    }
    std::string wire;
    std::array<unsigned char, 16384> chunk{};
    for (;;)
    {
        const int count = br_sslio_read(&session->io, chunk.data(), chunk.size());
        const bool closed = count <= 0;
        if (!closed)
            wire.append(reinterpret_cast<const char *>(chunk.data()),
                        static_cast<std::size_t>(count));
        const auto response = http::parse_response(wire, closed, max_body);
        using State = http::Response::State;
        if (response.state == State::complete)
        {
            result.ok = true;
            result.status = response.status;
            result.body = response.body;
            return result;
        }
        if (response.state == State::too_large)
        {
            result.error = "answer too large";
            return result;
        }
        if (response.state == State::malformed || closed)
        {
            const int code = br_ssl_engine_last_error(&session->client.eng);
            result.error = code != BR_ERR_OK && code != BR_ERR_IO ? tls_error(code)
                           : connection.error != nullptr          ? connection.error
                                                                  : "incomplete answer";
            return result;
        }
    }
}

void https_abort()
{
#ifdef __PROSPERO__
    std::lock_guard<std::mutex> lock(resolver_mutex);
    if (active_resolver >= 0)
        sceNetResolverAbort(active_resolver, 0);
#endif
}

LyricsFetch lrclib_fetch()
{
    return [](const std::string &path, const std::function<bool()> &cancelled)
    {
        // LG gives its search 8 s; the name lookup and the handshake count too.
        return https_get(lrclib::kHost, path, lrclib::user_agent(), lrclib::kMaxAnswer, 8000,
                         cancelled);
    };
}
} // namespace hui::afterglow
