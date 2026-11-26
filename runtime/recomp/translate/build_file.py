#!/usr/bin/env python3
"""Write the ninja file that compiles the console and the translated title.

Usage: recomp/translate/build_file.py recomp/build [backend,backend]

It builds two things: `wii-recomp`, the console, which is the same for every title, and
`title.dylib` (`title.so` on Linux), the title's translated code, which the console loads.

Backends are the graphics APIs to build in, the first being the default at run
time. WII_RECOMP_RENDERER picks among them. Without a list, each platform's usual ones,
leaving Vulkan out where its libraries aren't installed. VULKAN_SDK names a folder
holding the Vulkan libraries and headers, if they aren't installed system-wide.
"""
import hashlib
import os
import re
import sys
from glob import glob
from pathlib import Path

VULKAN_SDK = os.environ.get("VULKAN_SDK")


def take_target():
    """The platform the build is for: --target NAME on the command line (removed from
    sys.argv), or WII_RECOMP_TARGET, or the computer running this script. Only
    android-arm64 differs from the computer's own: it cross-compiles with the Android NDK."""
    target = os.environ.get("WII_RECOMP_TARGET", "")
    if "--target" in sys.argv:
        at = sys.argv.index("--target")
        if at + 1 >= len(sys.argv):
            sys.exit("stop: --target wants a name, such as android-arm64")
        target = sys.argv[at + 1]
        del sys.argv[at:at + 2]
    if target not in ("", "android-arm64"):
        sys.exit(f"stop: unknown target {target}; the choices are android-arm64, or leave it out for this computer")
    return target


TARGET = take_target()
ANDROID = TARGET == "android-arm64"
# The platform whose names the tables below are keyed by: what is being built for.
PLATFORM = "android" if ANDROID else sys.platform
# Whether the build is FOR Windows (a Windows computer building for Android is not).
WINDOWS = sys.platform == "win32" and not ANDROID
# Android is built with the NDK's clang, from any host. ANDROID_NDK_HOME names the NDK,
# SDL3_ANDROID_PREFIX an SDL3 install for arm64-v8a, and SHADERC_ANDROID_LIBS the folder
# of shaderc's static libraries built with the NDK (tools/android-build.ps1 sets all three).
# API 30 (Android 11) is the first with memfd_create in libc, which the command ring uses.
ANDROID_API = 30
ANDROID_NDK = Path(os.environ.get("ANDROID_NDK_HOME", "")) if ANDROID else None
ANDROID_HOST_TAG = {"win32": "windows-x86_64", "darwin": "darwin-x86_64", "linux": "linux-x86_64"}.get(sys.platform, "linux-x86_64")
ANDROID_BIN = (ANDROID_NDK / f"toolchains/llvm/prebuilt/{ANDROID_HOST_TAG}/bin").as_posix() if ANDROID else ""
ANDROID_EXE = ".exe" if sys.platform == "win32" else ""
ANDROID_TRIPLE = f"aarch64-linux-android{ANDROID_API}"
# armv8.2-a is what both of the Galaxy M33's core types (Cortex-A78 and A55) implement, so
# one build runs on all of them; the A78 is only the scheduling target, as the game thread
# is the one that matters and it runs on a big core.
ANDROID_CPU_FLAGS = f"--target={ANDROID_TRIPLE} -march=armv8.2-a -mtune=cortex-a78 -fPIC -fno-omit-frame-pointer"
SHADERC_LIBS = ["shaderc", "shaderc_util", "glslang", "OGLCompiler", "OSDependent", "SPIRV", "HLSL", "SPIRV-Tools-opt", "SPIRV-Tools"]
# Windows builds with MSYS2's clang (the CLANG64 packages), found through MSYS2_ROOT or
# in its usual place. Its libraries and headers are in that one folder.
MSYS2 = Path(os.environ.get("MSYS2_ROOT", "C:/msys64")) / "clang64"
if ANDROID:
    CLANG = f"{ANDROID_BIN}/clang{ANDROID_EXE}"
    CLANGXX = f"{ANDROID_BIN}/clang++{ANDROID_EXE}"
else:
    CLANG = (MSYS2 / "bin/clang.exe").as_posix() if WINDOWS else "clang"
# Arm's C leaves char unsigned and merges multiply-adds, both unlike the Intel Mac.
FLAGS = "-w -fno-strict-aliasing -fwrapv -fsigned-char -ffp-contract=off " + ("-I../runtime/win" if WINDOWS else "" if ANDROID else "-I/usr/local/include") + (f" -I{VULKAN_SDK}/include" if VULKAN_SDK and not ANDROID else "")  # Android's Vulkan headers are the NDK's.
if ANDROID:
    FLAGS += " " + ANDROID_CPU_FLAGS
GAME_OPTIMIZATION = "-O2"
# Android, weighed: -O3 for the title (left out: the translated code is large, and the Cortex-A78's instruction cache is 64 KiB);
# -fno-plt (no effect: this NDK's clang keeps calling through the procedure linkage table on AArch64 ELF, so it only
# would have changed the title); the console is built with -fno-semantic-interposition and linked with -Bsymbolic, so its own
# calls and its processor state (cpu, interrupt_budget) are reached directly, not through its table of imports. The title
# still reaches cpu through its table once per function, which only a fixed address (as on Windows) would remove.
# The quick tier (emit.py --profile): code that hasn't run in a profiled session yet. It
# used to be -O0, five times quicker to compile, but code first run in play (a match, a
# race) then ran so slowly that the game stuttered: Mario Strikers Charged's first match
# fell to 50 fps. At -O2 a whole game builds in about half a minute on a desktop.
QUICK_OPTIMIZATION = "-O2"
RUNTIME_OPTIMIZATION = "-O3"
import platform as _platform
if WINDOWS or (not ANDROID and sys.platform == "linux" and _platform.machine() == "x86_64"):
    FLAGS += " -march=x86-64-v3"  # AVX2 and FMA-capable processors: Haswell and Zen onwards.
if not ANDROID and sys.platform == "linux" and os.uname().machine == "aarch64":
    FLAGS += " -mcpu=cortex-a76"  # The Raspberry Pi 5.
COMMON_LINK_FLAGS = "-L/usr/local/lib -lglfw -lm -lpthread"
VULKAN_LINK_FLAGS = (f"-L{VULKAN_SDK}/lib -Wl,-rpath,{VULKAN_SDK}/lib " if VULKAN_SDK else "") + "-lvulkan"
SHADERC = "shaderc" if sys.platform == "linux" else "shaderc_shared"  # Debian's name for it.
VULKAN_LINK_FLAGS += f" -l{SHADERC}"

# For each platform, what it always links, and what each backend adds. Every platform
# draws at twice the Wii's size unless WII_RECOMP_SCALE asks otherwise (gx.h).
PLATFORMS = {
    "darwin": {"system": "-framework AudioToolbox -framework IOKit -framework CoreFoundation -lobjc", "opengl": "-framework OpenGL", "vulkan": VULKAN_LINK_FLAGS, "default": ["vulkan", "opengl"]},
    "linux": {"system": "-lasound", "opengl": "-lGL", "vulkan": VULKAN_LINK_FLAGS, "default": ["vulkan"]},
    "win32": {"system": "-lws2_32 -lwinmm", "opengl": "-lopengl32", "vulkan": "-lvulkan-1.dll -lshaderc_shared", "default": ["vulkan"]},
    # Android: SDL3 supplies the window, input and audio; no OpenGL backend is built.
    "android": {"system": "-llog -landroid", "opengl": "", "vulkan": "-lvulkan", "default": ["vulkan"], "defines": "-DWII_PLATFORM_SDL"},
}
if WINDOWS:
    COMMON_LINK_FLAGS = "-lglfw3 -lm -lpthread"
LIBRARY_SUFFIX = {"darwin": "dylib", "win32": "dll"}.get(PLATFORM, "so")
# On Android the console is a shared library the app loads (its entry is wiirecomp_main).
PROGRAM = "libwiirecomp.so" if ANDROID else "wii-recomp.exe" if WINDOWS else "wii-recomp"
# The console must export what translated code calls, and the title must be able to call it.
# On Windows a library names what it takes from the program at link time, so the console
# writes an import library, and the title links against it and exports only its description.
# On Android the title names libwiirecomp.so as needed, so the loader finds the console's symbols;
# 16 KiB pages are what Android 15 devices with 16 KiB kernels require of every library.
CONSOLE_LINK_FLAGS = {"darwin": "", "linux": "-rdynamic -ldl", "win32": "-Wl,--export-all-symbols -Wl,--out-implib,libconsole.a",
                      "android": "-shared -static-libstdc++ -Wl,--no-undefined -Wl,-Bsymbolic -Wl,-z,max-page-size=16384 -Wl,-soname,libwiirecomp.so -ldl"}
TITLE_LINK_FLAGS = {"darwin": "-shared -undefined dynamic_lookup", "linux": "-shared", "win32": "-shared ../translate/title.def ../build/libconsole.a",
                    "android": f"{ANDROID_CPU_FLAGS} -shared -Wl,-z,max-page-size=16384 -Wl,-soname,libtitle.so -Wl,--no-undefined -L. -lwiirecomp"}
# What the console and a title share. A title built against other versions is refused.
INTERFACE_HEADERS = ["cpu.h", "guest_memory.h", "title.h", "modules.h"]
AUDIO_SOURCES = {"darwin": "audio_output_mac.c", "linux": "audio_output_linux.c", "win32": "audio_output_sdl.c", "android": "audio_output_sdl.c"}
# The network uses POSIX sockets and fork; Windows has net_stub.c in their place for now.
NETWORK_SOURCES = {"win32": ["net_stub.c"], "android": ["net_stub.c"]}
POSIX_NETWORK_SOURCES = ["net_ip.c", "net_ssl.c", "net_routes.c"]
# Real Wii Remotes: IOKit's HID on the Mac, Bluetooth sockets and the kernel's HID on Linux (remote_link.h).
REMOTE_SOURCES = {"darwin": ["remote_link_mac.c"], "linux": ["remote_link_linux.c", "remote_link_hidraw.c"], "win32": ["remote_link_stub.c"], "android": ["remote_link_stub.c"]}
# The window: GLFW's everywhere but Android, where SDL3 makes it (window_sdl.c).
WINDOW_SOURCES = {"darwin": "window.c", "linux": "window.c", "win32": "window.c", "android": "window_sdl.c"}
BACKEND_SOURCES = {"opengl": ["gx_gl.c"], "vulkan": ["gx_vulkan.c", "gx_vulkan_device.c", "gx_vulkan_ngx.c", "gx_vulkan_pipeline.c", "gx_vulkan_window.c"]}
LIBRARY_FOLDERS = ["/usr/local/lib", "/usr/lib", "/usr/lib/aarch64-linux-gnu"] + ([f"{VULKAN_SDK}/lib"] if VULKAN_SDK else [])
# SDL3 reads Switch and other controllers (runtime/pads.c). On a Mac it's Homebrew's; on
# Linux, recomp/tools/sdl3_setup.sh builds it into /usr/local, or ~/.local without root.
SDL3_PREFIXES = [os.environ.get("SDL3_PREFIX"), "/usr/local", str(Path.home() / ".local"), "/usr"]


def sdl3_flags():
    """(compile flags, link flags) for SDL3, or stops with how to install it."""
    if ANDROID:
        prefix = os.environ.get("SDL3_ANDROID_PREFIX")
        if not prefix or not Path(f"{prefix}/include/SDL3/SDL.h").exists() or not Path(f"{prefix}/lib/libSDL3.so").exists():
            sys.exit("stop: SDL3_ANDROID_PREFIX must name an SDL3 install built for arm64-v8a (include/SDL3, lib/libSDL3.so); see tools/android-build.ps1.")
        return f"-I{prefix}/include", f"-L{prefix}/lib -lSDL3"
    if WINDOWS:
        if not (MSYS2 / "include/SDL3/SDL.h").exists():
            sys.exit(f"stop: SDL3 isn't in {MSYS2}. Install MSYS2's CLANG64 packages: see docs/windows.md.")
        return "", "-lSDL3"
    for prefix in filter(None, SDL3_PREFIXES):
        libraries = [Path(found).parent for found in glob(f"{prefix}/lib*/libSDL3.*") + glob(f"{prefix}/lib/*/libSDL3.*")]
        if libraries and Path(f"{prefix}/include/SDL3/SDL.h").exists():
            folder = libraries[0]
            compile_flags = "" if prefix == "/usr" else f"-I{prefix}/include"
            link_flags = "-lSDL3" if prefix == "/usr" else f"-L{folder} -Wl,-rpath,{folder} -lSDL3"
            return compile_flags, link_flags
    install = "brew install sdl3" if sys.platform == "darwin" else "recomp/tools/sdl3_setup.sh"
    sys.exit(f"stop: SDL3 isn't installed, and the console reads controllers through it. Install it with {install}.")
VULKAN_LIBRARIES = ["vulkan", SHADERC]


def has_library(name):
    if ANDROID:
        return True  # vulkan is the NDK's own, and shaderc is checked in android_flags().
    if WINDOWS:
        return bool(glob(f"{MSYS2.as_posix()}/lib/lib{name}*"))
    return any(glob(f"{folder}/lib{name}.*") for folder in LIBRARY_FOLDERS)


def default_backends(platform):
    wanted = platform["default"]
    missing = [name for name in VULKAN_LIBRARIES if not has_library(name)]
    if "vulkan" in wanted and missing and len(wanted) > 1:
        print("leaving Vulkan out, as these aren't installed:", ", ".join(missing))
        return [backend for backend in wanted if backend != "vulkan"]
    return wanted


def android_flags():
    """(compile flags, link flags) for the NDK's shaderc build, which is a set of static C++ libraries."""
    libs = os.environ.get("SHADERC_ANDROID_LIBS")
    include = ANDROID_NDK / "sources/third_party/shaderc/libshaderc/include"
    missing = [name for name in SHADERC_LIBS if not libs or not Path(f"{libs}/lib{name}.a").exists()]
    if missing or not include.exists():
        sys.exit(f"stop: SHADERC_ANDROID_LIBS must name the folder of the NDK's shaderc static libraries ({', '.join(missing) or 'headers missing'}); see tools/android-build.ps1.")
    # The libraries depend on each other in one direction, so a group keeps the order from mattering.
    return f"-I{include.as_posix()}", f"-L{libs} -Wl,--start-group " + " ".join(f"-l{name}" for name in SHADERC_LIBS) + " -Wl,--end-group"


def interface_check(runtime):
    digest = hashlib.sha256()
    for name in INTERFACE_HEADERS:
        digest.update((runtime / name).read_bytes())
    return f"-DTITLE_INTERFACE=0x{digest.hexdigest()[:16]}ull"


def take_option(name):
    """--NAME VALUE from the command line (removed from sys.argv), or None."""
    if name not in sys.argv:
        return None
    at = sys.argv.index(name)
    if at + 1 >= len(sys.argv):
        sys.exit(f"stop: {name} wants a value")
    value = sys.argv[at + 1]
    del sys.argv[at:at + 2]
    return value


def main():
    # --generated DIR: where the translated sources are, when not in BUILD/generated. A
    # cross build keeps its own objects beside, not among, the host build's.
    generated_dir = take_option("--generated")
    build = Path(sys.argv[1])
    interface = interface_check(Path(__file__).resolve().parents[1] / "runtime")
    generated_dir = Path(generated_dir) if generated_dir else build / "generated"
    generated = sorted(generated_dir.glob("*.c"))
    if ANDROID and not generated:
        sys.exit(f"stop: no translated sources in {generated_dir}; run translate/emit.py first")
    # Ninja runs in the build folder, so what it names is relative to there.
    def from_build(path):
        return Path(os.path.relpath(Path(path).resolve(), build.resolve())).as_posix()
    runtime = from_build(Path(__file__).resolve().parents[1] / "runtime") if ANDROID else "../runtime"
    generated_from_build = from_build(generated_dir) if ANDROID else "generated"
    if ANDROID and (not ANDROID_NDK or not (ANDROID_NDK / "source.properties").exists()):
        sys.exit("stop: ANDROID_NDK_HOME must name the Android NDK (r27 or newer).")
    platform = PLATFORMS[PLATFORM]
    backends = sys.argv[2].split(",") if len(sys.argv) > 2 else default_backends(platform)
    sdl_compile, sdl_link = sdl3_flags()
    extra_compile = ""
    if ANDROID:
        shaderc_compile, shaderc_link = android_flags()
        extra_compile = shaderc_compile
        # GLFW isn't part of an Android build; SDL3 and the NDK's libraries are.
        link_flags = " ".join([ANDROID_CPU_FLAGS, "-lm", sdl_link, platform["system"]] + [platform[backend] for backend in backends] + [shaderc_link])
    else:
        link_flags = " ".join([COMMON_LINK_FLAGS, sdl_link, platform["system"]] + [platform[backend] for backend in backends])
    defines = " ".join(f"-DGX_WITH_{backend.upper()}" for backend in backends) + f" -DGX_FIRST_{backends[0].upper()} " + platform.get("defines", "")
    left_out = {name for backend, names in BACKEND_SOURCES.items() if backend not in backends for name in names}
    # A file shared by two platforms is built for both, so what's left out is what this one doesn't use.
    left_out |= {name for name in AUDIO_SOURCES.values() if name != AUDIO_SOURCES[PLATFORM]}
    left_out |= {name for names in REMOTE_SOURCES.values() for name in names if name not in REMOTE_SOURCES[PLATFORM]}
    left_out |= {name for names in NETWORK_SOURCES.values() for name in names if name not in NETWORK_SOURCES.get(PLATFORM, [])}
    left_out |= {name for name in WINDOW_SOURCES.values() if name != WINDOW_SOURCES[PLATFORM]}
    if PLATFORM in NETWORK_SOURCES:
        left_out |= set(POSIX_NETWORK_SOURCES)
    python = Path(sys.executable).as_posix()
    embed = Path(__file__).resolve().with_name("embed.py").as_posix()
    runtime_flags = f"-include {runtime}/win/win_compat.h" if WINDOWS else ""
    # The console becomes a library on Android, so its program entry gets a name the app calls.
    runtime_flags += " -DWII_RECOMP_LIBRARY -Dmain=wiirecomp_main -fvisibility=default -fno-semantic-interposition" if ANDROID else ""
    title_inputs = " | libwiirecomp.so" if ANDROID else ""  # Windows: the console is built first (add_game.py), its import library in recomp/build.
    title_name = "libtitle.so" if ANDROID else f"title.{LIBRARY_SUFFIX}"
    console_outputs = " | libconsole.a" if WINDOWS else ""
    lines = [f"cflags = {FLAGS}", "",
             "rule cc", f"  command = {CLANG} $cflags -c $in -o $out", "  description = $in", "",
             # The runtime's headers change, and files that include them must follow.
             "rule runtime_cc", f"  command = {CLANG} $cflags -MMD -MF $out.d -c $in -o $out", "  description = $in",
             "  depfile = $out.d", "  deps = gcc", "",
             # Data the console carries inside, such as the audio processor's ROM.
             "rule embed", f"  command = {python} {embed} $in $out $symbol",
             "  description = $in", "",
             "rule link", f"  command = {CLANGXX if ANDROID else CLANG} $in -o $out {link_flags} {CONSOLE_LINK_FLAGS[PLATFORM]}", "",
             "rule link_title", f"  command = {CLANG} $in -o $out {TITLE_LINK_FLAGS[PLATFORM]}", ""]
    title_objects, objects = [], []
    for source in generated:
        target = f"obj/{source.stem}.o"
        # Hidden, so calls within the title stay direct; only its description is exported.
        optimization = QUICK_OPTIMIZATION if re.match(r"(m\d+_)?cold", source.name) else GAME_OPTIMIZATION
        lines.append(f"build {target}: cc {generated_from_build}/{source.name}\n  cflags = {optimization} {FLAGS} -fPIC -fvisibility=hidden {interface}")
        title_objects.append(target)
    for source in sorted((Path(__file__).resolve().parents[1] / "runtime").glob("*.c")):
        if source.name in left_out:
            continue
        target = f"obj/runtime_{source.stem}.o"
        lines.append(f"build {target}: runtime_cc {runtime}/{source.name}\n  cflags = {RUNTIME_OPTIMIZATION} {FLAGS} {runtime_flags} {sdl_compile}{" " + extra_compile if extra_compile else ""} {defines} {interface}")
        objects.append(target)
    for data in sorted((Path(__file__).resolve().parents[1] / "runtime/data").glob("*.bin")):
        symbol = f"data_{data.stem}"
        lines.append(f"build obj/{symbol}.c: embed {runtime}/data/{data.name}\n  symbol = {symbol}")
        lines.append(f"build obj/{symbol}.o: cc obj/{symbol}.c")
        objects.append(f"obj/{symbol}.o")
    lines.append(f"build {PROGRAM}{console_outputs}: link {' '.join(objects)}")
    lines.append(f"build {title_name}: link_title {' '.join(title_objects)}{title_inputs}")
    (build / "build.ninja").write_text("\n".join(lines) + "\n")
    print(len(objects), "console objects,", len(title_objects), "title objects")


if __name__ == "__main__":
    main()
