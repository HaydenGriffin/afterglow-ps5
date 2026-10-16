// Afterglow - Host smoke test for the Home Assistant glance client.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/home_assistant.hpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

namespace hui::sys
{
void log(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}
std::int64_t monotonic_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace hui::sys

namespace
{
using hui::afterglow::HomeAssistant;
using hui::afterglow::HomeLink;
using hui::afterglow::HomeSnapshot;

bool wait_for(HomeAssistant &client, double seconds,
              const std::function<bool(const HomeSnapshot &)> &done)
{
    const auto deadline = hui::sys::monotonic_us() + static_cast<std::int64_t>(seconds * 1e6);
    while (hui::sys::monotonic_us() < deadline)
    {
        if (done(client.snapshot()))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
bool result_for(const HomeSnapshot &s, const char *entity, bool ok, const char *message)
{
    for (const auto &r : s.results)
        if (r.entity_id == entity && r.ok == ok && r.message == message)
            return true;
    return false;
}
const hui::afterglow::HomeEntity *find(const HomeSnapshot &s, const char *entity)
{
    for (const auto &g : s.groups)
        for (const auto &e : g.entities)
            if (e.entity_id == entity)
                return &e;
    return nullptr;
}
int fail(const char *step)
{
    std::fprintf(stderr, "FAIL: %s\n", step);
    return 1;
}

int run_ok(HomeAssistant &client)
{
    const auto light_is = [](bool on)
    {
        return [on](const HomeSnapshot &s)
        {
            const auto *light = find(s, "light.living_room_lights");
            return s.link == HomeLink::connected && light != nullptr && light->available &&
                   light->on == on;
        };
    };
    if (!wait_for(client, 5, light_is(false)))
        return fail("initial states");
    const auto first = client.snapshot();
    const auto *temperature = find(first, "sensor.living_room_temperature");
    if (temperature == nullptr || temperature->state != "19.5" || temperature->unit != "°C")
        return fail("temperature formatting");
    client.send({"light.living_room_lights", "toggle"});
    if (!wait_for(
            client, 3, [&](const HomeSnapshot &s)
            { return result_for(s, "light.living_room_lights", true, "") && light_is(true)(s); }))
        return fail("toggle and state change");
    client.send({"scene.broken", "turn_on"});
    if (!wait_for(client, 3, [](const HomeSnapshot &s)
                  { return result_for(s, "scene.broken", false, "Scene exploded"); }))
        return fail("refused call");
    client.send({"sensor.living_room_temperature", "toggle"});
    if (!wait_for(
            client, 3, [](const HomeSnapshot &s)
            { return result_for(s, "sensor.living_room_temperature", false, "Not a control"); }))
        return fail("read-only entity refused locally");
    // The server drops the connection after the refused call: offline, then back.
    if (!wait_for(client, 3, [](const HomeSnapshot &s) { return s.link == HomeLink::offline; }))
        return fail("offline after drop");
    if (!wait_for(client, 5, light_is(true)))
        return fail("reconnect with backoff");
    return 0;
}

int run_invalid(HomeAssistant &client)
{
    if (!wait_for(client, 5, [](const HomeSnapshot &s) { return s.link == HomeLink::auth_failed; }))
        return fail("auth failure reported");
    client.send({"light.living_room_lights", "toggle"});
    if (!wait_for(client, 2,
                  [](const HomeSnapshot &s) {
                      return result_for(s, "light.living_room_lights", false,
                                        "Home Assistant refused the token");
                  }))
        return fail("press refused after auth failure");
    // No retry storm: the mock counts connections.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    return client.snapshot().link == HomeLink::auth_failed ? 0 : fail("stayed refused");
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    hui::afterglow::HomeConfig config;
    config.host = "127.0.0.1";
    config.port = static_cast<std::uint16_t>(std::atoi(argv[1]));
    config.token = std::string(argv[2]) == "invalid" ? "wrong-token" : "fixture-token";
    hui::afterglow::home_wire::parse_group(
        "Living room:sensor.living_room_temperature,light.living_room_lights=Lights,"
        "scene.broken",
        config.groups.emplace_back());
    HomeAssistant client;
    if (!client.start(config))
        return 3;
    const int result = std::string(argv[2]) == "invalid" ? run_invalid(client) : run_ok(client);
    const auto begin = hui::sys::monotonic_us();
    client.stop();
    client.join();
    if (hui::sys::monotonic_us() - begin > 500000)
        return fail("bounded stop");
    return result;
}
