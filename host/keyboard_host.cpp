// Afterglow - Host stand-in for the system keyboard: a scripted answer.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AFTERGLOW_KEYBOARD=<text> accepts with that text on the first poll; unset,
// the dialog is cancelled. Lets previews and tests drive keyboard requests.

#include "platform/ps5/keyboard.hpp"

#include <cstdlib>

namespace hui::ps5
{
namespace
{

class ScriptedImeBackend final : public ImeBackend
{
  public:
    bool open(const Params &params) override
    {
        const char *answer = std::getenv("AFTERGLOW_KEYBOARD");
        accepted_ = answer != nullptr;
        if (accepted_)
        {
            const auto text = to_utf16(answer, params.max_length);
            text.copy(params.buffer, text.size());
            params.buffer[text.size()] = u'\0';
        }
        open_ = true;
        return true;
    }
    Status status() override
    {
        return open_ ? Status::finished : Status::none;
    }
    End end() override
    {
        return accepted_ ? End::ok : End::cancelled;
    }
    void abort() override
    {
        accepted_ = false;
    }
    void close() override
    {
        open_ = false;
    }

  private:
    bool open_ = false, accepted_ = false;
};

} // namespace

std::unique_ptr<ImeBackend> make_ime_backend(int)
{
    return std::make_unique<ScriptedImeBackend>();
}

} // namespace hui::ps5
