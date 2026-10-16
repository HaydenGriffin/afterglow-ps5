// ps5-homebrew-ui - Audio output thread through sceAudioOut.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/ps5/audio_out.hpp"

#include "platform/ps5/system.hpp"

extern "C"
{
    int sceAudioOutInit(void);
    int sceAudioOutOpen(int user, int type, int index, unsigned int grain_frames,
                        unsigned int frequency, unsigned int format);
    int sceAudioOutOutput(int handle, const void *samples);
    int sceAudioOutClose(int handle);
}

namespace hui::ps5
{

namespace
{

constexpr int kAlreadyInitialized = static_cast<int>(0x8026000e);
constexpr int kSystemUser = 0xff;
constexpr int kPortMain = 0;
constexpr unsigned int kFormatS16Stereo = 1;

} // namespace

AudioOut::~AudioOut()
{
    stop();
}

bool AudioOut::start(afterglow::Client &client, afterglow::Analyzer &analyzer)
{
    if (running_ || handle_ >= 0)
        return false;
    client_ = &client;
    analyzer_ = &analyzer;
    return start(silent_mixer_);
}

bool AudioOut::start(audio::Mixer &mixer)
{
    if (running_ || handle_ >= 0)
        return false;
    const int init = sceAudioOutInit();
    if (init != 0 && init != kAlreadyInitialized)
    {
        sys::log("[HUI] audio sceAudioOutInit=0x%08x", static_cast<unsigned>(init));
        return false;
    }
    handle_ = sceAudioOutOpen(kSystemUser, kPortMain, 0, kGrainFrames, audio::kSampleRate,
                              kFormatS16Stereo);
    if (handle_ < 0)
    {
        sys::log("[HUI] audio sceAudioOutOpen=0x%08x", static_cast<unsigned>(handle_));
        handle_ = -1;
        return false;
    }
    mixer_ = &mixer;
    stop_.store(false);
    // Never name threads: pthread_setname_np hangs on this platform.
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
    {
        sceAudioOutClose(handle_);
        handle_ = -1;
        return false;
    }
    if (pthread_attr_setstacksize(&attributes, 1024 * 1024) != 0)
    {
        pthread_attr_destroy(&attributes);
        sceAudioOutClose(handle_);
        handle_ = -1;
        return false;
    }
    const int created = pthread_create(&thread_, &attributes, &AudioOut::thread_main, this);
    pthread_attr_destroy(&attributes);
    if (created != 0)
    {
        sys::log("[HUI] audio thread creation failed");
        sceAudioOutClose(handle_);
        handle_ = -1;
        return false;
    }
    running_ = true;
    sys::log("[HUI] audio open handle=%d grain=%d rate=%d", handle_, kGrainFrames,
             audio::kSampleRate);
    return true;
}

void *AudioOut::thread_main(void *self)
{
    static_cast<AudioOut *>(self)->run();
    return nullptr;
}

void AudioOut::run()
{
    alignas(64) std::int16_t grain[kGrainFrames * 2];
    while (!stop_.load(std::memory_order_relaxed))
    {
        if (client_ != nullptr)
            client_->render(grain, kGrainFrames);
        else
            mixer_->render(grain, kGrainFrames);
        if (sceAudioOutOutput(handle_, grain) < 0)
        {
            errors_.fetch_add(1, std::memory_order_relaxed);
            sys::sleep_us(5000);
            continue;
        }
        grains_.fetch_add(1, std::memory_order_relaxed);
        if (analyzer_ != nullptr)
            analyzer_->push(grain, kGrainFrames);
    }
    sceAudioOutOutput(handle_, nullptr); // drain the queued grain
}

void AudioOut::stop()
{
    if (running_)
    {
        stop_.store(true);
        pthread_join(thread_, nullptr);
        running_ = false;
    }
    if (handle_ >= 0)
        sceAudioOutClose(handle_);
    handle_ = -1;
}

} // namespace hui::ps5
