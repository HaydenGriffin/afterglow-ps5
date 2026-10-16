// Afterglow - Home Assistant websocket client for the Home glance.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/home_assistant.hpp"
#include "afterglow/protocol.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <set>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hui::afterglow
{
namespace
{
// Subscribed states only: a dozen entities with attributes stay far below this.
constexpr std::size_t kMaxMessage = 1024 * 1024;
constexpr std::size_t kMaxStates = kHomeMaxGroups * kHomeMaxEntities;
constexpr std::size_t kMaxResults = 8;
constexpr std::size_t kMaxQueued = 16;

// Bounded slicing of large JSON: find where a value ends without building a
// tree, so an event's size never decides how many tokens a parser needs.
std::size_t value_end(std::string_view s, std::size_t at)
{
    bool quoted = false, escape = false;
    int depth = 0;
    for (std::size_t i = at; i < s.size(); ++i)
    {
        const char c = s[i];
        if (quoted)
        {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
            {
                quoted = false;
                if (!depth)
                    return i + 1;
            }
        }
        else if (c == '"')
            quoted = true;
        else if (c == '[' || c == '{')
        {
            if (++depth > 32)
                return s.size();
        }
        else if (c == ']' || c == '}')
        {
            if (!depth)
                return i;
            if (--depth == 0)
                return i + 1;
        }
        else if (!depth && (c == ',' || c == ':' || std::isspace(static_cast<unsigned char>(c))))
            return i;
    }
    return s.size();
}
void skip_space(std::string_view s, std::size_t &at)
{
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
        ++at;
}
// A JSON string token to text; empty for anything else.
std::string decode_string(std::string_view raw)
{
    if (raw.size() < 2 || raw.front() != '"' || raw.size() > 8192)
        return {};
    const std::string object = "{\"v\":" + std::string(raw) + "}";
    Json json(object);
    return json.string(json.get(0, "v"));
}
// Strings decoded, numbers and booleans as written, null and containers empty.
std::string scalar_text(std::string_view raw)
{
    if (raw.empty() || raw == "null" || raw.front() == '{' || raw.front() == '[')
        return {};
    if (raw.front() == '"')
        return decode_string(raw);
    return std::string(raw.substr(0, 64));
}
// Visits each member of a JSON object, at most limit of them.
template <typename Visit> void members(std::string_view s, std::size_t limit, Visit visit)
{
    std::size_t at = 0, seen = 0;
    skip_space(s, at);
    if (at == s.size() || s[at++] != '{')
        return;
    while (at < s.size() && seen++ < limit)
    {
        skip_space(s, at);
        if (at == s.size() || s[at] != '"')
            return;
        auto end = value_end(s, at);
        const std::string_view key_raw = s.substr(at, end - at);
        at = end;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ':')
            return;
        skip_space(s, at);
        end = value_end(s, at);
        if (end <= at)
            return;
        // Keys are entity ids and attribute names: plain ASCII, no escapes.
        std::string_view key = key_raw.size() >= 2 ? key_raw.substr(1, key_raw.size() - 2) : "";
        visit(key, s.substr(at, end - at));
        at = end;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ',')
            return;
    }
}
std::string_view field(std::string_view s, std::string_view key)
{
    std::string_view found;
    members(s, 4096,
            [&](std::string_view name, std::string_view value)
            {
                if (found.empty() && name == key)
                    found = value;
            });
    return found;
}
template <typename Visit> void elements(std::string_view s, std::size_t limit, Visit visit)
{
    std::size_t at = 0, seen = 0;
    skip_space(s, at);
    if (at == s.size() || s[at++] != '[')
        return;
    while (at < s.size() && seen++ < limit)
    {
        skip_space(s, at);
        if (at == s.size() || s[at] == ']')
            return;
        const auto end = value_end(s, at);
        if (end <= at)
            return;
        visit(s.substr(at, end - at));
        at = end;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ',')
            return;
    }
}
std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}
std::string lower(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
// "not_home" reads "Not home"; words that already carry capitals stay as written.
std::string sentence(std::string_view s)
{
    std::string out(s);
    if (std::any_of(out.begin(), out.end(),
                    [](char c) { return std::isupper(static_cast<unsigned char>(c)) || c == ' '; }))
        return out;
    for (char &c : out)
        if (c == '_')
            c = ' ';
    if (!out.empty())
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}
bool toggles(std::string_view domain)
{
    return domain == "light" || domain == "switch" || domain == "fan" || domain == "input_boolean";
}
std::string weather_words(std::string_view condition)
{
    static constexpr std::pair<std::string_view, std::string_view> kWords[] = {
        {"clear-night", "Clear night"},
        {"cloudy", "Cloudy"},
        {"exceptional", "Exceptional"},
        {"fog", "Fog"},
        {"hail", "Hail"},
        {"lightning", "Lightning"},
        {"lightning-rainy", "Thunder"},
        {"partlycloudy", "Partly cloudy"},
        {"pouring", "Pouring"},
        {"rainy", "Rain"},
        {"snowy", "Snow"},
        {"snowy-rainy", "Sleet"},
        {"sunny", "Sunny"},
        {"windy", "Windy"},
        {"windy-variant", "Windy"},
    };
    for (const auto &[key, words] : kWords)
        if (condition == key)
            return std::string(words);
    return sentence(condition);
}
std::string binary_words(std::string_view device_class, bool on)
{
    if (device_class == "door" || device_class == "window" || device_class == "garage_door" ||
        device_class == "opening")
        return on ? "Open" : "Closed";
    if (device_class == "motion" || device_class == "occupancy" || device_class == "presence")
        return on ? "Detected" : "Clear";
    if (device_class == "moisture")
        return on ? "Wet" : "Dry";
    if (device_class == "connectivity")
        return on ? "Connected" : "Disconnected";
    if (device_class == "battery")
        return on ? "Low" : "Normal";
    if (device_class == "problem")
        return on ? "Problem" : "OK";
    return on ? "On" : "Off";
}
// Home Assistant writes micro with the Greek mu, which the baked Archivo
// atlas lacks; the micro sign looks the same and is in it.
std::string sofa_unit(std::string unit)
{
    const std::string_view mu = "\xce\xbc", micro = "\xc2\xb5";
    for (auto at = unit.find(mu); at != std::string::npos; at = unit.find(mu, at))
        unit.replace(at, mu.size(), micro);
    return unit;
}
void apply_attributes(home_wire::EntityState &state, std::string_view attributes)
{
    members(attributes, 256,
            [&](std::string_view key, std::string_view value)
            {
                if (key == "friendly_name")
                    state.friendly_name = scalar_text(value).substr(0, 120);
                else if (key == "unit_of_measurement")
                    state.unit = sofa_unit(scalar_text(value).substr(0, 16));
                else if (key == "device_class")
                    state.device_class = scalar_text(value).substr(0, 32);
                else if (key == "temperature")
                    state.temperature = scalar_text(value);
                else if (key == "current_temperature")
                    state.current_temperature = scalar_text(value);
                else if (key == "temperature_unit")
                    state.temperature_unit = scalar_text(value).substr(0, 8);
                else if (key == "media_title")
                    state.media_title = scalar_text(value).substr(0, 200);
                else if (key == "media_artist")
                    state.media_artist = scalar_text(value).substr(0, 200);
                else if (key == "hvac_action")
                    state.hvac_action = scalar_text(value).substr(0, 32);
            });
}
void remove_attribute(home_wire::EntityState &state, std::string_view key)
{
    if (key == "friendly_name")
        state.friendly_name.clear();
    else if (key == "unit_of_measurement")
        state.unit.clear();
    else if (key == "device_class")
        state.device_class.clear();
    else if (key == "temperature")
        state.temperature.clear();
    else if (key == "current_temperature")
        state.current_temperature.clear();
    else if (key == "temperature_unit")
        state.temperature_unit.clear();
    else if (key == "media_title")
        state.media_title.clear();
    else if (key == "media_artist")
        state.media_artist.clear();
    else if (key == "hvac_action")
        state.hvac_action.clear();
}
} // namespace

namespace home_wire
{
bool valid_entity_id(std::string_view id)
{
    const auto dot = id.find('.');
    if (id.size() > 255 || dot == std::string_view::npos || dot == 0 || dot + 1 == id.size())
        return false;
    for (std::size_t i = 0; i < id.size(); ++i)
    {
        const char c = id[i];
        if (i == dot)
            continue;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

bool parse_group(std::string_view value, HomeGroupSpec &group)
{
    group = {};
    const auto colon = value.find(':');
    if (colon == std::string_view::npos)
        return false;
    const auto title = trim(value.substr(0, colon));
    if (title.empty() || title.size() > 40)
        return false;
    group.title = std::string(title);
    std::string_view rest = value.substr(colon + 1);
    while (!rest.empty() && group.entities.size() < kHomeMaxEntities)
    {
        const auto comma = rest.find(',');
        const auto entry = trim(rest.substr(0, comma));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        const auto eq = entry.find('=');
        HomeEntitySpec spec;
        spec.entity_id = std::string(trim(entry.substr(0, eq)));
        if (eq != std::string_view::npos)
            spec.label = std::string(trim(entry.substr(eq + 1)).substr(0, 40));
        if (valid_entity_id(spec.entity_id))
            group.entities.push_back(std::move(spec));
    }
    return !group.entities.empty();
}

bool apply_config(HomeConfig &config, std::string_view key, std::string_view value)
{
    value = trim(value);
    if (key == "ha_host")
    {
        if (value.size() > 64)
            return false;
        config.host = std::string(value);
        return true;
    }
    if (key == "ha_port")
    {
        unsigned int port = 0;
        char extra = 0;
        const std::string text(value.substr(0, 16));
        if (std::sscanf(text.c_str(), "%u%c", &port, &extra) != 1 || port == 0 || port > 65535)
            return false;
        config.port = static_cast<std::uint16_t>(port);
        return true;
    }
    if (key == "ha_token")
    {
        if (value.empty() || value.size() > kHomeMaxToken)
            return false;
        config.token = std::string(value);
        return true;
    }
    if (key == "ha_group")
    {
        HomeGroupSpec group;
        if (config.groups.size() >= kHomeMaxGroups || !parse_group(value, group))
            return false;
        config.groups.push_back(std::move(group));
        return true;
    }
    return false;
}

HomeConfig parse_config(std::string_view text)
{
    HomeConfig config;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const auto eq = line.find('=');
        if (eq != std::string_view::npos && line.starts_with("ha_"))
            apply_config(config, line.substr(0, eq), line.substr(eq + 1));
    }
    return config;
}

std::string domain_of(std::string_view entity_id)
{
    const auto dot = entity_id.find('.');
    return dot == std::string_view::npos ? std::string() : std::string(entity_id.substr(0, dot));
}
bool controllable(std::string_view domain)
{
    return toggles(domain) || domain == "scene" || domain == "script";
}
bool service_allowed(std::string_view domain, std::string_view service)
{
    if (toggles(domain))
        return service == "toggle" || service == "turn_on" || service == "turn_off";
    if (domain == "scene" || domain == "script")
        return service == "turn_on";
    return false;
}

std::string auth_message(std::string_view token)
{
    return "{\"type\":\"auth\",\"access_token\":" + json_quote(token) + "}";
}
std::string subscribe_message(int id, const HomeConfig &config)
{
    std::set<std::string> seen;
    std::string ids;
    for (const auto &group : config.groups)
        for (const auto &entity : group.entities)
            if (seen.insert(entity.entity_id).second)
                ids += (ids.empty() ? "" : ",") + json_quote(entity.entity_id);
    return "{\"id\":" + std::to_string(id) + ",\"type\":\"subscribe_entities\",\"entity_ids\":[" +
           ids + "]}";
}
std::string service_message(int id, const HomeCommand &command)
{
    const auto domain = domain_of(command.entity_id);
    if (!valid_entity_id(command.entity_id) || !service_allowed(domain, command.service))
        return {};
    return "{\"id\":" + std::to_string(id) +
           ",\"type\":\"call_service\",\"domain\":" + json_quote(domain) +
           ",\"service\":" + json_quote(command.service) +
           ",\"target\":{\"entity_id\":" + json_quote(command.entity_id) + "}}";
}
std::string ping_message(int id)
{
    return "{\"id\":" + std::to_string(id) + ",\"type\":\"ping\"}";
}

bool StateTable::apply(std::string_view event)
{
    bool changed = false;
    members(field(event, "a"), kMaxStates,
            [&](std::string_view id, std::string_view compressed)
            {
                if (!valid_entity_id(id) ||
                    (states_.size() >= kMaxStates && !states_.contains(std::string(id))))
                    return;
                EntityState state;
                state.state = scalar_text(field(compressed, "s")).substr(0, 255);
                apply_attributes(state, field(compressed, "a"));
                states_[std::string(id)] = std::move(state);
                changed = true;
            });
    members(field(event, "c"), kMaxStates,
            [&](std::string_view id, std::string_view diff)
            {
                const auto it = states_.find(std::string(id));
                if (it == states_.end())
                    return;
                const auto added = field(diff, "+");
                const auto state = field(added, "s");
                if (!state.empty())
                    it->second.state = scalar_text(state).substr(0, 255);
                apply_attributes(it->second, field(added, "a"));
                elements(field(field(diff, "-"), "a"), 256, [&](std::string_view name)
                         { remove_attribute(it->second, decode_string(name)); });
                changed = true;
            });
    elements(field(event, "r"), kMaxStates,
             [&](std::string_view raw) { changed |= states_.erase(decode_string(raw)) > 0; });
    return changed;
}
const EntityState *StateTable::find(const std::string &entity_id) const
{
    const auto it = states_.find(entity_id);
    return it == states_.end() ? nullptr : &it->second;
}

std::string format_number(std::string_view raw, std::string_view unit)
{
    raw = trim(raw);
    if (raw.empty() || raw.size() > 32 ||
        raw.find_first_not_of("0123456789+-.eE") != std::string_view::npos)
        return {};
    const std::string text(raw);
    char *end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (errno || end != text.c_str() + text.size() || !std::isfinite(value))
        return {};
    const bool temperature = unit == "°C" || unit == "°F" || unit == "K";
    int decimals = 1;
    if (unit == "%" || (!temperature && std::abs(value) >= 100))
        decimals = 0;
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    std::string out = buffer;
    // Temperatures keep their tenth so a column of them lines up; other
    // values drop a ".0" that carries no information.
    if (!temperature && out.size() > 2 && out.ends_with(".0"))
        out.resize(out.size() - 2);
    if (out == "-0" || out == "-0.0")
        out.erase(0, 1);
    return out;
}

std::string with_unit(std::string_view value, std::string_view unit)
{
    if (unit.empty() || value.empty())
        return std::string(value);
    if (unit == "%" || unit == "°")
        return std::string(value) + std::string(unit);
    return std::string(value) + " " + std::string(unit);
}

std::string display_state(std::string_view domain, const EntityState &s)
{
    if (s.state == "unavailable")
        return "Unavailable";
    if (s.state == "unknown" || s.state.empty())
        return "Unknown";
    if (domain == "weather")
    {
        const auto unit = s.temperature_unit.empty() ? std::string("°C") : s.temperature_unit;
        const auto degrees = format_number(s.temperature, unit);
        return weather_words(s.state) + (degrees.empty() ? "" : " · " + with_unit(degrees, unit));
    }
    if (domain == "climate")
    {
        const std::string mode = s.state != "off" && !s.hvac_action.empty()
                                     ? sentence(s.hvac_action)
                                     : sentence(s.state);
        // Climate attributes carry no unit; the house's own sensors say which.
        const auto degrees = format_number(s.current_temperature, "°C");
        return mode + (degrees.empty() ? "" : " · " + with_unit(degrees, "°"));
    }
    if (domain == "media_player")
    {
        const bool active = s.state == "playing" || s.state == "paused";
        if (active && !s.media_title.empty())
            return (s.state == "paused" ? "Paused · " : "") + s.media_title +
                   (s.media_artist.empty() ? "" : " · " + s.media_artist);
        return sentence(s.state);
    }
    if (domain == "person" || domain == "device_tracker")
        return s.state == "home" ? "Home" : s.state == "not_home" ? "Away" : sentence(s.state);
    if (domain == "binary_sensor")
        return binary_words(s.device_class, s.state == "on");
    if (domain == "scene")
        return "Activate";
    if (domain == "script")
        return s.state == "on" ? "Running" : "Run";
    if (toggles(domain) && (s.state == "on" || s.state == "off"))
        return s.state == "on" ? "On" : "Off";
    const auto number = format_number(s.state, s.unit);
    return number.empty() ? sentence(s.state) : number;
}

std::string display_name(const HomeEntitySpec &spec, const EntityState *state,
                         std::string_view group_title)
{
    if (!spec.label.empty())
        return spec.label;
    if (state != nullptr && !state->friendly_name.empty())
    {
        const auto &name = state->friendly_name;
        const auto room = lower(group_title);
        if (!room.empty() && name.size() > room.size() + 1 &&
            lower(name.substr(0, room.size())) == room && name[room.size()] == ' ')
        {
            std::string rest = name.substr(room.size() + 1);
            rest[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(rest[0])));
            return rest;
        }
        return name;
    }
    const auto dot = spec.entity_id.find('.');
    return sentence(std::string_view(spec.entity_id).substr(dot + 1));
}

HomeEntity build_entity(const HomeEntitySpec &spec, const EntityState *state,
                        std::string_view group_title)
{
    HomeEntity entity;
    entity.entity_id = spec.entity_id;
    entity.domain = domain_of(spec.entity_id);
    entity.name = display_name(spec, state, group_title);
    entity.controllable = controllable(entity.domain);
    if (state == nullptr)
    {
        entity.state = "—";
        return entity;
    }
    entity.available = state->state != "unavailable" && state->state != "unknown";
    entity.state = display_state(entity.domain, *state);
    entity.on = (toggles(entity.domain) || entity.domain == "script") && state->state == "on";
    // Only a plain number takes the unit; worded states carry their own.
    if (entity.domain == "sensor" || entity.domain == "number" || entity.domain == "input_number")
        if (!format_number(state->state, state->unit).empty())
            entity.unit = state->unit;
    return entity;
}

std::vector<HomeGroup> build_groups(const HomeConfig &config, const StateTable &table)
{
    std::vector<HomeGroup> groups;
    groups.reserve(config.groups.size());
    for (const auto &spec : config.groups)
    {
        HomeGroup group;
        group.title = spec.title;
        for (const auto &entity : spec.entities)
            group.entities.push_back(
                build_entity(entity, table.find(entity.entity_id), spec.title));
        groups.push_back(std::move(group));
    }
    return groups;
}

Message parse_message(std::string_view text)
{
    Message m;
    m.type = scalar_text(field(text, "type")).substr(0, 32);
    const auto id = field(text, "id");
    if (!id.empty() && id.size() < 10 &&
        id.find_first_not_of("0123456789") == std::string_view::npos)
        m.id = std::atoi(std::string(id).c_str());
    m.success = field(text, "success") == "true";
    m.ha_version = scalar_text(field(text, "ha_version")).substr(0, 32);
    const auto error = field(text, "error");
    m.error = scalar_text(error.empty() ? field(text, "message") : field(error, "message"))
                  .substr(0, 160);
    m.event = field(text, "event");
    return m;
}

int backoff_seconds(int attempt)
{
    return attempt >= 5 ? 30 : std::min(30, 1 << std::max(attempt, 0));
}
std::string websocket_key(const std::array<std::uint8_t, 16> &nonce)
{
    static constexpr char kDigits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < nonce.size(); i += 3)
    {
        const std::uint32_t a = nonce[i];
        const std::uint32_t b = i + 1 < nonce.size() ? nonce[i + 1] : 0;
        const std::uint32_t c = i + 2 < nonce.size() ? nonce[i + 2] : 0;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        out += kDigits[(triple >> 18) & 63];
        out += kDigits[(triple >> 12) & 63];
        out += i + 1 < nonce.size() ? kDigits[(triple >> 6) & 63] : '=';
        out += i + 2 < nonce.size() ? kDigits[triple & 63] : '=';
    }
    return out;
}
} // namespace home_wire

namespace
{
// A minimal RFC 6455 client for one LAN server: numeric IPv4, no TLS, text
// frames. The companion's socket is private to companion.cpp, so this keeps
// its console rules (SO_NBIO, bounded waits) without sharing its code.
class HomeSocket
{
  public:
    explicit HomeSocket(std::atomic<bool> &stopping) : stopping_(stopping)
    {
    }
    ~HomeSocket()
    {
        close();
    }
    void close()
    {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = -1;
        incoming_.clear();
        fragment_.clear();
        fragment_open_ = false;
    }
    bool open(const std::string &host, std::uint16_t port)
    {
        close();
        if (connect_to(host, port) && upgrade(host, port))
        {
            last_receive = sys::monotonic_us();
            return true;
        }
        close();
        return false;
    }
    // Waits up to timeout_ms for data, then hands each whole text message to handler.
    bool tick(int timeout_ms)
    {
        if (fd_ < 0)
            return false;
        pollfd p{fd_, POLLIN, 0};
        const int n = poll(&p, 1, timeout_ms);
        if (n < 0)
            return errno == EINTR;
        if (n && (p.revents & POLLIN))
            return read_frames();
        return !(p.revents & (POLLERR | POLLHUP | POLLNVAL));
    }
    bool send_text(const std::string &text)
    {
        return send_frame(1, reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
    }
    std::function<bool(std::string_view)> handler;
    std::int64_t last_receive = 0;

  private:
    bool set_nonblocking()
    {
#ifdef __PROSPERO__
        // PS5 titles refuse F_SETFL and FIONBIO with EACCES; SO_NBIO is the
        // switch sandboxed titles may use (see companion.cpp).
        constexpr int kConsoleSoNbio = 0x1200;
        const int enabled = 1;
        return setsockopt(fd_, SOL_SOCKET, kConsoleSoNbio, &enabled, sizeof(enabled)) == 0;
#else
        int enabled = 1;
        return ioctl(fd_, FIONBIO, &enabled) == 0;
#endif
    }
    bool connect_to(const std::string &host, std::uint16_t port)
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
            return false;
#ifdef SO_NOSIGPIPE
        int no_sigpipe = 1;
        setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
        if (!set_nonblocking())
            return false;
        sockaddr_in address{};
#if defined(__FreeBSD__) || defined(__APPLE__)
        address.sin_len = sizeof(address);
#endif
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
            return false;
        if (::connect(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
            return true;
        if (errno != EINPROGRESS)
            return false;
        const auto deadline = sys::monotonic_us() + 3000000;
        while (!stopping_ && sys::monotonic_us() < deadline)
        {
            pollfd p{fd_, POLLOUT, 0};
            const int ready = poll(&p, 1, 100);
            if (ready < 0 && errno != EINTR)
                return false;
            if (ready > 0)
            {
                int error = 0;
                socklen_t size = sizeof(error);
                return getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0 &&
                       !(p.revents & (POLLERR | POLLHUP | POLLNVAL));
            }
        }
        return false;
    }
    bool write_bytes(const std::uint8_t *data, std::size_t size)
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
            const auto count = send(fd_, data + position, size - position, flags);
            if (count > 0)
                position += static_cast<std::size_t>(count);
            else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            {
                pollfd p{fd_, POLLOUT, 0};
                poll(&p, 1, 100);
            }
            else
                return false;
        }
        return position == size;
    }
    // Masks and keys need variety, not secrecy (arc4random is a null import
    // on PS5; companion.cpp records why).
    static std::uint32_t random32()
    {
        static std::atomic<std::uint64_t> counter{0};
        std::uint64_t z = counter.fetch_add(0x9e3779b97f4a7c15ULL) +
                          static_cast<std::uint64_t>(sys::monotonic_us());
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return static_cast<std::uint32_t>(z ^ (z >> 31));
    }
    // The accept hash is not checked: the server is a fixed LAN address and
    // the token, not the handshake, is what authenticates.
    bool upgrade(const std::string &host, std::uint16_t port)
    {
        std::array<std::uint8_t, 16> nonce{};
        for (std::size_t i = 0; i < nonce.size(); i += 4)
        {
            const std::uint32_t word = random32();
            for (std::size_t b = 0; b < 4; ++b)
                nonce[i + b] = static_cast<std::uint8_t>(word >> (8 * b));
        }
        const std::string request =
            "GET /api/websocket HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
            "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: " +
            home_wire::websocket_key(nonce) + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!write_bytes(reinterpret_cast<const std::uint8_t *>(request.data()), request.size()))
            return false;
        std::string response;
        const auto deadline = sys::monotonic_us() + 3000000;
        while (!stopping_ && sys::monotonic_us() < deadline && response.size() < 8192)
        {
            pollfd p{fd_, POLLIN, 0};
            const int ready = poll(&p, 1, 100);
            if (ready < 0 && errno != EINTR)
                return false;
            if (ready <= 0)
                continue;
            // One byte at a time: frames coalesced after the header stay queued.
            char c = 0;
            const auto count = recv(fd_, &c, 1, 0);
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
                continue;
            if (count <= 0)
                return false;
            response += c;
            if (response.ends_with("\r\n\r\n"))
                break;
        }
        return response.starts_with("HTTP/1.1 101") && response.ends_with("\r\n\r\n");
    }
    bool send_frame(int opcode, const std::uint8_t *data, std::size_t bytes)
    {
        if (bytes > 65535)
            return false;
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
        const std::uint32_t word = random32();
        const std::array<std::uint8_t, 4> mask{
            static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8),
            static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24)};
        frame.insert(frame.end(), mask.begin(), mask.end());
        for (std::size_t i = 0; i < bytes; ++i)
            frame.push_back(data[i] ^ mask[i % 4]);
        return write_bytes(frame.data(), frame.size());
    }
    bool message(const std::uint8_t *data, std::size_t size)
    {
        return !handler || handler(std::string_view(reinterpret_cast<const char *>(data), size));
    }
    bool read_frames()
    {
        std::array<std::uint8_t, 16384> buffer{};
        const auto count = recv(fd_, buffer.data(), buffer.size(), 0);
        if (count <= 0)
            return count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
        last_receive = sys::monotonic_us();
        incoming_.insert(incoming_.end(), buffer.begin(), buffer.begin() + count);
        if (incoming_.size() > kMaxMessage + buffer.size())
            return false;
        std::size_t position = 0;
        while (incoming_.size() - position >= 2)
        {
            const auto *frame = incoming_.data() + position;
            const bool fin = (frame[0] & 0x80) != 0;
            const int opcode = frame[0] & 15;
            // Servers never mask; reserved bits mean an extension we did not ask for.
            if ((frame[0] & 0x70) != 0 || (frame[1] & 0x80) != 0)
                return false;
            std::uint64_t length = frame[1] & 127;
            std::size_t header = 2;
            if (length == 126)
            {
                if (incoming_.size() - position < 4)
                    break;
                length = (static_cast<std::uint64_t>(frame[2]) << 8) | frame[3];
                header = 4;
            }
            else if (length == 127)
            {
                if (incoming_.size() - position < 10)
                    break;
                length = 0;
                for (int i = 2; i < 10; ++i)
                    length = (length << 8) | frame[i];
                header = 10;
            }
            if (length > kMaxMessage || (opcode >= 8 && (!fin || length > 125)))
                return false;
            if (incoming_.size() - position < header + length)
                break;
            const auto *payload = frame + header;
            const auto size = static_cast<std::size_t>(length);
            if (opcode == 8)
            {
                send_frame(8, payload, std::min<std::size_t>(size, 2));
                return false;
            }
            if (opcode == 9 && !send_frame(10, payload, size))
                return false;
            if (opcode == 0)
            {
                if (!fragment_open_ || fragment_.size() + size > kMaxMessage)
                    return false;
                fragment_.insert(fragment_.end(), payload, payload + size);
                if (fin)
                {
                    fragment_open_ = false;
                    if (!message(fragment_.data(), fragment_.size()))
                        return false;
                    fragment_.clear();
                }
            }
            else if (opcode == 1 || opcode == 2)
            {
                if (fragment_open_)
                    return false;
                if (fin)
                {
                    if (opcode == 1 && !message(payload, size))
                        return false;
                }
                else
                {
                    fragment_open_ = true;
                    fragment_.assign(payload, payload + size);
                }
            }
            else if (opcode != 9 && opcode != 10)
                return false;
            position += header + size;
        }
        if (position)
            incoming_.erase(incoming_.begin(),
                            incoming_.begin() + static_cast<std::ptrdiff_t>(position));
        return true;
    }

    std::atomic<bool> &stopping_;
    int fd_ = -1;
    bool fragment_open_ = false;
    std::vector<std::uint8_t> incoming_, fragment_;
};
} // namespace

struct HomeAssistant::Impl
{
    HomeConfig config;
    std::atomic<bool> stopping{false};
    bool started = false;
    pthread_t thread{};
    mutable std::mutex mutex;
    HomeSnapshot state;
    std::deque<HomeCommand> queued;
    std::uint64_t result_id = 0;

    // Worker-only.
    home_wire::StateTable table;
    struct Pending
    {
        HomeCommand command;
        std::int64_t deadline = 0;
    };
    std::map<int, Pending> pending;
    int next_id = 1, subscribe_id = 0;
    bool authenticated = false, refused = false;

    void publish(HomeLink link, std::string status)
    {
        std::lock_guard<std::mutex> lock(mutex);
        state.link = link;
        state.connected = link == HomeLink::connected;
        state.status = std::move(status);
        ++state.revision;
    }
    void publish_states()
    {
        auto groups = home_wire::build_groups(config, table);
        std::lock_guard<std::mutex> lock(mutex);
        state.groups = std::move(groups);
        ++state.revision;
    }
    void answer(const HomeCommand &command, bool ok, std::string message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        HomeResult result;
        result.id = ++result_id;
        result.entity_id = command.entity_id;
        result.service = command.service;
        result.ok = ok;
        result.message = std::move(message);
        state.results.push_back(std::move(result));
        if (state.results.size() > kMaxResults)
            state.results.erase(state.results.begin());
        ++state.revision;
    }
    std::deque<HomeCommand> take_commands()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::deque<HomeCommand> out;
        out.swap(queued);
        return out;
    }
    void fail_pending(const char *why)
    {
        for (const auto &[id, call] : pending)
            answer(call.command, false, why);
        pending.clear();
    }

    bool on_message(HomeSocket &socket, std::string_view text)
    {
        const auto m = home_wire::parse_message(text);
        if (m.type == "auth_required")
            return socket.send_text(home_wire::auth_message(config.token));
        if (m.type == "auth_ok")
        {
            authenticated = true;
            sys::log("[Afterglow] home assistant authenticated version=%s", m.ha_version.c_str());
            subscribe_id = next_id++;
            publish(HomeLink::connected, "Connected");
            return socket.send_text(home_wire::subscribe_message(subscribe_id, config));
        }
        if (m.type == "auth_invalid")
        {
            refused = true;
            sys::log("[Afterglow] home assistant refused the token");
            return false;
        }
        if (m.type == "event" && m.id == subscribe_id)
        {
            if (table.apply(m.event))
                publish_states();
            return true;
        }
        if (m.type == "result" && m.id == subscribe_id)
        {
            if (!m.success)
                sys::log("[Afterglow] home assistant subscribe refused: %s", m.error.c_str());
            return m.success;
        }
        if (m.type == "result")
        {
            const auto it = pending.find(m.id);
            if (it != pending.end())
            {
                answer(it->second.command, m.success,
                       m.success ? std::string()
                                 : (m.error.empty() ? std::string("Refused") : m.error));
                pending.erase(it);
            }
        }
        return true;
    }

    bool send_commands(HomeSocket &socket, std::int64_t now)
    {
        for (const auto &command : take_commands())
        {
            if (!authenticated)
            {
                answer(command, false, "Home Assistant is offline");
                continue;
            }
            const int id = next_id++;
            const auto message = home_wire::service_message(id, command);
            if (message.empty())
            {
                answer(command, false, "Not a control");
                continue;
            }
            if (!socket.send_text(message))
            {
                answer(command, false, "Connection lost");
                return false;
            }
            pending[id] = {command, now + 10000000};
        }
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (now < it->second.deadline)
            {
                ++it;
                continue;
            }
            answer(it->second.command, false, "No answer from Home Assistant");
            it = pending.erase(it);
        }
        return true;
    }

    // One connection from open to loss.
    void session(HomeSocket &socket)
    {
        authenticated = false;
        next_id = 1;
        subscribe_id = 0;
        table.clear();
        if (!socket.open(config.host, config.port))
            return;
        const auto opened = sys::monotonic_us();
        auto next_ping = opened + 30000000;
        bool okay = true;
        while (okay && !stopping)
        {
            okay = socket.tick(50);
            const auto now = sys::monotonic_us();
            if (!okay)
                break;
            if (!authenticated && now - opened > 10000000)
                break;
            // Pings every 30 s; nothing back for 75 s means a dead link.
            if (now - socket.last_receive > 75000000)
                break;
            if (authenticated && now >= next_ping)
            {
                okay = socket.send_text(home_wire::ping_message(next_id++));
                next_ping = now + 30000000;
            }
            okay = okay && send_commands(socket, now);
        }
        socket.close();
    }

    void run()
    {
        HomeSocket socket(stopping);
        socket.handler = [&](std::string_view text) { return on_message(socket, text); };
        int attempt = 0;
        while (!stopping)
        {
            publish(HomeLink::connecting, "Connecting to Home Assistant");
            session(socket);
            const bool was_connected = authenticated;
            fail_pending("Connection lost");
            if (refused)
                break;
            attempt = was_connected ? 0 : attempt + 1;
            const int wait = home_wire::backoff_seconds(attempt);
            publish(HomeLink::offline,
                    "Home Assistant is offline · retrying in " + std::to_string(wait) + " s");
            const auto until = sys::monotonic_us() + static_cast<std::int64_t>(wait) * 1000000;
            while (!stopping && sys::monotonic_us() < until)
            {
                for (const auto &command : take_commands())
                    answer(command, false, "Home Assistant is offline");
                usleep(50000);
            }
        }
        if (refused)
        {
            publish(HomeLink::auth_failed, "Home Assistant refused the token");
            while (!stopping)
            {
                for (const auto &command : take_commands())
                    answer(command, false, "Home Assistant refused the token");
                usleep(50000);
            }
        }
    }
};

HomeAssistant::HomeAssistant() : impl_(std::make_unique<Impl>())
{
}
HomeAssistant::~HomeAssistant()
{
    stop();
    join();
}

bool HomeAssistant::start(const HomeConfig &config)
{
    auto &p = *impl_;
    in_addr address{};
    if (p.started || !config.configured() || config.token.size() > kHomeMaxToken ||
        inet_pton(AF_INET, config.host.c_str(), &address) != 1)
        return false;
    p.config = config;
    p.stopping = false;
    p.refused = false;
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.state = {};
        p.state.configured = true;
        p.state.link = HomeLink::connecting;
        p.state.status = "Connecting to Home Assistant";
        p.state.revision = 1;
        p.state.groups = home_wire::build_groups(p.config, p.table);
    }
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
        return false;
    int result = pthread_attr_setstacksize(&attr, 512 * 1024);
    if (!result)
        result = pthread_create(
            &p.thread, &attr,
            [](void *v) -> void *
            {
                static_cast<Impl *>(v)->run();
                return nullptr;
            },
            &p);
    pthread_attr_destroy(&attr);
    p.started = result == 0;
    return p.started;
}
void HomeAssistant::stop()
{
    impl_->stopping = true;
}
void HomeAssistant::join()
{
    if (impl_->started)
    {
        pthread_join(impl_->thread, nullptr);
        impl_->started = false;
    }
}
HomeSnapshot HomeAssistant::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}
std::uint64_t HomeAssistant::revision() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state.revision;
}
void HomeAssistant::send(const HomeCommand &command)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->queued.size() >= kMaxQueued)
        impl_->queued.pop_front();
    impl_->queued.push_back(command);
}
} // namespace hui::afterglow
