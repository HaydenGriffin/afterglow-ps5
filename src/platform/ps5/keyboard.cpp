// Afterglow - The system on-screen keyboard (sceImeDialog) as a small state machine.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/keyboard.hpp"

#include <algorithm>

namespace hui::ps5
{
namespace
{
constexpr char32_t kReplacement = 0xfffd;

// Decodes one code point at text[i], advancing i; invalid bytes give U+FFFD.
char32_t next_code_point(std::string_view text, std::size_t &i)
{
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char lead = byte(i++);
    if (lead < 0x80)
        return lead;
    int extra = 0;
    char32_t value = 0, minimum = 0;
    if ((lead & 0xe0) == 0xc0)
        extra = 1, value = lead & 0x1f, minimum = 0x80;
    else if ((lead & 0xf0) == 0xe0)
        extra = 2, value = lead & 0x0f, minimum = 0x800;
    else if ((lead & 0xf8) == 0xf0)
        extra = 3, value = lead & 0x07, minimum = 0x10000;
    else
        return kReplacement;
    for (int k = 0; k < extra; ++k)
    {
        if (i >= text.size() || (byte(i) & 0xc0) != 0x80)
            return kReplacement;
        value = (value << 6) | (byte(i++) & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return kReplacement;
    return value;
}

void append_utf8(std::string &out, char32_t value)
{
    if (value < 0x80)
        out += static_cast<char>(value);
    else if (value < 0x800)
    {
        out += static_cast<char>(0xc0 | (value >> 6));
        out += static_cast<char>(0x80 | (value & 0x3f));
    }
    else if (value < 0x10000)
    {
        out += static_cast<char>(0xe0 | (value >> 12));
        out += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (value & 0x3f));
    }
    else
    {
        out += static_cast<char>(0xf0 | (value >> 18));
        out += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (value & 0x3f));
    }
}
} // namespace

std::u16string to_utf16(std::string_view text, std::size_t max_units)
{
    std::u16string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        const char32_t value = next_code_point(text, i);
        const std::size_t units = value >= 0x10000 ? 2 : 1;
        if (max_units != std::u16string::npos && out.size() + units > max_units)
            break;
        if (units == 1)
            out += static_cast<char16_t>(value);
        else
        {
            out += static_cast<char16_t>(0xd800 + ((value - 0x10000) >> 10));
            out += static_cast<char16_t>(0xdc00 + ((value - 0x10000) & 0x3ff));
        }
    }
    return out;
}

std::string to_utf8(std::u16string_view text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char32_t value = text[i];
        if (value >= 0xd800 && value <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 &&
            text[i + 1] <= 0xdfff)
            value = 0x10000 + ((value - 0xd800) << 10) + (text[++i] - 0xdc00);
        else if (value >= 0xd800 && value <= 0xdfff)
            value = kReplacement;
        append_utf8(out, value);
    }
    return out;
}

Keyboard::~Keyboard()
{
    abort();
}

bool Keyboard::open(const KeyboardOptions &options)
{
    if (state_ != State::idle)
        return false;
    const std::size_t max_length = std::clamp<std::size_t>(options.max_length, 1, kMaxLength);
    title_ = to_utf16(options.title, kMaxTitle);
    buffer_ = to_utf16(options.initial, max_length);
    // The dialog writes up to max_length units and a terminator in place.
    buffer_.resize(max_length + 1, u'\0');
    ImeBackend::Params params;
    params.title = title_.c_str();
    params.buffer = buffer_.data();
    params.max_length = static_cast<std::uint32_t>(max_length);
    params.secret = options.secret;
    params.kind = options.kind;
    state_ = backend_.open(params) ? State::running : State::failed;
    return true;
}

bool Keyboard::poll(KeyboardOutcome &outcome)
{
    if (state_ == State::idle)
        return false;
    outcome = KeyboardOutcome{};
    if (state_ == State::running)
    {
        const auto status = backend_.status();
        if (status == ImeBackend::Status::running)
            return false;
        if (status == ImeBackend::Status::finished && backend_.end() == ImeBackend::End::ok)
        {
            outcome.accepted = true;
            outcome.text = to_utf8(std::u16string_view(buffer_.c_str()));
        }
        backend_.close();
    }
    wipe();
    state_ = State::idle;
    return true;
}

void Keyboard::abort()
{
    if (state_ == State::running)
    {
        backend_.abort();
        backend_.close();
    }
    wipe();
    state_ = State::idle;
}

// The dialog typed into buffer_ in place; a password must not stay there.
void Keyboard::wipe()
{
    std::fill(buffer_.begin(), buffer_.end(), u'\0');
}

} // namespace hui::ps5
