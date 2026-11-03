// Afterglow - Starts and stops the Music over games payload, and asks how it is.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "afterglow/music_over_games.hpp"
#include "afterglow/music_over_games_wire.hpp"
#include "core/save_file.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace hui::afterglow
{
namespace
{
constexpr const char *kPayloadPath = "/app0/payload/afterglow-music.elf";
constexpr std::size_t kLargestPayload = 8u << 20;
constexpr int kLookEverySeconds = 2;
// How long a started payload gets to answer before it is started again.
constexpr int kLaunchPatienceSeconds = 60;

int connect_loopback(std::uint16_t port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    timeval patience{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &patience, sizeof patience);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &patience, sizeof patience);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const char *data, std::size_t size)
{
    while (size)
    {
        const auto sent = send(fd, data, size, 0);
        if (sent <= 0)
            return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

// One line to the payload and its answer. False if it is not there.
bool ask_payload(const std::string &what, std::string &answer)
{
    const int fd = connect_loopback(music_wire::kControlPort);
    if (fd < 0)
        return false;
    answer.clear();
    const auto line = what + "\n";
    bool answered = false;
    if (send_all(fd, line.data(), line.size()))
    {
        char bytes[512];
        for (;;)
        {
            const auto count = recv(fd, bytes, sizeof bytes, 0);
            if (count <= 0)
                break;
            answer.append(bytes, static_cast<std::size_t>(count));
            answered = true;
            const auto end = answer.find('\n');
            if (end != std::string::npos)
            {
                answer.resize(end);
                break;
            }
        }
    }
    close(fd);
    return answered;
}

OverGamesPhase phase_of(music_wire::PayloadState state)
{
    switch (state)
    {
    case music_wire::PayloadState::no_drive:
        return OverGamesPhase::no_drive;
    case music_wire::PayloadState::ready:
        return OverGamesPhase::ready;
    case music_wire::PayloadState::waiting:
        return OverGamesPhase::waiting;
    case music_wire::PayloadState::playing:
        return OverGamesPhase::playing;
    case music_wire::PayloadState::unknown:
        break;
    }
    return OverGamesPhase::starting;
}
} // namespace

MusicOverGames::~MusicOverGames()
{
    leaving_ = true;
    if (thread_started_)
        pthread_join(thread_, nullptr);
}

bool MusicOverGames::start()
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return false;
    pthread_attr_setstacksize(&attributes, 256 * 1024);
    thread_started_ = pthread_create(&thread_, &attributes, entry, this) == 0;
    pthread_attr_destroy(&attributes);
    return thread_started_;
}

void MusicOverGames::want_on(const std::string &host, int port)
{
    std::lock_guard<std::mutex> lock(mutex_);
    host_ = host;
    port_ = port;
    wanted_ = true;
    stop_pending_ = false;
    view_.phase = OverGamesPhase::starting;
    view_.song.clear();
}

void MusicOverGames::want_off()
{
    std::lock_guard<std::mutex> lock(mutex_);
    wanted_ = false;
    stop_pending_ = true;
    view_.phase = OverGamesPhase::off;
    view_.song.clear();
}

OverGamesView MusicOverGames::view() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return view_;
}

void MusicOverGames::publish(OverGamesPhase phase, std::string song)
{
    std::lock_guard<std::mutex> lock(mutex_);
    view_.phase = phase;
    view_.song = std::move(song);
}

void *MusicOverGames::entry(void *self)
{
    static_cast<MusicOverGames *>(self)->run();
    return nullptr;
}

bool MusicOverGames::launch(const std::string &host, int port)
{
    std::string elf;
    if (!save::read_file(kPayloadPath, &elf, kLargestPayload) ||
        !music_wire::embed_server(elf, host, port))
    {
        publish(OverGamesPhase::no_payload);
        return false;
    }
    const int fd = connect_loopback(music_wire::kLoaderPort);
    if (fd < 0)
    {
        publish(OverGamesPhase::no_loader);
        return false;
    }
    const bool sent = send_all(fd, elf.data(), elf.size());
    close(fd);
    if (!sent)
        publish(OverGamesPhase::no_loader);
    return sent;
}

void MusicOverGames::run()
{
    int launch_wait = 0;
    while (!leaving_.load())
    {
        std::string host;
        int port = 0;
        bool wanted = false, stop = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            host = host_;
            port = port_;
            wanted = wanted_;
            stop = stop_pending_;
            stop_pending_ = false;
        }
        std::string answer;
        if (stop)
            ask_payload(music_wire::kStop, answer);
        else if (ask_payload(music_wire::kStatus, answer))
        {
            launch_wait = 0;
            publish(phase_of(music_wire::parse_state(answer)), music_wire::song_in(answer));
        }
        else if (wanted && launch_wait <= 0)
        {
            launch_wait = kLaunchPatienceSeconds;
            if (launch(host, port))
                publish(OverGamesPhase::starting);
        }
        else if (!wanted)
            publish(OverGamesPhase::off);
        launch_wait -= kLookEverySeconds;
        for (int waited = 0; waited < kLookEverySeconds * 10 && !leaving_.load(); ++waited)
            usleep(100000);
    }
}
} // namespace hui::afterglow
