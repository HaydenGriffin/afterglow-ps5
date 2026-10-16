# projectM integration

`tools/build-projectm.sh [ps5|host|all]` fetches the pinned upstream revision,
initializes its pinned evaluator submodule, applies our source patch, generates
embedded shader resources and public headers, then builds incremental static
archives in `.deps/projectm`. The normal application and host-preview scripts
invoke it automatically. Dependencies remain outside the source repository.

The library uses exceptions internally for preset and shader failures. The
application uses the C API and retains `-fno-exceptions -fno-rtti`.

Local adaptations:

- `DynamicLibrary_static.cpp` supplies the linked EGL context functions without
  trying to open a shared EGL library on the PS5. GL entry points still come
  from the application's `eglGetProcAddress` callback.
- `visual-seed.patch` and `visual_seed.hpp` replace projectM's random seed source
  on PS5. libc++'s `std::random_device` calls an unavailable `arc4random` import.
  The replacement seeds visual effects from a monotonic clock and counter;
  it is deliberately scoped to projectM and is not a security random source.
  Host builds keep `std::random_device`.
- `blur-framebuffer.patch` preserves the caller framebuffer before upstream
  blur-texture allocation can unbind it. This prevents first-frame waveform
  drawing to the default framebuffer, caught by the surfaceless preview.
- `line-loop-strip.patch` draws waveform and shape outlines as a line strip
  that repeats its first index instead of `GL_LINE_LOOP`. The picture is the
  same; the PS5 driver otherwise lowers every line loop on the CPU and prints
  a `[ps5-gallium] lowered-line-loop` line per draw, about 700 lines a second
  in the title log while MilkDrop runs.
- `ps5_compat.c` refuses unused filesystem timestamp writes. The build extracts
  the SDK's C-locale conversion objects without linking its raw syscall object.

License texts are in `licenses/` and are copied into the installed app.
The complete pinned source, patch, build script and application source must
remain available when distributing a linked build.
