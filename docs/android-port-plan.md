# Android port plan (arm64-v8a, Galaxy M33 5G)

Status: written as a design study on 2026-11-26 and updated the same day. Sections 1 to 10 are the
original study and keep their [F]/[H] markers as written at the time; milestones A0 to A4 have since been
reached on the device (section 11 lists the status and `docs/08-progress.md` the numbers), A5 is partly done.

Target device: Samsung Galaxy M33 5G (Exynos 1280: 2x Cortex-A78 + 6x Cortex-A55, Mali-G68 MP4,
Vulkan 1.1, Android 14 or later). The CPU/GPU layout is the public spec, not checked here; every
Mali capability below is a **hypothesis until read from the A2 boot log**, because the runtime
already prints most of them at start-up.

## How to read this

- **[F]** = fact, with file:line evidence from the dev clone
  `work\wii-recomp\recomp` (branch `seaofstar-tod`). Line numbers refer to that clone:
  `runtime\recomp\runtime` in the root repo differs from it in at least four files
  (`control_pipe.c`, `gx_vulkan_ngx.c/.h`, `input.c`), so re-grep after the next sync.
  `build_file.py` is cited by identifier, not by line.
- **[H]** = hypothesis, with how it will be tested.
- Paths below are relative to `work\wii-recomp\recomp` unless they start with `docs` or `tools`.

## 1. Summary

The Android port is mostly "the existing `linux-aarch64` path (Raspberry Pi 5) without glibc,
ALSA, GLFW and `execv`, plus an app shell". [F] Upstream already targets Pi 5
(`CLAUDE.md` of the fork: "Targets: Intel Mac and Raspberry Pi 5, and Windows 10/11 and Linux
x86-64"; `build_file.py` has `-mcpu=cortex-a76` for Linux aarch64; `guest_memory.h:25` and
`drive.c:67` have aarch64 branches; `processor.c:66` and `threads.c:52,395-446` have aarch64
FPCR and context-switch assembly). The generated C is portable. The real work is the
window/input layer (GLFW to SDL3), the thread-context switch (no `ucontext` on bionic),
the cross build, loading and storage rules, and speed.

### Key blockers, ranked

1. **Thread context switching.** The non-Windows path of `threads.c` calls `getcontext`,
   `makecontext` and `setcontext` (lines 347-357, 380, 499, 615). Bionic declares `ucontext_t`
   but, to the best of my knowledge, does not implement those functions [H: check the NDK
   sysroot at A0 with `nm`/`grep` on `libc.so` and `ucontext.h`]. If missing, port the Win64
   pattern (`context_capture`/`context_resume`/`start_on_stack`, `threads.c:34-60,163-250,305-345`)
   to aarch64. The aarch64 `RECORDING_STUB` and `return_to_caller` already exist
   (`threads.c:421-470`); what is missing is the fresh-stack start and the exception-time capture.
2. **Speed.** [H] Whether a 2xA78 + 6xA55 phone can hold 30 fps is unknown. The guest runs on
   one thread, the Pi 5 (4x A76) is the nearest measured platform, and the title's per-frame
   CPU cost has not been measured on any Arm device. Decided only at A3.
3. **Mali GPU and driver fit.** [H] Dual-source blending, fragment stores/atomics (the
   bounding box), D32 depth blit, shader-written depth (`gl_FragDepth`, `gx_tev.c:626,637`) on a
   tile GPU, and the cost of compiling 600+ pipelines on a mobile driver at first start.
4. **Window and surface lifecycle.** GLFW is used directly in six runtime files (section 3).
   There is no `VK_ERROR_SURFACE_LOST_KHR` handling and `vk_check` exits on any error
   (`gx_vulkan_device.c:25-29`), so backgrounding the app would kill the game. And
   `preTransform = capabilities.currentTransform` (`gx_vulkan_device.c:~493-496`) will give a
   rotated picture on a portrait-native panel unless the blit/FSR path pre-rotates; switching to
   IDENTITY instead returns `VK_SUBOPTIMAL_KHR`, which this code treats as "outdated" and
   rebuilds the swapchain (`gx_vulkan.c:2326,2389-2411`), possibly every frame [H].
5. **Build system is host-keyed.** `build_file.py` and `add_game.py` choose flags, sources,
   link flags and library suffix from `sys.platform` (`add_game.py:49`; `PLATFORMS`,
   `CONSOLE_LINK_FLAGS`, `AUDIO_SOURCES`, `REMOTE_SOURCES`, `NETWORK_SOURCES`,
   `LIBRARY_SUFFIX` in `build_file.py`). A cross build needs an explicit target parameter.
   There is no CMake in upstream; ninja files are generated.
6. **`__linux__` is defined on Android**, so every Linux branch is taken: ALSA audio
   (`audio_output_linux.c:3,24`), BlueZ/hidraw remotes (`remote_link_linux.c`,
   `remote_link_hidraw.c`), fork-based network (`net_ip.c`, `net_ssl.c`, `net_routes.c`),
   `debug.c:188` (`event._sigev_un._tid`, a glibc field name [H: bionic may name it differently]),
   `gx_fifo.c:14` `<linux/futex.h>`, `drive.c:67` returning `linux-aarch64` for the image's
   native-code key.
7. **Process model.** `console.c:150,184` use `execv` (Wii Menu hand-over and hot reset) and
   power-off is `_exit(0)` (`console.c:~170`). Direct boot of a `.wrd` never calls `console_restart`
   unless a console folder is in use (`console.c:35,134`), so A2/A3 can ignore it, but the
   Activity must run the game in its own process. The NSMB reference does exactly this
   (`android:process=":game"`, "the native runtime is built to start once per process").
8. **Logs and control.** The runtime reports only through `stderr`, which an Android app
   discards. A2's evidence ("log + pipe shot") needs stderr redirected to a file (or logcat).
   The POSIX control pipe is a FIFO and one-way (`control_pipe.c:326-334`, replies are stderr only),
   and it cannot live on FUSE external storage.
9. **Paths and storage.** `use_folders_beside` (`main.c:147-160`) needs `realpath(argv[0])`, which is
   meaningless inside an APK. The Java side must set all eight `WII_RECOMP_*` path variables
   itself. A 2.36 GB image has to live in app-specific storage.
10. **Address space.** Fixed 1 GiB window at `0x1000000000`, a 1 GiB game-thread stack, and 32 MiB
    stacks per guest context (section 5). Probably fine on a 39- or 48-bit VA kernel [H].

## 2. Inventory: Win32 / x86-only dependencies in the runtime

"Covered" means an existing non-Windows, non-x86 branch. "aarch64 Linux" = the Pi 5 path.

| # | Area | Evidence [F] | What exists today | Android action |
|---|---|---|---|---|
| 1 | Guest memory reservation | `guest_memory.h:21-28` picks the base per CPU: x86_64 `0x100000000000`, Apple arm64 `0x8000000000`, other arm64 `0x1000000000` (64 GiB, "reachable with the 39-bit address spaces some kernels use"). `guest_memory.h:~60-90` `mmap(PROT_NONE, MAP_NORESERVE, MAP_FIXED_NOREPLACE)` of 1 GiB + 64 KiB, then `mprotect` of MEM1/MEM2/locked cache. `guest_memory.c:11-14,54` Windows uses `VirtualAlloc` and a vectored exception handler. | Linux/mac: `mmap` + `sigaction(SIGSEGV/SIGBUS)` handler, `say_crash` reads `uc_mcontext.pc` for Linux aarch64 (`guest_memory.c:114-120`). | **No change expected** [H]. Test that the address is free in a zygote child (`/proc/self/maps` at A2). Fallback: a second base constant for Android. Note ART/libsigchain: our `sigaction` replaces the handler and chains `before_segv`; check it still works under ART's chain [H]. |
| 2 | Processor state variables | `cpu.h:20-21,62-63`: on Windows `cpu` and `interrupt_budget` sit at a fixed address (`HOST_STATE_ADDRESS`, `guest_memory.h:32-35`), because a DLL cannot name an exe variable. Elsewhere `extern Cpu cpu;` defined in `main.c:34-36` and `interrupts.c`. | Linux: exe exports them (`-rdynamic`), the title resolves them at load. | On Android the runtime becomes a shared library, so `cpu` is reached through the GOT from the title [H: measurable slowdown on every register access]. Option: reuse the Windows fixed-address scheme for Android (also removes the GOT load). Needs `win_compat.c`-style reservation outside `_WIN32`. Both options change a hashed interface header (`cpu.h`, `guest_memory.h`), which forces a rebuild of the Windows `title.dll` too. |
| 3 | Loading the title | `title.c:6-21` Windows shim maps `dlopen/dlsym` to `LoadLibraryA/GetProcAddress`; `title.c:33,38` `dlopen(path, RTLD_NOW\|RTLD_LOCAL)`. `main.c:115-121` and `drive.c:350-355` choose `.dylib/.dll/.so`. `drive.c:63-73` platform key: `darwin-*`, `linux-aarch64`, `linux-x86_64`, `windows-x86_64`. `make_disc.py` `PLATFORMS` set has the same five. | POSIX `dlopen`; image carries native code per platform and `drive_native()` writes it to `WII_RECOMP_CACHE` first (`drive.c:~340-370`). | Add `android-arm64` to `drive.c` (test `__ANDROID__` before `__linux__`) and to `make_disc.py`. `drive_native` extracting to the app's internal files dir then `dlopen` there is the plan [H: SELinux allows mapping an app-data `.so`; external storage is noexec]. Fallback: pack `libtitle.so` into the APK's `jniLibs`. |
| 4 | Title/runtime symbol boundary | `translate/title.def` exports only `wii_recomp_title`. Windows links the title against `libconsole.a` (`build_file.py` `TITLE_LINK_FLAGS["win32"]`, `CONSOLE_LINK_FLAGS`). Linux/mac use `-rdynamic` on the exe and `-shared` / `-undefined dynamic_lookup` for the title. | See left. | Runtime becomes `libmain.so` (SDL's expected name) with the symbols exported; `libtitle.so` is linked with `-lmain` (so `DT_NEEDED` names it) [H: required for Android's linker namespaces; verify with `readelf -d` at A1]. |
| 5 | Named pipe / control | `control_pipe.c:285-326` Windows named pipe; `:326-334` POSIX: `unlink`, `mkfifo`, thread reading it (one-way, `pipe_write` is a no-op, replies on stderr). | Linux works as is. | Keep the FIFO branch; place it in app-internal storage; drive it with `adb shell run-as <pkg> sh -c 'echo "shot /path" > fifo'` [H]. Add a replies-to-file or loopback-TCP transport so `shot` evidence returns without parsing logcat. |
| 6 | Network | `net_win.c:6` (`#ifdef _WIN32`) stubs the IOS network; `build_file.py` `NETWORK_SOURCES`/`POSIX_NETWORK_SOURCES` leaves `net_ip.c`, `net_ssl.c`, `net_routes.c` (sockets, socketpair, fork, `sys/wait.h`) out on Windows; `main.c:203-206` sets `WII_RECOMP_SHOP_SERVER=off`. | Linux: real sockets via fork helpers. | Android: reuse the stub (rename to a generic `net_stub.c`, guard by a build flag, not `_WIN32`). `fork` in an app process is a bad idea. |
| 7 | Real Wii Remotes | `remote_link_win.c` (no-op), `remote_link_linux.c` (BlueZ sockets), `remote_link_hidraw.c`. | Mac, Linux. | Use a stub backend (as Windows does). Phone touch/sensor remote is done above the runtime (A4). |
| 8 | Audio output | `audio_output_win.c` (37 lines) is pure SDL3 (`SDL_OpenAudioDeviceStream`, 512-frame hint, `audio_pull`). `audio_output_linux.c:3,24` is ALSA (`snd_pcm_open`). | Mac: AudioToolbox; Linux: ALSA. | Reuse the SDL3 file as `audio_output_sdl.c` for Windows and Android (`build_file.py` `AUDIO_SOURCES`). SDL3's Android backend is AAudio/OpenSL [H]. |
| 9 | Threads / contexts | `threads.c:22` `HOST_STACK_BYTES` = 32 MiB; `:34-60` per-platform `CallerRegisters`; `:163-250` Win64 and x86-64 `return_to_caller`; aarch64 `return_to_caller` follows `:250`; `:305-345` Windows `start_on_stack`/`start_fresh`; `:347-357` POSIX `start_fresh` uses `getcontext/makecontext/setcontext`; `:380` `setcontext(&slot->host)`; `:395-470` `RECORDING_STUB` for Win64, x86-64, aarch64; `:499,615` `getcontext` in `save_context` (non-RECORDS_CALLER) and `enter_exception`. | aarch64 Linux uses ucontext for fresh start and exceptions and the recorded-registers stub for save/load. | **Blocker 1.** Add an aarch64 `start_on_stack` and capture/resume pair (the Win64 version is the template) and drop `ucontext` for `__ANDROID__`. Alternative: vendor `libucontext` (MIT) [H]. |
| 10 | FPU modes | `processor.c:8-10,62-72`: SSE `_mm_setcsr` on x86_64, `mrs/msr fpcr` on aarch64, else `gap_hit`. | aarch64 FPCR rounding + flush-to-zero already implemented (Pi). | No change. Keep `-ffp-contract=off -fsigned-char` (`build_file.py` `FLAGS`, "keep Arm matching the Mac"). |
| 11 | CPU feature check | `main.c:45-62` x86 `cpuid`/`xgetbv` constructor, guarded by `__x86_64__`. `build_file.py`: `-march=x86-64-v3` only for Windows and Linux x86_64. | Not compiled on Arm. | None. For Android choose `-mcpu`/`-march` (section 7). |
| 12 | Timers/priorities | `main.c:12-14,100-103`, `gx_fifo.c:494` `prctl(PR_SET_TIMERSLACK)`; `gx_fifo.c:463-486` futex wait/wake via `syscall(SYS_futex,...)`; `debug.c:10,151,180-188` SIGPROF sampler with `SIGEV_THREAD_ID` (`#if __linux__ && (x86_64\|\|aarch64)`). | Linux. | `prctl` and futex exist on bionic [H]. Disable the sampler on Android unless `_sigev_un._tid` compiles (use `sigev_notify_thread_id`). |
| 13 | Command ring | `gx_fifo.c:414-420` ring of 16 MiB mapped twice; `:519-531` `ring_file()`: `shm_open` on Apple, `memfd_create` otherwise; `:535-560` Windows `VirtualAlloc2`/`MapViewOfFile3`; `:565-575` POSIX double `mmap`. | Linux uses `memfd_create`. | Needs API 30+ for libc `memfd_create` (or `syscall(SYS_memfd_create)`); minSdk 30 is fine for Android 14 targets. Confirm app seccomp allows it [H]. |
| 14 | Process/path helpers | `main.c:123-160` `realpath`, `dirname`, `libgen.h`; `console.c:150,184` `execv`. | Linux. | Env-var overrides (section 8); `execv` only on console-folder paths. |
| 15 | Win-only files | `win/win_compat.h`, `win/sys/mman.h`, `win_compat.c` (VirtualAlloc-based `mmap/mprotect/munmap`), `runtime_flags = -include ../runtime/win/win_compat.h`. | Not used off Windows. | Nothing. |
| 16 | NGX / DLSS | `gx_vulkan_ngx.c:16-29` already has an `#ifndef _WIN32` stub block returning 0; `:32-34,236,298` Windows-only `LoadLibraryW/GetProcAddress`. Called from `gx_vulkan_device.c:59,72,158,179,231,246,262`. | Stubs on Mac/Linux. | **Already excluded off Windows.** Keep `WII_RECOMP_NGX` unset. No change. |
| 17 | OpenGL backend | `gx_gl.c:5,54-64,421` GLFW + desktop GL 3.3 core. | Optional backend (`BACKEND_SOURCES`). | Not built on Android (`build_file.py` backend list = `vulkan` only), as on the Pi. |

## 3. Window, input and SDL3

### Current GLFW usage [F]

| File | Use |
|---|---|
| `window.c` | everything: `glfwInit` (`:211`), `glfwInitVulkanLoader` (`:207-209`), `glfwCreateWindow` (`:225`), size/aspect (`:229-233`), key/cursor/button callbacks (`:234-236`, handlers `:82-145`), fullscreen via monitors (`:50-76`), work-area fit (`:185-187`), event loop `glfwWaitEventsTimeout` + `pads_poll` (`:267-282`), `glfwGetFramebufferSize` (`:244`) |
| `controllers.c:23-83` | key-name table built from `GLFW_KEY_*` codes (`controllers.ini` key names) |
| `gx_vulkan.c:138-140` | `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`; `window_ready` creates instance+surface |
| `gx_vulkan_device.c:56,62,85` | `glfwVulkanSupported`, `glfwGetRequiredInstanceExtensions`, `glfwCreateWindowSurface` |
| `gx_shader.c:116-118` | `glfwGetTime` for shader-build timing (trivial: use `clock_gettime`) |
| `gx_gl.c` | OpenGL only; not built |
| headers | `gx.h:320-324`, `gx_backend.h:80-81`, `gx_vulkan.h:105` expose `struct GLFWwindow *` |

### Current SDL3 usage [F]

`pads.c` (gamepads, hidapi hints, rumble, gyro/accel sensors, a virtual joystick for the
control pipe, `SDL_PollEvent` loop at `:482-487`), `audio_output_win.c` (audio),
`pad_map.h`. SDL 3.4.20 headers are in the MSYS2 CLANG64 tree on this PC. SDL is already a
hard build dependency (`sdl3_flags()` in `build_file.py`).

### What an SDL3-only layer needs

1. A small platform interface: `platform_window_create/size/refresh/run`,
   `platform_vulkan_instance_extensions`, `platform_vulkan_create_surface`, `platform_time`,
   with a GLFW implementation (today's behaviour, kept for desktop) and an SDL3 one. Replace
   `struct GLFWwindow *` in the three headers with `void *`.
2. Event pump: `pads.c:484` already drains **all** SDL events, so window, keyboard, mouse
   and touch events must go through one pump or they will be dropped. Replace
   `glfwWaitEventsTimeout` with `SDL_WaitEventTimeout` inside `window_run`.
3. Key names: `controllers.c` maps ini names to GLFW codes; add a table of SDL scancodes
   (or a neutral internal key enum) so `controllers.ini` keeps working.
4. Vulkan: `SDL_Vulkan_LoadLibrary`, `SDL_Vulkan_GetInstanceExtensions`,
   `SDL_Vulkan_CreateSurface`, `SDL_WINDOW_VULKAN`. On Android SDL owns the `ANativeWindow`
   lifecycle and sends background/foreground events; add surface re-creation on
   `VK_ERROR_SURFACE_LOST_KHR` instead of `vk_check`'s exit.
5. Entry point: Android's `SDLActivity` loads `libmain.so` and calls `SDL_main`; `main.c`
   needs `#include <SDL3/SDL_main.h>` on Android. `main()` runs on SDL's own thread, which is
   fine (`window_run` needs the "main" thread only on macOS).
6. Fullscreen/F11, `fit_to_screen`, aspect lock: drop on Android (the Activity is fullscreen
   landscape; letterbox in the blit).
7. Headless: `WII_RECOMP_NO_WINDOW=1` only suppresses presentation (`gx_vulkan.c:2307-2313`);
   `window_create` still creates a window. A true no-surface boot is reachable only via
   `window_ready(0)` (`gx_vulkan.h:96`, `gx_vulkan_device.c:54-55`), which nothing in `main`
   calls. Add `window_create` skipping the window when `NO_WINDOW` is set; this makes A2 possible as a plain
   NDK executable under `adb shell` with no APK or SDL window [H].
8. Touch: SDL3 synthesises mouse events from touch by default; the existing
   mouse-as-pointer path (`window.c:122-145`, `controllers_mouse_*`) therefore gives a working
   Wii pointer for free [H]. A proper touch remote is A4.

## 4. Vulkan

[F] Instance: `VK_API_VERSION_1_1` (`gx_vulkan_device.c:64`); window extensions from GLFW;
optional `VK_EXT_swapchain_colorspace` (`:70`), `VK_KHR_portability_enumeration` (`:65-69`),
validation layer on request (`:71-78`). Device: `VK_KHR_swapchain` required with a window
(`:130,176`); `VK_KHR_push_descriptor` **optional**, with a descriptor-pool fallback
(`:177-178`, `:~345-365`); `dualSrcBlend`, `fragmentStoresAndAtomics`,
`samplerAnisotropy` each requested only if available and degrade with a log line
(`:182-196`); `VK_KHR_performance_query` only for measuring; portability subset only on Metal.
Queue family must support graphics + present (`:100-113`); a second queue is optional
(`vk_window_wants_own_queue`). Swapchain: `TRANSFER_DST` (+ `COLOR_ATTACHMENT` for FSR),
format `B8G8R8A8/R8G8B8A8_UNORM`, present mode FIFO/MAILBOX/IMMEDIATE picked by
`WII_RECOMP_PRESENT` (`:407-440`), 3 images wanted. Pipeline cache: `VkPipelineCache`
seeded from `driver.bin` (`gx_vulkan_pipeline.c:349-355`).
Shaders: GLSL `#version 450` compiled at run time by **shaderc**
(`gx_vulkan_pipeline.c:6,68-69,344-346,386-390`, target `vulkan_1_1`); `gx_vulkan.c` FSR
shaders also GLSL, same path. NGX is already stubbed out (inventory row 16). FSR 1.0
(EASU/RCAS, `gx_vulkan.c:1633-1851`) is plain fragment shaders with push constants and needs
`swap_drawable` (swapchain `COLOR_ATTACHMENT` usage), so it is portable.

### Likely Mali-G68 / Vulkan 1.1 gaps (all [H] until the A2 log)

| Item | Why it matters | How the runtime reacts | Check |
|---|---|---|---|
| `dualSrcBlend` | constant-alpha blends | logs "no dual-source blending..." and continues (`:186-187`); shader text unaffected | A2 log; or vulkan.gpuinfo.org entry for Mali-G68 |
| `fragmentStoresAndAtomics` | bounding box via `atomicMin/Max` in the fragment shader (`gx_tev.c:693,738-746`) | logs and the bounding box stays as the game set it | A2 log |
| D32_SFLOAT blit/linear filter | depth copies (`gx_vulkan_device.c:~268-273`) | logs "can't blit depth, so depth copies will fail" | A2 log |
| `VK_KHR_push_descriptor` | texture binding path | falls back to descriptor pools | A2 log |
| Present modes | `MAILBOX` often missing on Android | falls back to FIFO | A2 log |
| `supportedUsageFlags` COLOR_ATTACHMENT on swapchain | FSR | falls back to linear blit with a log line (`gx_vulkan.c:1846-1849`) | A2 log |
| `currentTransform` != IDENTITY | rotated output | **not handled** | A2 screenshot of the window |
| Shader-written depth (`gl_FragDepth`) | disables early-Z on tile GPUs, cost unknown | none | A3 GPU timing |

### Shader cache portability [F]

The GLSL text depends on the picture scale (`gx_scale_numerator/denominator` is written into
`%.6f` constants, `gx_tev.c:659,740-746`; default is 2, `gx_draw.c:35`) and on
`bounding_box` (`gx_shader.c:206-211`), which follows `fragmentStoresAndAtomics`. So a
`stages.bin` pre-baked on the PC at scale 2 will **not** hit on the phone at another scale or
with a different bounding-box capability. Consequences [H]: either run the phone at the same
scale and caps (unlikely to be affordable at scale 2), or ship shaderc for arm64 and let the
phone compile the text, or generate `stages.bin`/recipes on the PC at the chosen Android
scale. `driver.bin` (the GPU driver's own cache) is device-specific and must always be built
on the phone; the first start recompiles every recipe (617 in the image) on a mobile driver,
which may take minutes [H]. This is an A3/A5 item, not an A2 blocker.

### shaderc on Android [H]

NDK ships `glslc` as a host tool; whether it still ships a **device** `libshaderc` is
uncertain (check at A0). If not, build shaderc for arm64 (its source tree supports Android) or
drop run-time compilation behind a SPIR-V cache and a fallback.

## 5. Guest memory layout [F]

- One window at a **fixed** host address: `GUEST_BASE_ADDRESS` (`guest_memory.h:21-28`),
  size `GUEST_WINDOW` = 1 GiB (`:40`) plus a 64 KiB guard; everything indexes it by
  `flat(addr) = GUEST_BASE + (addr & 0x3fffffff)` (`cpu.h:~95`). MEM1 24 MiB at 0, MEM2 64 MiB
  at 0x10000000, 16 KiB locked cache at 0x20000000; the rest is `PROT_NONE` guard pages the
  signal handler opens on touch (`guest_memory.c:~143-175`) and `memory_watch.c:44` uses
  `mprotect` for write tracking. Pages are 4 KiB or 16 KiB aware (`guest_memory.h:14-15` comment).
- The base address is **baked into the generated code** (the inline `flat()` adds a constant),
  so the title and the runtime must agree. `guest_memory.h` is a hashed interface header
  (`INTERFACE_HEADERS`), so changing it makes old libraries refuse to load.
- Other reservations: game thread stack 1 GiB (`main.c:38`, `pthread_attr_setstacksize`),
  32 MiB `malloc`'d per guest context, up to 256 contexts (`threads.c:20,22`), commands thread
  16 MiB stack + 16 MiB ring mapped twice (`gx_fifo.c:414,565-575`), Vulkan host rings of 32 + 16 + 4
  + 64 MiB (`gx_vulkan_device.c:14-17`) in each of `FRAMES_IN_FLIGHT = 3` frames
  (`gx_vulkan.h:18`) [H: about 350 MiB host-visible, allocation code not read], and the EFB at
  scale 2 (1280x960).
- Android assessment [H]: a 64 GiB base sits inside a 39-bit (512 GiB) address space and is
  unlikely to collide with the zygote's mappings; `MAP_FIXED_NOREPLACE` makes a clash an
  immediate, logged failure (`guest_memory.h:~70-85`). The 1 GiB stack and the NORESERVE window are
  virtual only. Total resident should be well below the M33's 6-8 GB. Check `/proc/self/maps`
  and `VmRSS` at A2.
- A fixed address is **not** mandatory for correctness only because every access adds the
  constant; making it relocatable would mean loading it from a register or global in every
  guest access (a large performance and code-size change). Not planned.

## 6. Generated code and the title library

- [F] `build-SO3EE9`: 464 `.c` files in `generated/` (221 `cold`, 213 `part`, 11 `m0_cold`,
  12 `m0_part`, plus `table`, `m0_table`, `title`, `modules`, `apploader`, two headers),
  81 MB of C, 463 objects (64 MB), `title.dll` 54 MB, `games\SO3EE9.wrd` 2.36 GB.
- [F] The files include only `functions.h`, `m0_functions.h`, `title.h` and `modules.h`.
  A grep for `immintrin|emmintrin|xmmintrin|_mm_|__builtin_ia32|__m128|arm_neon|__asm__`
  over `generated/` found nothing. Floating point and byte swaps use plain C and
  `__builtin_bswap*` (`cpu.h`). Functions chain with `__attribute__((musttail))` (e.g.
  `cold0098.c`), which needs Clang: the NDK's Clang qualifies. So the generated code is
  portable C.
- [F] The only x86 flag on the title is `-march=x86-64-v3` (`build.ninja` line 1 and each
  `cc` edge), injected by `build_file.py` for Windows and Linux x86_64. Others in use:
  `-w -fno-strict-aliasing -fwrapv -fsigned-char -ffp-contract=off -O2 -fPIC -fvisibility=hidden`
  and `-DTITLE_INTERFACE=<hash>`. `-fsigned-char` and `-ffp-contract=off` must stay for Arm.
- [F] The hash `TITLE_INTERFACE` covers `cpu.h guest_memory.h title.h modules.h`; the
  Android runtime and `libtitle.so` must be built from the same headers.
- Library boundary [F]: the title exports one symbol (`wii_recomp_title`, `translate/title.def`)
  and imports `cpu`, `interrupt_budget`, `dispatch`, `hw_read/hw_write`, `ps_load/ps_store`,
  `save_context`... from the runtime.
  For Android: build `libmain.so` with those symbols exported, build `libtitle.so` with
  `-shared -fvisibility=hidden`, an export of only `wii_recomp_title`, and a `DT_NEEDED` on
  `libmain.so` (`-L<dir> -lmain`) [H: unresolved symbols would otherwise fail at `dlopen`
  inside the app's linker namespace; verify at A1].
- Compile time: **unmeasured** for this title (object mtimes span three days; the build log has
  no timings; `build_file.py` comments only say "about half a minute" for a smaller game).
  Cross-compiling 464 files at `-O2` with the NDK Clang on this 16-thread PC is an A1
  measurement. Output size on arm64 is expected to be similar to or larger than the x64 54 MB [H].
- `GAME_OPTIMIZATION` and `QUICK_OPTIMIZATION` are both `-O2`; keep. Consider
  `-mcpu=cortex-a78` as tuning but a `-march=armv8.2-a` baseline so it also runs on the A55
  cores [H].

## 7. Build architecture for Android (proposal)

- Add an explicit **target** to `translate/build_file.py` and `tools/add_game.py`
  (`--target android-arm64`), independent of the host, with its own platform table:
  NDK Clang (`aarch64-linux-android30-clang` from the NDK's `toolchains/llvm`), `-fPIC`,
  no `-I../runtime/win`, no `-lglfw`, no `-lpthread` (libc), link `-lvulkan -llog -landroid
  -lSDL3 -lm` and shaderc [H], `AUDIO_SOURCES` = SDL, `NETWORK`/`REMOTE` = stubs,
  `LIBRARY_SUFFIX` = `so`, library names `libmain.so` and `libtitle.so`.
- Runtime is `-shared` (`libmain.so`), not an exe; `-O3` as now (`RUNTIME_OPTIMIZATION`).
- SDL3 for Android: build from source with the NDK (CMake or `Android.mk`), link statically
  into `libmain.so` (the NSMB reference does the same) or ship `libSDL3.so`. The MSYS2 SDL3
  is for Windows only.
- Same `generated/` sources for Windows and Android: the translation is not repeated, only
  the compile. `make_disc.py` gains an `android-arm64` native entry. Either one `.wrd` carries
  both `windows-x86_64` and `android-arm64`, or a separate Android image is packed
  (2.3 GB either way; the disc data dominates).
- Everything derived from the dump (`libtitle.so`, image, recipes) stays local and is never
  committed or distributed (ADR-008; `CLAUDE.md`). The APK that holds only `libmain.so` and
  Java is the only artefact that could ever be shared, and only if no game-derived data is in it.

## 8. Data locations and how the app points them at app storage

[F] All locations are overridable by environment variables read with `getenv`:

| Variable | Default (direct boot) | Source |
|---|---|---|
| (argv[1]) | the `.wrd` image or an extracted folder | `main.c:206-219`, `drive_insert` |
| `WII_RECOMP_SAVES` | `<program dir>/saves` (else `recomp/build/saves`) | `main.c:151`, `files.c:52-54` |
| `WII_RECOMP_SHADER_CACHE` | `<program dir>/shader-cache` | `main.c:151`, `gx_vulkan_pipeline.c:118-123,227` |
| `WII_RECOMP_TITLE_DATA` | `<program dir>/title-data` (`profile.txt`, `shaders.bin`) | `main.c:152`, `profile.c:27` |
| `WII_RECOMP_CACHE` | `<program dir>/native-cache` (the title `.so` extracted from the image) | `main.c:152`, `drive.c:340-370` |
| `WII_RECOMP_GAPS` | `<program dir>/gaps.txt` | `main.c:153` |
| `WII_RECOMP_REMOTE_SLOTS` | `<program dir>/remotes.txt` | `main.c:153` |
| `WII_RECOMP_CONTROLLERS` | `<program dir>/controllers.ini` | `main.c:154`, `controllers.c:432` |
| `WII_RECOMP_CERTIFICATES` | `<program dir>/certificates` | `main.c:154` |
| `WII_RECOMP_NATIVE` | explicit title library path (extracted-folder boot only) | `main.c:126` |
| `WII_RECOMP_SCALE`, `_FSR`, `_PRESENT`, `_WINDOW`, `_NO_WINDOW`, `_CONTROL`, `_FPS_LOG`, `_FRAME_LOG` | display / diagnostics | `gx_draw.c:40`, `gx_vulkan.c`, `gx_vulkan_device.c:407`, `window.c:162` |

`use_folders_beside` only sets a variable that is **unset** (`setenv(..., 0)`) and returns early
when `realpath(argv[0])` fails (`main.c:149`), so the Java side sets all eight before
`SDL_main`, as the NSMB reference does with `Os.setenv`.

Proposed layout on the phone [H]:

- `/storage/emulated/0/Android/data/<pkg>/files/SO3EE9.wrd`: pushed with `adb push`
  (2.36 GB). External app-specific storage needs no permission; `adb` (shell uid) can write it
  on most devices, verify on the M33 at A0. It is **noexec**, so native code is never run from
  here.
- `/data/data/<pkg>/files/`: `saves/`, `shader-cache/`, `title-data/`, `native-cache/`
  (extracted `libtitle.so`), `controllers.ini`, `control` (the FIFO), `log.txt`. Reachable
  from the PC with `adb shell run-as <pkg>` for a debuggable build.
- stderr: redirect to `files/log.txt` with `dup2` at start of `SDL_main` (and mirror
  to logcat), because the runtime prints everything on stderr.
- Saves: the user's save set (`RFF2_XX` through `/tmp/sys`, see the save-layout memory)
  has to be copied into `saves/` the first time; the same "every boot rewrites all RFF2_XX"
  rule applies, so snapshot before each run.

## 9. Reference skim: WitherBlitz/NSMB-Wii-Recompilation `android/` (study only, nothing copied)

Facts from the repo (GPL-3.0; a different runtime built on Aurora/Dawn, not this one):

- **Shell**: a Gradle app (`com.wither.nsmbw`, `compileSdk 34`, `minSdk 28`, `targetSdk 34`,
  `abiFilters 'arm64-v8a'`) with SDL's Java sources vendored (`org/libsdl/app/*`).
  `GameActivity extends SDLActivity`; there is no NativeActivity.
- **Native libs**: one `libmain.so` (game + runtime + SDL + Dawn linked together) staged
  from the native build by `tools/stage-android.ps1` into `android/native/jniLibs`;
  `useLegacyPackaging false` so it is stored uncompressed and page-aligned and mapped in place.
  `getLibraries()` and `getMainSharedObject()` are overridden; runtime data is staged as
  `assets`.
- **Process**: the game runs in `android:process=":game"` ("the native runtime is built to
  start once per process"), leaving it ends the process; an alternative main-process Activity
  exists for phones that kill extra processes ("Xiaomi mode") with a `RestartActivity`.
- **Environment**: Java `Os.setenv(...)` of runtime/data directories before the native code starts.
- **Manifest**: `android.hardware.vulkan.level` required, touchscreen/gamepad/bluetooth/sensor
  features optional, `sensorLandscape`, `resizeableActivity=false`, big `configChanges` list.
- **Touch and motion**: a Java `TouchControlsView` (on-screen remote, multi-finger, slide
  between buttons, cross-pad, layout editor activity) hands a button bitmask to native through
  `nativeSetTouchState(buttons, shake, tilt)`; `MotionInput` maps the phone's
  gravity/gyro to the remote's tilt and shake; menus accept taps (`nativeTap/nativePointer`).
- **App features**: separate Launcher, Settings, GPU-driver, log-export and APK-updater classes.

Lessons that apply here: SDLActivity is the proven path; run the game in its own process; set
paths through env from Java; keep the touch overlay in Java with a tiny JNI bridge; release
`libmain.so` uncompressed; require Vulkan in the manifest.

## 10. Proposed architecture

```
PC (Windows, existing)                         Phone (Galaxy M33 5G, Android 14)
------------------------------                 ------------------------------------------
user's dump -> extract -> translate            APK (debuggable at first; no game data in it)
  generated/*.c   (same files as Windows)        - Java: SDLActivity subclass, :game process,
NDK clang -> libtitle.so  (arm64)                  sets WII_RECOMP_* env, redirects stderr,
NDK clang -> libmain.so   (runtime+SDL3)           touch/sensor overlay (A4)
make_disc.py -> SO3EE9.wrd  (disc + android-     - libmain.so  = runtime + SDL3 (from NDK)
  arm64 native + recipes)                        app storage (never leaves the phone):
adb install  (APK)                                 files/SO3EE9.wrd   (external app dir)
adb push     (image)  -- never distributed         native-cache/<hash>.so (libtitle.so)
adb run-as / pull  (log, shots)                    saves/, shader-cache/, title-data/
```

- Window, input and audio: SDL3 on Android. The desktop build keeps GLFW behind the same
  platform interface (do not regress Windows).
- Renderer: the existing Vulkan backend, device chosen by the existing code; FSR as is.
- `libmain.so` and `libtitle.so` are built **on the user's PC from the user's dump** and pushed
  with adb. The APK that contains only `libmain.so` and Java is generic; the title library and
  image are the only game-derived files and stay local.
- Wii Menu / console folder, network and real remotes are not supported on Android.
- Source organisation: runtime changes go in the fork on a feature branch as general
  platform support (no per-game code, per the fork's rules); the Android shell goes in a new
  top-level `android` folder in this repo.

## 11. Gated milestones

Each gate needs evidence before the next starts. Only the main session runs the game
(`tools\playtest.ps1` rules apply to PC runs; Android runs use their own folder and a
snapshot of saves).

### A0 Toolchain
**Status: done.**
Install Android SDK command-line tools, platform 34, NDK (r27 or newer LTS), CMake and Ninja
if needed; a JDK 21 is present, `adb` is present
(WinGet platform-tools). `ANDROID_HOME` and `ANDROID_NDK_HOME` are currently unset and no SDK
was found in the usual places [F].
Evidence: `aarch64-linux-android30-clang --version`; a hello-world `libmain.so` running under
`adb shell`; `adb devices` lists the M33 (USB debugging on); `nm -D` on the NDK `libc.so` for
`getcontext/makecontext`; whether the NDK has a device `libshaderc`; `vulkaninfo`-equivalent
dump of the phone saved off-repo (dual-source, atomics, D32 blit, present modes, transforms);
`adb push` of a 2 GB test file to `Android/data/<pkg>/files` succeeds; `dlopen` of a `.so`
copied into `files/` via `run-as` works (this decides blocker 6/9).

### A1 Runtime + title compile for arm64-v8a
**Status: done** (`tools\android-build.ps1`).
Add the `--target` plumbing, SDL3 audio file, net/remote stubs, `android-arm64` platform key,
`__ANDROID__` guards (debug sampler off), aarch64 context switch (blocker 1), SDL3 platform
layer compiling (no run yet). Build SDL3 and (if needed) shaderc for arm64.
Evidence: `libmain.so` and `libtitle.so` link with no undefined symbols
(`llvm-readelf -d`, `llvm-nm -u`); measured wall time of the 464-file title compile and sizes;
the Windows build still passes its tests (`python -m unittest discover test`) and a normal
Windows playtest still boots (main session).

### A2 Headless boot on the device to the title
**Status: done.**
No window or touch yet. Either a plain NDK executable under `adb shell` (`/data/local/tmp`
for the binary, image in app storage) or the APK with `NO_WINDOW`, with a window-less boot
(`window_ready(0)`). Set the env, redirect stderr to `log.txt`, use the FIFO.
Evidence: `log.txt` shows `title ...: N native functions`, the guest base mapped, `vulkan: <GPU
name>` plus the capability lines (dual-source, atomics, push descriptors, present mode), the
shader-recipe line, steady "second N:" lines; a `shot` taken through the FIFO is pulled with
`adb` and shows the title screen at the game's own 1280x960 picture; `/proc/<pid>/maps` and
`VmRSS` recorded; no `crash:` lines.

### A3 Gameplay and fps
**Status: done for indoors and town** (30 fps, also throttled); sessions over 15 minutes and thermals are not measured.
Windowed SDL3 surface, FIFO-scripted input (the virtual joystick in `pads.c`), load a late save
slot as on PC (goal 1), run a scripted route for several minutes.
Evidence: frame-time log (`WII_RECOMP_FRAME_LOG`/`FPS_LOG`) with median, p95 and slow-frame
count per minute at scales 1 and 1.5/2, FSR on and off; thermals (`dumpsys thermalservice` before
and after) and 10-minute sustained behaviour; recipe/pipeline warm-up time on first start and on
the second start with `driver.bin`. Pass criterion is the same as PC goal 2 (steady
33.3 ms); anything less is reported as a limitation, never a fake mode (ADR-007).

### A4 Touch / controller UI
**Status: touch controls, layout editor and launcher done; a physical controller is untested on the device.**
On-screen remote overlay (Java view + JNI bitmask bridge), touch as pointer, phone IMU for
tilt/shake via SDL sensors, Bluetooth/USB gamepad through the existing SDL pad code, a layout
editor, haptics. `controllers.ini` remains hand-edit-free (project rule): a settings screen
writes it.
Evidence: scripted and manual runs on the device showing menu navigation, movement, and the
load menu with touch and with a gamepad; screenshots (local only).

### A5 Builder flow
**Status: partly done.** `tools\android-build.ps1` and `tools\android-push.ps1` take the dump to a running install; there is no in-app progress UI, update channel or crash reporter.
The PC launcher (or a `tools\android-build.ps1`) takes the user's dump and produces
`libtitle.so`, the image and the recipes for Android, installs the APK and pushes the image
with a progress UI, mirroring the Windows "Rebuild" page (ADR-014/015). Optional shader warm-up
run on the phone. Update channel and crash reporter parity are goal 3 items, to be decided later.
Evidence: from a clean checkout and the user's dump, one command ends with the game starting on
the phone; no game-derived file is tracked by git.

## 12. Risks

| Risk | Kind | Mitigation |
|---|---|---|
| Phone too slow for 30 fps (single guest thread on an A78, mid GPU) | [H] | A3 measures early; lower scale, FSR upscale from a lower scale, pin the game thread to a big core (`sched_setaffinity`, Android 12+ game mode / performance hint API [H]); if still short, report it. |
| No `ucontext` on bionic | [H] likely | aarch64 port of the Win64 context code (A1) or `libucontext`. |
| Shader compile hitching on Mali (mobile drivers compile slower, pipeline cache is per device) | [H] | pre-warm on first run, keep `driver.bin`, show a progress screen; PC `stages.bin` only helps if scale and caps match. |
| `dualSrcBlend` or atomics missing | [H] | runtime already degrades with a log line; visual impact judged at A2/A3. |
| Surface lost on background, rotation transform | [F] no handling today | surface re-create path and pre-rotation/IDENTITY decision at A2/A3. |
| Android kills the process in the background; runtime "starts once" | [F] statics, `_exit` | own `:game` process, treat leaving as exit, save on pause. |
| `dlopen` of an app-data `.so` or `memfd_create` blocked by SELinux/seccomp | [H] | A0 probes; fallback to `jniLibs`. |
| Image size and `adb push` of 2.36 GB; user storage | [F] size | resumable push; later let the app read via SAF if wanted. |
| Interface-header edits force a rebuild of the Windows title and image | [F] hashes | batch header changes; one `cpu.h`/`guest_memory.h` decision up front. |
| Upstream drift: the fork is its own repo; Android changes must stay general | [F] fork rules | feature branch, platform layer behind interfaces, no per-game code. |
| Licensing: SDL3 (zlib), Vulkan headers, shaderc (Apache-2.0), reference repo is GPL-3.0 | [F] | no code copied from the reference; third-party notices in the APK. |
| Thermal throttling (fanless phone) | [H] | A3 sustained run; frame cap and scale settings. |

## 13. Open questions for the user

- Android scale target (1 or 1.5) and whether FSR from a lower base is acceptable.
- Is a debuggable, adb-installed personal APK enough for the first milestones (yes for A0-A4)?
- Keep GLFW on desktop behind a platform interface, or move Windows to SDL3 as well (single
  code path, but touches the proven Windows window code and its tests)?

## Appendix A: ADR

Moved: the decision is ADR-016 in `docs/07-decisions.md` (accepted 2026-11-26). Two follow-up
decisions made during the port are ADR-017 (alpha without dual-source blending) and ADR-018
(thread placement).

## Appendix B: A3 and A4 support added (2026-11-26; run on the phone since, see docs/08-progress.md)

Measurement (runtime, all platforms):
- `frames: n, median x ms, p95 y ms, max z ms, over 50 ms: k` every 10 s of game frames (the gap between the game's
  screen copies, the same figure the old `slow frame` line used). `WII_RECOMP_FRAME_SUMMARY=0` turns it off, a number
  of seconds changes the interval.
- The per-frame `slow frame` line is `WII_RECOMP_SLOW_FRAME_MS=<ms>` (0 = none). Default 20 on PC (unchanged), off on Android.
- `picture: the game draws at N/D of the Wii's size, WxH (from ...)` once at start, and `picture: window WxH, the game's
  picture goes in ...` at each swapchain. Default scale with no `WII_RECOMP_SCALE` is 2 (1280x1056); `WII_RECOMP_SCALE=1` gives
  640x528. For a 2342x970 window the 4:3 picture is 1293x970 at x=524 (computed, to be confirmed in the log).

Input:
- Runtime: `host_input.c` exports `wii_recomp_input_buttons/stick/pointer`, `window_sdl.c` `wii_recomp_input_pointer_in_window` and
  `wii_recomp_picture_rect`; `android_jni.c` binds them to `org.wiirecomp.HostInput`. Touch buttons are their own share of the
  button mask (logged as `input touch p1 buttons ...`), so they never release a keyboard, pipe or pad button.
- App: `TouchOverlay` over the SDL surface, layout in `assets/touch_layout.json` (a copy in `SO3EE9/touch_layout.json` of the
  game's data folder wins; no rebuild needed). Left: floating Nunchuk stick (left 40% of the screen) and a D-pad; right: A, B, C, Z;
  top centre: -, +, 1, 2; eye button top right. The button names are the Wii's; what the game does with each is unverified
  (docs/controls.md says - is the map and + the menu; the brief said 1 is the map: not settled, check on the device).
- A game controller (gamepad source, not virtual, external on Android 14+) hides the touch controls and the eye; removing it shows
  them again. With a pad connected the pad code rewrites the Nunchuk stick and pad buttons each poll: the last writer wins.
- Back: short press is the remote's +. Hold Back about 0.8 s, then press Back again within 3 s to leave. Back from a gamepad is left to SDL.
- Gamepad (pad_map.c, SDL3 gamepad via SDLControllerManager, which the app's SDLActivity initialises): positional Nintendo layout.
  Right face button (SDL east) = Wii A, bottom (south) = B, top (north) = 2, left (west) = 1, ZR = B, L = Nunchuk C, ZL = Nunchuk Z,
  R = shake, Start = +, Back/Select = -, Guide = Home, D-pad = D-pad, left stick = Nunchuk stick. The pointer comes from a gyro, or the
  right stick if `pointer` in controllers.ini includes `stick` (not the default). Unproven on Android.
