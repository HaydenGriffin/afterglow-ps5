// Afterglow - Home Assistant glance model shared by the client, the screen and the sheet.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hui::afterglow
{
struct HomeEntity
{
    // state is the display value without its unit ("19.5", "Playing · Song");
    // unit is Home Assistant's unit_of_measurement ("°C", "%"), often empty.
    std::string entity_id, name, state, unit, domain;
    bool on = false;
    // Lights, switches, fans, input_booleans, scenes and scripts can be pressed.
    bool controllable = false;
    // Not reported by Home Assistant (yet), or unavailable/unknown there.
    bool available = false;
};
struct HomeGroup
{
    std::string title;
    std::vector<HomeEntity> entities;
};
// Where the connection stands; status is the words for it.
enum class HomeLink
{
    not_configured,
    connecting,
    connected,
    offline,
    auth_failed
};
// One answered press. id counts up per press so the sheet sees each once.
struct HomeResult
{
    std::uint64_t id = 0;
    std::string entity_id, service;
    bool ok = false;
    std::string message;
};
struct HomeSnapshot
{
    bool configured = false, connected = false;
    HomeLink link = HomeLink::not_configured;
    std::string status;
    std::vector<HomeGroup> groups;
    // The last few answered presses, oldest first (bounded).
    std::vector<HomeResult> results;
    std::uint64_t revision = 0;
};
// One press: service is "toggle", "turn_on" or "turn_off" in the entity's domain.
struct HomeCommand
{
    std::string entity_id, service;
};
} // namespace hui::afterglow
