// Afterglow - Home Assistant glance: config, wire messages, state formatting and the sheet.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/home_assistant.hpp"
#include "afterglow/home_sheet.hpp"
#include "afterglow/presentation.hpp"
#include "concept_fixture.hpp"

#include <cmath>

namespace
{
using hui::Action;
using hui::Direction;
using hui::afterglow::HomeCommand;
using hui::afterglow::HomeConfig;
using hui::afterglow::HomeEntitySpec;
using hui::afterglow::HomeGroupSpec;
using hui::afterglow::HomeLink;
using hui::afterglow::presentation;
namespace wire = hui::afterglow::home_wire;

// ---- afterglow.conf ----

TEST(AfterglowHomeConfig, ParsesHostPortTokenAndGroups)
{
    const auto config = wire::parse_config(
        "host=192.0.2.10\nha_host=192.0.2.20\r\nha_port=8124\nha_token= abc.def \n"
        "ha_group=Living room: sensor.living_room_temperature=Temperature , light.living_room\n"
        "ha_group=Outside:weather.home\n"
        "ma_token=not-ours\n");
    EXPECT_EQ(config.host, "192.0.2.20");
    EXPECT_EQ(config.port, 8124);
    EXPECT_EQ(config.token, "abc.def");
    ASSERT_EQ(config.groups.size(), 2u);
    EXPECT_EQ(config.groups[0].title, "Living room");
    ASSERT_EQ(config.groups[0].entities.size(), 2u);
    EXPECT_EQ(config.groups[0].entities[0].entity_id, "sensor.living_room_temperature");
    EXPECT_EQ(config.groups[0].entities[0].label, "Temperature");
    EXPECT_EQ(config.groups[0].entities[1].entity_id, "light.living_room");
    EXPECT_TRUE(config.groups[0].entities[1].label.empty());
    EXPECT_EQ(config.groups[1].entities[0].entity_id, "weather.home");
    EXPECT_TRUE(config.configured());
}

TEST(AfterglowHomeConfig, DefaultsAndMissingKeysLeaveItUnconfigured)
{
    HomeConfig config;
    EXPECT_EQ(config.port, 8123);
    EXPECT_FALSE(config.configured());
    config = wire::parse_config("ha_host=192.0.2.20\nha_token=t\n");
    EXPECT_FALSE(config.configured()) << "no groups, nothing to show";
    config = wire::parse_config("ha_host=192.0.2.20\nha_group=A:light.a\n");
    EXPECT_FALSE(config.configured()) << "no token";
}

TEST(AfterglowHomeConfig, RefusesBadValues)
{
    HomeConfig config;
    EXPECT_FALSE(wire::apply_config(config, "ha_port", "0"));
    EXPECT_FALSE(wire::apply_config(config, "ha_port", "70000"));
    EXPECT_FALSE(wire::apply_config(config, "ha_port", "81x"));
    EXPECT_EQ(config.port, 8123);
    EXPECT_FALSE(wire::apply_config(config, "ha_token", std::string(2049, 'x')));
    EXPECT_TRUE(wire::apply_config(config, "ha_token", std::string(2048, 'x')));
    EXPECT_FALSE(wire::apply_config(config, "ha_token", ""));
    EXPECT_FALSE(wire::apply_config(config, "ha_host", std::string(65, '1')));
    EXPECT_FALSE(wire::apply_config(config, "host", "192.0.2.1")) << "not a Home Assistant key";
}

TEST(AfterglowHomeConfig, GroupsAreBounded)
{
    std::string text;
    for (int g = 0; g < 10; ++g)
    {
        text += "ha_group=Group " + std::to_string(g) + ":";
        for (int e = 0; e < 15; ++e)
            text += (e ? "," : "") + std::string("sensor.s") + std::to_string(e);
        text += "\n";
    }
    const auto config = wire::parse_config(text);
    ASSERT_EQ(config.groups.size(), hui::afterglow::kHomeMaxGroups);
    for (const auto &group : config.groups)
        EXPECT_EQ(group.entities.size(), hui::afterglow::kHomeMaxEntities);
}

TEST(AfterglowHomeConfig, GroupSyntaxSkipsInvalidEntities)
{
    HomeGroupSpec group;
    EXPECT_FALSE(wire::parse_group("no colon here", group));
    EXPECT_FALSE(wire::parse_group(":light.a", group)) << "a heading is required";
    EXPECT_FALSE(wire::parse_group("Room:Light.A,not an id,.x,light.", group));
    ASSERT_TRUE(wire::parse_group("Room:Light.A,light.ok,sensor.bad-id,switch.fan_2=Fan", group));
    ASSERT_EQ(group.entities.size(), 2u);
    EXPECT_EQ(group.entities[0].entity_id, "light.ok");
    EXPECT_EQ(group.entities[1].entity_id, "switch.fan_2");
    EXPECT_EQ(group.entities[1].label, "Fan");
    EXPECT_TRUE(wire::valid_entity_id("input_boolean.tv_in_whole_home_music"));
    EXPECT_FALSE(wire::valid_entity_id("light"));
    EXPECT_FALSE(wire::valid_entity_id("light.a\"b"));
}

// ---- requests ----

TEST(AfterglowHomeWire, AuthAndSubscribeMessages)
{
    EXPECT_EQ(wire::auth_message("a\"b"), R"({"type":"auth","access_token":"a\"b"})");
    HomeConfig config;
    config.groups.push_back({"A", {{"light.a", ""}, {"sensor.t", "T"}}});
    config.groups.push_back({"B", {{"light.a", "Again"}, {"weather.home", ""}}});
    EXPECT_EQ(
        wire::subscribe_message(1, config),
        R"({"id":1,"type":"subscribe_entities","entity_ids":["light.a","sensor.t","weather.home"]})");
    EXPECT_EQ(wire::ping_message(7), R"({"id":7,"type":"ping"})");
}

TEST(AfterglowHomeWire, ServiceCallsOnlyForControls)
{
    HomeCommand c;
    c.entity_id = "light.living_room";
    c.service = "toggle";
    EXPECT_EQ(
        wire::service_message(3, c),
        R"({"id":3,"type":"call_service","domain":"light","service":"toggle","target":{"entity_id":"light.living_room"}})");
    c.entity_id = "input_boolean.guest";
    c.service = "turn_off";
    EXPECT_FALSE(wire::service_message(4, c).empty());
    c.entity_id = "scene.movie";
    c.service = "turn_on";
    EXPECT_NE(wire::service_message(5, c).find(R"("domain":"scene","service":"turn_on")"),
              std::string::npos);
    c.service = "toggle";
    EXPECT_TRUE(wire::service_message(6, c).empty()) << "scenes only turn on";
    c.entity_id = "sensor.temperature";
    c.service = "turn_on";
    EXPECT_TRUE(wire::service_message(7, c).empty()) << "sensors are read-only";
    c.entity_id = "lock.front_door";
    EXPECT_TRUE(wire::service_message(8, c).empty()) << "locks are never pressed from the sofa";
    c.entity_id = "light.a\",\"domain\":\"lock";
    c.service = "toggle";
    EXPECT_TRUE(wire::service_message(9, c).empty());
    c.entity_id = "light.a";
    c.service = "delete";
    EXPECT_TRUE(wire::service_message(10, c).empty());
}

TEST(AfterglowHomeWire, ParsesServerMessages)
{
    auto m = wire::parse_message(R"({"type":"auth_required","ha_version":"2026.10.1"})");
    EXPECT_EQ(m.type, "auth_required");
    EXPECT_EQ(m.ha_version, "2026.10.1");
    m = wire::parse_message(
        R"({"type":"auth_invalid","message":"Invalid access token or password"})");
    EXPECT_EQ(m.type, "auth_invalid");
    EXPECT_EQ(m.error, "Invalid access token or password");
    m = wire::parse_message(
        R"({"id":12,"type":"result","success":false,"error":{"code":"unauthorized","message":"Unauthorized"}})");
    EXPECT_EQ(m.id, 12);
    EXPECT_FALSE(m.success);
    EXPECT_EQ(m.error, "Unauthorized");
    m = wire::parse_message(R"({"id":2,"type":"result","success":true,"result":null})");
    EXPECT_TRUE(m.success);
    m = wire::parse_message(R"({"id":1,"type":"event","event":{"a":{}}})");
    EXPECT_EQ(m.event, R"({"a":{}})");
    EXPECT_EQ(wire::parse_message("not json").type, "");
}

TEST(AfterglowHomeWire, BackoffDoublesToThirtySeconds)
{
    EXPECT_EQ(wire::backoff_seconds(0), 1);
    EXPECT_EQ(wire::backoff_seconds(1), 2);
    EXPECT_EQ(wire::backoff_seconds(4), 16);
    EXPECT_EQ(wire::backoff_seconds(5), 30);
    EXPECT_EQ(wire::backoff_seconds(50), 30);
}

// ---- formatting ----

TEST(AfterglowHomeFormat, Numbers)
{
    EXPECT_EQ(wire::format_number("19.46", "°C"), "19.5");
    EXPECT_EQ(wire::format_number("22", "°C"), "22.0");
    EXPECT_EQ(wire::format_number("70.22", "%"), "70");
    EXPECT_EQ(wire::format_number("1038.8", "hPa"), "1039");
    EXPECT_EQ(wire::format_number("2.3333333", "lx"), "2.3");
    EXPECT_EQ(wire::format_number("2", "μg/m³"), "2");
    EXPECT_EQ(wire::format_number("0.0", "W"), "0");
    EXPECT_EQ(wire::format_number("-0.04", "°C"), "0.0");
    EXPECT_EQ(wire::format_number("on", ""), "");
    EXPECT_EQ(wire::format_number("0-10%", ""), "");
    EXPECT_EQ(wire::format_number("nan", ""), "");
    EXPECT_EQ(wire::format_number("0x10", ""), "");
    EXPECT_EQ(wire::with_unit("19.5", "°C"), "19.5 °C");
    EXPECT_EQ(wire::with_unit("70", "%"), "70%");
    EXPECT_EQ(wire::with_unit("Rain", ""), "Rain");
}

TEST(AfterglowHomeFormat, StatesReadAsWords)
{
    wire::EntityState s;
    s.state = "rainy";
    s.temperature = "17.3";
    s.temperature_unit = "°C";
    EXPECT_EQ(wire::display_state("weather", s), "Rain · 17.3 °C");
    s = {};
    s.state = "partlycloudy";
    EXPECT_EQ(wire::display_state("weather", s), "Partly cloudy");
    s = {};
    s.state = "heat";
    s.hvac_action = "heating";
    s.current_temperature = "18.9";
    EXPECT_EQ(wire::display_state("climate", s), "Heating · 18.9°");
    s.state = "off";
    EXPECT_EQ(wire::display_state("climate", s), "Off · 18.9°");
    s = {};
    s.state = "playing";
    s.media_title = "Slow Current";
    s.media_artist = "Preview";
    EXPECT_EQ(wire::display_state("media_player", s), "Slow Current · Preview");
    s.state = "paused";
    EXPECT_EQ(wire::display_state("media_player", s), "Paused · Slow Current · Preview");
    s = {};
    s.state = "idle";
    EXPECT_EQ(wire::display_state("media_player", s), "Idle");
    s.state = "not_home";
    EXPECT_EQ(wire::display_state("person", s), "Away");
    s.state = "home";
    EXPECT_EQ(wire::display_state("person", s), "Home");
    s.state = "Work";
    EXPECT_EQ(wire::display_state("person", s), "Work");
    s = {};
    s.state = "on";
    s.device_class = "door";
    EXPECT_EQ(wire::display_state("binary_sensor", s), "Open");
    s.device_class = "occupancy";
    s.state = "off";
    EXPECT_EQ(wire::display_state("binary_sensor", s), "Clear");
    s = {};
    s.state = "on";
    EXPECT_EQ(wire::display_state("light", s), "On");
    s.state = "unavailable";
    EXPECT_EQ(wire::display_state("light", s), "Unavailable");
    s.state = "unknown";
    EXPECT_EQ(wire::display_state("sensor", s), "Unknown");
    s.state = "Spaghetti bolognese";
    EXPECT_EQ(wire::display_state("sensor", s), "Spaghetti bolognese");
    s.state = "idle";
    EXPECT_EQ(wire::display_state("sensor", s), "Idle");
    s.state = "19.46";
    s.unit = "°C";
    EXPECT_EQ(wire::display_state("sensor", s), "19.5");
    s = {};
    s.state = "off";
    EXPECT_EQ(wire::display_state("script", s), "Run");
}

TEST(AfterglowHomeFormat, NamesDropTheRoom)
{
    wire::EntityState s;
    s.friendly_name = "Living Room Climate Temperature";
    EXPECT_EQ(wire::display_name({"sensor.x", ""}, &s, "Living room"), "Climate Temperature");
    EXPECT_EQ(wire::display_name({"sensor.x", "Temperature"}, &s, "Living room"), "Temperature");
    EXPECT_EQ(wire::display_name({"sensor.x", ""}, &s, "Outside"),
              "Living Room Climate Temperature");
    s.friendly_name = "Living Room";
    EXPECT_EQ(wire::display_name({"sensor.x", ""}, &s, "Living room"), "Living Room")
        << "nothing would be left";
    EXPECT_EQ(wire::display_name({"sensor.hot_water_status", ""}, nullptr, "Heating"),
              "Hot water status");
}

// ---- subscribe_entities stream ----

constexpr const char *kAdded = R"({"a":{
 "sensor.living_room_climate_temperature":{"s":"19.46","a":{"state_class":"measurement","unit_of_measurement":"°C","device_class":"temperature","friendly_name":"Living Room Climate Temperature"},"c":"01J","lc":1789655097.36},
 "light.living_room_lights":{"s":"off","a":{"supported_color_modes":["color_temp","xy"],"friendly_name":"Living Room Lights","entity_id":["light.a","light.b"]},"c":"01K","lc":1789655097.1},
 "weather.forecast_home":{"s":"rainy","a":{"temperature":17.3,"temperature_unit":"°C","humidity":94,"friendly_name":"Forecast Home"},"c":"01L","lc":1789655000},
 "person.ewa":{"s":"not_home","a":{"friendly_name":"Ewa","latitude":1.5,"source":"device_tracker.x"},"c":"01M","lc":1},
 "media_player.home_speakers":{"s":"playing","a":{"media_title":"Café \"Live\"","media_artist":"Artist","friendly_name":"Home speakers","group_members":["a","b"]},"c":"01N","lc":2},
 "switch.fan":{"s":"unavailable","a":{"friendly_name":"Fan"},"c":"01O","lc":3}
}})";

HomeConfig fixture_config()
{
    HomeConfig config;
    config.host = "127.0.0.1";
    config.token = "t";
    HomeGroupSpec room;
    room.title = "Living room";
    room.entities = {{"sensor.living_room_climate_temperature", ""},
                     {"light.living_room_lights", "Lights"},
                     {"switch.fan", ""},
                     {"scene.movie", "Movie time"}};
    HomeGroupSpec other;
    other.title = "Elsewhere";
    other.entities = {{"weather.forecast_home", "Weather"},
                      {"person.ewa", ""},
                      {"media_player.home_speakers", "Music"}};
    config.groups = {room, other};
    return config;
}

TEST(AfterglowHomeSnapshot, BuildsFromAddedStates)
{
    wire::StateTable table;
    ASSERT_TRUE(table.apply(kAdded));
    EXPECT_EQ(table.size(), 6u);
    const auto groups = wire::build_groups(fixture_config(), table);
    ASSERT_EQ(groups.size(), 2u);
    const auto &room = groups[0].entities;
    ASSERT_EQ(room.size(), 4u);
    EXPECT_EQ(room[0].name, "Climate Temperature");
    EXPECT_EQ(room[0].state, "19.5");
    EXPECT_EQ(room[0].unit, "°C");
    EXPECT_FALSE(room[0].controllable);
    EXPECT_TRUE(room[0].available);
    EXPECT_EQ(room[1].name, "Lights");
    EXPECT_TRUE(room[1].controllable);
    EXPECT_FALSE(room[1].on);
    EXPECT_EQ(room[1].domain, "light");
    EXPECT_FALSE(room[2].available) << "unavailable switch";
    EXPECT_EQ(room[2].state, "Unavailable");
    EXPECT_EQ(room[3].state, "—") << "never reported";
    EXPECT_FALSE(room[3].available);
    EXPECT_TRUE(room[3].controllable);
    const auto &other = groups[1].entities;
    EXPECT_EQ(other[0].state, "Rain · 17.3 °C");
    EXPECT_TRUE(other[0].unit.empty());
    EXPECT_EQ(other[1].name, "Ewa");
    EXPECT_EQ(other[1].state, "Away");
    EXPECT_EQ(other[2].state, "Café \"Live\" · Artist");
}

TEST(AfterglowHomeSnapshot, AppliesChangesAndRemovals)
{
    wire::StateTable table;
    table.apply(kAdded);
    EXPECT_TRUE(table.apply(
        R"({"c":{"light.living_room_lights":{"+":{"s":"on","a":{"brightness":120},"c":"02","lc":5}},
               "sensor.living_room_climate_temperature":{"+":{"s":"20.04","lu":6}},
               "media_player.home_speakers":{"+":{"s":"idle","lc":7},"-":{"a":["media_title","media_artist"]}}}})"));
    const auto *light = table.find("light.living_room_lights");
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(light->state, "on");
    EXPECT_EQ(light->friendly_name, "Living Room Lights") << "kept across a partial change";
    const auto groups = wire::build_groups(fixture_config(), table);
    EXPECT_TRUE(groups[0].entities[1].on);
    EXPECT_EQ(groups[0].entities[0].state, "20.0");
    EXPECT_EQ(groups[1].entities[2].state, "Idle");
    EXPECT_TRUE(table.find("media_player.home_speakers")->media_title.empty());

    EXPECT_FALSE(table.apply(R"({"c":{"light.unknown":{"+":{"s":"on"}}}})"))
        << "changes for entities never added are ignored";
    EXPECT_TRUE(table.apply(R"({"r":["person.ewa"]})"));
    EXPECT_EQ(table.find("person.ewa"), nullptr);
    EXPECT_FALSE(table.apply("garbage"));
    EXPECT_FALSE(table.apply(R"({"a":{"bad id":{"s":"on"}}})"));
}

TEST(AfterglowHomeSnapshot, MicroUsesTheGlyphTheFontHas)
{
    wire::StateTable table;
    table.apply(R"({"a":{"sensor.pm25":{"s":"2","a":{"unit_of_measurement":"\u03bcg/m\u00b3"}}}})");
    EXPECT_EQ(table.find("sensor.pm25")->unit, "\u00b5g/m\u00b3");
}

TEST(AfterglowHomeSnapshot, TableIsBounded)
{
    wire::StateTable table;
    std::string event = R"({"a":{)";
    for (int i = 0; i < 200; ++i)
        event += (i ? "," : "") + std::string("\"sensor.s") + std::to_string(i) + R"(":{"s":"1"})";
    event += "}}";
    table.apply(event);
    EXPECT_LE(table.size(), hui::afterglow::kHomeMaxGroups * hui::afterglow::kHomeMaxEntities);
}

TEST(AfterglowHomeClient, RefusesToStartWithoutNumericHost)
{
    hui::afterglow::HomeAssistant client;
    HomeConfig config = fixture_config();
    config.host = "homeassistant.local";
    EXPECT_FALSE(client.start(config));
    config.host = "127.0.0.1";
    config.token.clear();
    EXPECT_FALSE(client.start(config));
}

// ---- the sheet ----

class AfterglowHomeSheet : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        presentation() = {};
    }
    void TearDown() override
    {
        presentation() = {};
    }
    void connect()
    {
        auto &home = presentation().home;
        wire::StateTable table;
        table.apply(kAdded);
        home.configured = true;
        home.connected = true;
        home.link = HomeLink::connected;
        home.status = "Connected";
        home.groups = wire::build_groups(fixture_config(), table);
        ++home.revision;
    }
    void input(const hui::InputFrame &value, float dt = 0.02f)
    {
        sheet_.update(value, dt, feedback_);
    }
    void cross()
    {
        input(press(Action::confirm));
    }
    std::size_t draw_count()
    {
        hui::gfx::DrawList list;
        hui::ui::Canvas canvas{list, fonts_, 0, 0};
        sheet_.draw(canvas, 680, 200, 1144, 736);
        for (const auto &shape : list.instances())
            for (float value : shape.rect)
                EXPECT_TRUE(std::isfinite(value));
        return list.instances().size();
    }
    // The light row: heading, temperature, then the light.
    const hui::afterglow::SheetRow &light() const
    {
        return sheet_.rows().at(2);
    }
    hui::afterglow::HomeSheet sheet_;
};

TEST_F(AfterglowHomeSheet, NotConfiguredSaysHowToSetUp)
{
    sheet_.open();
    input(idle());
    EXPECT_EQ(sheet_.note(), "Not set up");
    EXPECT_FALSE(sheet_.back());
    EXPECT_FALSE(sheet_.covers_screen());
    EXPECT_GT(draw_count(), 0u);
    cross();
    EXPECT_TRUE(presentation().home_commands.empty());
}

TEST_F(AfterglowHomeSheet, NotesFollowTheLink)
{
    auto &home = presentation().home;
    home.configured = true;
    home.link = HomeLink::connecting;
    EXPECT_EQ(sheet_.note(), "Connecting…");
    home.link = HomeLink::auth_failed;
    EXPECT_EQ(sheet_.note(), "Token refused");
    home.link = HomeLink::offline;
    home.status = "Home Assistant is offline · retrying in 4 s";
    EXPECT_EQ(sheet_.note(), home.status);
    connect();
    EXPECT_EQ(sheet_.note(), "Connected · 7 entities");
    presentation().home.link = HomeLink::offline;
    EXPECT_EQ(sheet_.note(), "Offline · last known states");
    sheet_.open();
    input(idle());
    EXPECT_GT(draw_count(), 0u);
}

TEST_F(AfterglowHomeSheet, CrossTogglesALightOptimisticallyAndConfirms)
{
    connect();
    sheet_.open();
    input(idle());
    // The temperature row is first and refuses Cross.
    cross();
    EXPECT_TRUE(presentation().home_commands.empty());
    input(nav(Direction::down));
    cross();
    ASSERT_EQ(presentation().home_commands.size(), 1u);
    EXPECT_EQ(presentation().home_commands[0].entity_id, "light.living_room_lights");
    EXPECT_EQ(presentation().home_commands[0].service, "toggle");
    EXPECT_TRUE(light().chosen) << "shown on at once";
    EXPECT_EQ(light().item.value, "On");
    presentation().home_commands.clear();
    EXPECT_GT(draw_count(), 0u);

    // Home Assistant reports it on: the override gives way to the truth.
    auto &home = presentation().home;
    home.groups[0].entities[1].on = true;
    ++home.revision;
    for (int i = 0; i < 10; ++i)
        input(idle(), 1.0f);
    EXPECT_TRUE(light().chosen) << "confirmed, so the timeout no longer applies";
    cross();
    ASSERT_EQ(presentation().home_commands.size(), 1u);
    EXPECT_FALSE(light().chosen);
}

TEST_F(AfterglowHomeSheet, RefusalRevertsAndSaysWhy)
{
    connect();
    sheet_.open();
    input(nav(Direction::down));
    cross();
    ASSERT_EQ(presentation().home_commands.size(), 1u);
    hui::afterglow::HomeResult result;
    result.id = 1;
    result.entity_id = "light.living_room_lights";
    result.service = "toggle";
    result.ok = false;
    result.message = "Unauthorized";
    presentation().home.results.push_back(result);
    ++presentation().home.revision;
    input(idle());
    EXPECT_FALSE(light().chosen) << "reverted";
    EXPECT_EQ(sheet_.notice(), "Couldn’t switch Lights: Unauthorized");
    presentation().home_commands.clear();
    cross();
    ASSERT_EQ(presentation().home_commands.size(), 1u);
    EXPECT_GT(draw_count(), 0u);
}

TEST_F(AfterglowHomeSheet, UnconfirmedPressRevertsAfterTimeout)
{
    connect();
    sheet_.open();
    input(nav(Direction::down));
    cross();
    input(idle(), 1.0f);
    EXPECT_TRUE(light().chosen);
    for (int i = 0; i < 5; ++i)
        input(idle(), 1.0f);
    EXPECT_FALSE(light().chosen) << "the light never turned on";
    presentation().home_commands.clear();
    cross();
    EXPECT_EQ(presentation().home_commands.size(), 1u);
}

TEST_F(AfterglowHomeSheet, UnavailableAndOfflineRowsRefuse)
{
    connect();
    sheet_.open();
    input(nav(Direction::down));
    input(nav(Direction::down));
    cross(); // the unavailable fan
    EXPECT_TRUE(presentation().home_commands.empty());
    presentation().home.link = HomeLink::offline;
    presentation().home.connected = false;
    ++presentation().home.revision;
    input(nav(Direction::up));
    cross(); // the light, while offline
    EXPECT_TRUE(presentation().home_commands.empty());
    EXPECT_EQ(sheet_.notice(), "Home Assistant is offline");
    EXPECT_TRUE(sheet_.rows().at(3).item.disabled);
    EXPECT_EQ(sheet_.rows().at(3).item.value, "Unavailable");
    EXPECT_EQ(sheet_.rows().at(1).item.value, "19.5 °C");
}

TEST_F(AfterglowHomeSheet, ScenesTurnOn)
{
    connect();
    sheet_.open();
    for (int i = 0; i < 3; ++i)
        input(nav(Direction::down));
    // The scene was never reported, so it is unavailable; report it.
    auto &scene = presentation().home.groups[0].entities[3];
    scene.available = true;
    scene.state = "Activate";
    ++presentation().home.revision;
    input(idle());
    cross();
    ASSERT_EQ(presentation().home_commands.size(), 1u);
    EXPECT_EQ(presentation().home_commands[0].entity_id, "scene.movie");
    EXPECT_EQ(presentation().home_commands[0].service, "turn_on");
}
} // namespace

TEST(AfterglowHomeWire, WebSocketKeyIsSixteenBytesOfBase64)
{
    std::array<std::uint8_t, 16> nonce{};
    for (std::size_t i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<std::uint8_t>(i);
    // base64 of bytes 0..15, as Python's base64.b64encode(bytes(range(16))) gives.
    EXPECT_EQ(hui::afterglow::home_wire::websocket_key(nonce), "AAECAwQFBgcICQoLDA0ODw==");
    nonce.fill(0xff);
    EXPECT_EQ(hui::afterglow::home_wire::websocket_key(nonce).size(), 24u);
}
