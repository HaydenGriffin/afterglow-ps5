// Afterglow - The sheets' playlist rows: Save as a playlist, Add to a playlist and the picker.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/panel.hpp"
#include "afterglow/programme_scene.hpp"
#include "afterglow/style.hpp"
#include "concept_fixture.hpp"

#include <cmath>

namespace
{
using hui::Action;
using hui::Direction;
using hui::afterglow::ApiCall;
using hui::afterglow::ApiReply;
using hui::afterglow::CompanionTrack;
using hui::afterglow::presentation;
using hui::afterglow::SheetRow;
using Page = hui::afterglow::Panel::Page;

class AfterglowPanelPlaylists : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        presentation() = {};
        auto &s = presentation();
        s.companion_configured = true;
        s.companion.connected = true;
        s.companion.queue_id = "q";
    }
    void TearDown() override
    {
        presentation() = {};
    }
    // As the screen and main.cpp do each frame: the sheet, then Music Assistant's side.
    void input(const hui::InputFrame &value)
    {
        panel_.tick(0.02f);
        panel_.update(value, 0.02f, feedback_, true);
        clock_ += 20;
        std::vector<ApiCall> calls;
        presentation().playlists.update(clock_, replies_, calls, next_);
        replies_.clear();
        sent_.insert(sent_.end(), calls.begin(), calls.end());
        // pump_playlists's toast, on this test's clock rather than the steady one.
        if (auto notice = presentation().playlists.take_notice(); !notice.empty())
            presentation().show_toast(std::move(notice));
    }
    void down(int times)
    {
        for (int i = 0; i < times; ++i)
            input(nav(Direction::down));
    }
    void cross()
    {
        input(press(Action::confirm));
    }
    void wait(float seconds)
    {
        for (int i = 0, frames = static_cast<int>(seconds / 0.02f + 0.5f); i < frames; ++i)
            input(idle());
    }
    // Music Assistant answers the last call; the next frame hears it.
    void reply(const char *result, int error = 0)
    {
        ApiReply r;
        r.id = sent_.back().id;
        r.result = result;
        r.error_code = error;
        replies_.push_back(r);
        input(idle());
        input(idle()); // the rows show it a frame later
    }
    static CompanionTrack song(const char *title, const char *id)
    {
        CompanionTrack value;
        value.title = title;
        value.artist = "Harbour Lamps";
        value.queue_item_id = std::string("qi-") + id;
        value.item_id = id;
        value.uri = std::string("library://track/") + id;
        value.media_type = "track";
        return value;
    }
    // The row titled so; a failed lookup ends the test rather than reading null.
    const SheetRow *row(const std::string &title) const
    {
        for (const auto &r : panel_.rows())
            if (r.item.title == title)
                return &r;
        static const SheetRow missing = []
        {
            SheetRow r;
            r.item.title = "(missing)";
            r.item.value = "(missing)";
            r.item.subtitle = "(missing)";
            return r;
        }();
        ADD_FAILURE() << "no row " << title;
        return &missing;
    }
    int index_of(const std::string &title) const
    {
        const auto &rows = panel_.rows();
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].item.title == title)
                return static_cast<int>(i);
        return -1;
    }
    hui::afterglow::Panel panel_;
    std::vector<ApiCall> sent_;
    std::vector<ApiReply> replies_;
    std::uint64_t next_ = 0;
    std::int64_t clock_ = 0;
};

TEST_F(AfterglowPanelPlaylists, SaveTheQueueSitsUnderUpNextAndConfirmsOnItsRow)
{
    auto &s = presentation();
    s.companion.upcoming = {song("Lighthouse", "1"), song("Low tide", "2")};
    s.companion.queue_index = 0;
    s.companion.queue_total = 3;
    panel_.open(Page::queue);
    const int save = index_of("Save as a playlist");
    ASSERT_GT(save, 0);
    EXPECT_EQ(panel_.rows()[static_cast<std::size_t>(save) - 1].item.title, "Low tide")
        << "the last row under the Up next songs";
    EXPECT_TRUE(panel_.rows()[static_cast<std::size_t>(save) + 1].item.header) << "then Playback";
    EXPECT_EQ(row("Save as a playlist")->item.value.rfind("As “Queue · ", 0), 0u);
    EXPECT_FALSE(row("Save as a playlist")->item.disabled);

    down(3); // Lighthouse, Low tide, Save as a playlist
    cross();
    EXPECT_EQ(sent_.back().command, "music/playlists/library_items") << "the name check";
    input(idle());
    EXPECT_EQ(row("Save as a playlist")->item.value, "Saving…");
    EXPECT_TRUE(row("Save as a playlist")->item.disabled) << "a second press does nothing";
    reply("[]");
    EXPECT_EQ(sent_.back().command, "player_queues/save_as_playlist");
    reply(R"({"id":"t","status":"success","progress_text":"Added 3 item(s) to playlist"})");
    input(idle());
    const auto *done = row("Save as a playlist");
    EXPECT_TRUE(done->chosen) << "the drawn tick";
    EXPECT_EQ(done->item.value.rfind("Saved as “Queue · ", 0), 0u);
    EXPECT_EQ(s.toast, done->item.value);
    wait(2.6f);
    EXPECT_FALSE(row("Save as a playlist")->chosen);
    EXPECT_EQ(row("Save as a playlist")->item.value.rfind("As “", 0), 0u);
}

TEST_F(AfterglowPanelPlaylists, SaveTheQueueRefusesWhileEmptyOrOffline)
{
    auto &s = presentation();
    panel_.open(Page::queue);
    EXPECT_TRUE(row("Save as a playlist")->item.disabled) << "nothing to save";
    s.companion.queue_total = 4;
    s.companion.connected = false;
    input(idle());
    wait(0.6f);
    EXPECT_TRUE(row("Save as a playlist")->item.disabled) << "offline";
}

TEST_F(AfterglowPanelPlaylists, QueueSongPicksAPlaylistAndItsOwnRowConfirms)
{
    auto &s = presentation();
    s.companion.upcoming = {song("Lighthouse", "1")};
    s.companion.queue_index = 0;
    s.companion.queue_total = 2;
    panel_.open(Page::queue);
    down(1);
    cross();
    ASSERT_EQ(panel_.page(), Page::queue_actions);
    EXPECT_EQ(index_of("Add to a playlist") + 1, index_of("Remove from the queue"));
    down(4); // Play next, Move up, Move down, Add to a playlist
    cross();
    ASSERT_EQ(panel_.page(), Page::playlist);
    EXPECT_FALSE(panel_.covers_screen()) << "a sheet like the song's page";
    EXPECT_EQ(sent_.back().command, "music/playlists/library_items");
    EXPECT_EQ(panel_.rows()[0].item.title, "New playlist");
    EXPECT_EQ(panel_.rows()[1].item.title, "Loading playlists…");
    EXPECT_TRUE(panel_.rows()[1].item.disabled);
    reply(R"([{"item_id":"5","name":"Sunday kitchen","is_editable":true},)"
          R"({"item_id":"6","name":"Daily mix","is_editable":true,"is_dynamic":true}])");
    ASSERT_EQ(panel_.rows().size(), 2u) << "only playlists a song can go into";
    EXPECT_TRUE(panel_.back()) << "Back leaves the picker for the song";
    EXPECT_EQ(panel_.page(), Page::queue_actions);
    cross();
    ASSERT_EQ(panel_.page(), Page::playlist);
    reply(R"([{"item_id":"5","name":"Sunday kitchen","is_editable":true}])");
    down(1);
    cross();
    EXPECT_EQ(panel_.page(), Page::queue) << "back on the Up next list";
    EXPECT_EQ(sent_.back().args, R"({"db_playlist_id":"5","uris":["library://track/1"]})");
    EXPECT_EQ(row("Lighthouse")->item.value, "Adding to “Sunday kitchen”…");
    reply(R"({"id":"t","status":"success","progress_text":"Added 1 item(s) to playlist"})");
    input(idle());
    EXPECT_TRUE(row("Lighthouse")->chosen);
    EXPECT_EQ(row("Lighthouse")->item.value, "Added to “Sunday kitchen”");
    EXPECT_EQ(s.toast, "Added to “Sunday kitchen”");
    wait(2.6f);
    EXPECT_EQ(row("Lighthouse")->item.value, "Harbour Lamps") << "the artist returns";
}

TEST_F(AfterglowPanelPlaylists, DetailsAddsToANewPlaylistOnItsOwnRow)
{
    auto &s = presentation();
    s.companion.track = song("Slow Current", "7");
    panel_.open(Page::details);
    EXPECT_EQ(index_of("Add to a playlist"), 1) << "after the favourites row";
    down(1);
    cross();
    ASSERT_EQ(panel_.page(), Page::playlist);
    EXPECT_EQ(panel_.rows()[0].item.value.rfind("As “Playlist · ", 0), 0u);
    reply("[]");
    EXPECT_EQ(panel_.rows()[1].item.title, "No playlists you can add to yet");
    cross(); // New playlist
    EXPECT_EQ(panel_.page(), Page::details);
    reply("[]"); // the name is free
    EXPECT_EQ(sent_.back().command, "music/playlists/create_playlist");
    reply(R"({"item_id":"31"})");
    EXPECT_EQ(sent_.back().args, R"({"db_playlist_id":"31","uris":["library://track/7"]})");
    reply(R"({"id":"t","status":"failed","last_error":"nope"})");
    input(idle());
    EXPECT_EQ(row("Add to a playlist")->item.value, "Couldn’t add. Try again");
    EXPECT_FALSE(row("Add to a playlist")->chosen);
    wait(3);
    EXPECT_EQ(row("Add to a playlist")->item.value, "Couldn’t add. Try again")
        << "anything but a success stays six seconds";
    wait(3.2f);
    EXPECT_TRUE(row("Add to a playlist")->item.value.empty());
}

TEST_F(AfterglowPanelPlaylists, DetailsOffersItOnlyForSongs)
{
    auto &s = presentation();
    s.companion.track = song("Night Light Radio", "3");
    s.companion.track.media_type = "radio";
    panel_.open(Page::details);
    EXPECT_TRUE(row("Add to a playlist")->item.disabled);
}

TEST_F(AfterglowPanelPlaylists, BrowsePickerTakesTheActionsColumnAndTheSongsRowConfirms)
{
    auto &s = presentation();
    panel_.open(Page::browse);
    s.browse_pending = false;
    s.companion.browse_items = {song("Lighthouse", "4")};
    ++s.companion.browse_revision;
    input(nav(Direction::right));
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions);
    EXPECT_EQ(index_of("Add to a playlist"), index_of("Add to the queue") + 1);
    down(3);
    cross();
    ASSERT_EQ(panel_.page(), Page::playlist);
    EXPECT_TRUE(panel_.covers_screen()) << "full screen, in the actions column";
    reply("", hui::afterglow::kApiNoAnswer);
    EXPECT_EQ(panel_.rows()[1].item.title, "Couldn’t load playlists");
    EXPECT_EQ(panel_.rows()[1].item.value, "Try again");
    down(1);
    cross();
    input(idle());
    EXPECT_EQ(panel_.rows()[1].item.title, "Loading playlists…") << "Try again reloads";
    reply(R"([{"item_id":"5","name":"Sunday kitchen","is_editable":true}])");
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::item_actions) << "Back is the song's actions";
    cross();
    reply(R"([{"item_id":"5","name":"Sunday kitchen","is_editable":true}])");
    down(1);
    cross();
    EXPECT_EQ(panel_.page(), Page::browse) << "a pick goes on to the list";
    reply(R"({"id":"t","status":"success","progress_text":"No new playlist items to add"})");
    input(idle());
    const auto *item = row("Lighthouse");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->item.subtitle, "Already in “Sunday kitchen”, or not available there");
    EXPECT_FALSE(item->chosen);
}

TEST_F(AfterglowPanelPlaylists, BrowseAlbumsHaveNoPlaylistRow)
{
    auto &s = presentation();
    panel_.open(Page::browse);
    s.browse_pending = false;
    auto album = song("Evening Light", "9");
    album.media_type = "album";
    album.uri = "library://album/9";
    s.companion.browse_items = {album};
    ++s.companion.browse_revision;
    input(nav(Direction::right));
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions);
    EXPECT_EQ(index_of("Add to a playlist"), -1) << "songs only for now";
}

TEST_F(AfterglowPanelPlaylists, PickerAndProgrammeDrawInsideTheSafeArea)
{
    auto &s = presentation();
    s.companion.track = song("Slow Current", "7");
    s.thumbnail = [](const std::string &) { return 0U; };
    panel_.open(Page::details);
    down(1);
    cross();
    reply(R"([{"item_id":"5","name":"Sunday kitchen","is_editable":true}])");
    hui::gfx::DrawList list;
    hui::ui::Canvas canvas{list, fonts_, 0, 0};
    panel_.draw(canvas);
    EXPECT_FALSE(list.instances().empty());

    hui::afterglow::ProgrammePage page;
    page.where = "A very long speaker name that goes on and on and on and on and on and on";
    page.summary = "24 songs · 19:40 to 23:55";
    page.headline = {"Tonight", "16 October"};
    for (int i = 0; i < 22; ++i)
    {
        hui::afterglow::ProgrammeRow r;
        r.number = std::to_string(i + 1);
        r.title = "A title long enough to need cutting short before it reaches the time";
        r.artist = i % 2 ? "Harbour Lamps" : "";
        r.time = "21:04";
        page.columns[static_cast<std::size_t>(i / 11)].push_back(r);
    }
    page.keep_value = "Kept as “Tonight · 16 Oct” · 3 songs couldn’t be added";
    page.keep_tick = true;
    hui::gfx::DrawList scene_list;
    const hui::afterglow::SceneClock clock;
    const hui::afterglow::LyricView lyrics;
    const hui::afterglow::Scene scene{scene_list, fonts_, presentation(), clock, lyrics};
    hui::afterglow::draw_programme(scene, page, 0.5f);
    ASSERT_FALSE(scene_list.instances().empty());
    for (const auto &shape : scene_list.instances())
    {
        for (float value : shape.rect)
            ASSERT_TRUE(std::isfinite(value));
        // SDF glyph quads carry transparent padding that grows with the size.
        const bool glyph = shape.params[3] == 1.0f;
        const float slack = glyph ? 24.0f : 2.0f;
        if (shape.rect[2] < 1900) // the ground and the dim veil cover the screen
        {
            EXPECT_GE(shape.rect[0], hui::afterglow::style::kSafeLeft - slack);
            EXPECT_LE(shape.rect[0] + shape.rect[2], hui::afterglow::style::kSafeRight + slack);
            EXPECT_LE(shape.rect[1] + shape.rect[3], hui::afterglow::style::kSafeBottom + slack);
        }
    }
}
} // namespace
