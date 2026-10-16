// Afterglow - The system keyboard's state machine, UTF-16 and the host stand-in.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/keyboard.hpp"
#include <cstdlib>
#include <gtest/gtest.h>

using hui::ps5::ImeBackend;
using hui::ps5::Keyboard;
using hui::ps5::KeyboardOptions;
using hui::ps5::KeyboardOutcome;

namespace
{
class FakeIme final : public ImeBackend
{
  public:
    bool open(const Params &params) override
    {
        ++opens;
        last = params;
        initial = params.buffer;
        return open_ok;
    }
    Status status() override
    {
        return status_now;
    }
    End end() override
    {
        return end_now;
    }
    void abort() override
    {
        ++aborts;
    }
    void close() override
    {
        ++closes;
    }

    bool open_ok = true;
    Status status_now = Status::running;
    End end_now = End::ok;
    Params last;
    std::u16string initial;
    int opens = 0, aborts = 0, closes = 0;
};
} // namespace

TEST(AfterglowImeKeyboard, AnswersOnceWhenTheDialogFinishes)
{
    FakeIme ime;
    Keyboard keyboard(ime);
    KeyboardOptions options;
    options.title = "Search";
    options.initial = "Blur";
    options.max_length = 64;
    ASSERT_TRUE(keyboard.open(options));
    EXPECT_FALSE(keyboard.open(options)) << "one dialog at a time";
    EXPECT_EQ(ime.opens, 1);
    EXPECT_EQ(std::u16string(ime.last.title), u"Search");
    EXPECT_EQ(ime.initial, u"Blur");
    EXPECT_EQ(ime.last.max_length, 64u);
    EXPECT_FALSE(ime.last.secret);
    KeyboardOutcome outcome;
    EXPECT_FALSE(keyboard.poll(outcome));
    EXPECT_TRUE(keyboard.active());
    const std::u16string typed = u"Blur – Parklife";
    typed.copy(ime.last.buffer, typed.size());
    ime.last.buffer[typed.size()] = u'\0';
    ime.status_now = ImeBackend::Status::finished;
    ASSERT_TRUE(keyboard.poll(outcome));
    EXPECT_TRUE(outcome.accepted);
    EXPECT_EQ(outcome.text, "Blur \xe2\x80\x93 Parklife");
    EXPECT_EQ(ime.closes, 1);
    EXPECT_FALSE(keyboard.active());
    EXPECT_FALSE(keyboard.poll(outcome)) << "answered only once";
    EXPECT_EQ(ime.last.buffer[0], u'\0') << "the typed text is wiped once taken";
    EXPECT_EQ(ime.last.buffer[5], u'\0');
}

TEST(AfterglowImeKeyboard, CancelGivesNoText)
{
    FakeIme ime;
    Keyboard keyboard(ime);
    KeyboardOptions options;
    options.initial = "secret";
    options.secret = true;
    keyboard.open(options);
    EXPECT_TRUE(ime.last.secret);
    ime.status_now = ImeBackend::Status::finished;
    ime.end_now = ImeBackend::End::cancelled;
    KeyboardOutcome outcome;
    ASSERT_TRUE(keyboard.poll(outcome));
    EXPECT_FALSE(outcome.accepted);
    EXPECT_TRUE(outcome.text.empty());
}

TEST(AfterglowImeKeyboard, AFailedOpenStillAnswers)
{
    FakeIme ime;
    ime.open_ok = false;
    Keyboard keyboard(ime);
    keyboard.open({});
    KeyboardOutcome outcome{true, "stale"};
    ASSERT_TRUE(keyboard.poll(outcome));
    EXPECT_FALSE(outcome.accepted);
    EXPECT_TRUE(outcome.text.empty());
    EXPECT_EQ(ime.closes, 0) << "nothing to release";
    EXPECT_TRUE(keyboard.open({})) << "usable again";
}

TEST(AfterglowImeKeyboard, ADialogTheSystemDroppedIsACancel)
{
    FakeIme ime;
    Keyboard keyboard(ime);
    keyboard.open({});
    ime.status_now = ImeBackend::Status::none;
    KeyboardOutcome outcome;
    ASSERT_TRUE(keyboard.poll(outcome));
    EXPECT_FALSE(outcome.accepted);
    EXPECT_EQ(ime.closes, 1);
}

TEST(AfterglowImeKeyboard, AbortAndDestructionReleaseARunningDialog)
{
    FakeIme ime;
    {
        Keyboard keyboard(ime);
        keyboard.open({});
        keyboard.abort();
        EXPECT_EQ(ime.aborts, 1);
        EXPECT_EQ(ime.closes, 1);
        keyboard.open({});
    }
    EXPECT_EQ(ime.aborts, 2);
    EXPECT_EQ(ime.closes, 2);
}

TEST(AfterglowImeKeyboard, LimitsAreClampedAndInitialTextFits)
{
    FakeIme ime;
    Keyboard keyboard(ime);
    KeyboardOptions options;
    options.title = std::string(300, 't');
    options.initial = "abcdef";
    options.max_length = 4;
    keyboard.open(options);
    EXPECT_EQ(std::u16string(ime.last.title).size(), Keyboard::kMaxTitle);
    EXPECT_EQ(ime.initial, u"abcd");
    keyboard.abort();
    options.max_length = 100000;
    keyboard.open(options);
    EXPECT_EQ(ime.last.max_length, Keyboard::kMaxLength);
    keyboard.abort();
    options.max_length = 0;
    keyboard.open(options);
    EXPECT_EQ(ime.last.max_length, 1u);
}

TEST(AfterglowImeKeyboard, Utf16RoundTripsAndNeverSplitsAPair)
{
    const std::string text = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x8e\xb5"; // a é € 🎵
    const auto wide = hui::ps5::to_utf16(text);
    EXPECT_EQ(wide.size(), 5u);
    EXPECT_EQ(hui::ps5::to_utf8(wide), text);
    EXPECT_EQ(hui::ps5::to_utf16(text, 4), u"aé€") << "the pair does not fit";
    EXPECT_EQ(hui::ps5::to_utf16("\xff"
                                 "a"),
              u"�a");
    EXPECT_EQ(hui::ps5::to_utf16("\xe2\x82"), u"�");
    EXPECT_EQ(hui::ps5::to_utf8(std::u16string(1, char16_t(0xd800))), "\xef\xbf\xbd");
}

TEST(AfterglowImeKeyboard, HostStandInAnswersFromTheEnvironment)
{
    auto ime = hui::ps5::make_ime_backend(0);
    Keyboard keyboard(*ime);
    KeyboardOutcome outcome;
    unsetenv("AFTERGLOW_KEYBOARD");
    keyboard.open({});
    ASSERT_TRUE(keyboard.poll(outcome));
    EXPECT_FALSE(outcome.accepted);
    setenv("AFTERGLOW_KEYBOARD", "Massive Attack", 1);
    KeyboardOptions options;
    options.max_length = 7;
    keyboard.open(options);
    ASSERT_TRUE(keyboard.poll(outcome));
    EXPECT_TRUE(outcome.accepted);
    EXPECT_EQ(outcome.text, "Massive");
    unsetenv("AFTERGLOW_KEYBOARD");
}
