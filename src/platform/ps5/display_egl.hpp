// ps5-homebrew-ui - EGL display and OpenGL 4.6 Core context.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <EGL/egl.h>

namespace hui::ps5
{

// Owns the EGL display, window surface and GL 4.6 Core context. ps5-opengl
// presents fullscreen; with runtime display modes the size is chosen before
// EGL starts, so changing it means close() and open() again. Every GL object
// dies with the context and must be recreated by its owner.
class Display
{
  public:
    Display() = default;
    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;
    ~Display();

    // Opens at width x height (1920x1080, 2560x1440 or 3840x2160) when the
    // SDK supports runtime display modes, else at the SDK's fixed profile.
    // Refresh accepts 60 or 120; unsupported output safely remains at 60.
    // A changed refresh needs an app restart and recreation of every GL object.
    bool open(int width = 1920, int height = 1080, int refresh_hz = 60);
    bool swap();
    void close();
    // True when the SDK can change the display size at runtime.
    static bool supports_display_modes();

    int width() const
    {
        return width_;
    }
    int height() const
    {
        return height_;
    }
    // Accepted output rate, queried after the first presentation. Until then
    // return a conservative 60; the SDK profile macro is not live HDMI status.
    int refresh_hz() const
    {
        return refresh_hz_;
    }
    bool refresh_confirmed() const
    {
        return refresh_confirmed_;
    }
    EGLint last_error() const
    {
        return last_error_;
    }

  private:
    void fail(const char *operation);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    int width_ = 0;
    int height_ = 0;
    int refresh_hz_ = 60;
    int requested_refresh_hz_ = 60;
    bool refresh_queried_ = false;
    bool refresh_confirmed_ = false;
    EGLint last_error_ = EGL_SUCCESS;
};

const char *egl_error_name(EGLint error);

} // namespace hui::ps5
