// Afterglow - Check my setup: rows and copy, the worker, the sheet and the idle offer.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/idle.hpp"
#include "afterglow/panel.hpp"
#include "afterglow/presentation.hpp"
#include "afterglow/setup_check.hpp"
#include "afterglow/style.hpp"
#include "concept_fixture.hpp"
#include <chrono>
#include <cmath>
#include <map>
#include <thread>

namespace
{
using namespace hui::afterglow;
using Page = Panel::Page;

SetupFacts good_facts()
{
    SetupFacts facts;
    facts.configured = true;
    facts.host = "music.local:8095";
    facts.plays = true;
    facts.sendspin = true;
    facts.online_lyrics = true;
    facts.presets = 393;
    facts.measured = 393;
    facts.now_s = 1792161000;
    return facts;
}
SetupResults good_results()
{
    SetupResults results;
    results.reached = results.authed = results.player_found = results.relay_ok = true;
    results.version = "2.10.6";
    results.server_s = 1792161000 - 20;
    results.lyrics_status = 200;
    return results;
}
std::map<std::string, SetupRow> by_id(const std::vector<SetupRow> &rows)
{
    std::map<std::string, SetupRow> out;
    for (const auto &r : rows)
        out[r.id] = r;
    return out;
}
std::vector<std::string> ids(const std::vector<SetupRow> &rows)
{
    std::vector<std::string> out;
    for (const auto &r : rows)
        out.push_back(r.id);
    return out;
}

TEST(AfterglowSetupCheck, VersionsFrom210PassAndADevelopmentBuildPasses)
{
    EXPECT_TRUE(setup_version_ok("2.10.0"));
    EXPECT_TRUE(setup_version_ok("2.10.6"));
    EXPECT_TRUE(setup_version_ok("2.11.0b3"));
    EXPECT_TRUE(setup_version_ok("3.0.0"));
    EXPECT_TRUE(setup_version_ok("0.0.0"));
    EXPECT_FALSE(setup_version_ok("2.9.3"));
    EXPECT_FALSE(setup_version_ok("1.8.0"));
    EXPECT_FALSE(setup_version_ok(""));
    EXPECT_FALSE(setup_version_ok("dev"));
}

TEST(AfterglowSetupCheck, AnLrclibBusyAnswerIsBusyNotFailed)
{
    EXPECT_EQ(setup_lyrics_state(200), SetupState::pass);
    EXPECT_EQ(setup_lyrics_state(404), SetupState::pass);
    EXPECT_EQ(setup_lyrics_state(503), SetupState::busy);
    EXPECT_EQ(setup_lyrics_state(429), SetupState::busy);
    EXPECT_EQ(setup_lyrics_state(0), SetupState::fail);
}

TEST(AfterglowSetupCheck, ReadsTheServersDateHeader)
{
    EXPECT_EQ(parse_http_date("Fri, 16 Oct 2026 14:49:13 GMT"),
              1792161000 - 1792161000 % 86400 + 14 * 3600 + 49 * 60 + 13);
    EXPECT_EQ(parse_http_date("Thu, 01 Jan 1970 00:00:00 GMT"), 0);
    EXPECT_EQ(parse_http_date("Sat, 29 Feb 2028 23:59:59 GMT"), 1835481599);
    EXPECT_EQ(parse_http_date(""), 0);
    EXPECT_EQ(parse_http_date("Fri, 16 Oct 2026 14:49:13 CET"), 0);
    EXPECT_EQ(parse_http_date("Fri, 16 Foo 2026 14:49:13 GMT"), 0);
    EXPECT_EQ(parse_http_date("yesterday"), 0);
}

TEST(AfterglowSetupCheck, SaysHowFarTheClockIsOff)
{
    EXPECT_EQ(clock_offset_label(-12 * 60), "12 min behind");
    EXPECT_EQ(clock_offset_label(301), "5 min ahead");
    EXPECT_EQ(clock_offset_label(-(3 * 3600 + 12 * 60)), "3 h 12 min behind");
    EXPECT_EQ(clock_offset_label(2 * 3600), "2 h ahead");
    EXPECT_EQ(clock_offset_label(-3 * 86400 - 5), "3 days behind");
}

TEST(AfterglowSetupCheck, AllGoodEveryRowPassesInTheSpecsOrder)
{
    const auto results = good_results();
    const auto rows = setup_rows(good_facts(), &results);
    EXPECT_EQ(ids(rows), (std::vector<std::string>{"server", "account", "player", "sendspin",
                                                   "clock", "lyrics", "scan"}));
    for (const auto &r : rows)
    {
        EXPECT_EQ(r.state, SetupState::pass) << r.id;
        EXPECT_TRUE(r.next.empty()) << r.id;
    }
    EXPECT_EQ(by_id(rows)["server"].value, "Version 2.10.6");
    EXPECT_EQ(by_id(rows)["clock"].value, "Right");
    EXPECT_EQ(setup_note(rows), "Everything looks right.");
    EXPECT_EQ(setup_summary(rows, true), "All good");
    EXPECT_EQ(setup_summary({}, false), "Not run");
}

TEST(AfterglowSetupCheck, FollowingShowsTheRelayInsteadOfTheSpeaker)
{
    auto facts = good_facts();
    facts.plays = false;
    facts.follows = true;
    facts.online_lyrics = false;
    facts.presets = 0;
    const auto results = good_results();
    EXPECT_EQ(ids(setup_rows(facts, &results)),
              (std::vector<std::string>{"server", "account", "visualizer", "clock"}));
}

TEST(AfterglowSetupCheck, WhileRunningTheProbedRowsSayChecking)
{
    const auto rows = by_id(setup_rows(good_facts(), nullptr));
    for (const char *id : {"server", "account", "player", "clock", "lyrics"})
        EXPECT_EQ(rows.at(id).value, "Checking…") << id;
    EXPECT_EQ(rows.at("sendspin").state, SetupState::pass) << "a fact, not a probe";
    EXPECT_EQ(setup_note(setup_rows(good_facts(), nullptr)), "Checking…");
}

TEST(AfterglowSetupCheck, WithoutAServerTheFirstRowSaysSignIn)
{
    auto facts = good_facts();
    facts.configured = false;
    const SetupResults none;
    const auto rows = by_id(setup_rows(facts, &none));
    EXPECT_EQ(rows.at("server").value, "Not set up");
    EXPECT_EQ(rows.at("server").next, "Sign in under Settings, Account.");
    EXPECT_EQ(rows.at("account").state, SetupState::skip);
    EXPECT_EQ(rows.at("account").value, "Not checked");
}

TEST(AfterglowSetupCheck, AnUnreachableServerLeavesWhatDependsOnItNotChecked)
{
    auto facts = good_facts();
    facts.follows = true;
    SetupResults results;
    results.lyrics_status = 200;
    const auto rows = by_id(setup_rows(facts, &results));
    EXPECT_EQ(rows.at("server").value, "Can’t reach it");
    EXPECT_EQ(rows.at("server").next, "Check that Music Assistant is running at music.local:8095.");
    for (const char *id : {"account", "player", "visualizer", "clock"})
    {
        EXPECT_EQ(rows.at(id).state, SetupState::skip) << id;
        EXPECT_TRUE(rows.at(id).next.empty()) << id;
    }
}

TEST(AfterglowSetupCheck, EachFailureNamesOneNextStep)
{
    auto facts = good_facts();
    auto results = good_results();
    results.version = "2.9.3";
    auto rows = by_id(setup_rows(facts, &results));
    EXPECT_EQ(rows["server"].value, "Version 2.9.3");
    EXPECT_EQ(rows["server"].next, "Update Music Assistant to 2.10 or newer.");

    results = good_results();
    results.authed = false;
    rows = by_id(setup_rows(facts, &results));
    EXPECT_EQ(rows["account"].next, "Sign in again under Settings, Account.");
    EXPECT_EQ(rows["player"].state, SetupState::skip);
    facts.config_token = true;
    EXPECT_EQ(by_id(setup_rows(facts, &results))["account"].next,
              "Put a new ma_token in afterglow.conf, or sign in under Settings, Account.");

    facts = good_facts();
    facts.sendspin = false;
    facts.follows = true;
    facts.measured = 120;
    results = good_results();
    results.player_found = false;
    results.relay_ok = false;
    results.server_s = facts.now_s + 3 * 3600 + 12 * 60;
    results.lyrics_status = 0;
    const auto list = setup_rows(facts, &results);
    rows = by_id(list);
    EXPECT_EQ(rows["player"].next,
              "Check that Music Assistant’s Sendspin provider is on, then reopen Afterglow.");
    EXPECT_EQ(rows["sendspin"].next, "Check that Music Assistant’s Sendspin provider is on.");
    EXPECT_EQ(rows["visualizer"].value, "Plugin not found");
    EXPECT_EQ(rows["clock"].value, "3 h 12 min behind");
    EXPECT_EQ(rows["clock"].next,
              "Set the date and time in the PS5’s settings. Online lyrics need it.");
    EXPECT_EQ(rows["lyrics"].next,
              "Check the PS5’s internet connection, or turn Online lyrics off.");
    EXPECT_EQ(rows["scan"].value, "120 of 393 measured");
    EXPECT_EQ(rows["scan"].next, "Run Measure preset performance in Settings, Visuals & presets.");
    EXPECT_EQ(setup_note(list), "6 things need a look.");
    EXPECT_EQ(setup_summary(list, true), "6 to look at");
}

TEST(AfterglowSetupCheck, BusyLyricsAreNotCountedAsAFailure)
{
    auto results = good_results();
    results.lyrics_status = 503;
    const auto rows = setup_rows(good_facts(), &results);
    const auto lyrics = by_id(rows)["lyrics"];
    EXPECT_EQ(lyrics.state, SetupState::busy);
    EXPECT_EQ(lyrics.value, "Busy right now");
    EXPECT_EQ(lyrics.next, "LRCLIB is busy. Lyrics are tried again by themselves.");
    EXPECT_EQ(setup_note(rows), "Everything looks right.");
}

TEST(AfterglowSetupCheck, TheWorkerRunsOnceAndAsksTheRelayOnlyWithAToken)
{
    int servers = 0, relays = 0, lyrics = 0;
    SetupProbes probes;
    probes.server = [&](const SetupRequest &request, SetupResults &out, std::atomic<bool> &)
    {
        ++servers;
        out.reached = true;
        out.version = "2.10.6";
        out.authed = request.token == "good";
    };
    probes.relay = [&](const SetupRequest &, std::atomic<bool> &)
    {
        ++relays;
        return true;
    };
    probes.lyrics = [&](std::atomic<bool> &)
    {
        ++lyrics;
        return 503;
    };
    SetupCheck check(probes);
    const auto finish = [&check](SetupResults &out)
    {
        for (int i = 0; i < 500 && !check.take(out); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    SetupRequest request;
    request.host = "music.local";
    request.token = "good";
    request.lyrics = true;
    ASSERT_TRUE(check.run(request));
    SetupResults out;
    finish(out);
    EXPECT_TRUE(out.relay_ok);
    EXPECT_EQ(out.lyrics_status, 503);
    request.token = "refused";
    request.lyrics = false;
    ASSERT_TRUE(check.run(request));
    finish(out);
    EXPECT_FALSE(out.authed);
    EXPECT_EQ(servers, 2);
    EXPECT_EQ(relays, 1) << "no relay login with a refused token";
    EXPECT_EQ(lyrics, 1) << "lrclib.net only while Online lyrics is on";
}

TEST(AfterglowSetupCheck, PumpStartsARunWhenAskedAndFillsTheRows)
{
    auto &model = presentation();
    model = {};
    SetupProbes probes;
    probes.server = [](const SetupRequest &, SetupResults &out, std::atomic<bool> &)
    {
        out.reached = out.authed = true;
        out.version = "2.10.6";
    };
    SetupCheck check(probes);
    auto facts = good_facts();
    facts.plays = false;
    facts.online_lyrics = false;
    SetupRequest request;
    request.host = "music.local";
    request.token = "good";
    pump_setup(model, check, facts, request);
    EXPECT_TRUE(model.setup_rows.empty()) << "nothing runs until asked";
    model.setup_requested = true;
    pump_setup(model, check, facts, request);
    EXPECT_FALSE(model.setup_requested);
    EXPECT_TRUE(model.setup_has_run);
    EXPECT_EQ(setup_note(model.setup_rows), "Checking…");
    for (int i = 0; i < 500 && setup_note(model.setup_rows) == "Checking…"; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        pump_setup(model, check, facts, request);
    }
    EXPECT_EQ(by_id(model.setup_rows)["server"].value, "Version 2.10.6");
    model = {};
}

TEST(AfterglowSetupCheck, WithoutAServerOnlyLrclibIsAsked)
{
    auto &model = presentation();
    model = {};
    int servers = 0;
    SetupProbes probes;
    probes.server = [&](const SetupRequest &, SetupResults &, std::atomic<bool> &) { ++servers; };
    probes.lyrics = [](std::atomic<bool> &) { return 200; };
    SetupCheck check(probes);
    auto facts = good_facts();
    facts.configured = false;
    SetupRequest request;
    request.host = "music.local";
    request.lyrics = true;
    model.setup_requested = true;
    pump_setup(model, check, facts, request);
    EXPECT_EQ(by_id(model.setup_rows)["server"].value, "Not set up");
    for (int i = 0; i < 500 && setup_note(model.setup_rows) == "Checking…"; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        pump_setup(model, check, facts, request);
    }
    EXPECT_EQ(by_id(model.setup_rows)["lyrics"].value, "lrclib.net answers");
    EXPECT_EQ(servers, 0);
    model = {};
}

TEST(AfterglowSetupCheck, TheIdleStripOffersTheCheckOnlyWithNothingToOffer)
{
    const auto kinds = [](const std::vector<IdleTile> &tiles)
    {
        std::vector<IdleTileKind> out;
        for (const auto &t : tiles)
            out.push_back(t.kind);
        return out;
    };
    using K = IdleTileKind;
    EXPECT_EQ(kinds(idle_tiles(nullptr, {}, RecentState::ready, true)),
              (std::vector<K>{K::browse, K::setup}));
    EXPECT_EQ(kinds(idle_tiles(nullptr, {}, RecentState::error, true)),
              (std::vector<K>{K::retry, K::browse, K::setup}));
    CompanionTrack played;
    played.title = "Evening Light";
    played.uri = "library://album/1";
    EXPECT_EQ(kinds(idle_tiles(nullptr, {played}, RecentState::ready, true)),
              (std::vector<K>{K::play, K::browse}));
    EXPECT_EQ(kinds(idle_tiles(&played, {}, RecentState::ready, true)),
              (std::vector<K>{K::resume, K::browse}));
    EXPECT_EQ(kinds(idle_tiles(nullptr, {}, RecentState::loading, true)),
              (std::vector<K>{K::browse}));
    EXPECT_EQ(kinds(idle_tiles(nullptr, {}, RecentState::ready)), (std::vector<K>{K::browse}));
    IdleTile tile;
    tile.kind = K::setup;
    const auto text = idle_text(tile, RecentState::ready, false);
    EXPECT_EQ(text.detail, "Something not right? Check my setup");
    EXPECT_EQ(text.hint, "Check your setup");
}

class AfterglowSetupSheet : public hui::testing::ConceptFixture
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
    void input(const hui::InputFrame &value)
    {
        panel_.tick(0.02f);
        panel_.update(value, 0.02f, feedback_, true);
    }
    void cross()
    {
        input(press(hui::Action::confirm));
    }
    std::vector<std::string> labels() const
    {
        std::vector<std::string> out;
        for (const auto &r : panel_.rows())
            out.push_back(r.item.title);
        return out;
    }
    Panel panel_;
};

TEST_F(AfterglowSetupSheet, SettingsEndsInCheckMySetupWhichRunsAtOnce)
{
    auto &s = presentation();
    panel_.open(Page::settings);
    const auto &last = panel_.rows().back();
    EXPECT_EQ(last.item.title, "Check my setup");
    EXPECT_EQ(last.item.value, "Not run");
    for (std::size_t i = 1; i < panel_.rows().size(); ++i)
        input(nav(hui::Direction::down));
    cross();
    EXPECT_EQ(panel_.page(), Page::setup);
    EXPECT_TRUE(s.setup_requested);
    s.setup_requested = false;
    s.setup_has_run = true;
    s.setup_rows = preview_setup_rows();
    input(idle());
    const auto shown = labels();
    EXPECT_EQ(shown.front(), "Music Assistant");
    EXPECT_EQ(shown.back(), "Check again");
    const auto &lyrics = panel_.rows()[5];
    EXPECT_EQ(lyrics.item.title, "Online lyrics");
    EXPECT_EQ(lyrics.item.value, "Busy right now");
    EXPECT_EQ(lyrics.item.subtitle, "LRCLIB is busy. Lyrics are tried again by themselves.");
    EXPECT_TRUE(lyrics.strong);
    // Cross on a check does nothing; on Check again it runs again.
    cross();
    EXPECT_FALSE(s.setup_requested);
    for (std::size_t i = 0; i < panel_.rows().size(); ++i)
        input(nav(hui::Direction::down));
    cross();
    EXPECT_TRUE(s.setup_requested);
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::settings);
    EXPECT_EQ(panel_.rows().back().item.value, "1 to look at");
}

TEST_F(AfterglowSetupSheet, OpenedFromTheOfferItRunsAndBackCloses)
{
    panel_.open(Page::setup);
    EXPECT_TRUE(presentation().setup_requested);
    EXPECT_FALSE(panel_.back());
}

TEST_F(AfterglowSetupSheet, DetailsPutsTheCodeBesideTheFactsInsideTheSafeArea)
{
    auto &s = presentation();
    s.companion_configured = true;
    s.companion.connected = true;
    s.companion.track.title = "Song";
    s.companion.track.artist = "Artist";
    s.companion.track.uri = "library://track/7";
    s.companion.track.share_url = "https://open.spotify.com/track/afterglowdemo0001";
    s.companion.track.share_service = "Spotify";
    panel_.open(Page::details);
    input(idle());
    hui::gfx::DrawList list;
    hui::ui::Canvas canvas{list, fonts_, 0, 0};
    panel_.draw(canvas);
    int ink = 0;
    for (const auto &shape : list.instances())
    {
        if (shape.rect[0] >= style::kColumn)
            EXPECT_LE(shape.rect[0] + shape.rect[2], style::kSafeRight + 0.5f);
        if (shape.rect[0] >= style::kSafeRight - kShareCodeSize && shape.rect[2] < 30)
            ++ink;
    }
    EXPECT_GT(ink, 100) << "the code's modules are drawn as flat squares";
    // The actions start below the code block, whatever the facts' height.
    ASSERT_FALSE(panel_.rows().empty());
    s.companion.track.share_url.clear();
    input(idle());
}
} // namespace
