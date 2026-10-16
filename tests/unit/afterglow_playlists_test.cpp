// Afterglow - Playlists: names, decoders, refusals and the job's steps.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/playlists.hpp"
#include <gtest/gtest.h>

namespace
{
using namespace hui::afterglow;
namespace pw = playlist_wire;

LocalTime evening()
{
    // 16 October 2026, 21:30 (local = utc here).
    const std::int64_t at = 1792186200;
    return local_time_at(at, at);
}

// A fake Music Assistant: each frame answers the calls sent the frame before.
struct Fake
{
    Playlists lists;
    std::vector<ApiCall> sent;
    std::uint64_t next = 0;
    std::int64_t clock = 0;
    void frame(std::vector<ApiReply> replies = {})
    {
        std::vector<ApiCall> calls;
        lists.update(clock, replies, calls, next);
        sent.insert(sent.end(), calls.begin(), calls.end());
    }
    const ApiCall &last() const
    {
        return sent.back();
    }
    void reply(std::string result, int error = 0)
    {
        ApiReply r;
        r.id = last().id;
        r.result = std::move(result);
        r.error_code = error;
        frame({r});
    }
};

TEST(AfterglowPlaylists, NamesNeedNoKeyboard)
{
    const auto now = evening();
    EXPECT_EQ(now.day, 16);
    EXPECT_EQ(now.month, 10);
    EXPECT_EQ(now.hour, 21);
    EXPECT_EQ(pw::playlist_name("Queue", now.day, now.month), "Queue · 16 Oct");
    EXPECT_EQ(pw::playlist_name("Playlist", 3, 1), "Playlist · 3 Jan");
    EXPECT_EQ(pw::unique_name("Queue · 16 Oct", {"Other"}), "Queue · 16 Oct");
    EXPECT_EQ(pw::unique_name("Queue · 16 Oct", {"Queue · 16 Oct", "Queue · 16 Oct (2)"}),
              "Queue · 16 Oct (3)");
}

TEST(AfterglowPlaylists, OnlyEditablePlaylistsInTheOrderAsked)
{
    const auto list =
        pw::decode_editable(R"([{"item_id":"1","name":"A","is_editable":true,"is_dynamic":false},)"
                            R"({"item_id":"2","name":"B","is_editable":false},)"
                            R"({"item_id":"3","name":"C","is_editable":true,"is_dynamic":true},)"
                            R"({"item_id":4,"name":"D","is_editable":true}])");
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].name, "A");
    EXPECT_EQ(list[1].item_id, "4");
    std::string many = "[";
    for (int i = 0; i < 12; ++i)
        many += std::string(i ? "," : "") + R"({"item_id":")" + std::to_string(i) +
                R"(","name":"P","is_editable":true})";
    EXPECT_EQ(pw::decode_editable(many + "]").size(), 8u);
}

TEST(AfterglowPlaylists, TasksAndCounts)
{
    EXPECT_EQ(pw::decode_task("null").state, pw::TaskState::done);
    EXPECT_EQ(pw::decode_task(R"({"id":"t","status":"pending"})").state, pw::TaskState::running);
    EXPECT_EQ(pw::decode_task(R"({"id":"t","status":"cancelled"})").state, pw::TaskState::failed);
    const auto done = pw::decode_task(
        R"({"id":"t","status":"success","progress_text":"Added 12 item(s) to playlist"})");
    EXPECT_EQ(done.state, pw::TaskState::done);
    EXPECT_EQ(done.added, 12);
    EXPECT_EQ(pw::decode_task(
                  R"({"id":"t","status":"success","progress_text":"No new playlist items to add"})")
                  .added,
              0);
    EXPECT_EQ(pw::decode_task(R"({"id":"t","status":"running","progress_text":"Matching"})").added,
              -1);
}

TEST(AfterglowPlaylists, RefusalsAndOutcomes)
{
    EXPECT_EQ(pw::refusal(22, pw::Verb::save), "This account can’t change playlists");
    EXPECT_EQ(pw::refusal(8, pw::Verb::save), "Nothing in the queue can go in a playlist");
    EXPECT_EQ(pw::refusal(3, pw::Verb::create), "Music Assistant has nowhere to keep playlists");
    EXPECT_EQ(pw::refusal(3, pw::Verb::add), "Couldn’t add. Try again");
    EXPECT_EQ(pw::refusal(kApiNoAnswer, pw::Verb::save), "Couldn’t save. Try again");
    EXPECT_EQ(pw::outcome_text(pw::JobKind::save, "Q", 20, 20).text, "Saved as “Q”");
    const auto partial = pw::outcome_text(pw::JobKind::save, "Q", 17, 20);
    EXPECT_FALSE(partial.ok);
    EXPECT_EQ(partial.text, "Saved as “Q” · 3 songs couldn’t be added");
    EXPECT_EQ(pw::outcome_text(pw::JobKind::keep, "T", 4, 5).text,
              "Kept as “T” · 1 song couldn’t be added");
    EXPECT_TRUE(pw::outcome_text(pw::JobKind::save, "Q", -1, 20).ok);
    EXPECT_EQ(pw::outcome_text(pw::JobKind::add, "S", 0, 1).text,
              "Already in “S”, or not available there");
}

TEST(AfterglowPlaylists, RequestArguments)
{
    EXPECT_EQ(pw::save_args("q1", "Queue · 16 Oct"),
              R"({"queue_id":"q1","name":"Queue · 16 Oct"})");
    EXPECT_EQ(pw::add_args("5", {"a://track/1", "b://track/2"}),
              R"({"db_playlist_id":"5","uris":["a://track/1","b://track/2"]})");
    EXPECT_EQ(pw::editable_args(),
              R"({"limit":50,"offset":0,"order_by":"timestamp_modified_desc"})");
    EXPECT_EQ(pw::task_args("t1"), R"({"task_id":"t1"})");
    EXPECT_EQ(pw::created_id(R"({"item_id":"77","name":"x"})"), "77");
}

TEST(AfterglowPlaylists, SaveTheQueueChecksTheNameAndFollowsTheTask)
{
    Fake f;
    ASSERT_TRUE(f.lists.save_queue("q1", 9, evening()));
    EXPECT_FALSE(f.lists.save_queue("q1", 9, evening())) << "one job at a time";
    f.frame();
    EXPECT_EQ(f.last().command, "music/playlists/library_items");
    EXPECT_EQ(f.lists.confirmation("queue-save", 0)->text, "Saving…");
    f.reply(R"([{"name":"Queue · 16 Oct"}])");
    EXPECT_EQ(f.last().command, "player_queues/save_as_playlist");
    EXPECT_EQ(f.last().args, R"x({"queue_id":"q1","name":"Queue · 16 Oct (2)"})x");
    f.reply(R"({"id":"t1","status":"pending"})");
    const auto before = f.sent.size();
    f.clock += 500;
    f.frame();
    EXPECT_EQ(f.sent.size(), before) << "polls once a second";
    f.clock += 500;
    f.frame();
    EXPECT_EQ(f.last().command, "tasks/get");
    f.reply(R"({"id":"t1","status":"success","progress_text":"Added 9 item(s) to playlist"})");
    EXPECT_FALSE(f.lists.busy());
    EXPECT_EQ(f.lists.take_notice(), "Saved as “Queue · 16 Oct (2)”");
    const auto shown = f.lists.confirmation("queue-save", f.clock);
    ASSERT_TRUE(shown.has_value());
    EXPECT_TRUE(shown->tick);
    EXPECT_FALSE(f.lists.confirmation("queue-save", f.clock + 2500).has_value());
    EXPECT_FALSE(f.lists.confirmation("other", f.clock).has_value());
}

TEST(AfterglowPlaylists, RefusedSaveStaysLongerWithoutATick)
{
    Fake f;
    f.lists.save_queue("q1", 3, evening());
    f.frame();
    f.reply("[]");
    f.reply("", 22);
    const auto shown = f.lists.confirmation("queue-save", f.clock + 3000);
    ASSERT_TRUE(shown.has_value());
    EXPECT_FALSE(shown->tick);
    EXPECT_EQ(shown->text, "This account can’t change playlists");
    EXPECT_FALSE(f.lists.confirmation("queue-save", f.clock + 6000).has_value());
}

TEST(AfterglowPlaylists, ATaskThatNeverEndsIsStillSaving)
{
    Fake f;
    f.lists.save_queue("q", 2, evening());
    f.frame();
    f.reply("[]");
    f.reply(R"({"id":"t","status":"pending"})");
    for (int i = 0; i < 40 && f.lists.busy(); ++i)
    {
        f.clock += 1000;
        f.frame();
        if (f.last().command == "tasks/get")
            f.reply(R"({"id":"t","status":"running"})");
    }
    EXPECT_EQ(f.lists.job().text, "Still saving in Music Assistant");
}

TEST(AfterglowPlaylists, NewPlaylistCreatesThenAdds)
{
    Fake f;
    ASSERT_TRUE(f.lists.add_to_new({"lib://track/2"}, "details", evening()));
    f.frame();
    f.reply("[]");
    EXPECT_EQ(f.last().command, "music/playlists/create_playlist");
    EXPECT_EQ(f.last().args, R"({"name":"Playlist · 16 Oct"})");
    f.reply(R"({"item_id":"77"})");
    EXPECT_EQ(f.last().command, "music/playlists/add_playlist_tracks");
    EXPECT_EQ(f.last().args, R"({"db_playlist_id":"77","uris":["lib://track/2"]})");
    f.reply("null"); // an older server: no task, already done
    EXPECT_EQ(f.lists.job().text, "Added to “Playlist · 16 Oct”");
    EXPECT_TRUE(f.lists.job().ok);
}

TEST(AfterglowPlaylists, AddToAnExistingPlaylistAndThePicker)
{
    Fake f;
    f.lists.load_editable();
    EXPECT_EQ(f.lists.picker_rows(evening())[1].label, "Loading playlists…");
    f.frame();
    EXPECT_EQ(f.last().args, pw::editable_args());
    f.reply("", kApiNoAnswer);
    auto rows = f.lists.picker_rows(evening());
    EXPECT_EQ(rows[1].kind, PickerRow::Kind::retry);
    f.lists.load_editable();
    f.frame();
    f.reply(R"([{"item_id":"5","name":"Sunday","is_editable":true}])");
    rows = f.lists.picker_rows(evening());
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].label, "New playlist");
    EXPECT_EQ(rows[0].detail, "As “Playlist · 16 Oct”");
    EXPECT_EQ(rows[1].label, "Sunday");
    ASSERT_TRUE(f.lists.add_to(rows[1].playlist, {"lib://track/1"}, "browse:lib://track/1"));
    f.frame();
    EXPECT_EQ(f.last().args, R"({"db_playlist_id":"5","uris":["lib://track/1"]})");
    f.reply(R"({"id":"t","status":"success","progress_text":"No new playlist items to add"})");
    EXPECT_EQ(f.lists.confirmation("browse:lib://track/1", f.clock)->text,
              "Already in “Sunday”, or not available there");
}

TEST(AfterglowPlaylists, NoEditablePlaylists)
{
    Fake f;
    f.lists.load_editable();
    f.frame();
    f.reply(R"([{"item_id":"5","name":"Theirs","is_editable":false}])");
    EXPECT_EQ(f.lists.picker_rows(evening())[1].label, "No playlists you can add to yet");
}
} // namespace
