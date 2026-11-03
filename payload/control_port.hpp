// Afterglow - Lets the app ask the payload how it is, or stop it.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app runs in a sandbox and sees none of the payload's files, but both can reach loopback. A
// line comes in, a line goes back, and the connection closes: "status", or "stop".
#pragma once
#include "afterglow/music_over_games_wire.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace afterglow::usbflac
{
class ControlPort
{
  public:
    ~ControlPort()
    {
        drop_client();
        if (listener_ >= 0)
            close(listener_);
    }
    bool start()
    {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0)
            return false;
        const int on = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(hui::afterglow::music_wire::kControlPort);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address) ||
            listen(listener_, 4))
            return false;
        fcntl(listener_, F_SETFL, fcntl(listener_, F_GETFL, 0) | O_NONBLOCK);
        return true;
    }
    // Answers whoever is asking, a little at a time. True once when told to stop.
    bool poll(const std::string &status, double now)
    {
        if (client_ < 0)
        {
            client_ = accept(listener_, nullptr, nullptr);
            if (client_ < 0)
                return false;
            fcntl(client_, F_SETFL, fcntl(client_, F_GETFL, 0) | O_NONBLOCK);
            asked_.clear();
            accepted_at_ = now;
        }
        char bytes[64];
        const auto count = recv(client_, bytes, sizeof bytes, 0);
        if (count > 0 && asked_.size() < 64)
            asked_.append(bytes, static_cast<std::size_t>(count));
        const auto end = asked_.find('\n');
        if (end == std::string::npos)
        {
            const bool quiet = count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
            if (!quiet || now - accepted_at_ > kPatience)
                drop_client();
            return false;
        }
        const bool stop = asked_.compare(0, end, hui::afterglow::music_wire::kStop) == 0;
        const auto reply = (stop ? std::string("stopping") : status) + "\n";
        send(client_, reply.data(), reply.size(), 0);
        drop_client();
        return stop;
    }

  private:
    static constexpr double kPatience = 1.0;
    void drop_client()
    {
        if (client_ >= 0)
            close(client_);
        client_ = -1;
    }
    int listener_ = -1, client_ = -1;
    std::string asked_;
    double accepted_at_ = 0;
};
} // namespace afterglow::usbflac
