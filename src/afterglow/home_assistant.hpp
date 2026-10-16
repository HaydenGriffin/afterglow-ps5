// Afterglow - Home Assistant websocket client for the Home glance.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/home_model.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow
{
// afterglow.conf:
//   ha_host=192.0.2.20           numeric IPv4, like host
//   ha_port=8123
//   ha_token=<long-lived access token>
//   ha_group=Living room:sensor.living_room_temperature=Temperature,light.living_room
// Each ha_group line is a heading, a colon, then comma-separated entity ids,
// each optionally followed by =Label. Lines past the bounds are ignored.
constexpr std::size_t kHomeMaxGroups = 8;
constexpr std::size_t kHomeMaxEntities = 12;
constexpr std::size_t kHomeMaxToken = 2048;

struct HomeEntitySpec
{
    std::string entity_id, label;
};
struct HomeGroupSpec
{
    std::string title;
    std::vector<HomeEntitySpec> entities;
};
struct HomeConfig
{
    std::string host, token;
    std::uint16_t port = 8123;
    std::vector<HomeGroupSpec> groups;
    // Every key is present; the host is not checked here (start() refuses non-IPv4).
    bool configured() const
    {
        return !host.empty() && !token.empty() && !groups.empty();
    }
};

namespace home_wire
{
// Applies one key=value of afterglow.conf; false when the key is not Home
// Assistant's or its value is refused.
bool apply_config(HomeConfig &config, std::string_view key, std::string_view value);
// Every ha_* line of a whole afterglow.conf.
HomeConfig parse_config(std::string_view text);
// One ha_group value; false when it names no valid entity.
bool parse_group(std::string_view value, HomeGroupSpec &group);
bool valid_entity_id(std::string_view entity_id);

// Pressable domains and what each accepts: lights, switches, fans and
// input_booleans toggle; scenes and scripts only turn on.
bool controllable(std::string_view domain);
bool service_allowed(std::string_view domain, std::string_view service);
std::string domain_of(std::string_view entity_id);

std::string auth_message(std::string_view token);
// subscribe_entities limited to the configured entities: Home Assistant then
// sends only their states, never the whole house (thousands of entities).
std::string subscribe_message(int id, const HomeConfig &config);
// Empty when the domain or service is refused, so nothing unsafe is ever sent.
std::string service_message(int id, const HomeCommand &command);
std::string ping_message(int id);

// What the glance keeps of one entity's state.
struct EntityState
{
    std::string state, unit, friendly_name, device_class;
    // Selected attributes, raw JSON scalars decoded to text.
    std::string temperature, current_temperature, temperature_unit, media_title, media_artist,
        hvac_action;
};
// The compressed subscribe_entities stream: "a" adds whole states, "c" carries
// "+" changes and "-" removed attributes, "r" removes entities.
class StateTable
{
  public:
    // One event object; true when anything changed.
    bool apply(std::string_view event);
    void clear()
    {
        states_.clear();
    }
    const EntityState *find(const std::string &entity_id) const;
    std::size_t size() const
    {
        return states_.size();
    }

  private:
    std::map<std::string, EntityState> states_;
};

// "19.46" with °C reads "19.5"; "70.22" with % reads "70"; "1038.8" reads "1039".
std::string format_number(std::string_view raw, std::string_view unit);
// The value column: numbers formatted, words sentence-cased, weather and climate
// with their temperatures, media with what is playing.
std::string display_state(std::string_view domain, const EntityState &state);
// The configured label, else the friendly name without the group's room
// ("Living Room Climate Temperature" under "Living room" reads "Climate Temperature").
std::string display_name(const HomeEntitySpec &spec, const EntityState *state,
                         std::string_view group_title);
// Joins a value and its unit as the sheet shows them ("19.5 °C", "70%").
std::string with_unit(std::string_view value, std::string_view unit);
HomeEntity build_entity(const HomeEntitySpec &spec, const EntityState *state,
                        std::string_view group_title);
std::vector<HomeGroup> build_groups(const HomeConfig &config, const StateTable &table);

// The kind of message an incoming frame is and the fields the client needs.
struct Message
{
    std::string type;
    int id = -1;
    bool success = false;
    std::string error, ha_version;
    std::string_view event;
};
Message parse_message(std::string_view text);
// Seconds to wait before reconnect attempt n (0-based): 1, 2, 4, ... 30.
int backoff_seconds(int attempt);
// RFC 6455 Sec-WebSocket-Key: sixteen random bytes in base64 (24 characters);
// Home Assistant's server refuses any other length.
std::string websocket_key(const std::array<std::uint8_t, 16> &nonce);
} // namespace home_wire

// Runs on its own worker thread: connect, auth, subscribe, then keep the
// snapshot current and send presses. Reconnects with backoff; a refused token
// stops it until the app restarts (the token only changes with afterglow.conf).
class HomeAssistant
{
  public:
    HomeAssistant();
    ~HomeAssistant();
    HomeAssistant(const HomeAssistant &) = delete;
    HomeAssistant &operator=(const HomeAssistant &) = delete;

    // False when not configured, the host is not numeric IPv4, or already started.
    bool start(const HomeConfig &config);
    void stop();
    void join();
    HomeSnapshot snapshot() const;
    std::uint64_t revision() const;
    // Queued for the worker; refused domains/services answer with a failed result.
    void send(const HomeCommand &command);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hui::afterglow
