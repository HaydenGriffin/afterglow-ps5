// Afterglow - Companion account mirroring and static-delay wire tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include <gtest/gtest.h>

using namespace hui::afterglow;

TEST(AfterglowCompanionAccount, DevicePreferencesMergeKeepsLgDataAndFavourites)
{
    const std::string account =
        "{\"unrelated\":{\"keep\":42},\"visualizer_favorites\":[\"A\"],"
        "\"vizman\":{\"mirrored\":[\"A\"],\"favoritesMeta\":{\"A\":{\"on\":true,\"at\":1}},"
        "\"devices\":{\"tv-y4cnnfdu\":{\"fpsCap\":30},\"ps5-old\":{\"brightness\":10}}}}";
    EXPECT_EQ(companion_wire::device_preferences(account, "tv-y4cnnfdu"), "{\"fpsCap\":30}");
    EXPECT_EQ(companion_wire::device_preferences(account, "ps5-x"), "");
    EXPECT_EQ(companion_wire::device_preferences("{}", "ps5-x"), "");
    const auto merged =
        companion_wire::merge_device_preferences(account, "ps5-x", "{\"brightness\":40}");
    EXPECT_EQ(companion_wire::device_preferences(merged, "ps5-x"), "{\"brightness\":40}");
    EXPECT_EQ(companion_wire::device_preferences(merged, "tv-y4cnnfdu"), "{\"fpsCap\":30}");
    EXPECT_EQ(companion_wire::device_preferences(merged, "ps5-old"), "{\"brightness\":10}");
    EXPECT_NE(merged.find("\"unrelated\":{\"keep\":42}"), std::string::npos);
    EXPECT_NE(merged.find("\"visualizer_favorites\":[\"A\"]"), std::string::npos);
    EXPECT_NE(merged.find("\"favoritesMeta\":{\"A\":{\"on\":true,\"at\":1}}"), std::string::npos);
    // A first write creates vizman.devices.
    const auto fresh = companion_wire::merge_device_preferences("{}", "ps5-x", "{\"a\":1}");
    EXPECT_EQ(fresh, "{\"vizman\":{\"devices\":{\"ps5-x\":{\"a\":1}}}}");
    const auto again = companion_wire::merge_device_preferences(merged, "ps5-x", "{\"a\":2}");
    EXPECT_EQ(companion_wire::device_preferences(again, "ps5-x"), "{\"a\":2}");
}

TEST(AfterglowCompanionAccount, StaticDelayDecodesValueOrDefaultAndProtocolKeys)
{
    std::string key;
    int ms = -1;
    EXPECT_TRUE(companion_wire::decode_static_delay(
        "{\"player_id\":\"p\",\"values\":{\"volume_control\":{\"value\":null},"
        "\"sendspin_static_delay\":{\"key\":\"sendspin_static_delay\",\"type\":\"integer\","
        "\"default_value\":0,\"range\":[0,5000],\"value\":120}}}",
        key, ms));
    EXPECT_EQ(key, "sendspin_static_delay");
    EXPECT_EQ(ms, 120);
    EXPECT_TRUE(companion_wire::decode_static_delay(
        "{\"values\":{\"sendspin_static_delay\":{\"default_value\":0,\"value\":null}}}", key, ms));
    EXPECT_EQ(ms, 0);
    EXPECT_TRUE(companion_wire::decode_static_delay(
        "{\"values\":{\"afterglow-ps5-232||protocol||sendspin_static_delay\":"
        "{\"default_value\":0,\"value\":9000}}}",
        key, ms));
    EXPECT_EQ(key, "afterglow-ps5-232||protocol||sendspin_static_delay");
    EXPECT_EQ(ms, 5000);
    // The PS5's universal player while its Sendspin side is offline: no setting.
    EXPECT_FALSE(companion_wire::decode_static_delay(
        "{\"values\":{\"afterglow-ps5-232||protocol||enabled\":{\"value\":true}}}", key, ms));
    EXPECT_FALSE(companion_wire::decode_static_delay("{\"values\":{}}", key, ms));
    EXPECT_FALSE(companion_wire::decode_static_delay("", key, ms));
}

TEST(AfterglowCompanionAccount, StaticDelayRequestClampsAndNeedsNoQueue)
{
    auto wire =
        companion_wire::static_delay_request("afterglow-ps5-232", "sendspin_static_delay", 7000);
    EXPECT_EQ(wire.command, "config/players/save");
    EXPECT_EQ(wire.args, "{\"player_id\":\"afterglow-ps5-232\",\"values\":"
                         "{\"sendspin_static_delay\":5000}}");
    wire = companion_wire::static_delay_request("p", "k", -40);
    EXPECT_EQ(wire.args, "{\"player_id\":\"p\",\"values\":{\"k\":0}}");
    EXPECT_FALSE(companion_wire::static_delay_request("", "k", 10).error.empty());

    CompanionSnapshot target; // not connected, no queue: a player setting still works
    CompanionAction action;
    action.kind = CompanionActionKind::static_delay;
    action.value = 250;
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty())
        << "unavailable until the setting has been read";
    target.static_delay_player = "afterglow-ps5-232";
    target.static_delay_key = "sendspin_static_delay";
    wire = companion_wire::action_request(action, target);
    EXPECT_TRUE(wire.error.empty());
    EXPECT_EQ(wire.command, "config/players/save");
    EXPECT_NE(wire.args.find("\"sendspin_static_delay\":250"), std::string::npos);
}

TEST(AfterglowCompanionAccount, AccountMirrorDefaultsBeforeAnyRead)
{
    Companion companion;
    companion.set_account_device("ps5-afterglow-ps5-232");
    EXPECT_EQ(companion.account_revision(), 0u);
    EXPECT_EQ(companion.account_preferences(), "");
    companion.save_account_preferences("not json");
    companion.save_account_preferences("{\"brightness\":40}");
    companion.set_own_player("afterglow-ps5-232", "upd19db919");
    EXPECT_EQ(companion.snapshot().own_player_id, "upd19db919");
    EXPECT_EQ(companion.snapshot().static_delay_ms, -1);
}

TEST(AfterglowCompanionAccount, StaticDelayActionGoesToThePlayerItWasSetFor)
{
    CompanionSnapshot target;
    target.static_delay_player = "new-player";
    target.static_delay_key = "sendspin_static_delay";
    CompanionAction action;
    action.kind = CompanionActionKind::static_delay;
    action.value = 120;
    action.text = "old-player";
    action.option = "old||protocol||sendspin_static_delay";
    const auto wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.args, "{\"player_id\":\"old-player\",\"values\":"
                         "{\"old||protocol||sendspin_static_delay\":120}}");
}
