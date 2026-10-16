// Afterglow - sceImeDialog behind the Keyboard state machine.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The payload SDK ships libSceImeDialog's import stub but no header, so the
// parameter block is declared here. Layout as in the PS4 SDK's ime_dialog.h
// (also used by shadPS4): strings are UTF-16, 96 bytes in all.

#include "platform/ps5/keyboard.hpp"

#include "platform/ps5/system.hpp"

#include <cstddef>
#include <cstdint>

namespace
{

struct ImeDialogParam
{
    std::int32_t user_id;
    std::int32_t type;
    std::uint64_t supported_languages; // 0: every language
    std::int32_t enter_label;
    std::int32_t input_method;
    void *filter;
    std::uint32_t option;
    std::uint32_t max_text_length;
    char16_t *input_text_buffer;
    float posx, posy;
    std::int32_t horizontal_alignment, vertical_alignment;
    const char16_t *placeholder;
    const char16_t *title;
    std::int8_t reserved[16];
};
static_assert(sizeof(ImeDialogParam) == 96);
static_assert(offsetof(ImeDialogParam, filter) == 24);
static_assert(offsetof(ImeDialogParam, input_text_buffer) == 40);
static_assert(offsetof(ImeDialogParam, title) == 72);

struct ImeDialogResult
{
    std::int32_t end_status;
    std::int32_t reserved[12];
};

constexpr std::int32_t kTypeDefault = 0, kTypeUrl = 2, kTypeNumber = 4;
constexpr std::uint32_t kOptionPassword = 0x4;
constexpr std::int32_t kAlignCenter = 1;
constexpr int kStatusRunning = 1, kStatusFinished = 2;
constexpr std::int32_t kEndOk = 0, kEndCancelled = 1;
// libSceImeDialog.sprx in the system module table (as on PS4).
constexpr std::uint16_t kSysmoduleImeDialog = 0x0096;

} // namespace

extern "C"
{
    int sceImeDialogInit(const ImeDialogParam *param, const void *extended);
    int sceImeDialogGetStatus(void);
    int sceImeDialogGetResult(ImeDialogResult *result);
    int sceImeDialogAbort(void);
    int sceImeDialogTerm(void);
    int sceSysmoduleLoadModule(std::uint16_t id);
}

namespace hui::ps5
{
namespace
{

class SceImeBackend final : public ImeBackend
{
  public:
    explicit SceImeBackend(int user) : user_(user)
    {
    }

    bool open(const Params &params) override
    {
        if (!prepare())
            return false;
        ImeDialogParam param{};
        param.user_id = user_;
        param.type = params.kind == KeyboardKind::url      ? kTypeUrl
                     : params.kind == KeyboardKind::number ? kTypeNumber
                                                           : kTypeDefault;
        param.option = params.secret ? kOptionPassword : 0;
        param.max_text_length = params.max_length;
        param.input_text_buffer = params.buffer;
        param.posx = 960.0f;
        param.posy = 540.0f;
        param.horizontal_alignment = kAlignCenter;
        param.vertical_alignment = kAlignCenter;
        param.title = params.title;
        const int result = sceImeDialogInit(&param, nullptr);
        sys::log("[Afterglow] keyboard open=0x%08x", static_cast<unsigned>(result));
        return result == 0;
    }

    Status status() override
    {
        const int status = sceImeDialogGetStatus();
        if (status == kStatusRunning)
            return Status::running;
        return status == kStatusFinished ? Status::finished : Status::none;
    }

    End end() override
    {
        ImeDialogResult result{};
        const int rc = sceImeDialogGetResult(&result);
        if (rc != 0)
            sys::log("[Afterglow] keyboard result=0x%08x", static_cast<unsigned>(rc));
        if (rc != 0)
            return End::aborted;
        return result.end_status == kEndOk          ? End::ok
               : result.end_status == kEndCancelled ? End::cancelled
                                                    : End::aborted;
    }

    void abort() override
    {
        sceImeDialogAbort();
    }

    void close() override
    {
        sceImeDialogTerm();
    }

  private:
    // Before the first sceImeDialogInit: the dialog's module is loaded
    // through the system module table. sceCommonDialogInitialize should follow,
    // but the payload SDK ships no libSceCommonDialog import stub, so it cannot
    // be linked; the log says so in case the dialog misbehaves on hardware.
    // A failed load (a negative SCE error) fails this open rather than calling
    // into a module that is not there, and is tried again on the next one.
    bool prepare()
    {
        if (prepared_)
            return true;
        const int loaded = sceSysmoduleLoadModule(kSysmoduleImeDialog);
        sys::log("[Afterglow] keyboard module=0x%08x; sceCommonDialogInitialize skipped (no "
                 "libSceCommonDialog stub in the SDK)",
                 static_cast<unsigned>(loaded));
        prepared_ = loaded >= 0;
        return prepared_;
    }

    int user_;
    bool prepared_ = false;
};

} // namespace

std::unique_ptr<ImeBackend> make_ime_backend(int user)
{
    return std::make_unique<SceImeBackend>(user);
}

} // namespace hui::ps5
