// Afterglow - The system on-screen keyboard (sceImeDialog) as a small state machine.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace hui::ps5
{

// What the keyboard's layout is tuned for (sceImeDialog's type).
enum class KeyboardKind
{
    text,
    url,
    number
};

struct KeyboardOptions
{
    std::string title, initial;
    // Most UTF-16 code units the user may enter (the dialog allows 1..2048).
    std::size_t max_length = 256;
    // Hide what is typed.
    bool secret = false;
    KeyboardKind kind = KeyboardKind::text;
};

struct KeyboardOutcome
{
    bool accepted = false;
    std::string text;
};

// The dialog itself. keyboard_ime.cpp talks to sceImeDialog on the console;
// host/keyboard_host.cpp answers from AFTERGLOW_KEYBOARD for host runs.
class ImeBackend
{
  public:
    enum class Status
    {
        none,
        running,
        finished
    };
    enum class End
    {
        ok,
        cancelled,
        aborted
    };
    struct Params
    {
        const char16_t *title = nullptr;
        // Holds the initial text; the dialog writes the result here. Has
        // max_length + 1 units and stays alive until close().
        char16_t *buffer = nullptr;
        std::uint32_t max_length = 0;
        bool secret = false;
        KeyboardKind kind = KeyboardKind::text;
    };

    virtual ~ImeBackend() = default;
    virtual bool open(const Params &params) = 0;
    virtual Status status() = 0;
    // How the dialog ended; only valid once status() is finished.
    virtual End end() = 0;
    // Ends a running dialog early (the app is closing).
    virtual void abort() = 0;
    // Releases the dialog after it finished or was aborted.
    virtual void close() = 0;
};

// The platform's dialog for user (the pad's initial user).
std::unique_ptr<ImeBackend> make_ime_backend(int user);

// One dialog at a time: open(), then poll() every frame until it answers.
class Keyboard
{
  public:
    static constexpr std::size_t kMaxLength = 2048;
    static constexpr std::size_t kMaxTitle = 128;

    explicit Keyboard(ImeBackend &backend) : backend_(backend)
    {
    }
    Keyboard(const Keyboard &) = delete;
    Keyboard &operator=(const Keyboard &) = delete;
    ~Keyboard();

    // False if one is already open. If the dialog cannot open, the next
    // poll() answers "not accepted" so the asker is never left waiting.
    bool open(const KeyboardOptions &options);
    // True once per dialog, when it has ended; outcome then holds the answer.
    bool poll(KeyboardOutcome &outcome);
    // A dialog is open (or its failure is waiting to be reported).
    bool active() const
    {
        return state_ != State::idle;
    }
    // Ends any open dialog without an answer (the app is closing).
    void abort();

  private:
    enum class State
    {
        idle,
        running,
        failed
    };
    void wipe();
    ImeBackend &backend_;
    State state_ = State::idle;
    std::u16string title_, buffer_;
};

// UTF-8 <-> UTF-16. Invalid input becomes U+FFFD. to_utf16 stops before
// max_units code units without splitting a surrogate pair.
std::u16string to_utf16(std::string_view text, std::size_t max_units = std::u16string::npos);
std::string to_utf8(std::u16string_view text);

} // namespace hui::ps5
