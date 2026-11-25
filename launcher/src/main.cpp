// SeaOfStar-ToD launcher.
//
// Starts the wii-recomp console with the user's own extracted disc, sets the environment
// the console needs (MSYS2 runtime DLLs, WII_RECOMP_NATIVE), and exposes the graphics
// options that matter: render scale, window size, FSR 1.0 and its sharpness, the frame
// rate overlay, fullscreen and the present mode. It also lists the gamepads SDL3 can see
// and the controls both input methods use. No game data is embedded or distributed.
//
// The window is drawn by hand with GDI+ so the launcher can share the game's dark,
// modern look without shipping a UI toolkit. The red-and-cream palette and the ball motif
// are drawn here from circles and lines: no Pokémon artwork or logo is used.

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <commdlg.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

using namespace Gdiplus;

// These are documented Windows 10/11 attribute numbers; older SDKs may not name them.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

namespace {

constexpr wchar_t kWindowClass[] = L"SeaOfStarToDLauncher";
constexpr wchar_t kWindowTitle[] = L"SeaOfStar-ToD \u2014 Rune Factory: Tides of Destiny";
constexpr wchar_t kInputClass[] = L"SeaOfStarToDInputCheck";
constexpr wchar_t kInputTitle[] = L"Input check";
constexpr int kInputWidth = 780;
constexpr int kInputHeight = 560;

constexpr wchar_t kMapClass[] = L"SeaOfStarToDControls";
constexpr wchar_t kMapTitle[] = L"Controls";
constexpr int kMapWidth = 920;
constexpr int kMapHeight = 650;

// The logical client size the layout was drawn for. The real pixels are this times
// g_scale, which follows the window's DPI and shrinks on a small screen so the launcher
// always fits.
constexpr int kClientWidth = 1000;
constexpr int kClientHeight = 700;

enum ControlId : int {
    ID_PLAY = 1,
    ID_REBUILD,
    ID_SAVES,
    ID_LOGS,
    ID_PLAY_LOG,
    ID_CONFIG,
    ID_CONTROLS,
    ID_QUIT,
    ID_SETTINGS,
    ID_GAMEFILES,
    ID_BACK,
    ID_DONE,
    ID_USE_IMAGE,
    ID_USE_FOLDER,
    ID_OPEN_IMAGE,
    ID_OPEN_DISC,
    ID_OPEN_TITLE,
    ID_USE_CONSOLE,
    ID_OPEN_CONSOLE,
    ID_BUILD_HIDE,
    ID_BUILD_LOG,
    ID_HERO_FILES,
    ID_CARD_SETTINGS,
    ID_HERO_REBUILD,
    ID_CRASH_OPEN = 100,
    ID_CRASH_DISMISS,
    ID_FULLSCREEN = 40,
    ID_FSR,
    ID_SHARPNESS,
    ID_SCALE_1 = 60,
    ID_SCALE_15,
    ID_SCALE_2,
    ID_SCALE_3,
    ID_SIZE_720 = 70,
    ID_SIZE_960,
    ID_SIZE_1200,
    ID_SIZE_1440,
    ID_PRESENT_FIFO = 80,
    ID_PRESENT_MAILBOX,
    ID_PRESENT_IMMEDIATE,
    ID_OVERLAY = 90,
    ID_RESCAN,
    ID_INPUT_TEST,
};

enum class Page { Home, Settings, GameFiles, Build };

enum class Kind { Button, Toggle, Segment, Slider };

struct Widget {
    int id = 0;
    Kind kind = Kind::Button;
    RectF rect;
};

struct Config {
    std::wstring msys = L"C:\\msys64\\clang64\\bin";
    std::wstring console;
    std::wstring title;
    std::wstring disc;
    std::wstring image;
    std::wstring saves;
    bool fullscreen = false;
    bool fsr = true;
    bool fpsOverlay = true;
    double sharpness = 0.20;
    double scale = 1.5;
    int windowWidth = 1280;
    int windowHeight = 720;
    std::wstring present = L"fifo";
};

struct GamepadInfo {
    std::wstring name;
    std::wstring detail;
    int battery = -1;  // Percent, or -1 when the pad doesn't say.
};

struct Layout {
    RectF quit;
    RectF gear;
    RectF hero;
    RectF devices;
    RectF rescan;
    RectF testInput;
    RectF play;
    RectF heroFiles;
    RectF heroRebuild;
    RectF crashOpen, crashDismiss;
    RectF rebuild, saves, logs, playLog, config, controls;
    RectF gameFiles;
    RectF settings;
    RectF settingsButton;
    RectF graphics, ingame;
    RectF back, done;
    RectF useImage, useFolder, openImage, openDisc, openTitle, useConsole, openConsole;
    RectF buildHide, buildLog, buildPlay;
    RectF fullscreenSwitch, fsrSwitch, overlaySwitch;
    RectF sharpnessTrack, sharpnessHit;
    RectF scale[4];
    RectF size[4];
    RectF present[3];
    float rowTop[4];   // Home: the build summary rows.
    // Settings: the slots each setting sits in. GameFiles: one slot per path row.
    RectF slotDisplay[3], slotGraphics[3], slotIngame, slotFiles[4];
    // Build page: the stage line, the progress bar and the console.
    RectF buildStage, buildBar, buildConsole;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
std::wstring g_root;
std::wstring g_ini;
Config g_config;
Page g_page = Page::Home;
// The build runs as a child process and writes its console output to a file; the page
// tails that file, so the launcher can show the same progress a console would.
HANDLE g_buildProcess = nullptr;
HANDLE g_buildJob = nullptr;
std::wstring g_buildLogPath;
unsigned long long g_buildLogOffset = 0;
std::wstring g_buildPartial;
std::vector<std::wstring> g_buildLines;
std::wstring g_buildStage = L"Starting";
int g_buildPercent = 0;
bool g_building = false;
int g_buildExitCode = -1;
int g_buildEncoding = 0;  // 0 undecided, 1 UTF-16LE, 2 UTF-8.
std::string g_buildCarry;
// The console's own screen setting (IPL.AR in SYSCONF) decides the shape the game draws
// for, and the runtime keeps the window that shape. The launcher reads it so the sizes it
// offers and shows match what the game will actually use.
int g_aspectWidth = 16;
int g_aspectHeight = 9;
PROCESS_INFORMATION g_process{};
bool g_running = false;
int g_dpi = 96;
// Render-only: forces the packed-image state (an ImageState value) for the headless --render
// command. -1 means the real state, which is always the case in the window.
int g_renderImage = -1;
double g_scale = 1.0;
ULONG_PTR g_gdiplus = 0;
std::wstring g_status = L"Ready.";
std::vector<Widget> g_widgets;
int g_hot = 0;
int g_pressed = 0;
int g_slider = 0;
bool g_trackingLeave = false;

FontFamily* g_family = nullptr;
FontFamily* g_familyMono = nullptr;
FontFamily* g_familyHeavy = nullptr;  // Titles and button labels: heavy, outlined text.
int g_heavyStyle = FontStyleRegular;   // Bold when the heavy family is only a fallback.
Font* g_fontBrand = nullptr;
Font* g_fontH1 = nullptr;
Font* g_fontH2 = nullptr;
Font* g_fontBody = nullptr;
Font* g_fontSmall = nullptr;
Font* g_fontButton = nullptr;
Font* g_fontTiny = nullptr;
Font* g_fontPlay = nullptr;
Font* g_fontMono = nullptr;

// SDL3 is loaded by hand. The runtime already needs it, so it sits in the MSYS2 folder
// the launcher puts on the game's PATH; the launcher uses it only to list gamepads. If it
// is missing, the controller card says so and the game can still be played with the
// keyboard and mouse.
struct Sdl {
    HMODULE module = nullptr;
    bool initialized = false;
    bool (*init)(unsigned) = nullptr;
    void (*quit)() = nullptr;
    void (*pump)() = nullptr;
    void (*update)() = nullptr;
    unsigned* (*gamepads)(int*) = nullptr;
    void* (*open)(unsigned) = nullptr;
    void (*close)(void*) = nullptr;
    const char* (*name)(void*) = nullptr;
    int (*type)(void*) = nullptr;
    const char* (*serial)(void*) = nullptr;
    int (*power)(void*, int*) = nullptr;
    bool (*button)(void*, int) = nullptr;
    int (*axis)(void*, int) = nullptr;
    void (*release)(void*) = nullptr;
};
Sdl g_sdl;
bool g_padsTried = false;
bool g_padsError = false;
std::vector<GamepadInfo> g_pads;

// The input-check window reads the pads itself while it is open, so it can show the
// buttons and sticks as they are, including the deadzone the runtime will apply.
struct TestPad {
    void* handle = nullptr;
    std::wstring name;
};
HWND g_inputWindow = nullptr;
std::vector<TestPad> g_testPads;
double g_testDeadzone = 0.18;
double g_testSpeed = 1.0;
std::wstring g_testConfigPath;

// The controls editor writes the same controllers.ini the console reads. Values are kept
// in memory so painting never touches the disk; every change is written immediately.
HWND g_mapWindow = nullptr;
int g_mapTab = 0;             // 0 keyboard, 1 gamepad, 2 mouse, 3 player 1
int g_mapListening = -1;      // Action index in the current tab, or -1.
std::wstring g_mapConfigPath;
std::wstring g_mapStatus = L"Changes apply the next time the game starts.";
std::vector<TestPad> g_mapPads;
bool g_mapPadWas[32]{};
bool g_mapTriggerWas[2]{};
int g_mapHotTab = -1;
int g_mapHotRow = -1;
int g_mapHotClear = -1;
int g_mapHotButton = 0;       // 1 guide, 2 defaults, 3 close
int g_mapTicks = 0;
bool g_mapTrackingLeave = false;
double g_mapDeadzone = 0.18;
double g_mapSpeed = 1.0;
double g_mapGyro = 1.0;
int g_mapSlider = 0;          // 1 deadzone, 2 stick speed, 3 gyro speed while dragging
int g_mapHotSlider = 0;       // 1 deadzone, 2 stick speed under the pointer

void LaunchGame();
void ClosePads();
void OpenInputTest();
void CloseTestPads();
void OpenControls();
void BuildWidgets();
void GoTo(Page page);
void StartBuild();
void PollBuild();

int S(int value) { return static_cast<int>(std::lround(value * g_scale)); }
float SF(float value) { return value * static_cast<float>(g_scale); }

Color C(int r, int g, int b, int a = 255) {
    return Color(static_cast<BYTE>(a), static_cast<BYTE>(r), static_cast<BYTE>(g), static_cast<BYTE>(b));
}

std::wstring Join(const std::wstring& base, const std::wstring& child) {
    if (base.empty()) return child;
    if (base.back() == L'\\' || base.back() == L'/') return base + child;
    return base + L"\\" + child;
}

bool Exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// True when `path` exists and is at least as new as `other`. A packed image older than
// the title library would run the previous build, so Play falls back to the loose disc.
bool AsNewAs(const std::wstring& path, const std::wstring& other) {
    WIN32_FILE_ATTRIBUTE_DATA mine{}, theirs{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &mine)) return false;
    if (!GetFileAttributesExW(other.c_str(), GetFileExInfoStandard, &theirs)) return true;
    return CompareFileTime(&mine.ftLastWriteTime, &theirs.ftLastWriteTime) >= 0;
}

// A path in the console's folder, where the build and the runtime keep their files.
std::wstring BesideConsole(const wchar_t* child) {
    std::wstring folder = g_config.console;
    const size_t slash = folder.find_last_of(L"\\/");
    if (slash != std::wstring::npos) folder.resize(slash);
    return folder + L"\\" + child;
}

// Packed-image status. Stale is split by cause so the Home notice can say why.
enum class ImageState { Missing, Current, StaleTitle, StaleRecipes };

ImageState ClassifyImage() {
    if (g_renderImage >= 0) return static_cast<ImageState>(g_renderImage);
    if (g_config.image.empty() || !Exists(g_config.image)) return ImageState::Missing;
    if (!AsNewAs(g_config.image, g_config.title)) return ImageState::StaleTitle;
    const std::wstring recipes = BesideConsole(L"title-data\\00010000-534f3345\\shaders.bin");
    if (Exists(recipes) && !AsNewAs(g_config.image, recipes)) return ImageState::StaleRecipes;
    return ImageState::Current;
}

// The packed image is current when it is at least as new as the library it carries and
// the recipes recorded for the title. Playing adds recipes, so a rebuild after play is
// what refreshes the image with everything the session found.
bool ImageCurrent() { return ClassifyImage() == ImageState::Current; }

std::wstring ModuleDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(0, slash);
}

std::wstring FindRepoRoot() {
    std::wstring directory = ModuleDirectory();
    for (int depth = 0; depth < 8; ++depth) {
        if (Exists(Join(directory, L"tools\\common.ps1")) || Exists(Join(directory, L"config\\game.json")))
            return directory;
        const size_t slash = directory.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        const std::wstring parent = directory.substr(0, slash);
        if (parent == directory) break;
        directory = parent;
    }
    return ModuleDirectory();
}

std::wstring Resolve(const std::wstring& path) {
    if (path.empty()) return path;
    if (path.size() >= 2 && path[1] == L':') return path;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return path;
    return Join(g_root, path);
}

std::wstring ReadIni(const wchar_t* section, const wchar_t* key, const std::wstring& fallback) {
    wchar_t buffer[2048]{};
    GetPrivateProfileStringW(section, key, fallback.c_str(), buffer, ARRAYSIZE(buffer), g_ini.c_str());
    return buffer;
}

void WriteIni(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), g_ini.c_str());
}

double ClampDouble(double value, double low, double high) {
    return value < low ? low : value > high ? high : value;
}

void LoadConfig() {
    g_config.msys = ReadIni(L"paths", L"msys", g_config.msys);
    g_config.console = Resolve(ReadIni(L"paths", L"console", L"work\\wii-recomp\\recomp\\build\\wii-recomp.exe"));
    g_config.title = Resolve(ReadIni(L"paths", L"title", L"work\\wii-recomp\\recomp\\build-SO3EE9\\title.dll"));
    g_config.disc = Resolve(ReadIni(L"paths", L"disc", L"extracted\\SO3EE9\\sys"));
    g_config.image = Resolve(ReadIni(L"paths", L"image", L"work\\wii-recomp\\games\\SO3EE9.wrd"));
    g_config.saves = Resolve(ReadIni(L"paths", L"saves", L"work\\wii-recomp\\recomp\\build\\saves"));
    g_config.fullscreen = ReadIni(L"options", L"fullscreen", L"0") == L"1";
    g_config.fsr = ReadIni(L"options", L"fsr", L"1") != L"0";
    g_config.fpsOverlay = ReadIni(L"options", L"overlay", L"1") != L"0";
    g_config.sharpness = ClampDouble(_wtof(ReadIni(L"options", L"sharpness", L"0.20").c_str()), 0.0, 2.0);
    g_config.scale = ClampDouble(_wtof(ReadIni(L"options", L"scale", L"2").c_str()), 0.5, 4.0);

    const std::wstring window = ReadIni(L"options", L"window", L"1280x720");
    int width = 0, height = 0;
    if (swscanf_s(window.c_str(), L"%dx%d", &width, &height) == 2 && width >= 320 && height >= 240 &&
        width <= 7680 && height <= 4320) {
        g_config.windowWidth = width;
        g_config.windowHeight = height;
    }

    const std::wstring present = ReadIni(L"options", L"present", L"fifo");
    g_config.present = (present == L"mailbox" || present == L"immediate") ? present : L"fifo";
}

// SYSCONF is the console's settings file: "SCv0", a count, an offset for each item, then
// the items. An item is a type-and-name-length byte, the name, and the value. IPL.AR is
// the screen shape: 1 for a 16:9 TV, 0 for 4:3. Reading it here means the launcher's
// window sizes match the shape the runtime will enforce.
void DetectAspect() {
    const std::wstring path = Join(g_config.saves, L"shared2\\sys\\SYSCONF");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    std::vector<unsigned char> data;
    DWORD read = 0;
    if (GetFileSizeEx(file, &size) && size.QuadPart >= 8 && size.QuadPart <= 0x10000) {
        data.resize(static_cast<size_t>(size.QuadPart));
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size())
            data.clear();
    }
    CloseHandle(file);
    if (data.size() < 8 || std::memcmp(data.data(), "SCv0", 4) != 0) return;
    static const char name[] = "IPL.AR";
    for (size_t i = 0; i + sizeof name <= data.size(); ++i) {
        if (std::memcmp(data.data() + i, name, sizeof name - 1) != 0) continue;
        const bool widescreen = data[i + sizeof name - 1] != 0;
        g_aspectWidth = widescreen ? 16 : 4;
        g_aspectHeight = widescreen ? 9 : 3;
        return;
    }
}

// Writes a key only when the file has nothing there, so a config written by an
// older launcher picks up the newer options without losing its paths.
void EnsureIni(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    wchar_t buffer[2048]{};
    GetPrivateProfileStringW(section, key, L"", buffer, ARRAYSIZE(buffer), g_ini.c_str());
    if (buffer[0] == L'\0') WriteIni(section, key, value);
}

void WriteDefaultsIfMissing() {
    EnsureIni(L"paths", L"msys", g_config.msys);
    EnsureIni(L"paths", L"console", L"work\\wii-recomp\\recomp\\build\\wii-recomp.exe");
    EnsureIni(L"paths", L"title", L"work\\wii-recomp\\recomp\\build-SO3EE9\\title.dll");
    EnsureIni(L"paths", L"disc", L"extracted\\SO3EE9\\sys");
    EnsureIni(L"paths", L"image", L"work\\wii-recomp\\games\\SO3EE9.wrd");
    EnsureIni(L"paths", L"saves", L"work\\wii-recomp\\recomp\\build\\saves");
    EnsureIni(L"options", L"fullscreen", L"0");
    EnsureIni(L"options", L"fsr", L"1");
    EnsureIni(L"options", L"overlay", L"1");
    EnsureIni(L"options", L"sharpness", L"0.20");
    EnsureIni(L"options", L"scale", L"1.5");
    EnsureIni(L"options", L"window", L"1280x720");
    EnsureIni(L"options", L"present", L"fifo");
}

bool Ready() {
    // The packed image carries the disc and the library, so a current image is enough
    // on its own. Without one, the loose title and extracted disc are required.
    return Exists(g_config.console) &&
           (ImageCurrent() || (Exists(g_config.title) && Exists(Join(g_config.disc, L"main.dol"))));
}

// The first wizard step is done once the console and either a packed image or an
// extracted disc are set, whether or not the build has run yet.
bool FilesSet() {
    return Exists(g_config.console) &&
           (Exists(g_config.image) || Exists(Join(g_config.disc, L"main.dol")));
}

void SetStatus(const std::wstring& text) {
    g_status = text;
    if (g_window) {
        RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
}

void ShowMissing(const wchar_t* label, const std::wstring& path) {
    std::wstring message = L"Missing ";
    message += label;
    message += L":\n";
    message += path;
    message += L"\n\nRun the build steps in README.md first.";
    MessageBoxW(g_window, message.c_str(), kWindowTitle, MB_ICONWARNING | MB_OK);
}

void OpenFolder(const std::wstring& path) {
    if (!Exists(path)) {
        ShowMissing(L"folder", path);
        return;
    }
    const std::wstring arguments = L"\"" + path + L"\"";
    ShellExecuteW(g_window, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

void SaveOption(const wchar_t* key, const std::wstring& value) {
    WriteIni(L"options", key, value);
}

std::wstring FormatBytes(unsigned long long bytes) {
    wchar_t buffer[64]{};
    if (bytes >= 1024ull * 1024 * 1024) swprintf_s(buffer, L"%.2f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ull * 1024) swprintf_s(buffer, L"%.0f MB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024) swprintf_s(buffer, L"%.0f KB", bytes / 1024.0);
    else swprintf_s(buffer, L"%llu bytes", bytes);
    return buffer;
}

bool FileSizeOf(const std::wstring& path, unsigned long long& bytes) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    bytes = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

// The title-data files say how much is baked: the recipe header carries the record size,
// and the profile lists the functions that ran. They are read here only for the status
// line, so a missing file simply hides the detail.
unsigned RecipeCount() {
    const std::wstring path = BesideConsole(L"title-data\\00010000-534f3345\\shaders.bin");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size{};
    unsigned char header[32]{};
    DWORD read = 0;
    unsigned count = 0;
    if (GetFileSizeEx(file, &size) && size.QuadPart >= 32 &&
        ReadFile(file, header, sizeof header, &read, nullptr) && read == sizeof header &&
        std::memcmp(header, "WRSR", 4) == 0) {
        const unsigned record = header[8] | (header[9] << 8) | (header[10] << 16) | (header[11] << 24);
        if (record > 0 && size.QuadPart >= 32) count = static_cast<unsigned>((size.QuadPart - 32) / record);
    }
    CloseHandle(file);
    return count;
}

unsigned ProfileCount() {
    const std::wstring path = BesideConsole(L"title-data\\00010000-534f3345\\profile.txt");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size{};
    std::string text;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 8 * 1024 * 1024) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) text.clear();
        else text.resize(read);
    }
    CloseHandle(file);
    unsigned count = 0;
    bool lineStart = true, comment = false;
    for (char c : text) {
        if (lineStart) {
            comment = c == '#';
            lineStart = false;
        }
        if (c == '\n') {
            if (!comment) ++count;
            lineStart = true;
        }
    }
    return count;
}


// ---- setup state ----
//
// One snapshot of the four setup steps (dump, console, title library, packed image), kept in
// g_setup and mirrored to config\setup-state.json. It is recomputed at startup, after a
// build, after the game exits and when a path changes; nothing in the launcher polls it.

// One snapshot of the setup steps. A missing image is a setup step, not a stale one.
struct SetupState {
    bool dump = false;
    bool console = false;
    bool title = false;
    ImageState image = ImageState::Missing;
    std::wstring dumpPath;          // The main.dol that was found, or the one that was looked for.
    unsigned recipes = 0;           // Records in title-data shaders.bin right now.
    int recipesInImage = -1;        // Records when the image was last seen current; -1 = unknown.
    unsigned long long imageBytes = 0;
    FILETIME imageTime{}, titleTime{}, consoleTime{}, recipesTime{};
    bool hasImageTime = false, hasTitleTime = false, hasConsoleTime = false, hasRecipesTime = false;
    bool writeFailed = false;
};

SetupState g_setup;

bool FileTimeOf(const std::wstring& path, FILETIME& time) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    time = data.ftLastWriteTime;
    return true;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                         nullptr);
    std::string out(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string JsonString(const std::wstring& text) {
    std::string out = "\"";
    for (char c : Narrow(text)) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escape[8];
                sprintf_s(escape, "\\u%04x", static_cast<unsigned>(c));
                out += escape;
            } else {
                out += c;
            }
        }
    }
    return out + "\"";
}

// ISO 8601 in local time with the UTC offset, e.g. 2026-11-24T21:59:08+07:00.
std::string IsoTime(const FILETIME& utcFile) {
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&utcFile, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return "";
    FILETIME utcAgain{}, localFile{};
    SystemTimeToFileTime(&utc, &utcAgain);
    SystemTimeToFileTime(&local, &localFile);
    const long long utcTicks = static_cast<long long>((static_cast<unsigned long long>(utcAgain.dwHighDateTime) << 32) |
                                                      utcAgain.dwLowDateTime);
    const long long localTicks = static_cast<long long>(
        (static_cast<unsigned long long>(localFile.dwHighDateTime) << 32) | localFile.dwLowDateTime);
    const long long minutes = (localTicks - utcTicks) / 600000000LL;
    const long long magnitude = minutes < 0 ? -minutes : minutes;
    char buffer[40];
    sprintf_s(buffer, "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d", local.wYear, local.wMonth, local.wDay, local.wHour,
              local.wMinute, local.wSecond, minutes < 0 ? '-' : '+', static_cast<int>(magnitude / 60),
              static_cast<int>(magnitude % 60));
    return buffer;
}

// The same moment as it is shown in the UI: local, no offset.
std::wstring ShortTime(const FILETIME& utcFile) {
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&utcFile, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"";
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d", local.wYear, local.wMonth, local.wDay, local.wHour,
               local.wMinute, local.wSecond);
    return buffer;
}

std::string ReadTextFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 1024 * 1024) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) text.clear();
        else text.resize(read);
    }
    CloseHandle(file);
    return text;
}

std::wstring SetupStatePath() { return Join(Join(g_root, L"config"), L"setup-state.json"); }

// The recipe count the image carried when it was last seen current, kept in the previous
// state file together with the image time it belongs to. It is only trusted when that time
// still matches the image on disk, so the "N new recipes" figure is a measurement rather
// than a guess.
int ReadBaseline(const std::string& imageIso) {
    const std::string text = ReadTextFile(SetupStatePath());
    const size_t stamp = text.find("\"baseline_image_mtime\": \"");
    const size_t number = text.find("\"in_image\": ");
    if (stamp == std::string::npos || number == std::string::npos || imageIso.empty()) return -1;
    const size_t start = stamp + 25;
    if (text.compare(start, imageIso.size(), imageIso) != 0 || text.compare(start + imageIso.size(), 1, "\"") != 0)
        return -1;
    const char* digits = text.c_str() + number + 12;
    return (*digits >= '0' && *digits <= '9') ? std::atoi(digits) : -1;
}

const char* ImageStateName(ImageState state) {
    switch (state) {
    case ImageState::Current: return "current";
    case ImageState::StaleTitle: return "stale_title_newer";
    case ImageState::StaleRecipes: return "stale_new_recipes";
    default: return "missing";
    }
}

void ComputeSetupState() {
    SetupState next;
    next.dumpPath = Join(g_config.disc, L"main.dol");
    next.dump = Exists(next.dumpPath);
    if (!next.dump && Exists(Join(g_config.disc, L"sys\\main.dol"))) {
        next.dumpPath = Join(g_config.disc, L"sys\\main.dol");
        next.dump = true;
    }
    next.console = Exists(g_config.console);
    next.title = Exists(g_config.title);
    next.image = ClassifyImage();
    next.recipes = RecipeCount();
    FileSizeOf(g_config.image, next.imageBytes);
    next.hasImageTime = FileTimeOf(g_config.image, next.imageTime);
    next.hasTitleTime = FileTimeOf(g_config.title, next.titleTime);
    next.hasConsoleTime = FileTimeOf(g_config.console, next.consoleTime);
    next.hasRecipesTime = FileTimeOf(BesideConsole(L"title-data\\00010000-534f3345\\shaders.bin"), next.recipesTime);
    if (next.image == ImageState::Current) {
        next.recipesInImage = static_cast<int>(next.recipes);
    } else if (next.image == ImageState::StaleRecipes && next.hasImageTime) {
        next.recipesInImage = ReadBaseline(IsoTime(next.imageTime));
    }
    g_setup = next;
}

std::string StepJson(const char* name, bool done, const std::wstring& path, bool hasTime, const FILETIME& time,
                     const char* extra = "") {
    std::string out = std::string("    \"") + name + "\": { \"done\": " + (done ? "true" : "false") +
                      ", \"path\": " + JsonString(path);
    if (hasTime) out += ", \"mtime\": \"" + IsoTime(time) + "\"";
    out += extra;
    return out + " }";
}

// Writes config\setup-state.json (UTF-8). Returns false, with the reason in g_status via the
// caller, when the file cannot be written.
bool WriteSetupState() {
    const SetupState& s = g_setup;
    const std::wstring recipesPath = BesideConsole(L"title-data\\00010000-534f3345\\shaders.bin");
    FILETIME nowFile{};
    SYSTEMTIME nowUtc{};
    GetSystemTime(&nowUtc);
    SystemTimeToFileTime(&nowUtc, &nowFile);

    std::string imageExtra = std::string(", \"status\": \"") + ImageStateName(s.image) + "\"";
    if (s.imageBytes) imageExtra += ", \"bytes\": " + std::to_string(s.imageBytes);

    std::string json = "{\n";
    json += "  \"schema\": 1,\n";
    json += std::string("  \"launcher\": { \"build\": \"") + __DATE__ + " " + __TIME__ + "\", \"computed\": \"" +
            IsoTime(nowFile) + "\" },\n";
    json += "  \"steps\": {\n";
    json += StepJson("dump", s.dump, s.dumpPath, false, {}) + ",\n";
    json += StepJson("console", s.console, g_config.console, s.hasConsoleTime, s.consoleTime) + ",\n";
    json += StepJson("title", s.title, g_config.title, s.hasTitleTime, s.titleTime) + ",\n";
    json += StepJson("image", s.image == ImageState::Current, g_config.image, s.hasImageTime, s.imageTime,
                     imageExtra.c_str()) + "\n";
    json += "  },\n";
    json += "  \"recipes\": {\n";
    json += "    \"path\": " + JsonString(recipesPath) + ",\n";
    json += "    \"count\": " + std::to_string(s.recipes) + ",\n";
    if (s.hasRecipesTime) json += "    \"mtime\": \"" + IsoTime(s.recipesTime) + "\",\n";
    json += "    \"in_image\": " + (s.recipesInImage >= 0 ? std::to_string(s.recipesInImage) : std::string("null")) + ",\n";
    json += "    \"new_since_image\": " +
            (s.recipesInImage >= 0 ? std::to_string(static_cast<int>(s.recipes) - s.recipesInImage) : std::string("null")) +
            ",\n";
    json += "    \"baseline_image_mtime\": \"" + (s.hasImageTime ? IsoTime(s.imageTime) : std::string()) + "\"\n";
    json += "  },\n";
    json += std::string("  \"ready_to_play\": ") + (Ready() ? "true" : "false") + "\n";
    json += "}\n";

    CreateDirectoryW(Join(g_root, L"config").c_str(), nullptr);
    HANDLE file = CreateFileW(SetupStatePath().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) &&
                    written == json.size();
    CloseHandle(file);
    return ok;
}

// Recomputes the snapshot and rewrites the file. A failed write is reported on the status
// line; the in-memory snapshot (and so the Home notice) is still updated.
bool RefreshSetup() {
    ComputeSetupState();
    g_setup.writeFailed = !WriteSetupState();
    if (g_setup.writeFailed) {
        SetStatus(L"Could not write " + SetupStatePath() + L"; the setup state is shown but not saved.");
    }
    if (g_window) {
        BuildWidgets();
        RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
    return !g_setup.writeFailed;
}

bool ImageStale() { return g_setup.image == ImageState::StaleTitle || g_setup.image == ImageState::StaleRecipes; }

// The one-line reason shown on Home while the image is stale.
std::wstring StaleReason() {
    if (g_setup.image == ImageState::StaleTitle)
        return L"The image is older than title.dll: Play uses the loose files until you rebuild.";
    if (g_setup.recipesInImage >= 0 && static_cast<int>(g_setup.recipes) > g_setup.recipesInImage)
        return L"The image is older than " + std::to_wstring(static_cast<int>(g_setup.recipes) - g_setup.recipesInImage) +
               L" new shader recipes: Play uses the loose files until you rebuild.";
    return L"The image is older than the latest shader recipes (" + std::to_wstring(g_setup.recipes) +
           L" now): Play uses the loose files until you rebuild.";
}

// ---- play logs ----

constexpr size_t kPlayLogsKept = 20;

std::wstring PlayLogDir() { return Join(Join(g_root, L"logs"), L"play"); }

// Full paths of logs\play\*.log, oldest first (the names are timestamps, so a name sort is
// a time sort).
std::vector<std::wstring> ListPlayLogs() {
    std::vector<std::wstring> logs;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(Join(PlayLogDir(), L"*.log").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return logs;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) logs.push_back(Join(PlayLogDir(), data.cFileName));
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(logs.begin(), logs.end());
    return logs;
}

std::wstring NewestPlayLog() {
    const std::vector<std::wstring> logs = ListPlayLogs();
    return logs.empty() ? std::wstring() : logs.back();
}

// Deletes the oldest logs so that, with the one about to be created, kPlayLogsKept remain.
void PrunePlayLogs() {
    const std::vector<std::wstring> logs = ListPlayLogs();
    if (logs.size() < kPlayLogsKept) return;
    for (size_t i = 0; i + (kPlayLogsKept - 1) < logs.size(); ++i) DeleteFileW(logs[i].c_str());
}

void OpenPath(const std::wstring& path) {
    if (!Exists(path)) {
        ShowMissing(L"file", path);
        return;
    }
    ShellExecuteW(g_window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- crash reports ----
//
// A session is "normal" when the game exits with code 0 and its play log holds the
// runtime's power-off line ("console: turning off"); anything else is unexpected and gets a
// local report under logs\crash\<stamp>\report.txt. Nothing is uploaded: the log holds user
// paths, so the report only ever sits on disk for the user to look at or attach by hand.

constexpr size_t kCrashReportsKept = 20;
constexpr size_t kCrashReadBytes = 256 * 1024;
constexpr size_t kCrashTailLines = 200;
constexpr const char* kPowerOffLine = "console: turning off";

struct CrashNotice {
    bool pending = false;
    std::wstring stamp;       // The report folder name; also what [crash] seen stores.
    std::wstring dir;         // logs\crash\<stamp>
    unsigned long code = 0;   // Exit code read back from that report.
};
CrashNotice g_crash;
std::wstring g_sessionLog;    // Play log of the game launched by Play; empty when none was created.

std::wstring CrashDir() { return Join(Join(g_root, L"logs"), L"crash"); }

// Reads up to maxBytes from the start or the end of a file. False when it cannot be opened.
bool ReadFileRange(const std::wstring& path, bool fromEnd, size_t maxBytes, std::string& out, bool& truncated) {
    out.clear();
    truncated = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    size_t want = static_cast<size_t>(size.QuadPart);
    if (want > maxBytes) {
        truncated = true;
        want = maxBytes;
        if (fromEnd) {
            LARGE_INTEGER at{};
            at.QuadPart = size.QuadPart - static_cast<long long>(maxBytes);
            SetFilePointerEx(file, at, nullptr, FILE_BEGIN);
        }
    }
    out.resize(want);
    DWORD read = 0;
    const bool ok = want == 0 || ReadFile(file, out.data(), static_cast<DWORD>(want), &read, nullptr);
    CloseHandle(file);
    out.resize(ok ? read : 0);
    return ok;
}

std::wstring HexCode(unsigned long code) {
    wchar_t text[16]{};
    swprintf_s(text, L"0x%08lX", code);
    return text;
}

std::string LastLines(const std::string& text, bool cutAtStart, size_t count) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        size_t stop = end;
        if (stop > start && text[stop - 1] == '\r') --stop;
        lines.push_back(text.substr(start, stop - start));
        start = end + 1;
    }
    if (cutAtStart && !lines.empty()) lines.erase(lines.begin());  // First line is probably partial.
    const size_t from = lines.size() > count ? lines.size() - count : 0;
    std::string out;
    for (size_t i = from; i < lines.size(); ++i) out += lines[i] + "\n";
    return out;
}

// The sub-directories of logs\crash, oldest first (names are timestamps).
std::vector<std::wstring> ListCrashFolders() {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(Join(CrashDir(), L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return names;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(data.cFileName, L".") != 0 &&
            wcscmp(data.cFileName, L"..") != 0)
            names.push_back(data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(names.begin(), names.end());
    return names;
}

// Keeps the newest kCrashReportsKept report folders. Returns how many could not be removed.
int PruneCrashReports() {
    const std::vector<std::wstring> names = ListCrashFolders();
    int failed = 0;
    for (size_t i = 0; i + kCrashReportsKept < names.size(); ++i) {
        const std::wstring dir = Join(CrashDir(), names[i]);
        const std::wstring report = Join(dir, L"report.txt");
        if ((Exists(report) && !DeleteFileW(report.c_str())) || !RemoveDirectoryW(dir.c_str())) ++failed;
    }
    return failed;
}

struct SessionVerdict {
    bool unexpected = false;
    std::wstring reason;
};

SessionVerdict ClassifySession(unsigned long code, bool logRead, const std::string& tail) {
    SessionVerdict v;
    const bool powerOff = logRead && tail.find(kPowerOffLine) != std::string::npos;
    if (!logRead) {
        v.unexpected = true;
        v.reason = code == 0 ? L"exit code 0, but the play log is missing or unreadable, so a clean shutdown is unproven"
                             : L"exit code is not 0 and the play log is missing or unreadable";
    } else if (!powerOff && code != 0) {
        v.unexpected = true;
        v.reason = L"exit code is not 0 and the log has no 'console: turning off' line";
    } else if (!powerOff) {
        v.unexpected = true;
        v.reason = L"exit code 0, but the log has no 'console: turning off' line";
    } else if (code != 0) {
        v.unexpected = true;
        v.reason = L"the log has 'console: turning off', but the exit code is not 0";
    } else {
        v.reason = L"exit code 0 and the log has 'console: turning off'";
    }
    return v;
}

std::string OptionsSection() {
    wchar_t buffer[8192]{};
    GetPrivateProfileSectionW(L"options", buffer, ARRAYSIZE(buffer), g_ini.c_str());
    std::string out = "[options]\n";
    bool any = false;
    for (const wchar_t* p = buffer; *p; p += wcslen(p) + 1) {
        out += Narrow(p) + "\n";
        any = true;
    }
    if (!any) out += "(empty or missing)\n";
    return out;
}

// Writes logs\crash\<stamp>\report.txt. On failure returns false with the reason in error.
bool WriteCrashReport(const std::wstring& logPath, unsigned long code, const SessionVerdict& verdict,
                      const std::string& head, const std::string& tail, bool tailCut, std::wstring& reportPath,
                      std::wstring& error) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[32]{};
    swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
    CreateDirectoryW(Join(g_root, L"logs").c_str(), nullptr);
    CreateDirectoryW(CrashDir().c_str(), nullptr);
    std::wstring name = stamp;
    for (int n = 2; Exists(Join(CrashDir(), name)) && n < 100; ++n) name = std::wstring(stamp) + L"-" + std::to_wstring(n);
    const std::wstring dir = Join(CrashDir(), name);
    if (!CreateDirectoryW(dir.c_str(), nullptr)) {
        error = L"Could not create " + dir + L" (Windows error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    std::string gpu;
    for (size_t start = 0; start < head.size();) {
        size_t end = head.find('\n', start);
        if (end == std::string::npos) end = head.size();
        std::string line = head.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("vulkan: ", 0) == 0) {
            gpu = line;
            break;
        }
        start = end + 1;
    }

    char when[48];
    sprintf_s(when, "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
              now.wSecond);

    std::string report = "RF:ToD launcher crash report\n";
    report += std::string("Launcher build: ") + __DATE__ + " " + __TIME__ + "\n";
    report += std::string("Time: ") + when + "\n";
    report += "Exit code: " + std::to_string(code) + " (" + Narrow(HexCode(code)) + ")\n";
    report += "Classification: unexpected\n";
    report += "Reason: " + Narrow(verdict.reason) + "\n";
    report += "Play log: " + Narrow(logPath.empty() ? L"(none)" : logPath) + "\n";
    report += "GPU: " + (gpu.empty() ? std::string("(no 'vulkan: ' line in the log)") : gpu) + "\n\n";
    report += OptionsSection() + "\n";
    report += "config\\setup-state.json:\n";
    const std::string state = ReadTextFile(SetupStatePath());
    report += (state.empty() ? std::string("(missing or empty)\n") : state);
    if (!state.empty() && state.back() != '\n') report += "\n";
    report += "\nLast " + std::to_string(kCrashTailLines) + " lines of the play log" + (tailCut ? " (log is longer)" : "") +
              ":\n";
    report += tail.empty() ? std::string("(no log content)\n") : LastLines(tail, tailCut, kCrashTailLines);

    reportPath = Join(dir, L"report.txt");
    HANDLE file = CreateFileW(reportPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Could not write " + reportPath + L" (Windows error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, report.data(), static_cast<DWORD>(report.size()), &written, nullptr) &&
                    written == report.size();
    CloseHandle(file);
    if (!ok) {
        error = L"Could not write all of " + reportPath + L".";
        return false;
    }
    const int stuck = PruneCrashReports();
    if (stuck > 0) error = L"Report saved, but " + std::to_wstring(stuck) + L" old crash folder(s) could not be removed.";
    return true;
}

// Classifies one finished session and, when it was unexpected, writes the report. Fills
// summary with a one-line description and reportPath with the report (empty if none).
// Returns true when the session was unexpected.
bool EvaluateSession(const std::wstring& logPath, unsigned long code, std::wstring& summary, std::wstring& reportPath,
                     std::wstring& reason, bool& wroteOk) {
    std::string tail, head;
    bool tailCut = false, headCut = false;
    const bool readTail = !logPath.empty() && ReadFileRange(logPath, true, kCrashReadBytes, tail, tailCut);
    if (readTail) {
        ReadFileRange(logPath, false, kCrashReadBytes, head, headCut);
    }
    const SessionVerdict verdict = ClassifySession(code, readTail, tail);
    reason = verdict.reason;
    reportPath.clear();
    wroteOk = true;
    if (!verdict.unexpected) {
        summary = L"normal";
        return false;
    }
    summary = L"unexpected";
    std::wstring error;
    wroteOk = WriteCrashReport(logPath, code, verdict, head, tail, tailCut, reportPath, error);
    if (!error.empty()) reason += L" [" + error + L"]";
    if (!wroteOk) reportPath.clear();
    return true;
}

// Newest report folder not yet marked seen becomes the Home notice.
void LoadCrashNotice() {
    g_crash = CrashNotice{};
    const std::vector<std::wstring> names = ListCrashFolders();
    if (names.empty()) return;
    const std::wstring& newest = names.back();
    if (newest.compare(ReadIni(L"crash", L"seen", L"")) <= 0) return;
    const std::wstring dir = Join(CrashDir(), newest);
    std::string text;
    bool cut = false;
    if (!ReadFileRange(Join(dir, L"report.txt"), false, 64 * 1024, text, cut)) return;
    const size_t at = text.find("Exit code: ");
    if (at == std::string::npos) return;
    g_crash.pending = true;
    g_crash.stamp = newest;
    g_crash.dir = dir;
    g_crash.code = std::strtoul(text.c_str() + at + 11, nullptr, 10);
}

void MarkCrashSeen() {
    if (!g_crash.pending) return;
    WriteIni(L"crash", L"seen", g_crash.stamp);
    if (ReadIni(L"crash", L"seen", L"") != g_crash.stamp) {
        SetStatus(L"Could not save [crash] seen to " + g_ini + L"; the notice will return on the next start.");
    }
    g_crash.pending = false;
    if (g_window) {
        BuildWidgets();
        RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }
}

// Called when the game process launched by Play has exited.
void RecordSessionEnd(unsigned long code, const std::wstring& logPath) {
    std::wstring summary, reportPath, reason;
    bool wroteOk = true;
    const bool unexpected = EvaluateSession(logPath, code, summary, reportPath, reason, wroteOk);
    if (!unexpected) return;
    if (!wroteOk) {
        SetStatus(L"Game exited unexpectedly (" + HexCode(code) + L"); the crash report could not be written: " + reason);
        return;
    }
    LoadCrashNotice();
    SetStatus(L"Game ended unexpectedly (" + HexCode(code) + L"); report: " + reportPath);
}

// Headless: --check-session <log> <exit code>. Prints the classification (to the parent
// console or redirected stdout) and also writes logs\crash\last-check.txt. Exit 0.
void CheckSessionCli(const std::wstring& logPath, unsigned long code) {
    std::wstring summary, reportPath, reason;
    bool wroteOk = true;
    EvaluateSession(logPath, code, summary, reportPath, reason, wroteOk);
    std::string out = "classification: " + Narrow(summary) + "\n";
    out += "exit code: " + std::to_string(code) + " (" + Narrow(HexCode(code)) + ")\n";
    out += "reason: " + Narrow(reason) + "\n";
    out += "log: " + Narrow(logPath) + "\n";
    out += "report: " + (reportPath.empty() ? std::string(wroteOk ? "(none)" : "(write failed)") : Narrow(reportPath)) + "\n";

    CreateDirectoryW(Join(g_root, L"logs").c_str(), nullptr);
    CreateDirectoryW(CrashDir().c_str(), nullptr);
    const std::wstring lastCheck = Join(CrashDir(), L"last-check.txt");
    HANDLE file = CreateFileW(lastCheck.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
        CloseHandle(file);
    } else {
        out += "warning: could not write " + Narrow(lastCheck) + "\n";
    }
    HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!std_out || std_out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            std_out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (std_out && std_out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(std_out, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    }
}

void GoTo(Page page) {
    if (g_page == page) return;
    g_page = page;
    g_hot = 0;
    g_pressed = 0;
    if (g_slider) {
        g_slider = 0;
        ReleaseCapture();
    }
    BuildWidgets();
    if (g_window) RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
}

void PickImage() {
    wchar_t file[2048]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"Packed image (*.wrd)\0*.wrd\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = ARRAYSIZE(file);
    dialog.lpstrTitle = L"Use a pre-baked RF:ToD image";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    g_config.image = file;
    WriteIni(L"paths", L"image", g_config.image);
    SetStatus(ImageCurrent() ? L"Packed image set and current." : L"Packed image set; rebuild to refresh it.");
    RefreshSetup();
    BuildWidgets();
    RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
}

void PickDisc() {
    BROWSEINFOW browse{};
    browse.hwndOwner = g_window;
    browse.lpszTitle = L"Choose the extracted disc folder: the one with main.dol and boot.bin";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
    if (!item) return;
    wchar_t folder[MAX_PATH]{};
    const bool chosen = SHGetPathFromIDListW(item, folder) != FALSE;
    CoTaskMemFree(item);
    if (!chosen) return;
    std::wstring path = folder;
    // Point at the sys folder, which is what the runtime opens, even when the whole
    // extracted tree was chosen.
    if (Exists(Join(path, L"sys\\main.dol")) && !Exists(Join(path, L"main.dol"))) path = Join(path, L"sys");
    g_config.disc = path;
    WriteIni(L"paths", L"disc", g_config.disc);
    SetStatus(Exists(Join(g_config.disc, L"main.dol")) ? L"Extracted disc set."
                                                       : L"That folder has no main.dol; pick the sys folder.");
    RefreshSetup();
    BuildWidgets();
    RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
}

void PickConsole() {
    wchar_t file[2048]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = g_window;
    dialog.lpstrFilter = L"Game console (wii-recomp.exe)\0wii-recomp.exe\0Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = ARRAYSIZE(file);
    dialog.lpstrTitle = L"Use wii-recomp.exe";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    g_config.console = file;
    WriteIni(L"paths", L"console", g_config.console);
    SetStatus(L"Game console set; rebuild or play from here.");
    RefreshSetup();
    BuildWidgets();
    RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
}

std::wstring FormatDouble(double value, const wchar_t* format) {
    wchar_t buffer[64]{};
    swprintf_s(buffer, format, value);
    return buffer;
}

// ---- gamepads ----

std::wstring Widen(const char* text) {
    if (!text || !*text) return L"";
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 1) return L"";
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], length);
    out.resize(std::wcslen(out.c_str()));
    return out;
}

const wchar_t* PadTypeName(int type) {
    switch (type) {
    case 1: return L"Standard";
    case 2: return L"Xbox 360";
    case 3: return L"Xbox";
    case 4: return L"PlayStation 3";
    case 5: return L"PlayStation 4";
    case 6: return L"PlayStation 5";
    case 7: return L"Switch Pro";
    case 8: return L"Joy-Con left";
    case 9: return L"Joy-Con right";
    case 10: return L"Joy-Con pair";
    case 11: return L"GameCube";
    case 12: return L"Steam Controller";
    default: return L"Gamepad";
    }
}

template <typename T>
bool LoadProc(HMODULE module, const char* name, T& function) {
    function = reinterpret_cast<T>(GetProcAddress(module, name));
    return function != nullptr;
}

bool LoadSdl() {
    if (g_sdl.module) return true;
    // SDL3.dll and its own dependency (libiconv) sit together in the MSYS2 folder; the
    // launcher's process PATH doesn't include it, so add it to the DLL search list.
    SetDllDirectoryW(g_config.msys.c_str());
    const std::wstring beside = Join(g_config.msys, L"SDL3.dll");
    HMODULE module = LoadLibraryW(beside.c_str());
    if (!module) module = LoadLibraryW(L"SDL3.dll");
    if (!module) return false;
    Sdl sdl;
    sdl.module = module;
    const bool ok = LoadProc(module, "SDL_Init", sdl.init) && LoadProc(module, "SDL_Quit", sdl.quit) &&
                    LoadProc(module, "SDL_PumpEvents", sdl.pump) && LoadProc(module, "SDL_GetGamepads", sdl.gamepads) &&
                    LoadProc(module, "SDL_OpenGamepad", sdl.open) && LoadProc(module, "SDL_CloseGamepad", sdl.close) &&
                    LoadProc(module, "SDL_GetGamepadName", sdl.name) &&
                    LoadProc(module, "SDL_GetGamepadType", sdl.type) &&
                    LoadProc(module, "SDL_GetGamepadSerial", sdl.serial) &&
                    LoadProc(module, "SDL_GetGamepadPowerInfo", sdl.power) && LoadProc(module, "SDL_free", sdl.release);
    if (!ok) {
        FreeLibrary(module);
        return false;
    }
    LoadProc(module, "SDL_UpdateGamepads", sdl.update);   // The input check needs these three;
    LoadProc(module, "SDL_GetGamepadButton", sdl.button); // the controller card works without.
    LoadProc(module, "SDL_GetGamepadAxis", sdl.axis);
    g_sdl = sdl;
    return true;
}

void ClosePads() {
    if (!g_sdl.module) return;
    if (g_sdl.initialized && g_sdl.quit) g_sdl.quit();
    FreeLibrary(g_sdl.module);
    g_sdl = Sdl{};
}

// The first gamepad SDL opens is the one the runtime gives player one, so the list here is
// the list the game will see. SDL_INIT_GAMEPAD is 0x2000.
void RefreshPads(bool restart) {
    g_padsTried = true;
    if (restart) ClosePads();
    g_pads.clear();
    g_padsError = false;
    if (!LoadSdl()) {
        g_padsError = true;
        return;
    }
    if (!g_sdl.initialized) {
        if (!g_sdl.init(0x00002000u)) {
            g_padsError = true;
            return;
        }
        g_sdl.initialized = true;
    }
    g_sdl.pump();
    int count = 0;
    unsigned* ids = g_sdl.gamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        void* pad = g_sdl.open(ids[i]);
        if (!pad) continue;
        GamepadInfo info;
        info.name = Widen(g_sdl.name(pad));
        if (info.name.empty()) info.name = L"Gamepad";
        std::wstring detail = PadTypeName(g_sdl.type(pad));
        const char* serial = g_sdl.serial(pad);
        if (serial && *serial) {
            detail += L"  ·  ";
            detail += Widen(serial);
        }
        int percent = -1;
        const int state = g_sdl.power(pad, &percent);
        if (percent >= 0) {
            detail += L"  ·  ";
            detail += std::to_wstring(percent);
            detail += L"%";
        }
        if (state == 3) detail += L" charging";
        else if (state == 4) detail += L" full";
        else if (state == 2) detail += L" wired";
        else if (state == 1) detail += L" on battery";
        info.detail = detail;
        info.battery = percent;
        g_pads.push_back(info);
        g_sdl.close(pad);
    }
    g_sdl.release(ids);
}

// The pads the input check is reading, opened again for it. Closed when the window closes.
void CloseTestPads() {
    if (g_sdl.close) {
        for (TestPad& pad : g_testPads) {
            if (pad.handle) g_sdl.close(pad.handle);
        }
    }
    g_testPads.clear();
}

void RefreshTestPads() {
    CloseTestPads();
    if (!g_sdl.initialized || !g_sdl.gamepads || !g_sdl.open) return;
    g_sdl.pump();
    int count = 0;
    unsigned* ids = g_sdl.gamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count && g_testPads.size() < 2; ++i) {
        void* handle = g_sdl.open(ids[i]);
        if (!handle) continue;
        TestPad pad;
        pad.handle = handle;
        pad.name = Widen(g_sdl.name(handle));
        if (pad.name.empty()) pad.name = L"Gamepad";
        g_testPads.push_back(pad);
    }
    g_sdl.release(ids);
}

// The deadzone and speed the console will use, read from the controllers.ini beside it.
void ReadPadSettings() {
    g_testDeadzone = 0.18;
    g_testSpeed = 1.0;
    std::wstring directory = g_config.console;
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash != std::wstring::npos) directory.resize(slash);
    g_testConfigPath = Join(directory, L"controllers.ini");
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(L"pad", L"deadzone", L"0.18", buffer, ARRAYSIZE(buffer), g_testConfigPath.c_str());
    const double deadzone = _wtof(buffer);
    if (deadzone >= 0 && deadzone <= 0.9) g_testDeadzone = deadzone;
    GetPrivateProfileStringW(L"pad", L"stick_speed", L"1.0", buffer, ARRAYSIZE(buffer), g_testConfigPath.c_str());
    const double speed = _wtof(buffer);
    if (speed >= 0.25 && speed <= 4) g_testSpeed = speed;
}

// ---- layout ----

Layout MakeLayout(float width) {
    Layout l{};
    const float m = static_cast<float>(S(32));
    const float cardWidth = width - 2 * m;
    const float right = width - m;

    l.quit = RectF(right - S(78), static_cast<float>(S(24)), static_cast<float>(S(78)), static_cast<float>(S(32)));
    l.gear = RectF(l.quit.X - S(8) - S(36), l.quit.Y, static_cast<float>(S(36)), static_cast<float>(S(32)));

    if (g_page == Page::Settings) {
        const float gap = static_cast<float>(S(16));
        const float top = static_cast<float>(S(180));
        const float cardHeight = static_cast<float>(S(276));
        l.settings = RectF(m, top, cardWidth * 0.49f, cardHeight);
        l.graphics = RectF(l.settings.X + l.settings.Width + gap, top,
                           m + cardWidth - (l.settings.X + l.settings.Width + gap), cardHeight);
        l.ingame = RectF(m, top + cardHeight + S(12), cardWidth, static_cast<float>(S(114)));

        // Every setting sits in a slot; the controls are placed from their slot, and the draw
        // code takes its labels from the same slots.
        const auto slotIn = [](const RectF& card, int rel, int height) {
            return RectF(card.X + S(14), card.Y + S(rel), card.Width - S(28), static_cast<float>(S(height)));
        };
        l.slotDisplay[0] = slotIn(l.settings, 44, 50);
        l.slotDisplay[1] = slotIn(l.settings, 100, 70);
        l.slotDisplay[2] = slotIn(l.settings, 176, 70);
        l.slotGraphics[0] = slotIn(l.graphics, 44, 70);
        l.slotGraphics[1] = slotIn(l.graphics, 120, 52);
        l.slotGraphics[2] = slotIn(l.graphics, 178, 70);
        l.slotIngame = slotIn(l.ingame, 40, 48);

        const float switchW = static_cast<float>(S(44)), switchH = static_cast<float>(S(24));
        const float pillGap = static_cast<float>(S(8));
        const float pillH = static_cast<float>(S(30));

        const RectF& d0 = l.slotDisplay[0];
        l.fullscreenSwitch = RectF(d0.X + d0.Width - S(14) - switchW, d0.Y + S(13), switchW, switchH);
        const RectF& d1 = l.slotDisplay[1];
        const float sizeWidth = (d1.Width - S(28) - 3 * pillGap) / 4;
        for (int i = 0; i < 4; ++i)
            l.size[i] = RectF(d1.X + S(14) + i * (sizeWidth + pillGap), d1.Y + S(34), sizeWidth, pillH);
        const RectF& d2 = l.slotDisplay[2];
        const float presentWidth = (d2.Width - S(28) - 2 * pillGap) / 3;
        for (int i = 0; i < 3; ++i)
            l.present[i] = RectF(d2.X + S(14) + i * (presentWidth + pillGap), d2.Y + S(34), presentWidth, pillH);

        const RectF& g0 = l.slotGraphics[0];
        const float scaleWidth = (g0.Width - S(28) - 3 * pillGap) / 4;
        for (int i = 0; i < 4; ++i)
            l.scale[i] = RectF(g0.X + S(14) + i * (scaleWidth + pillGap), g0.Y + S(34), scaleWidth, pillH);
        const RectF& g1 = l.slotGraphics[1];
        l.fsrSwitch = RectF(g1.X + g1.Width - S(14) - switchW, g1.Y + S(14), switchW, switchH);
        const RectF& g2 = l.slotGraphics[2];
        l.sharpnessTrack = RectF(g2.X + S(14), g2.Y + S(54), g2.Width - S(28), static_cast<float>(S(10)));
        l.sharpnessHit = RectF(l.sharpnessTrack.X - S(10), l.sharpnessTrack.Y - S(12),
                               l.sharpnessTrack.Width + S(20), static_cast<float>(S(30)));

        const RectF& in0 = l.slotIngame;
        l.overlaySwitch = RectF(in0.X + S(14) + S(240), in0.Y + S(12), switchW, switchH);
        l.controls = RectF(in0.X + in0.Width - S(14) - S(104), in0.Y + S(6), static_cast<float>(S(104)),
                           static_cast<float>(S(36)));
        l.testInput = RectF(l.controls.X - S(8) - S(108), l.controls.Y, static_cast<float>(S(108)),
                            static_cast<float>(S(36)));

        l.back = RectF(m, static_cast<float>(S(592)), static_cast<float>(S(100)), static_cast<float>(S(36)));
        l.done = RectF(right - S(100), static_cast<float>(S(592)), static_cast<float>(S(100)),
                       static_cast<float>(S(40)));
        return l;
    }

    if (g_page == Page::GameFiles) {
        const float top = static_cast<float>(S(180));
        l.settings = RectF(m, top, cardWidth, static_cast<float>(S(376)));
        for (int i = 0; i < 4; ++i)
            l.slotFiles[i] = RectF(m + S(14), top + S(42) + i * S(82), cardWidth - S(28), static_cast<float>(S(70)));
        const float buttonRight = l.slotFiles[0].X + l.slotFiles[0].Width - S(14);
        const float buttonH = static_cast<float>(S(30));
        l.useImage = RectF(buttonRight - S(110), l.slotFiles[0].Y + S(20), static_cast<float>(S(110)), buttonH);
        l.openImage = RectF(l.useImage.X - S(8) - S(64), l.useImage.Y, static_cast<float>(S(64)), buttonH);
        l.useFolder = RectF(l.useImage.X, l.slotFiles[1].Y + S(20), static_cast<float>(S(110)), buttonH);
        l.openDisc = RectF(l.useFolder.X - S(8) - S(64), l.useFolder.Y, static_cast<float>(S(64)), buttonH);
        l.openTitle = RectF(l.openDisc.X, l.slotFiles[2].Y + S(20), static_cast<float>(S(64)), buttonH);
        l.useConsole = RectF(l.useImage.X, l.slotFiles[3].Y + S(20), static_cast<float>(S(110)), buttonH);
        l.openConsole = RectF(l.useConsole.X - S(8) - S(64), l.useConsole.Y, static_cast<float>(S(64)), buttonH);

        l.back = RectF(m, static_cast<float>(S(580)), static_cast<float>(S(100)), static_cast<float>(S(36)));
        l.rebuild = RectF(right - S(100) - S(10) - S(140), static_cast<float>(S(576)),
                          static_cast<float>(S(140)), static_cast<float>(S(40)));
        l.play = RectF(right - S(100), static_cast<float>(S(572)), static_cast<float>(S(100)),
                       static_cast<float>(S(44)));
        return l;
    }

    if (g_page == Page::Build) {
        l.settings = RectF(m, static_cast<float>(S(180)), cardWidth, static_cast<float>(S(384)));
        l.buildStage = RectF(l.settings.X + S(14), l.settings.Y + S(42), l.settings.Width - S(28), static_cast<float>(S(48)));
        l.buildBar = RectF(l.settings.X + S(24), l.settings.Y + S(100), l.settings.Width - S(48), static_cast<float>(S(24)));
        l.buildConsole = RectF(l.settings.X + S(20), l.settings.Y + S(134), l.settings.Width - S(40), static_cast<float>(S(192)));
        l.back = RectF(m, static_cast<float>(S(580)), static_cast<float>(S(100)), static_cast<float>(S(36)));
        l.buildHide = RectF(right - S(100) - S(10) - S(120) - S(10) - S(120), static_cast<float>(S(580)),
                            static_cast<float>(S(120)), static_cast<float>(S(38)));
        l.buildLog = RectF(right - S(100) - S(10) - S(120), static_cast<float>(S(580)),
                           static_cast<float>(S(120)), static_cast<float>(S(38)));
        l.buildPlay = RectF(right - S(100), static_cast<float>(S(576)), static_cast<float>(S(100)),
                            static_cast<float>(S(44)));
        return l;
    }

    // Home.
    l.hero = RectF(m, static_cast<float>(S(112)), cardWidth * 0.62f, static_cast<float>(S(212)));
    const float deviceGap = static_cast<float>(S(16));
    l.devices = RectF(l.hero.X + l.hero.Width + deviceGap, l.hero.Y,
                      m + cardWidth - (l.hero.X + l.hero.Width + deviceGap), static_cast<float>(S(212)));
    l.rescan = RectF(l.devices.X + l.devices.Width - S(24) - S(76), l.devices.Y + S(14),
                     static_cast<float>(S(76)), static_cast<float>(S(24)));
    l.testInput = RectF(l.rescan.X - S(8) - S(76), l.rescan.Y, static_cast<float>(S(76)), static_cast<float>(S(24)));
    l.play = RectF(l.hero.X + l.hero.Width - S(24) - S(150), l.hero.Y + S(38), static_cast<float>(S(150)),
                   static_cast<float>(S(48)));
    l.heroFiles = RectF(l.play.X - S(8) - S(110), l.play.Y + S(5), static_cast<float>(S(110)),
                        static_cast<float>(S(38)));
    l.heroRebuild = RectF(l.play.X, l.hero.Y + S(112), static_cast<float>(S(150)), static_cast<float>(S(34)));
    l.crashDismiss = RectF(l.play.X + l.play.Width - S(90), l.heroRebuild.Y, static_cast<float>(S(90)), static_cast<float>(S(34)));
    l.crashOpen = RectF(l.crashDismiss.X - S(8) - S(120), l.heroRebuild.Y, static_cast<float>(S(120)), static_cast<float>(S(34)));

    const float actionY = static_cast<float>(S(336));
    const float actionH = static_cast<float>(S(38));
    l.gameFiles = RectF(m, actionY, static_cast<float>(S(110)), actionH);
    l.rebuild = RectF(m + S(120), actionY, static_cast<float>(S(120)), actionH);
    l.controls = RectF(m + S(250), actionY, static_cast<float>(S(110)), actionH);
    l.saves = RectF(m + S(370), actionY, static_cast<float>(S(90)), actionH);
    l.logs = RectF(m + S(470), actionY, static_cast<float>(S(80)), actionH);
    l.config = RectF(m + S(560), actionY, static_cast<float>(S(110)), actionH);
    l.playLog = RectF(m + S(680), actionY, static_cast<float>(S(110)), actionH);

    l.settings = RectF(m, static_cast<float>(S(388)), cardWidth, static_cast<float>(S(226)));
    l.settingsButton = RectF(l.settings.X + l.settings.Width - S(24) - S(76), l.settings.Y + S(12),
                             static_cast<float>(S(76)), static_cast<float>(S(24)));
    for (int i = 0; i < 4; ++i) l.rowTop[i] = l.settings.Y + S(50) + i * static_cast<float>(S(40));
    return l;
}

// ---- theme ----
//
// Every colour and font the launcher draws with lives here, under a role name. Drawing code
// asks for a role (Th.cardText), never for a literal, so a restyle is an edit of this table.
// The look is a cartoon island village: sky and sea behind, parchment panels with a thick
// dark outline, glossy candy buttons. Everything is drawn in code from these values and
// system fonts; nothing is bundled or taken from another game.

Color Alpha(const Color& c, REAL alpha) {
    return Color(static_cast<BYTE>(alpha), c.GetR(), c.GetG(), c.GetB());
}

COLORREF Rgb(const Color& c) { return RGB(c.GetR(), c.GetG(), c.GetB()); }

// Linear blend of two colours, t in 0..1 (0 is a, 1 is b).
Color Mix(const Color& a, const Color& b, float t) {
    const auto lerp = [t](BYTE x, BYTE y) { return static_cast<BYTE>(std::lround(x + (y - x) * t)); };
    return Color(lerp(a.GetA(), b.GetA()), lerp(a.GetR(), b.GetR()), lerp(a.GetG(), b.GetG()),
                 lerp(a.GetB(), b.GetB()));
}

// One glossy button colour: face gradient, the darker lip under it, the outline, and the
// plain label colour used when a label is too small for an outlined one.
struct Candy {
    Color top, bottom, lip, outline, text;
};

struct Theme {
    // Window background: sky, sea, clouds and the hills at the bottom.
    Color skyTop = C(96, 190, 240);
    Color skyBottom = C(186, 230, 248);
    Color sun = C(255, 244, 176, 150);
    Color sea = C(66, 160, 210);
    Color cloud = C(255, 255, 255, 215);
    Color cloudShade = C(206, 232, 248, 215);
    Color hillBack = C(92, 182, 84);
    Color hillFront = C(124, 204, 96);
    Color hillLine = C(60, 128, 56);

    // Text on the window background (always over sky or hills, so it is dark).
    Color bgText = C(8, 34, 62);
    Color bgTextDim = C(12, 44, 78);
    Color bgTextMuted = C(16, 52, 90);
    Color brandText = C(122, 40, 14);

    // Titles: heavy white letters with a dark outline.
    Color titleFill = C(255, 255, 255);
    Color titleOutlineBg = C(18, 46, 82);
    Color titleOutlineCard = C(78, 46, 22);
    Color titleShadow = C(0, 0, 0, 70);

    // Panels (cards): parchment with a thick dark outline, a light bevel on top, a darker
    // edge below and a soft shadow.
    Color panelTop = C(253, 243, 216);
    Color panelBottom = C(244, 226, 186);
    Color panelOutline = C(78, 46, 22);
    Color panelBevel = C(255, 252, 238);
    Color panelEdge = C(214, 176, 116);
    Color shadow = C(30, 16, 4, 70);

    // Text on panels, dark brown for contrast on parchment.
    Color cardText = C(62, 36, 16);
    Color cardTextDim = C(100, 66, 36);
    Color cardTextMuted = C(126, 92, 58);
    Color cardTitle = C(140, 84, 30);
    Color cardTitleBar = C(240, 176, 32);
    Color accentText = C(176, 40, 24);

    // Status tones on panels (dark enough for text on parchment).
    Color good = C(22, 112, 44);
    Color warn = C(146, 86, 0);
    Color bad = C(184, 34, 34);

    // Buttons.
    Candy candyGreen{C(136, 226, 84), C(62, 176, 44), C(34, 118, 30), C(22, 66, 24), C(18, 56, 18)};
    Candy candyGold{C(255, 232, 110), C(244, 182, 36), C(186, 122, 12), C(96, 60, 10), C(84, 48, 6)};
    Candy candyRed{C(255, 130, 104), C(214, 58, 44), C(140, 28, 22), C(80, 16, 12), C(255, 248, 244)};
    Candy candyGrey{C(200, 200, 196), C(164, 164, 160), C(120, 120, 116), C(82, 82, 80), C(40, 40, 38)};
    Candy candySand{C(248, 234, 200), C(230, 208, 160), C(186, 156, 104), C(110, 74, 40), C(92, 56, 24)};
    Color btnLabel = C(255, 255, 255);
    Color glossTop = C(255, 255, 255, 120);
    Color glossBottom = C(255, 255, 255, 18);
    Color mixLight = C(255, 255, 255);  // What hot faces and pills are blended toward.
    Color mixDark = C(0, 0, 0);         // What pressed faces are blended toward.

    // Switches, sliders, progress and the other small controls.
    Color knobFill = C(255, 252, 244);
    Color trackTop = C(196, 168, 118);
    Color trackBottom = C(222, 198, 150);
    Color gearGlyph = C(96, 58, 12);
    Color gearGlyphHot = C(60, 30, 4);

    // Header status pill.
    Color pillOk = C(126, 224, 92);
    Color pillBusy = C(255, 214, 84);
    Color pillBad = C(255, 132, 112);
    Color pillText = C(50, 30, 14);

    // Flow steps.
    Color stepLine = C(78, 46, 22, 170);

    // Home: hero card, notices, devices, build summary, footer.
    Color heroGlow = C(255, 226, 120, 130);
    Color heroGlowEdge = C(255, 226, 120, 0);
    Color heroRing = C(214, 170, 96, 90);
    Color eyebrowOk = C(150, 88, 0);
    Color eyebrowWarn = C(176, 40, 24);
    Color noticeDotCrash = C(200, 60, 40);
    Color noticeDotStale = C(230, 160, 20);
    Color deviceLow = C(214, 100, 30);
    Color deviceOk = C(50, 170, 70);
    Color rowDivider = C(120, 84, 48, 70);
    Color footerLine = C(40, 88, 40, 150);
    Color buildPercent = C(176, 40, 24);

    // Build page console: a dark panel, so light text.
    Color consoleFill = C(46, 34, 28);
    Color consoleBorder = C(26, 16, 10);
    Color logText = C(236, 226, 204);
    Color logHeader = C(255, 200, 100);
    Color logFail = C(255, 136, 120);
    Color logOk = C(150, 232, 120);
    Color logWarn = C(255, 216, 96);

    // Input check: sticks and pad labels (chips and triggers use the candy colours).
    Color stickFill = C(232, 212, 166);
    Color stickDeadzone = C(140, 100, 60);
    Color stickDot = C(240, 80, 48);
    Color stickDotRing = C(255, 246, 226);
    Color stickRaw = C(120, 120, 130, 240);
    Color stickRawEdge = C(60, 36, 16);
    Color padLabel = C(176, 40, 24);

    // Controls editor rows.
    Color rowFill = C(255, 248, 226);
    Color rowHotFill = C(255, 238, 190);
    Color rowBorder = C(200, 170, 120);
    Color rowHotBorder = C(150, 100, 48);
    Color rowListenA = C(255, 232, 140);
    Color rowListenB = C(255, 208, 96);
    Color rowListenBorder = C(176, 40, 24);
    Color rowText = C(62, 36, 16);
    Color rowValue = C(100, 66, 36);
    Color rowValueNone = C(126, 92, 58);
    Color rowValueListening = C(176, 40, 24);
    Color clearFill = C(232, 210, 166);
    Color clearHotFill = C(255, 196, 170);
    Color clearText = C(100, 66, 36);
    Color clearHotText = C(176, 40, 24);

    // Emblem (the crest in the header).
    Color emblemSeaTop = C(88, 190, 244);
    Color emblemSeaBottom = C(22, 98, 176);
    Color emblemWave = C(255, 255, 255);
    Color emblemWaveSoft = C(255, 255, 255, 190);
    Color emblemLeafTop = C(150, 232, 90);
    Color emblemLeafBottom = C(52, 158, 56);
    Color emblemVein = C(226, 255, 200, 200);

    // Page furniture: recessed slots, ribbon folds, key caps, tabs, input-check wells and the
    // pale body of the drawn devices.
    Color slotFill = C(255, 250, 236);
    Color slotShade = C(206, 172, 116, 130);
    Color pathFill = C(246, 236, 208);
    Color ribbonFold = C(196, 120, 14);
    Candy candyKey{C(255, 253, 246), C(238, 224, 192), C(196, 160, 104), C(110, 74, 40), C(62, 36, 16)};
    Candy candyKeyNone{C(246, 236, 212), C(238, 224, 194), C(214, 190, 150), C(178, 148, 104), C(112, 78, 46)};
    Candy candyKeyListen{C(255, 238, 150), C(255, 208, 96), C(200, 140, 30), C(176, 40, 24), C(150, 24, 16)};
    Color tabIdleTop = C(236, 214, 170);
    Color tabIdleBottom = C(212, 182, 126);
    Color tabIdleText = C(84, 52, 24);
    Color wellTop = C(196, 164, 110);
    Color wellBottom = C(240, 222, 178);
    Color wellLine = C(150, 112, 66, 150);
    Color deviceTop = C(252, 250, 244);
    Color deviceBottom = C(220, 222, 228);
    Color deviceButton = C(86, 70, 62);
    Color consoleBar = C(30, 20, 16);
    // Window frame (the title bar and border Windows draws around the client area).
    Color frameCaption = C(22, 78, 128);
    Color frameBorder = C(78, 46, 22);
    Color frameCaptionText = C(255, 255, 255);

    // Fonts: families with fallbacks, and the pixel sizes at 100% scale. The heavy family is
    // used for titles and button labels; body text stays in the regular family.
    const wchar_t* fontFamily = L"Segoe UI";
    const wchar_t* fontFamilyFallback = L"Arial";
    const wchar_t* fontHeavy = L"Segoe UI Black";
    const wchar_t* fontHeavyFallback = L"Arial Black";
    const wchar_t* fontMono = L"Consolas";
    const wchar_t* fontMonoFallback = L"Courier New";
    int sizeBrand = 12, sizeH1 = 30, sizeH2 = 22, sizeBody = 13, sizeSmall = 11, sizeButton = 13, sizeTiny = 11,
        sizePlay = 16, sizeMono = 10;
    // Labels at or above this size get an outlined heavy face; smaller ones stay plain bold,
    // because an outline turns muddy at 11 px.
    int outlineMinSize = 13;
};

const Theme Th{};

// ---- drawing primitives ----

void RoundRectPath(GraphicsPath& path, const RectF& r, REAL radius) {
    path.Reset();
    radius = std::max(0.5f, std::min(radius, std::min(r.Width, r.Height) / 2));
    const REAL d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRound(Graphics& g, const RectF& r, REAL radius, const Color& color) {
    GraphicsPath path;
    RoundRectPath(path, r, radius);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

void StrokeRound(Graphics& g, const RectF& r, REAL radius, const Color& color, REAL width = 1.0f) {
    GraphicsPath path;
    RoundRectPath(path, r, radius);
    Pen pen(color, width);
    g.DrawPath(&pen, &path);
}

RectF Deflate(const RectF& r, REAL d) {
    return RectF(r.X + d, r.Y + d, std::max(1.0f, r.Width - 2 * d), std::max(1.0f, r.Height - 2 * d));
}

// The thick outline every panel and control wears. Drawn inside the rect, so a shape never
// grows past the rectangle the layout (and the hit test) gave it.
REAL Edge() { return std::max(2.0f, SF(2.0f)); }

// A vertical-gradient rounded shape with its outline inset by half the pen width.
void ChunkyRound(Graphics& g, const RectF& r, REAL radius, const Color& top, const Color& bottom,
                 const Color& outline, REAL width) {
    const RectF body = Deflate(r, width / 2);
    GraphicsPath path;
    RoundRectPath(path, body, radius - width / 2);
    LinearGradientBrush fill(body, top, bottom, LinearGradientModeVertical);
    g.FillPath(&fill, &path);
    Pen pen(outline, width);
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &path);
}

// A soft drop shadow: three translucent layers, each a little larger.
void SoftShadow(Graphics& g, const RectF& r, REAL radius, REAL drop) {
    for (int i = 2; i >= 0; --i) {
        const REAL grow = SF(1.0f) * i;
        FillRound(g, RectF(r.X - grow, r.Y + drop - grow * 0.5f, r.Width + 2 * grow, r.Height + 2 * grow), radius + grow,
                  Alpha(Th.shadow, Th.shadow.GetA() / 3.0f));
    }
}

// A parchment panel: shadow, thick outline, light bevel along the top, darker edge below.
void Panel(Graphics& g, const RectF& r, REAL radius) {
    SoftShadow(g, r, radius, SF(4.0f));
    const REAL edge = std::max(3.0f, SF(3.0f));
    ChunkyRound(g, r, radius, Th.panelTop, Th.panelBottom, Th.panelOutline, edge);

    const RectF inner = Deflate(r, edge);
    GraphicsState state = g.Save();
    GraphicsPath clip;
    RoundRectPath(clip, inner, radius - edge);
    g.SetClip(&clip);
    SolidBrush lower(Alpha(Th.panelEdge, 150));
    g.FillRectangle(&lower, inner.X, inner.Y + inner.Height - SF(5.0f), inner.Width, SF(5.0f));
    g.Restore(state);

    Pen bevel(Th.panelBevel, std::max(1.5f, SF(1.5f)));
    bevel.SetStartCap(LineCapRound);
    bevel.SetEndCap(LineCapRound);
    const REAL y = inner.Y + SF(1.5f);
    g.DrawLine(&bevel, inner.X + radius, y, inner.X + inner.Width - radius, y);
}

// Heavy outlined text: the outline is drawn first, the fill over it, so the letters keep their
// shape and gain a dark edge. 'shadow' is an optional drop in pixels. 'size' is the em size
// in pixels. The family must be a heavy one (see g_familyHeavy).
void OutlinedText(Graphics& g, const std::wstring& text, const FontFamily* family, REAL size, int style,
                  const Color& fill, const Color& outline, const RectF& box,
                  StringAlignment align = StringAlignmentCenter, REAL shadow = 0.0f) {
    if (text.empty()) return;
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    GraphicsPath path;
    path.AddString(text.c_str(), static_cast<INT>(text.size()), family, style, size, box, &format);
    const REAL width = std::max(2.5f, size * 0.18f);

    if (shadow > 0) {
        std::unique_ptr<GraphicsPath> dropped(path.Clone());
        Matrix move;
        move.Translate(0.0f, shadow);
        dropped->Transform(&move);
        Pen shade(Th.titleShadow, width);
        shade.SetLineJoin(LineJoinRound);
        g.DrawPath(&shade, dropped.get());
        SolidBrush shadeFill(Th.titleShadow);
        g.FillPath(&shadeFill, dropped.get());
    }
    Pen pen(outline, width);
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &path);
    SolidBrush brush(fill);
    g.FillPath(&brush, &path);
}

// Width of a heavy string, from its path bounds (DrawString pads differently).
REAL HeavyWidth(const std::wstring& text, REAL size) {
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    GraphicsPath path;
    path.AddString(text.c_str(), static_cast<INT>(text.size()), g_familyHeavy, g_heavyStyle, size, PointF(0, 0), &format);
    RectF bounds;
    path.GetBounds(&bounds);
    return bounds.Width;
}

// A page or card title: heavy white letters with a dark outline and a small drop.
void Title(Graphics& g, const std::wstring& text, REAL size, const RectF& box, const Color& outline,
           StringAlignment align = StringAlignmentNear) {
    OutlinedText(g, text, g_familyHeavy, size, g_heavyStyle, Th.titleFill, outline, box, align, SF(2.0f));
}

// The base of every glossy control: shadow, the darker lip, the outline and the face with a
// highlight on its top half. The face is the rect minus the lip, so everything stays inside
// the rect. Pressed, the face sinks onto the lip (and the lip and shadow are gone). Returns
// the face, where the label goes.
RectF CandyBody(Graphics& g, const RectF& r, REAL radius, const Candy& candy, bool hot, bool pressed, REAL lip) {
    Color top = candy.top, bottom = candy.bottom;
    if (hot) {
        top = Mix(top, Th.mixLight, 0.20f);
        bottom = Mix(bottom, Th.mixLight, 0.20f);
    }
    if (pressed) {
        top = Mix(top, Th.mixDark, 0.10f);
        bottom = Mix(bottom, Th.mixDark, 0.10f);
    }
    const REAL edge = Edge();
    RectF face(r.X, r.Y, r.Width, r.Height - lip);
    if (pressed) {
        face = RectF(r.X, r.Y + lip, r.Width, r.Height - lip);
    } else {
        SoftShadow(g, RectF(r.X, r.Y + lip, r.Width, r.Height - lip), radius, SF(2.0f));
        ChunkyRound(g, r, radius, candy.lip, candy.lip, candy.outline, edge);
    }
    ChunkyRound(g, face, radius, top, bottom, candy.outline, edge);

    // Gloss on the top half of the face, and a thin light line along its top edge.
    const RectF gloss = Deflate(RectF(face.X, face.Y, face.Width, face.Height * 0.52f), edge + SF(1.0f));
    if (gloss.Width > 4 && gloss.Height > 3) {
        GraphicsPath path;
        RoundRectPath(path, gloss, std::max(1.0f, radius - edge - SF(1.0f)));
        LinearGradientBrush brush(gloss, Th.glossTop, Th.glossBottom, LinearGradientModeVertical);
        g.FillPath(&brush, &path);
    }
    return face;
}

enum class Tone { Auto, Green, Gold, Red };

void Text(Graphics& g, const std::wstring& text, Font* font, const Color& color, const RectF& box,
          StringAlignment align = StringAlignmentNear, bool ellipsis = false) {
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    if (ellipsis) format.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(text.c_str(), static_cast<INT>(text.size()), font, box, &format, &brush);
}

void SegmentPill(Graphics& g, const RectF& r, const std::wstring& label, bool selected, bool hot, bool enabled) {
    const Candy& candy = !enabled ? Th.candyGrey : selected ? Th.candyGreen : Th.candySand;
    const RectF face = CandyBody(g, r, r.Height / 2.0f, candy, hot && enabled && !selected, false, SF(3.0f));
    Text(g, label, g_fontTiny, candy.text, face, StringAlignmentCenter);
}

void Button(Graphics& g, const RectF& r, const std::wstring& label, bool primary, bool hot, bool pressed,
            bool enabled, bool playIcon = false, Font* font = nullptr, Tone tone = Tone::Auto) {
    const Candy& candy = !enabled ? Th.candyGrey
                         : tone == Tone::Red ? Th.candyRed
                         : (tone == Tone::Green || (tone == Tone::Auto && primary)) ? Th.candyGreen
                                                                                    : Th.candyGold;
    const REAL radius = SF(primary ? 14.0f : 11.0f);
    const REAL lip = SF(r.Height >= SF(45.0f) ? 5.0f : r.Height >= SF(33.0f) ? 4.0f : 3.0f);
    const RectF face = CandyBody(g, r, radius, candy, hot && enabled, pressed && enabled, lip);

    Font* labelFont = font ? font : g_fontButton;
    const REAL size = labelFont->GetSize();
    // Font sizes are S()-rounded pixels, so compare against S() as well, not SF().
    const bool bigLabel = size >= static_cast<REAL>(S(Th.outlineMinSize)) - 0.01f;
    const bool outlined = enabled && bigLabel;
    Font* plainFont = bigLabel ? labelFont : g_fontTiny;

    const auto drawLabel = [&](const RectF& box, StringAlignment align) {
        if (outlined) OutlinedText(g, label, g_familyHeavy, size, g_heavyStyle, Th.btnLabel, candy.outline, box, align, SF(1.0f));
        else Text(g, label, plainFont, candy.text, box, align);
    };
    if (!playIcon) {
        drawLabel(face, StringAlignmentCenter);
        return;
    }

    RectF bounds;
    g.MeasureString(label.c_str(), static_cast<INT>(label.size()), labelFont, PointF(0, 0), &bounds);
    const float textWidth = outlined ? HeavyWidth(label, size) : bounds.Width;
    const float iconW = SF(13), iconGap = SF(10);
    const float total = iconW + iconGap + textWidth;
    const float start = face.X + (face.Width - total) / 2;
    const float mid = face.Y + face.Height / 2;

    PointF triangle[3] = {PointF(start, mid - SF(8)), PointF(start, mid + SF(8)), PointF(start + iconW, mid)};
    Pen edge(candy.outline, std::max(2.0f, SF(2.5f)));
    edge.SetLineJoin(LineJoinRound);
    if (outlined) g.DrawPolygon(&edge, triangle, 3);
    SolidBrush iconBrush(outlined ? Th.btnLabel : candy.text);
    g.FillPolygon(&iconBrush, triangle, 3);
    drawLabel(RectF(start + iconW + iconGap - SF(2), face.Y, textWidth + SF(8), face.Height), StringAlignmentNear);
}

// A round knob with a dark outline and a little shadow.
void Knob(Graphics& g, REAL cx, REAL cy, REAL diameter, bool hot) {
    const REAL edge = Edge();
    SolidBrush shade(Alpha(Th.shadow, 90));
    g.FillEllipse(&shade, cx - diameter / 2, cy - diameter / 2 + SF(1.5f), diameter, diameter);
    const RectF body(cx - diameter / 2, cy - diameter / 2, diameter, diameter);
    const Color fill = hot ? Mix(Th.knobFill, Th.cardTitleBar, 0.25f) : Th.knobFill;
    SolidBrush brush(fill);
    const RectF inset = Deflate(body, edge / 2);
    g.FillEllipse(&brush, inset);
    Pen pen(Th.panelOutline, edge);
    g.DrawEllipse(&pen, inset);
}

void Switch(Graphics& g, const RectF& r, bool on, bool hot, bool enabled) {
    const Candy& candy = !enabled ? Th.candyGrey : on ? Th.candyGreen : Th.candySand;
    const REAL lip = SF(3.0f);
    const RectF face = CandyBody(g, r, r.Height / 2.0f, candy, hot && enabled, false, lip);
    const REAL knob = face.Height - SF(2.0f);
    const REAL pad = SF(1.0f) + knob / 2;
    const REAL cx = on ? face.X + face.Width - pad : face.X + pad;
    Knob(g, cx, face.Y + face.Height / 2, knob, false);
}

// A thick outlined groove with a candy fill and a knob; fraction is 0..1.
void SliderBody(Graphics& g, const RectF& track, float fraction, bool enabled, bool hot, REAL knobSize) {
    const REAL h = track.Height;
    const REAL edge = Edge();
    ChunkyRound(g, track, h / 2, Th.trackTop, Th.trackBottom, Th.panelOutline, edge);
    const REAL fill = track.Width * std::max(0.0f, std::min(1.0f, fraction));
    if (fill > 1) {
        const Candy& candy = enabled ? Th.candyGreen : Th.candyGrey;
        const RectF part(track.X, track.Y, std::max(fill, h), h);
        ChunkyRound(g, part, h / 2, candy.top, candy.bottom, candy.outline, edge);
    }
    const REAL x = track.X + std::max(knobSize / 2 - edge, std::min(track.Width - knobSize / 2 + edge, fill));
    Knob(g, x, track.Y + h / 2, knobSize, hot && enabled);
}

void Slider(Graphics& g, const RectF& track, double value, bool enabled) {
    SliderBody(g, track, static_cast<float>(value / 2.0), enabled, false, SF(18));
}

// Paragraphs wrap; Text above is for single lines. Used by the help text on the pages,
// where the reference launchers put the explanation under each setting.
void Paragraph(Graphics& g, const std::wstring& text, Font* font, const Color& color, const RectF& box,
               StringAlignment align = StringAlignmentNear) {
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisWord);
    format.SetFormatFlags(StringFormatFlagsLineLimit);
    g.DrawString(text.c_str(), static_cast<INT>(text.size()), font, box, &format, &brush);
}

// A small status dot with a thin dark rim.
void Dot(Graphics& g, float x, float y, const Color& tone) {
    SolidBrush brush(tone);
    g.FillEllipse(&brush, x, y, SF(8), SF(8));
    Pen rim(Th.panelOutline, std::max(1.0f, SF(1.2f)));
    g.DrawEllipse(&rim, x + SF(0.6f), y + SF(0.6f), SF(6.8f), SF(6.8f));
}

// A gear, drawn from a ring and eight teeth, on a small gold candy button: the settings
// entry point. While the settings page is open the button stays pressed in.
void Gear(Graphics& g, const RectF& r, bool hot, bool active) {
    const RectF face = CandyBody(g, r, SF(11), Th.candyGold, hot, active, SF(3.0f));
    const float cx = face.X + face.Width / 2, cy = face.Y + face.Height / 2;
    const float radius = SF(6.5f);
    const Color tone = hot ? Th.gearGlyphHot : Th.gearGlyph;
    Pen pen(tone, std::max(1.8f, SF(1.9f)));
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * 3.14159265f / 4.0f;
        const float x1 = cx + std::cos(angle) * radius * 0.95f;
        const float y1 = cy + std::sin(angle) * radius * 0.95f;
        const float x2 = cx + std::cos(angle) * radius * 1.5f;
        const float y2 = cy + std::sin(angle) * radius * 1.5f;
        g.DrawLine(&pen, x1, y1, x2, y2);
    }
    g.DrawEllipse(&pen, cx - radius, cy - radius, radius * 2, radius * 2);
    SolidBrush centre(tone);
    g.FillEllipse(&centre, cx - SF(2.6f), cy - SF(2.6f), SF(5.2f), SF(5.2f));
}

// A thick outlined XP-style bar with a glossy fill.
void Progress(Graphics& g, const RectF& track, int percent) {
    const REAL edge = Edge();
    ChunkyRound(g, track, track.Height / 2, Th.trackTop, Th.trackBottom, Th.panelOutline, edge);
    const float clamped = static_cast<float>(std::max(0, std::min(100, percent))) / 100.0f;
    const REAL fill = track.Width * clamped;
    if (fill > 1) {
        const RectF part(track.X, track.Y, std::max(fill, edge * 2 + 4), track.Height);
        ChunkyRound(g, part, track.Height / 2, Th.candyGreen.top, Th.candyGreen.bottom, Th.candyGreen.outline, edge);
        const RectF gloss = Deflate(RectF(part.X, part.Y, part.Width, part.Height * 0.5f), edge + SF(1.0f));
        if (gloss.Width > 4 && gloss.Height > 2) {
            GraphicsPath path;
            RoundRectPath(path, gloss, gloss.Height / 2);
            LinearGradientBrush brush(gloss, Th.glossTop, Th.glossBottom, LinearGradientModeVertical);
            g.FillPath(&brush, &path);
        }
    }
}

// ---- page furniture ----
//
// Shapes the pages share: a recessed slot, the gold section tag, the ribbon banner, key
// caps, mark badges, attached tabs and an info card. All are drawn inside the rect they are
// given, so the rect a layout struct holds is also the rect a hit test uses.

REAL TextWidth(Graphics& g, const std::wstring& text, Font* font) {
    RectF bounds;
    g.MeasureString(text.c_str(), static_cast<INT>(text.size()), font, PointF(0, 0), &bounds);
    return bounds.Width;
}

// A recessed field: a lighter inset a setting or a file row sits in, with a dark line along
// its inside top edge so it reads as sunk into the card.
void Slot(Graphics& g, const RectF& r, REAL radius, bool hot = false) {
    FillRound(g, r, radius, hot ? Th.rowHotFill : Th.slotFill);
    StrokeRound(g, Deflate(r, SF(0.75f)), radius, hot ? Th.rowHotBorder : Th.rowBorder, std::max(1.5f, SF(1.5f)));
    Pen shade(Th.slotShade, std::max(1.0f, SF(1.5f)));
    shade.SetStartCap(LineCapRound);
    shade.SetEndCap(LineCapRound);
    g.DrawLine(&shade, r.X + radius, r.Y + SF(2.5f), r.X + r.Width - radius, r.Y + SF(2.5f));
}

// The small gold tag that names a card ("DISPLAY"). Sits at the card's top left.
RectF SectionTag(Graphics& g, const RectF& card, const wchar_t* title) {
    const REAL h = SF(22);
    const REAL w = TextWidth(g, title, g_fontTiny) + SF(26);
    const RectF pill(card.X + SF(18), card.Y + SF(11), w, h);
    const RectF face = CandyBody(g, pill, h / 2, Th.candyGold, false, false, SF(3.0f));
    Text(g, title, g_fontTiny, Th.candyGold.text, face, StringAlignmentCenter);
    return pill;
}

// A ribbon with folded tails, the page title: heavy outlined letters on a gold band.
// (x, y) is the top left of the left tail. Returns the whole ribbon's rect.
RectF Banner(Graphics& g, const std::wstring& text, REAL x, REAL y, REAL height) {
    const REAL size = height * 0.56f;
    const REAL tail = SF(20);
    const REAL bodyWidth = HeavyWidth(text, size) + SF(48);
    const RectF body(x + tail, y, bodyWidth, height);
    const REAL edge = Edge();
    for (int side = 0; side < 2; ++side) {
        const REAL tw = tail + SF(10);
        const REAL tx = side == 0 ? body.X - tail : body.X + body.Width - SF(10);
        const REAL ty = y + height * 0.22f;
        const REAL th = height * 0.80f;
        PointF points[5];
        if (side == 0) {
            points[0] = PointF(tx, ty); points[1] = PointF(tx + tw, ty); points[2] = PointF(tx + tw, ty + th);
            points[3] = PointF(tx, ty + th); points[4] = PointF(tx + SF(10), ty + th / 2);
        } else {
            points[0] = PointF(tx, ty); points[1] = PointF(tx + tw, ty); points[2] = PointF(tx + tw - SF(10), ty + th / 2);
            points[3] = PointF(tx + tw, ty + th); points[4] = PointF(tx, ty + th);
        }
        SolidBrush fold(Th.ribbonFold);
        g.FillPolygon(&fold, points, 5);
        Pen pen(Th.candyGold.outline, edge);
        pen.SetLineJoin(LineJoinRound);
        g.DrawPolygon(&pen, points, 5);
    }
    const RectF face = CandyBody(g, body, SF(10), Th.candyGold, false, false, SF(4.0f));
    OutlinedText(g, text, g_familyHeavy, size, g_heavyStyle, Th.titleFill, Th.titleOutlineCard, face,
                 StringAlignmentCenter, SF(1.0f));
    return RectF(x, y, bodyWidth + 2 * tail, height);
}

// A key cap: the chunky pill that shows a binding or a value.
enum class Cap { Plain, On, Off, None, Listening };

REAL CapWidth(Graphics& g, const std::wstring& text) {
    return std::max(SF(34.0f), TextWidth(g, text, g_fontTiny) + SF(16.0f));
}

void KeyCap(Graphics& g, const RectF& r, const std::wstring& text, Cap style) {
    const Candy& candy = style == Cap::On ? Th.candyGreen
                         : style == Cap::Off ? Th.candyGrey
                         : style == Cap::None ? Th.candyKeyNone
                         : style == Cap::Listening ? Th.candyKeyListen
                                                   : Th.candyKey;
    const RectF face = CandyBody(g, r, std::min(SF(7.0f), r.Height / 2), candy, false, false, SF(2.5f));
    Text(g, text, g_fontTiny, candy.text, face, StringAlignmentCenter);
}

// A round badge with a drawn glyph: a tick, an exclamation mark, a cross, three dots or
// nothing but the plain disc.
enum class Mark { Tick, Bang, Cross, Dots, Plain };

void MarkBadge(Graphics& g, const RectF& r, Mark mark) {
    const Candy& candy = mark == Mark::Tick ? Th.candyGreen
                         : mark == Mark::Bang ? Th.candyGold
                         : mark == Mark::Cross ? Th.candyRed
                         : mark == Mark::Dots ? Th.candyGold
                                              : Th.candySand;
    const RectF face = CandyBody(g, r, r.Width / 2, candy, false, false, SF(2.0f));
    const REAL cx = face.X + face.Width / 2, cy = face.Y + face.Height / 2;
    const REAL u = r.Width / 18.0f;
    const Color ink = (mark == Mark::Tick || mark == Mark::Cross) ? Th.btnLabel : candy.text;
    Pen pen(ink, std::max(1.8f, 2.2f * u));
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    switch (mark) {
    case Mark::Tick: {
        const PointF p[3] = {PointF(cx - 4.5f * u, cy + 0.2f * u), PointF(cx - 1.2f * u, cy + 3.6f * u),
                             PointF(cx + 4.6f * u, cy - 3.6f * u)};
        g.DrawLines(&pen, p, 3);
        break;
    }
    case Mark::Bang: {
        g.DrawLine(&pen, cx, cy - 4.2f * u, cx, cy + 0.8f * u);
        SolidBrush dot(ink);
        g.FillEllipse(&dot, cx - 1.3f * u, cy + 2.6f * u, 2.6f * u, 2.6f * u);
        break;
    }
    case Mark::Cross:
        g.DrawLine(&pen, cx - 3.4f * u, cy - 3.4f * u, cx + 3.4f * u, cy + 3.4f * u);
        g.DrawLine(&pen, cx + 3.4f * u, cy - 3.4f * u, cx - 3.4f * u, cy + 3.4f * u);
        break;
    case Mark::Dots: {
        SolidBrush dot(ink);
        for (int i = -1; i <= 1; ++i) g.FillEllipse(&dot, cx + i * 4.6f * u - 1.4f * u, cy - 1.4f * u, 2.8f * u, 2.8f * u);
        break;
    }
    case Mark::Plain: {
        SolidBrush dot(ink);
        g.FillEllipse(&dot, cx - 2.2f * u, cy - 2.2f * u, 4.4f * u, 4.4f * u);
        break;
    }
    }
}

// A folder tab sitting on the top edge of a card. Idle tabs are drawn before the card (the
// card covers their lower end); the selected tab is drawn after it and reaches over the
// card's outline, so it looks attached. r is the hit rect: the tab never leaves it.
void Tab(Graphics& g, const RectF& r, const wchar_t* label, bool selected, bool hot) {
    const REAL edge = Edge();
    const REAL radius = SF(11.0f);
    const REAL top = selected ? r.Y : r.Y + SF(5.0f);
    const REAL x0 = r.X + edge / 2, x1 = r.X + r.Width - edge / 2;
    const REAL y0 = top + edge / 2, y1 = r.Y + r.Height;
    GraphicsPath open;
    open.AddLine(x0, y1, x0, y0 + radius);
    open.AddArc(x0, y0, radius * 2, radius * 2, 180, 90);
    open.AddLine(x0 + radius, y0, x1 - radius, y0);
    open.AddArc(x1 - radius * 2, y0, radius * 2, radius * 2, 270, 90);
    open.AddLine(x1, y0 + radius, x1, y1);

    Color faceTop = selected ? Th.candyGold.top : Th.tabIdleTop;
    Color faceBottom = selected ? Th.candyGold.bottom : Th.tabIdleBottom;
    if (hot && !selected) {
        faceTop = Mix(faceTop, Th.mixLight, 0.25f);
        faceBottom = Mix(faceBottom, Th.mixLight, 0.25f);
    }
    LinearGradientBrush fill(RectF(x0, y0, x1 - x0, y1 - y0), faceTop, faceBottom, LinearGradientModeVertical);
    g.FillPath(&fill, &open);
    if (selected) {
        Pen gloss(Th.glossTop, std::max(1.5f, SF(1.5f)));
        gloss.SetStartCap(LineCapRound);
        gloss.SetEndCap(LineCapRound);
        g.DrawLine(&gloss, x0 + radius, y0 + edge + SF(1.0f), x1 - radius, y0 + edge + SF(1.0f));
    }
    Pen pen(selected ? Th.candyGold.outline : Th.panelOutline, edge);
    pen.SetLineJoin(LineJoinRound);
    g.DrawPath(&pen, &open);

    const RectF box(r.X, r.Y, r.Width, r.Height - SF(4.0f));
    if (selected) {
        OutlinedText(g, label, g_familyHeavy, g_fontButton->GetSize(), g_heavyStyle, Th.titleFill,
                     Th.candyGold.outline, box, StringAlignmentCenter, SF(1.0f));
    } else {
        Text(g, label, g_fontTiny, Th.tabIdleText, RectF(box.X, box.Y + SF(5.0f), box.Width, box.Height - SF(2.0f)),
             StringAlignmentCenter);
    }
}

// A short note panel: a gold "i" badge and wrapped lines of small text. The first line is
// the stronger one.
void InfoCard(Graphics& g, const RectF& r, const std::vector<std::wstring>& notes) {
    Slot(g, r, SF(10.0f));
    const RectF badge(r.X + SF(10), r.Y + SF(10), SF(24), SF(24));
    const RectF face = CandyBody(g, badge, SF(12), Th.candyGold, false, false, SF(2.0f));
    OutlinedText(g, L"i", g_familyHeavy, SF(15.0f), g_heavyStyle, Th.btnLabel, Th.candyGold.outline, face,
                 StringAlignmentCenter, 0.0f);
    const REAL x = r.X + SF(46), w = r.Width - SF(60);
    REAL y = r.Y + SF(10);
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsLineLimit);
    for (size_t i = 0; i < notes.size(); ++i) {
        RectF bounds;
        g.MeasureString(notes[i].c_str(), static_cast<INT>(notes[i].size()), g_fontSmall, RectF(0, 0, w, SF(400)),
                        &format, &bounds);
        Paragraph(g, notes[i], g_fontSmall, i == 0 ? Th.cardText : Th.cardTextDim, RectF(x, y, w, bounds.Height + SF(2)));
        y += bounds.Height + SF(6);
    }
}

// A Wii remote and a Nunchuk, side by side with the cable between them.
void DrawRemoteDiagram(Graphics& g, const RectF& area) {
    const REAL edge = Edge();
    const REAL cx = area.X + area.Width / 2;
    const RectF remote(cx - SF(78), area.Y + SF(4), SF(48), SF(136));
    const RectF nunchuk(cx + SF(26), area.Y + SF(26), SF(52), SF(92));
    const PointF cableA(remote.X + remote.Width / 2, remote.Y + remote.Height);
    const PointF cableB(nunchuk.X + nunchuk.Width / 2, nunchuk.Y + nunchuk.Height);

    Pen cable(Th.panelOutline, std::max(2.5f, SF(3.0f)));
    cable.SetStartCap(LineCapRound);
    cable.SetEndCap(LineCapRound);
    g.DrawBezier(&cable, cableA, PointF(cableA.X, cableA.Y + SF(18)), PointF(cableB.X, cableB.Y + SF(18)), cableB);

    SoftShadow(g, remote, SF(14), SF(3.0f));
    ChunkyRound(g, remote, SF(14), Th.deviceTop, Th.deviceBottom, Th.panelOutline, edge);
    SoftShadow(g, nunchuk, SF(20), SF(3.0f));
    ChunkyRound(g, nunchuk, SF(20), Th.deviceTop, Th.deviceBottom, Th.panelOutline, edge);

    const REAL rx = remote.X + remote.Width / 2;
    SolidBrush dark(Th.deviceButton);
    g.FillRectangle(&dark, rx - SF(3.5f), remote.Y + SF(12), SF(7), SF(20));
    g.FillRectangle(&dark, rx - SF(10), remote.Y + SF(18.5f), SF(20), SF(7));
    const auto disc = [&](REAL x, REAL y, REAL d, const Candy& candy) {
        ChunkyRound(g, RectF(x - d / 2, y - d / 2, d, d), d / 2, candy.top, candy.bottom, candy.outline, std::max(1.5f, SF(1.5f)));
    };
    disc(rx, remote.Y + SF(46), SF(17), Th.candyGreen);
    for (int i = -1; i <= 1; ++i) disc(rx + i * SF(12), remote.Y + SF(68), SF(9), Th.candyGrey);
    disc(rx, remote.Y + SF(88), SF(12), Th.candySand);
    disc(rx, remote.Y + SF(106), SF(12), Th.candySand);

    const REAL nx = nunchuk.X + nunchuk.Width / 2;
    disc(nx, nunchuk.Y + SF(22), SF(24), Th.candyGrey);
    disc(nx - SF(9), nunchuk.Y + SF(50), SF(14), Th.candyGold);
    ChunkyRound(g, RectF(nx - SF(14), nunchuk.Y + SF(62), SF(28), SF(12)), SF(6), Th.candyRed.top, Th.candyRed.bottom,
                Th.candyRed.outline, std::max(1.5f, SF(1.5f)));

    Text(g, L"Wii Remote", g_fontSmall, Th.cardTextDim, RectF(remote.X - SF(20), area.Y + SF(162), remote.Width + SF(40), SF(16)),
         StringAlignmentCenter);
    Text(g, L"Nunchuk", g_fontSmall, Th.cardTextDim, RectF(nunchuk.X - SF(20), area.Y + SF(162), nunchuk.Width + SF(40), SF(16)),
         StringAlignmentCenter);
}

// A mouse with its five buttons, each with a callout showing what it presses.
// values[]: left, right, middle, side 1, side 2.
void DrawMouseDiagram(Graphics& g, const RectF& area, const std::wstring* values) {
    const REAL edge = Edge();
    const REAL cx = area.X + area.Width / 2;
    const REAL mw = SF(104), mh = SF(160);
    const RectF body(cx - mw / 2, area.Y + std::max(SF(10), (area.Height - mh) / 2), mw, mh);
    SoftShadow(g, body, SF(46), SF(4.0f));
    ChunkyRound(g, body, SF(46), Th.deviceTop, Th.deviceBottom, Th.panelOutline, edge);

    // Left and right buttons are the top of the shell; the wheel sits between them.
    const REAL splitY = body.Y + SF(62);
    const RectF leftButton(body.X + edge, body.Y + edge, mw / 2 - edge - SF(2), splitY - body.Y - edge);
    const RectF rightButton(cx + SF(2), body.Y + edge, mw / 2 - edge - SF(2), splitY - body.Y - edge);
    const Candy& tone = Th.candyGold;
    ChunkyRound(g, leftButton, SF(40), tone.top, tone.bottom, tone.outline, std::max(1.5f, SF(1.5f)));
    ChunkyRound(g, rightButton, SF(40), Th.candyGreen.top, Th.candyGreen.bottom, Th.candyGreen.outline, std::max(1.5f, SF(1.5f)));
    const RectF wheel(cx - SF(7), body.Y + SF(14), SF(14), SF(34));
    ChunkyRound(g, wheel, SF(7), Th.candyRed.top, Th.candyRed.bottom, Th.candyRed.outline, std::max(1.5f, SF(1.5f)));
    const RectF side1(body.X - SF(5), body.Y + SF(74), SF(11), SF(22));
    const RectF side2(body.X - SF(5), body.Y + SF(100), SF(11), SF(22));
    ChunkyRound(g, side1, SF(4), Th.candySand.top, Th.candySand.bottom, Th.candySand.outline, std::max(1.5f, SF(1.5f)));
    ChunkyRound(g, side2, SF(4), Th.candySand.top, Th.candySand.bottom, Th.candySand.outline, std::max(1.5f, SF(1.5f)));

    struct Callout { const wchar_t* name; bool left; REAL y; PointF target; const std::wstring* value; };
    const Callout callouts[5] = {
        {L"Left button", true, body.Y + SF(6), PointF(leftButton.X + leftButton.Width / 2, leftButton.Y + SF(24)), &values[0]},
        {L"Side button 1 (back)", true, body.Y + SF(70), PointF(side1.X + side1.Width / 2, side1.Y + side1.Height / 2), &values[3]},
        {L"Side button 2 (forward)", true, body.Y + SF(110), PointF(side2.X + side2.Width / 2, side2.Y + side2.Height / 2), &values[4]},
        {L"Right button", false, body.Y + SF(6), PointF(rightButton.X + rightButton.Width / 2, rightButton.Y + SF(24)), &values[1]},
        {L"Middle button (wheel click)", false, body.Y + SF(54), PointF(wheel.X + wheel.Width, wheel.Y + wheel.Height / 2), &values[2]},
    };
    for (const Callout& c : callouts) {
        const REAL capW = CapWidth(g, *c.value);
        const REAL labelW = SF(150);
        const REAL boxW = std::max(capW, TextWidth(g, c.name, g_fontSmall)) ;
        (void)labelW;
        const REAL edgeX = c.left ? body.X - SF(48) : body.X + body.Width + SF(48);
        const REAL x = c.left ? edgeX - boxW : edgeX;
        const REAL midY = c.y + SF(19);
        Pen line(Th.panelOutline, std::max(1.5f, SF(1.5f)));
        line.SetStartCap(LineCapRound);
        line.SetEndCap(LineCapRound);
        g.DrawLine(&line, c.target.X, c.target.Y, edgeX + (c.left ? SF(6) : -SF(6)), midY);
        Text(g, c.name, g_fontSmall, Th.cardTextDim, RectF(x, c.y - SF(2), boxW + SF(4), SF(16)),
             c.left ? StringAlignmentFar : StringAlignmentNear);
        const std::wstring& shown = *c.value;
        const REAL capX = c.left ? x + boxW - capW : x;
        KeyCap(g, RectF(capX, c.y + SF(15), capW, SF(22)), shown, shown == L"None" ? Cap::None : Cap::Plain);
    }
}

// A game pad in outline, for the empty state of the input check.
void DrawPadIcon(Graphics& g, const RectF& area) {
    const REAL edge = Edge();
    const REAL x = area.X, y = area.Y, w = area.Width, h = area.Height;
    const RectF leftGrip(x + 0.04f * w, y + 0.30f * h, 0.34f * w, 0.66f * h);
    const RectF rightGrip(x + 0.62f * w, y + 0.30f * h, 0.34f * w, 0.66f * h);
    GraphicsPath body;
    RoundRectPath(body, RectF(x + 0.10f * w, y + 0.06f * h, 0.80f * w, 0.56f * h), 0.2f * h);

    Pen outline(Th.panelOutline, edge * 2);
    outline.SetLineJoin(LineJoinRound);
    g.DrawEllipse(&outline, leftGrip);
    g.DrawEllipse(&outline, rightGrip);
    g.DrawPath(&outline, &body);
    LinearGradientBrush fill(RectF(x, y, w, h), Th.deviceTop, Th.deviceBottom, LinearGradientModeVertical);
    g.FillEllipse(&fill, leftGrip);
    g.FillEllipse(&fill, rightGrip);
    g.FillPath(&fill, &body);

    SolidBrush dark(Th.deviceButton);
    const REAL px = x + 0.28f * w, py = y + 0.34f * h, arm = 0.045f * w;
    g.FillRectangle(&dark, px - arm / 2, py - 2.4f * arm, arm, 4.8f * arm);
    g.FillRectangle(&dark, px - 2.4f * arm, py - arm / 2, 4.8f * arm, arm);
    const REAL bx = x + 0.72f * w, by = y + 0.34f * h, off = 0.06f * w, d = 0.05f * w;
    const Candy* tones[4] = {&Th.candyGreen, &Th.candyRed, &Th.candyGold, &Th.candySand};
    const PointF spots[4] = {PointF(bx, by + off), PointF(bx + off, by), PointF(bx, by - off), PointF(bx - off, by)};
    for (int i = 0; i < 4; ++i) {
        ChunkyRound(g, RectF(spots[i].X - d / 2, spots[i].Y - d / 2, d, d), d / 2, tones[i]->top, tones[i]->bottom,
                    tones[i]->outline, std::max(1.2f, SF(1.2f)));
    }
    for (int i = 0; i < 2; ++i) {
        const REAL sx = x + (0.40f + 0.20f * i) * w, sy = y + 0.50f * h, sd = 0.10f * w;
        ChunkyRound(g, RectF(sx - sd / 2, sy - sd / 2, sd, sd), sd / 2, Th.wellTop, Th.wellBottom, Th.panelOutline,
                    std::max(1.2f, SF(1.2f)));
    }
}

// The three steps of the flow, the same shape as the reference launchers use:
// 1 Game files, 2 Build, 3 Play. A parchment pill holds three medallions; steps before the
// active one are ticked. 'right' is the pill's right edge, y its top.
void DrawSteps(Graphics& g, float right, float y, int active, int completed) {
    const wchar_t* labels[3] = {L"Game files", L"Build", L"Play"};
    const float item = SF(150);
    const float pillH = SF(38);
    const RectF pill(right - item * 3 - SF(22), y, item * 3 + SF(22), pillH);
    SoftShadow(g, pill, pillH / 2, SF(3.0f));
    ChunkyRound(g, pill, pillH / 2, Th.panelTop, Th.panelBottom, Th.panelOutline, std::max(2.5f, SF(2.5f)));
    const float start = pill.X + SF(14);
    for (int i = 0; i < 3; ++i) {
        const float x = start + item * i;
        const bool current = i == active;
        const bool done = i < completed;
        const RectF disc(x, y + (pillH - SF(26)) / 2, SF(26), SF(26));
        const REAL labelW = TextWidth(g, labels[i], g_fontBody);
        if (i < 2) {
            // The connector starts after the label, so it never strikes through it.
            const float x1 = x + SF(34) + labelW + SF(8);
            const float x2 = x + item - SF(2);
            if (x2 - x1 > SF(8)) {
                const Color a = i < completed ? Th.candyGreen.top : Th.trackTop;
                const Color b = i < completed ? Th.candyGreen.bottom : Th.trackBottom;
                const Color o = i < completed ? Th.candyGreen.outline : Th.panelOutline;
                ChunkyRound(g, RectF(x1, y + pillH / 2 - SF(3.5f), x2 - x1, SF(7)), SF(3.5f), a, b, o, std::max(1.5f, SF(1.5f)));
            }
        }
        const Candy& candy = current ? Th.candyGold : done ? Th.candyGreen : Th.candySand;
        if (done) {
            MarkBadge(g, disc, Mark::Tick);
        } else {
            const RectF face = CandyBody(g, disc, SF(13), candy, false, false, SF(2.5f));
            const wchar_t digit[2] = {static_cast<wchar_t>(L'1' + i), 0};
            OutlinedText(g, digit, g_familyHeavy, SF(14.0f), g_heavyStyle, Th.btnLabel, candy.outline, face,
                         StringAlignmentCenter, 0.0f);
        }
        Text(g, labels[i], g_fontBody, current ? Th.cardText : done ? Th.good : Th.cardTextDim,
             RectF(x + SF(34), y, labelW + SF(6), pillH));
    }
}

// ---- painting ----

// The header's state pill: a rounded capsule filled with the state's tone, a dot and a label.
void StatusPill(Graphics& g, const RectF& pill, const wchar_t* label, const Color& tone) {
    const REAL edge = Edge();
    SoftShadow(g, pill, pill.Height / 2, SF(2.0f));
    ChunkyRound(g, pill, pill.Height / 2, Mix(tone, Th.mixLight, 0.30f), tone, Th.panelOutline, edge);
    SolidBrush dot(Th.pillText);
    g.FillEllipse(&dot, pill.X + SF(13), pill.Y + pill.Height / 2 - SF(3), SF(6), SF(6));
    Text(g, label, g_fontTiny, Th.pillText, RectF(pill.X + SF(25), pill.Y, pill.Width - SF(25), pill.Height));
}

// An original crest: a shield with a sea-blue field, a gold rim, a white wave and a green
// leaf. Drawn from curves; it is not taken from any game or brand.
void Emblem(Graphics& g, const RectF& r) {
    const REAL x = r.X, y = r.Y, w = r.Width, h = r.Height, cx = r.X + r.Width / 2;
    GraphicsPath shield;
    shield.AddLine(x + 0.06f * w, y + 0.06f * h, x + 0.94f * w, y + 0.06f * h);
    shield.AddLine(x + 0.94f * w, y + 0.06f * h, x + 0.94f * w, y + 0.50f * h);
    shield.AddBezier(x + 0.94f * w, y + 0.50f * h, x + 0.94f * w, y + 0.80f * h, x + 0.68f * w, y + 0.92f * h, cx, y + h);
    shield.AddBezier(cx, y + h, x + 0.32f * w, y + 0.92f * h, x + 0.06f * w, y + 0.80f * h, x + 0.06f * w, y + 0.50f * h);
    shield.CloseFigure();

    SoftShadow(g, RectF(x + 0.06f * w, y + 0.06f * h, 0.88f * w, 0.80f * h), SF(6), SF(2.0f));
    LinearGradientBrush field(RectF(x, y, w, h), Th.emblemSeaTop, Th.emblemSeaBottom, LinearGradientModeVertical);
    g.FillPath(&field, &shield);

    GraphicsState state = g.Save();
    g.SetClip(&shield);
    // Wave: two sine bands across the lower half.
    for (int band = 0; band < 2; ++band) {
        GraphicsPath wave;
        const REAL base = y + (0.60f + 0.13f * band) * h;
        const REAL amp = 0.045f * h;
        PointF points[9];
        for (int i = 0; i < 9; ++i) {
            points[i] = PointF(x + w * (0.02f + 0.12f * i), base + ((i % 2) ? -amp : amp));
        }
        wave.AddCurve(points, 9, 0.5f);
        Pen pen(band == 0 ? Th.emblemWave : Th.emblemWaveSoft, std::max(1.6f, 0.07f * w));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawPath(&pen, &wave);
    }
    g.Restore(state);

    // Leaf above the waves, with a light vein.
    GraphicsPath leaf;
    leaf.AddBezier(cx - 0.14f * w, y + 0.52f * h, cx - 0.22f * w, y + 0.22f * h, cx + 0.10f * w, y + 0.12f * h, cx + 0.22f * w, y + 0.14f * h);
    leaf.AddBezier(cx + 0.22f * w, y + 0.14f * h, cx + 0.22f * w, y + 0.40f * h, cx + 0.02f * w, y + 0.54f * h, cx - 0.14f * w, y + 0.52f * h);
    leaf.CloseFigure();
    LinearGradientBrush leafFill(RectF(cx - 0.2f * w, y + 0.12f * h, 0.45f * w, 0.42f * h), Th.emblemLeafTop,
                                 Th.emblemLeafBottom, LinearGradientModeVertical);
    g.FillPath(&leafFill, &leaf);
    Pen leafEdge(Th.candyGreen.outline, std::max(1.2f, 0.035f * w));
    leafEdge.SetLineJoin(LineJoinRound);
    g.DrawPath(&leafEdge, &leaf);
    Pen vein(Th.emblemVein, std::max(1.0f, 0.025f * w));
    g.DrawLine(&vein, cx - 0.12f * w, y + 0.50f * h, cx + 0.18f * w, y + 0.17f * h);

    // Gold rim, then the dark outline.
    const REAL rim = std::max(2.5f, 0.07f * w);
    Pen gold(Th.cardTitleBar, rim);
    gold.SetLineJoin(LineJoinRound);
    gold.SetAlignment(PenAlignmentInset);
    g.DrawPath(&gold, &shield);
    Pen outline(Th.panelOutline, std::max(2.0f, 0.05f * w));
    outline.SetLineJoin(LineJoinRound);
    g.DrawPath(&outline, &shield);
}

void DrawHeader(Graphics& g, const Layout& l) {
    Emblem(g, RectF(SF(30), SF(14), SF(44), SF(52)));
    Text(g, L"SEA OF STAR", g_fontBrand, Th.brandText, RectF(SF(84), SF(14), SF(240), SF(20)));

    Title(g, L"Tides of Destiny", SF(static_cast<float>(Th.sizeH1)), RectF(SF(82), SF(32), SF(520), SF(42)), Th.titleOutlineBg);
    Text(g, L"Static recompilation  ·  native Windows x64  ·  disc SO3EE9", g_fontSmall, Th.bgTextDim,
         RectF(SF(84), SF(76), SF(560), SF(20)));

    const bool ready = Ready();
    const bool baked = ready && ImageCurrent();
    const wchar_t* label = g_running ? L"RUNNING" : g_building ? L"BUILDING"
                          : baked         ? L"PRE-BAKED"
                          : ready         ? L"READY"
                                          : L"SET UP";
    const Color tone = g_running || g_building ? Th.pillBusy : ready ? Th.pillOk : Th.pillBad;

    const float pillW = SF(112);
    const RectF pill(l.gear.X - SF(10) - pillW, l.quit.Y + SF(1), pillW, SF(28));
    StatusPill(g, pill, label, tone);

    Button(g, l.quit, L"Quit", false, g_hot == ID_QUIT, g_pressed == ID_QUIT, true, false, nullptr, Tone::Red);
    Gear(g, l.gear, g_hot == ID_SETTINGS, g_page != Page::Home);
}

// The runtime keeps the window the shape of the console's screen setting, and shrinks one
// that would not fit the desktop. The launcher shows the size that will actually be used
// rather than the one that was asked for.
void EffectiveWindow(int& width, int& height) {
    width = g_config.windowWidth;
    height = width * g_aspectHeight / g_aspectWidth;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int areaWidth = work.right - work.left;
    const int areaHeight = work.bottom - work.top - S(40);
    if (areaWidth <= 0 || areaHeight <= 0) return;
    if (width <= areaWidth && height <= areaHeight) return;
    int fittedHeight = areaHeight;
    int fittedWidth = fittedHeight * g_aspectWidth / g_aspectHeight;
    if (fittedWidth > areaWidth) {
        fittedWidth = areaWidth;
        fittedHeight = fittedWidth * g_aspectHeight / g_aspectWidth;
    }
    width = fittedWidth;
    height = fittedHeight;
}

void DrawHero(Graphics& g, const Layout& l) {
    Panel(g, l.hero, SF(18));

    // Warm light behind the Play button, and a few rings around it, kept inside the panel.
    GraphicsState state = g.Save();
    GraphicsPath card;
    RoundRectPath(card, Deflate(l.hero, std::max(3.0f, SF(3.0f))), SF(15));
    g.SetClip(&card);
    GraphicsPath glowPath;
    glowPath.AddEllipse(l.play.X - SF(150), l.play.Y - SF(150), l.play.Width + SF(300), l.play.Height + SF(300));
    PathGradientBrush glow(&glowPath);
    glow.SetCenterColor(Th.heroGlow);
    Color surround[] = {Th.heroGlowEdge};
    int count = 1;
    glow.SetSurroundColors(surround, &count);
    g.FillPath(&glow, &glowPath);

    Pen ring(Th.heroRing, std::max(1.5f, SF(1.5f)));
    for (int i = 0; i < 3; ++i) {
        const REAL inset = SF(static_cast<float>(26 + i * 34));
        g.DrawEllipse(&ring, l.play.X + l.play.Width / 2 - inset, l.play.Y + l.play.Height / 2 - inset, inset * 2,
                      inset * 2);
    }
    g.Restore(state);

    const bool ready = Ready();
    Text(g, g_running ? L"IN PLAY" : ready ? L"READY TO PLAY" : L"SET UP NEEDED", g_fontTiny,
         ready || g_running ? Th.eyebrowOk : Th.eyebrowWarn, RectF(l.hero.X + SF(28), l.hero.Y + SF(18), SF(300), SF(18)));
    Title(g, ready ? L"Native Windows x64" : L"Point at your own disc", SF(static_cast<float>(Th.sizeH2)),
          RectF(l.hero.X + SF(28), l.hero.Y + SF(40), SF(380), SF(34)), Th.titleOutlineCard);
    Text(g,
         ready ? L"Rune Factory: Tides of Destiny, rebuilt from your own disc."
               : L"Choose a packed image or an extracted folder, then build once.",
         g_fontBody, Th.cardTextDim, RectF(l.hero.X + SF(28), l.hero.Y + SF(80), l.hero.Width - SF(56), SF(22)));

    const int internalW = static_cast<int>(std::lround(640 * g_config.scale));
    const int internalH = static_cast<int>(std::lround(480 * g_config.scale));
    int windowW = 0, windowH = 0;
    EffectiveWindow(windowW, windowH);
    const wchar_t* present = g_config.present == L"mailbox" ? L"mailbox" : g_config.present == L"immediate" ? L"immediate" : L"vsync";
    wchar_t meta[256]{};
    swprintf_s(meta, L"%d × %d internal  ·  %d × %d window  ·  FSR %s  ·  %s",
               internalW, internalH, windowW, windowH,
               g_config.fsr ? L"1.0" : L"off", present);
    Text(g, meta, g_fontSmall, Th.cardTextMuted,
         RectF(l.hero.X + SF(28), l.hero.Y + l.hero.Height - SF(34), l.hero.Width - SF(56), SF(20)));

    const bool enabled = !g_running;
    Button(g, l.play, g_running ? L"Running" : ready ? L"Play" : L"Set up", true, g_hot == ID_PLAY,
           g_pressed == ID_PLAY, enabled, ready, g_fontPlay);
    if (ready) {
        Button(g, l.heroFiles, L"Game files", false, g_hot == ID_HERO_FILES, g_pressed == ID_HERO_FILES, !g_running,
               false, g_fontSmall);
    }

    // A stale image is not an error: Play still works from the loose files. The notice says
    // why the image is not used and offers the same rebuild as the Build page.
    // An unexpected exit takes the same slot and comes first; the stale-image notice returns
    // once the crash notice is dismissed, so the two never overlap.
    if (g_crash.pending) {
        const float textX = l.hero.X + SF(28);
        const float textW = l.crashOpen.X - SF(16) - textX - SF(14);
        Dot(g, textX, l.crashOpen.Y + SF(4), Th.noticeDotCrash);
        Paragraph(g, L"The last session ended unexpectedly (exit code " + HexCode(g_crash.code) + L").", g_fontSmall,
                  Th.cardTextDim, RectF(textX + SF(14), l.crashOpen.Y - SF(2), textW, SF(36)));
        Text(g, L"Report: logs\\crash\\" + g_crash.stamp, g_fontSmall, Th.cardTextMuted,
             RectF(textX + SF(14), l.crashOpen.Y + SF(36), textW, SF(18)));
        Button(g, l.crashOpen, L"Open report", false, g_hot == ID_CRASH_OPEN, g_pressed == ID_CRASH_OPEN, true, false,
               g_fontSmall);
        Button(g, l.crashDismiss, L"Dismiss", false, g_hot == ID_CRASH_DISMISS, g_pressed == ID_CRASH_DISMISS, true,
               false, g_fontSmall);
    } else if (ImageStale() && !g_building) {
        const float textX = l.hero.X + SF(28);
        const float textW = l.heroRebuild.X - SF(16) - textX - SF(14);
        Dot(g, textX, l.heroRebuild.Y + SF(4), Th.noticeDotStale);
        Paragraph(g, StaleReason(), g_fontSmall, Th.cardTextDim, RectF(textX + SF(14), l.heroRebuild.Y - SF(2), textW, SF(36)));
        if (g_setup.hasImageTime) {
            Text(g, L"Image built " + ShortTime(g_setup.imageTime) + L"  ·  shaders.bin: " +
                        std::to_wstring(g_setup.recipes) + L" recipes",
                 g_fontSmall, Th.cardTextMuted, RectF(textX + SF(14), l.heroRebuild.Y + SF(36), textW, SF(18)));
        }
        Button(g, l.heroRebuild, L"Rebuild", false, g_hot == ID_HERO_REBUILD, g_pressed == ID_HERO_REBUILD,
               !g_running, false, g_fontSmall);
    }
}

void DrawActions(Graphics& g, const Layout& l) {
    struct Action { int id; const RectF* rect; const wchar_t* label; };
    const Action actions[] = {
        {ID_GAMEFILES, &l.gameFiles, L"Game files"},
        {ID_REBUILD, &l.rebuild, L"Rebuild"},
        {ID_CONTROLS, &l.controls, L"Controls"},
        {ID_SAVES, &l.saves, L"Saves"},
        {ID_LOGS, &l.logs, L"Logs"},
        {ID_CONFIG, &l.config, L"Open config"},
        {ID_PLAY_LOG, &l.playLog, L"Last log"},
    };
    for (const Action& a : actions) {
        bool enabled = a.id != ID_REBUILD || (!g_running && !g_building);
        if (a.id == ID_PLAY_LOG) enabled = !NewestPlayLog().empty();
        Button(g, *a.rect, a.label, false, g_hot == a.id, g_pressed == a.id, enabled);
    }
}

void DrawDevices(Graphics& g, const Layout& l) {
    Panel(g, l.devices, SF(16));

    Text(g, L"CONTROLLERS", g_fontTiny, Th.cardTextMuted,
         RectF(l.devices.X + SF(20), l.devices.Y + SF(14), SF(130), SF(20)));
    Button(g, l.testInput, L"Test input", false, g_hot == ID_INPUT_TEST, g_pressed == ID_INPUT_TEST, true, false, g_fontTiny);
    Button(g, l.rescan, L"Rescan", false, g_hot == ID_RESCAN, g_pressed == ID_RESCAN, true, false, g_fontTiny);

    const float left = l.devices.X + SF(20);
    const float width = l.devices.Width - SF(40);
    float y = l.devices.Y + SF(44);

    if (!g_padsTried) {
        Text(g, L"Looking for controllers…", g_fontBody, Th.cardTextDim, RectF(left, y, width, SF(20)));
    } else if (g_padsError) {
        Text(g, L"SDL3 was not found", g_fontBody, Th.cardText, RectF(left, y, width, SF(20)));
        Text(g, L"Put SDL3.dll beside the console or in the MSYS2 folder.", g_fontSmall, Th.cardTextMuted,
             RectF(left, y + SF(20), width, SF(18)), StringAlignmentNear, true);
    } else if (g_pads.empty()) {
        Text(g, L"No gamepad detected", g_fontBody, Th.cardText, RectF(left, y, width, SF(20)));
        Text(g, L"Connect one and press Rescan.", g_fontSmall, Th.cardTextMuted,
             RectF(left, y + SF(20), width, SF(16)), StringAlignmentNear, true);
        Text(g, L"Keyboard and mouse always work.", g_fontSmall, Th.cardTextMuted,
             RectF(left, y + SF(37), width, SF(16)), StringAlignmentNear, true);
    } else {
        for (size_t i = 0; i < g_pads.size() && i < 2; ++i) {
            const bool low = g_pads[i].battery >= 0 && g_pads[i].battery < 20;
            SolidBrush dot(low ? Th.deviceLow : Th.deviceOk);
            g.FillEllipse(&dot, left, y + SF(6), SF(6), SF(6));
            Text(g, g_pads[i].name, g_fontBody, Th.cardText, RectF(left + SF(14), y, width - SF(14), SF(20)),
                 StringAlignmentNear, true);
            Text(g, g_pads[i].detail, g_fontSmall, Th.cardTextMuted, RectF(left + SF(14), y + SF(19), width - SF(14), SF(16)),
                 StringAlignmentNear, true);
            y += SF(38);
        }
        if (g_pads.size() > 2) {
            Text(g, L"+" + std::to_wstring(g_pads.size() - 2) + L" more", g_fontSmall, Th.cardTextMuted,
                 RectF(left, y - SF(6), width, SF(16)));
        }
    }

    const float mapY = l.devices.Y + l.devices.Height - SF(46);
    Text(g, L"Pad    left stick · A/B · L/ZL · D-pad", g_fontSmall, Th.cardTextDim,
         RectF(left, mapY, width, SF(16)), StringAlignmentNear, true);
    Text(g, L"Keys   WASD · Enter A · Backspace B · mouse", g_fontSmall, Th.cardTextMuted,
         RectF(left, mapY + SF(18), width, SF(16)), StringAlignmentNear, true);
}

void CardTitle(Graphics& g, const RectF& card, const wchar_t* title) {
    SectionTag(g, card, title);
}

// The page title ribbon and the line of help under it, shared by the pages after Home.
void PageTitle(Graphics& g, const wchar_t* title, const wchar_t* help) {
    Banner(g, title, SF(32), SF(108), SF(38));
    Text(g, help, g_fontSmall, Th.bgTextDim, RectF(SF(34), SF(153), SF(560), SF(18)));
}

void DrawSettings(Graphics& g, const Layout& l) {
    PageTitle(g, L"Settings", L"Display, graphics and in-game options. Saved to config\\launcher.ini.");

    const float pad = SF(14);
    const auto label = [&](const RectF& slot, const wchar_t* text, const wchar_t* note) {
        Text(g, text, g_fontBody, Th.cardText, RectF(slot.X + pad, slot.Y + SF(5), SF(240), SF(20)));
        if (note) Text(g, note, g_fontSmall, Th.cardTextMuted, RectF(slot.X + pad, slot.Y + SF(25), SF(300), SF(16)));
    };

    // ---- Display ----
    Panel(g, l.settings, SF(16));
    CardTitle(g, l.settings, L"DISPLAY");
    for (const RectF& slot : l.slotDisplay) Slot(g, slot, SF(10));

    label(l.slotDisplay[0], L"Fullscreen", L"Start on the whole monitor");
    Switch(g, l.fullscreenSwitch, g_config.fullscreen, g_hot == ID_FULLSCREEN, true);

    int shownWidth = 0, shownHeight = 0;
    EffectiveWindow(shownWidth, shownHeight);
    wchar_t sizeText[64]{};
    swprintf_s(sizeText, L"%d × %d · %d:%d", shownWidth, shownHeight, g_aspectWidth, g_aspectHeight);
    const RectF& sizeSlot = l.slotDisplay[1];
    label(sizeSlot, L"Window size", nullptr);
    Text(g, sizeText, g_fontSmall, Th.cardTextMuted,
         RectF(sizeSlot.X + sizeSlot.Width - pad - SF(220), sizeSlot.Y + SF(6), SF(220), SF(18)), StringAlignmentFar);
    const wchar_t* sizeLabels[4] = {L"720p", L"1080p", L"1440p", L"2160p"};
    const int heights[4] = {720, 1080, 1440, 2160};
    for (int i = 0; i < 4; ++i) {
        const bool selected = g_config.windowHeight == heights[i];
        SegmentPill(g, l.size[i], sizeLabels[i], selected, g_hot == ID_SIZE_720 + i, true);
    }

    label(l.slotDisplay[2], L"Present mode", nullptr);
    const wchar_t* presentLabels[3] = {L"VSync", L"Mailbox", L"Immediate"};
    for (int i = 0; i < 3; ++i) {
        const bool selected = (i == 0 && g_config.present == L"fifo") ||
                              (i == 1 && g_config.present == L"mailbox") ||
                              (i == 2 && g_config.present == L"immediate");
        SegmentPill(g, l.present[i], presentLabels[i], selected, g_hot == ID_PRESENT_FIFO + i, true);
    }
    Text(g, L"Shape follows the console's screen setting (IPL.AR).", g_fontSmall, Th.cardTextMuted,
         RectF(l.settings.X + SF(24), l.settings.Y + SF(252), l.settings.Width - SF(48), SF(16)), StringAlignmentNear, true);

    // ---- Graphics ----
    Panel(g, l.graphics, SF(16));
    CardTitle(g, l.graphics, L"GRAPHICS");
    for (const RectF& slot : l.slotGraphics) Slot(g, slot, SF(10));

    label(l.slotGraphics[0], L"Render scale", nullptr);
    const wchar_t* scaleLabels[4] = {L"1×", L"1.5×", L"2×", L"3×"};
    const double scaleValues[4] = {1.0, 1.5, 2.0, 3.0};
    for (int i = 0; i < 4; ++i) {
        const bool selected = std::fabs(g_config.scale - scaleValues[i]) < 0.001;
        SegmentPill(g, l.scale[i], scaleLabels[i], selected, g_hot == ID_SCALE_1 + i, true);
    }

    label(l.slotGraphics[1], L"FSR 1.0 upscaling", L"AMD EASU + RCAS on the upscale");
    Switch(g, l.fsrSwitch, g_config.fsr, g_hot == ID_FSR, true);

    const RectF& sharpSlot = l.slotGraphics[2];
    label(sharpSlot, L"Sharpness", nullptr);
    Text(g, FormatDouble(g_config.sharpness, L"%.2f"), g_fontSmall, g_config.fsr ? Th.cardTextDim : Th.cardTextMuted,
         RectF(sharpSlot.X + sharpSlot.Width - pad - SF(70), sharpSlot.Y + SF(6), SF(70), SF(20)), StringAlignmentFar);
    Text(g, g_config.fsr ? L"RCAS strength, 0 is sharpest" : L"Turn FSR on to apply it", g_fontSmall, Th.cardTextMuted,
         RectF(sharpSlot.X + pad, sharpSlot.Y + SF(25), SF(260), SF(16)));
    Slider(g, l.sharpnessTrack, g_config.sharpness, g_config.fsr);

    Text(g, L"Real upscaling in the runtime's Vulkan present pass.", g_fontSmall, Th.cardTextMuted,
         RectF(l.graphics.X + SF(24), l.graphics.Y + SF(252), l.graphics.Width - SF(48), SF(16)), StringAlignmentNear, true);

    // ---- In game ----
    Panel(g, l.ingame, SF(16));
    CardTitle(g, l.ingame, L"IN-GAME");
    Slot(g, l.slotIngame, SF(10));
    label(l.slotIngame, L"Frame rate overlay", L"FPS in the window's top right corner");
    Switch(g, l.overlaySwitch, g_config.fpsOverlay, g_hot == ID_OVERLAY, true);
    Button(g, l.testInput, L"Test input", false, g_hot == ID_INPUT_TEST, g_pressed == ID_INPUT_TEST, true, false,
           g_fontSmall);
    Button(g, l.controls, L"Controls", false, g_hot == ID_CONTROLS, g_pressed == ID_CONTROLS, true, false,
           g_fontSmall);
    Text(g, L"Saved to config\\launcher.ini  ·  changes apply the next time you start the game.", g_fontSmall,
         Th.cardTextMuted, RectF(l.ingame.X + SF(24), l.ingame.Y + SF(92), l.ingame.Width - SF(48), SF(16)));

    Button(g, l.back, L"Back", false, g_hot == ID_BACK, g_pressed == ID_BACK, true, false, g_fontSmall);
    Button(g, l.done, L"Done", true, g_hot == ID_DONE, g_pressed == ID_DONE, true, false, g_fontButton);
}

void DrawBuildSummary(Graphics& g, const Layout& l) {
    Panel(g, l.settings, SF(16));
    Text(g, L"BUILD", g_fontTiny, Th.cardTextMuted,
         RectF(l.settings.X + SF(24), l.settings.Y + SF(16), SF(120), SF(16)));
    Button(g, l.settingsButton, L"Settings", false, g_hot == ID_CARD_SETTINGS, g_pressed == ID_CARD_SETTINGS, true, false,
           g_fontTiny);

    const bool hasImage = Exists(g_config.image);
    const bool current = ImageCurrent();
    unsigned long long imageBytes = 0;
    const bool imageSized = hasImage && FileSizeOf(g_config.image, imageBytes);
    const unsigned recipes = RecipeCount();
    const unsigned profile = ProfileCount();

    wchar_t display[160]{};
    int windowW = 0, windowH = 0;
    EffectiveWindow(windowW, windowH);
    const wchar_t* presentLabel = g_config.present == L"mailbox" ? L"mailbox"
                                   : g_config.present == L"immediate" ? L"immediate"
                                                                     : L"vsync";
    swprintf_s(display, L"%d × %d  ·  scale %.4g×  ·  FSR %s  ·  %s", windowW, windowH, g_config.scale,
               g_config.fsr ? L"1.0" : L"off", presentLabel);

    struct Row { const wchar_t* label; std::wstring value; std::wstring note; Color tone; };
    std::vector<Row> rows;
    rows.push_back({L"Packed image",
                    hasImage ? (current ? L"current" : L"stale") : L"missing",
                    hasImage ? (imageSized ? FormatBytes(imageBytes) + L"  ·  disc, code and recipes"
                                           : L"disc, code and recipes")
                             : L"Set a .wrd image on Game files.",
                    current ? Th.good : hasImage ? Th.warn : Th.bad});
    rows.push_back({L"Shader recipes",
                    recipes ? std::to_wstring(recipes) + L" prepared" : L"none yet",
                    profile ? std::to_wstring(profile) + L" functions in the profile"
                            : L"Recipes replay at boot; new scenes add more.",
                    recipes ? Th.good : Th.cardTextMuted});
    rows.push_back({L"Display", display, L"Window shape follows the console's screen setting.", Th.cardTextDim});
    rows.push_back({L"Frame rate", L"30 fps", L"Fixed timestep, frame-coupled; no 60 fps switch.",
                    Th.warn});

    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = l.rowTop[i];
        const Row& row = rows[i];
        if (i > 0) {
            Pen line(Th.rowDivider, 1.0f);
            g.DrawLine(&line, l.settings.X + SF(24), y - SF(6), l.settings.X + l.settings.Width - SF(24),
                       y - SF(6));
        }
        SolidBrush dot(row.tone);
        g.FillEllipse(&dot, l.settings.X + SF(24), y + SF(7), SF(7), SF(7));
        Text(g, row.label, g_fontBody, Th.cardText,
             RectF(l.settings.X + SF(38), y, SF(180), SF(20)));
        Text(g, row.value, g_fontSmall, row.tone,
             RectF(l.settings.X + SF(220), y, l.settings.Width - SF(244), SF(20)), StringAlignmentFar, true);
        Text(g, row.note, g_fontSmall, Th.cardTextMuted,
             RectF(l.settings.X + SF(38), y + SF(18), l.settings.Width - SF(62), SF(16)), StringAlignmentNear, true);
    }
}

void DrawGameFiles(Graphics& g, const Layout& l) {
    PageTitle(g, L"Game files", L"The build reads these paths. A current image is enough to play on its own.");
    Panel(g, l.settings, SF(16));
    CardTitle(g, l.settings, L"YOUR FILES");

    const bool hasImage = Exists(g_config.image);
    const bool current = ImageCurrent();
    const bool hasDisc = Exists(Join(g_config.disc, L"main.dol"));
    const bool hasTitle = Exists(g_config.title);
    const bool hasConsole = Exists(g_config.console);

    unsigned long long bytes = 0;
    std::wstring imageNote;
    if (hasImage && FileSizeOf(g_config.image, bytes))
        imageNote = FormatBytes(bytes) + (current ? L"  ·  current, play uses it" : L"  ·  stale, rebuild to refresh");
    else imageNote = hasImage ? L"current" : L"missing  ·  choose a .wrd or build one";

    std::wstring titleNote = hasTitle ? L"built" : current ? L"carried by the packed image" : L"missing  ·  rebuild";
    if (hasTitle && FileSizeOf(g_config.title, bytes)) titleNote = FormatBytes(bytes);

    struct Row {
        const wchar_t* label;
        std::wstring path;
        std::wstring note;
        Color tone;
    };
    const Row rows[4] = {
        {L"Packed image", g_config.image, imageNote,
         current ? Th.good : hasImage ? Th.warn : Th.bad},
        {L"Extracted disc", g_config.disc,
         hasDisc ? L"ready  ·  sys\\main.dol found" : L"missing  ·  extract your own disc first",
         hasDisc ? Th.good : Th.bad},
        {L"Native library", g_config.title, titleNote,
         hasTitle ? Th.good : current ? Th.warn : Th.bad},
        {L"Game console", g_config.console, hasConsole ? L"found" : L"missing  ·  run tools\\build.ps1",
         hasConsole ? Th.good : Th.bad},
    };
    const float pathRights[4] = {l.openImage.X, l.openDisc.X, l.openTitle.X, l.openConsole.X};

    for (int i = 0; i < 4; ++i) {
        const RectF& slot = l.slotFiles[i];
        Slot(g, slot, SF(10));
        const Color& tone = rows[i].tone;
        const Mark mark = tone.GetValue() == Th.good.GetValue() ? Mark::Tick
                          : tone.GetValue() == Th.warn.GetValue() ? Mark::Bang
                                                                  : Mark::Cross;
        MarkBadge(g, RectF(slot.X + SF(12), slot.Y + SF(8), SF(20), SF(20)), mark);
        Text(g, rows[i].label, g_fontBody, Th.cardText, RectF(slot.X + SF(42), slot.Y + SF(7), SF(220), SF(20)));

        const float pathLeft = slot.X + SF(42);
        const RectF field(pathLeft - SF(4), slot.Y + SF(28), std::max(SF(120), pathRights[i] - pathLeft - SF(10)), SF(20));
        FillRound(g, field, SF(6), Th.pathFill);
        StrokeRound(g, field, SF(6), Th.rowBorder, 1.0f);
        Text(g, rows[i].path, g_fontSmall, Th.cardTextDim, RectF(field.X + SF(6), field.Y, field.Width - SF(12), field.Height),
             StringAlignmentNear, true);
        Text(g, rows[i].note, g_fontTiny, tone, RectF(pathLeft, slot.Y + SF(50), slot.Width - SF(66), SF(14)),
             StringAlignmentNear, true);
    }

    Button(g, l.useImage, L"Use image…", false, g_hot == ID_USE_IMAGE, g_pressed == ID_USE_IMAGE, true, false,
           g_fontSmall);
    Button(g, l.openImage, L"Folder", false, g_hot == ID_OPEN_IMAGE, g_pressed == ID_OPEN_IMAGE, hasImage, false,
           g_fontTiny);
    Button(g, l.useFolder, L"Use folder…", false, g_hot == ID_USE_FOLDER, g_pressed == ID_USE_FOLDER, true, false,
           g_fontSmall);
    Button(g, l.openDisc, L"Folder", false, g_hot == ID_OPEN_DISC, g_pressed == ID_OPEN_DISC, hasDisc, false,
           g_fontTiny);
    Button(g, l.openTitle, L"Folder", false, g_hot == ID_OPEN_TITLE, g_pressed == ID_OPEN_TITLE, hasTitle, false,
           g_fontTiny);
    Button(g, l.useConsole, L"Use exe…", false, g_hot == ID_USE_CONSOLE, g_pressed == ID_USE_CONSOLE, true, false,
           g_fontSmall);
    Button(g, l.openConsole, L"Folder", false, g_hot == ID_OPEN_CONSOLE, g_pressed == ID_OPEN_CONSOLE, hasConsole,
           false, g_fontTiny);

    Button(g, l.back, L"Back", false, g_hot == ID_BACK, g_pressed == ID_BACK, true, false, g_fontSmall);
    Button(g, l.rebuild, g_building ? L"Building…" : L"Rebuild", false, g_hot == ID_REBUILD,
           g_pressed == ID_REBUILD, !g_running && !g_building, false, g_fontButton);
    Button(g, l.play, L"Play", true, g_hot == ID_PLAY, g_pressed == ID_PLAY,
           Ready() && !g_running && !g_building, true, g_fontPlay);
}

void DrawBuild(Graphics& g, const Layout& l) {
    PageTitle(g, L"Build", L"Progress and output of the rebuild.");
    Panel(g, l.settings, SF(16));
    CardTitle(g, l.settings, L"REBUILD");

    const wchar_t* stage = g_building ? g_buildStage.c_str()
                           : g_buildExitCode == 0 ? L"Done"
                           : g_buildExitCode > 0 ? L"Stopped with an error"
                                                 : L"Not started";
    const Mark mark = g_building ? Mark::Dots : g_buildExitCode == 0 ? Mark::Tick
                      : g_buildExitCode > 0 ? Mark::Cross : Mark::Plain;
    Slot(g, l.buildStage, SF(12));
    MarkBadge(g, RectF(l.buildStage.X + SF(10), l.buildStage.Y + (l.buildStage.Height - SF(30)) / 2, SF(30), SF(30)), mark);
    {
        // The stage text comes from the build log, so keep a long one clear of the percent badge.
        const RectF box(l.buildStage.X + SF(52), l.buildStage.Y + SF(8), l.buildStage.Width - SF(52) - SF(96), SF(32));
        GraphicsState keep = g.Save();
        g.SetClip(box);
        Title(g, stage, SF(static_cast<float>(Th.sizeH2)), box, Th.titleOutlineCard);
        g.Restore(keep);
    }
    if (g_building || g_buildExitCode >= 0) {
        wchar_t percent[16]{};
        swprintf_s(percent, L"%d%%", g_buildPercent);
        KeyCap(g, RectF(l.buildStage.X + l.buildStage.Width - SF(14) - SF(64), l.buildStage.Y + SF(12), SF(64), SF(24)),
               percent, g_building ? Cap::Listening : Cap::Plain);
    }

    Progress(g, l.buildBar, g_buildPercent);

    // The console: a dark panel with a title strip, so the log keeps its terminal look.
    const RectF& panel = l.buildConsole;
    ChunkyRound(g, panel, SF(10), Th.consoleFill, Th.consoleFill, Th.consoleBorder, Edge());
    GraphicsState state = g.Save();
    GraphicsPath panelClip;
    RoundRectPath(panelClip, Deflate(panel, Edge()), SF(8));
    g.SetClip(&panelClip);
    const float stripH = SF(22);
    SolidBrush strip(Th.consoleBar);
    g.FillRectangle(&strip, panel.X, panel.Y, panel.Width, stripH + Edge());
    const Color dots[3] = {Th.candyRed.top, Th.candyGold.top, Th.candyGreen.top};
    for (int i = 0; i < 3; ++i) {
        SolidBrush dotBrush(dots[i]);
        g.FillEllipse(&dotBrush, panel.X + SF(12) + i * SF(14), panel.Y + SF(8), SF(8), SF(8));
    }
    Text(g, L"build output", g_fontTiny, Th.logText, RectF(panel.X + SF(60), panel.Y + SF(3), SF(200), SF(18)));

    const float lineHeight = SF(17);
    const float bodyTop = panel.Y + stripH + SF(8);
    const int capacity = static_cast<int>((panel.Y + panel.Height - SF(8) - bodyTop) / lineHeight);
    const int first = std::max(0, static_cast<int>(g_buildLines.size()) - capacity);
    float y = bodyTop;
    for (int i = first; i < static_cast<int>(g_buildLines.size()); ++i) {
        const std::wstring& line = g_buildLines[static_cast<size_t>(i)];
        Color tone = Th.logText;
        if (line.rfind(L"== ", 0) == 0) tone = Th.logHeader;
        else if (line.find(L"[fail]") != std::wstring::npos || line.find(L"failed") != std::wstring::npos)
            tone = Th.logFail;
        else if (line.find(L"[ ok ]") != std::wstring::npos) tone = Th.logOk;
        else if (line.find(L"[warn]") != std::wstring::npos) tone = Th.logWarn;
        Text(g, line, g_fontMono, tone, RectF(panel.X + SF(12), y, panel.Width - SF(24), lineHeight));
        y += lineHeight;
    }
    if (g_buildLines.empty()) {
        Text(g, g_building ? L"Waiting for the first line…" : L"The build's output appears here.",
             g_fontSmall, Th.logText, RectF(panel.X + SF(12), bodyTop, panel.Width - SF(24), SF(18)));
    }
    g.Restore(state);

    const std::wstring hint = g_building
        ? L"You can go back; the build keeps running and the header pill follows it."
        : g_buildExitCode == 0
            ? L"Image refreshed. Play now; play further, then rebuild to add the recipes new scenes drew."
            : g_buildExitCode > 0 ? L"Open the log for the error; the old image was left in place."
                                  : L"Rebuild translates the disc, compiles the title and packs the image.";
    Text(g, hint, g_fontSmall, Th.cardTextDim,
         RectF(l.settings.X + SF(24), l.settings.Y + SF(334), l.settings.Width - SF(48), SF(16)), StringAlignmentNear,
         true);
    const std::wstring logPath =
        g_buildLogPath.empty() ? Join(Join(g_root, L"logs"), L"launcher-build.log") : g_buildLogPath;
    Text(g, L"Log: " + logPath, g_fontTiny, Th.cardTextMuted,
         RectF(l.settings.X + SF(24), l.settings.Y + SF(356), l.settings.Width - SF(48), SF(14)),
         StringAlignmentNear, true);

    Button(g, l.back, L"Back", false, g_hot == ID_BACK, g_pressed == ID_BACK, true, false, g_fontSmall);
    if (g_building) {
        Button(g, l.buildHide, L"Keep building", false, g_hot == ID_BUILD_HIDE, g_pressed == ID_BUILD_HIDE, true,
               false, g_fontSmall);
    }
    Button(g, l.buildLog, L"Open log", false, g_hot == ID_BUILD_LOG, g_pressed == ID_BUILD_LOG, Exists(logPath),
           false, g_fontSmall);
    Button(g, l.buildPlay, L"Play", true, g_hot == ID_PLAY, g_pressed == ID_PLAY,
           Ready() && !g_running && !g_building, true, g_fontPlay);
}

void DrawFooter(Graphics& g, float y) {
    Pen line(Th.footerLine, 1.0f);
    g.DrawLine(&line, SF(32), y, SF(32) + (SF(1000) - SF(64)), y);

    Text(g, g_status, g_fontSmall, Th.bgTextDim, RectF(SF(32), y + SF(10), SF(660), SF(20)), StringAlignmentNear,
         true);
    Text(g, L"config\\launcher.ini  ·  local build only", g_fontSmall, Th.bgTextMuted,
         RectF(SF(1000) - SF(352), y + SF(10), SF(320), SF(20)), StringAlignmentFar);
}

// A rolling hill: a smooth curve through the points, filled down to the bottom edge, with a
// darker line along its crest.
void Hill(Graphics& g, const PointF* points, int count, REAL bottom, const Color& fill) {
    GraphicsPath body;
    body.AddCurve(points, count, 0.5f);
    body.AddLine(points[count - 1].X, bottom, points[0].X, bottom);
    body.CloseFigure();
    SolidBrush brush(fill);
    g.FillPath(&brush, &body);
    GraphicsPath crest;
    crest.AddCurve(points, count, 0.5f);
    Pen line(Th.hillLine, std::max(2.0f, SF(2.0f)));
    line.SetLineJoin(LineJoinRound);
    g.DrawPath(&line, &crest);
}

void Cloud(Graphics& g, REAL x, REAL y, REAL scale) {
    struct Puff { REAL dx, dy, w, h; };
    const Puff puffs[] = {{0, 14, 92, 34}, {20, 0, 58, 44}, {56, 8, 70, 40}, {96, 16, 66, 30}, {-16, 20, 54, 24}};
    for (int pass = 0; pass < 2; ++pass) {
        SolidBrush brush(pass == 0 ? Th.cloudShade : Th.cloud);
        const REAL drop = pass == 0 ? 6.0f * scale : 0.0f;
        for (const Puff& p : puffs)
            g.FillEllipse(&brush, x + p.dx * scale, y + p.dy * scale + drop, p.w * scale, p.h * scale);
    }
}

// The scene behind every window: sky, a soft sun, clouds, the sea and two rolling hills. The
// shapes are fractions of the window, so the small windows get the same picture.
void DrawBackdrop(Graphics& g, int width, int height) {
    const REAL w = static_cast<REAL>(width), h = static_cast<REAL>(height);
    const REAL k = std::min(w / 1000.0f, h / 700.0f);
    const RectF full(0, 0, w, h);
    LinearGradientBrush sky(full, Th.skyTop, Th.skyBottom, LinearGradientModeVertical);
    g.FillRectangle(&sky, full);

    GraphicsPath sunPath;
    sunPath.AddEllipse(w * 0.70f, -h * 0.22f, h * 0.75f, h * 0.75f);
    PathGradientBrush sun(&sunPath);
    sun.SetCenterColor(Th.sun);
    Color sunEdge[] = {Alpha(Th.sun, 0)};
    int sunCount = 1;
    sun.SetSurroundColors(sunEdge, &sunCount);
    g.FillPath(&sun, &sunPath);

    Cloud(g, w * 0.46f, h * 0.05f, 1.0f * k);
    Cloud(g, w * 0.80f, h * 0.17f, 0.8f * k);
    Cloud(g, w * 0.12f, h * 0.30f, 0.7f * k);

    // The sea, between the sky and the hills.
    SolidBrush sea(Th.sea);
    g.FillEllipse(&sea, -0.25f * w, 0.68f * h, 1.5f * w, 0.5f * h);

    const PointF back[] = {PointF(-0.02f * w, 0.90f * h), PointF(0.18f * w, 0.855f * h), PointF(0.42f * w, 0.885f * h),
                           PointF(0.66f * w, 0.85f * h), PointF(0.88f * w, 0.875f * h), PointF(1.02f * w, 0.86f * h)};
    Hill(g, back, 6, h + 4, Th.hillBack);
    const PointF front[] = {PointF(-0.02f * w, 0.935f * h), PointF(0.22f * w, 0.905f * h), PointF(0.50f * w, 0.93f * h),
                            PointF(0.78f * w, 0.90f * h), PointF(1.02f * w, 0.92f * h)};
    Hill(g, front, 5, h + 4, Th.hillFront);
}

// Draws one frame into g, width x height pixels. The window's paint handler and the headless
// --render command both call this, so a render shows exactly what the window shows.
void RenderMain(Graphics& g, int width, int height) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    DrawBackdrop(g, width, height);

    const Layout l = MakeLayout(static_cast<float>(width));
    DrawHeader(g, l);
    switch (g_page) {
    case Page::Settings:
        DrawSettings(g, l);
        DrawFooter(g, l.done.Y + l.done.Height + SF(10));
        break;
    case Page::GameFiles:
        DrawSteps(g, static_cast<float>(width) - SF(32), SF(108), 0, 0);
        DrawGameFiles(g, l);
        DrawFooter(g, SF(636));
        break;
    case Page::Build:
        DrawSteps(g, static_cast<float>(width) - SF(32), SF(108), 1, FilesSet() ? 1 : 0);
        DrawBuild(g, l);
        DrawFooter(g, SF(636));
        break;
    case Page::Home:
    default:
        DrawHero(g, l);
        DrawActions(g, l);
        DrawDevices(g, l);
        DrawBuildSummary(g, l);
        DrawFooter(g, l.settings.Y + l.settings.Height + SF(18));
        break;
    }
}

void Paint(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &paint);
        return;
    }

    Bitmap back(width, height, PixelFormat32bppPARGB);
    Graphics g(&back);
    RenderMain(g, width, height);

    Graphics screen(dc);
    screen.DrawImage(&back, 0, 0);
    EndPaint(hwnd, &paint);
}

// Defined with the main window's code below; the tool windows use it too, so their frame
// matches the launcher's.
void ApplyDarkFrame(HWND window);

// ---- input check ----

void Chip(Graphics& g, const RectF& r, const std::wstring& label, bool on) {
    const Candy& candy = on ? Th.candyGreen : Th.candySand;
    const RectF face = CandyBody(g, r, SF(8), candy, false, false, SF(3.0f));
    Text(g, label, g_fontTiny, candy.text, face, StringAlignmentCenter);
}

float Axis01(void* pad, int axis) {
    if (!g_sdl.axis) return 0;
    int value = g_sdl.axis(pad, axis);
    if (value > 32767) value = 32767;
    if (value < -32767) value = -32767;
    return static_cast<float>(value) / 32767.0f;
}

float Trigger01(void* pad, int axis) {
    const float value = Axis01(pad, axis);
    return value < 0 ? 0 : value;
}

// A trigger: a gold name tag, then a chunky groove that fills as the trigger is pulled.
void TriggerBar(Graphics& g, const RectF& r, const wchar_t* name, float value) {
    const RectF tag(r.X, r.Y, SF(28), r.Height);
    const RectF tagFace = CandyBody(g, tag, SF(7), Th.candyGold, false, false, SF(2.5f));
    Text(g, name, g_fontTiny, Th.candyGold.text, tagFace, StringAlignmentCenter);
    RectF track(r.X + SF(36), r.Y + SF(2), r.Width - SF(36), r.Height - SF(4));
    const REAL edge = Edge();
    ChunkyRound(g, track, track.Height / 2, Th.wellTop, Th.wellBottom, Th.panelOutline, edge);
    const float fill = static_cast<float>(track.Width) * value;
    if (fill > 1) {
        RectF part(track.X, track.Y, std::max(fill, track.Height), track.Height);
        ChunkyRound(g, part, track.Height / 2, Th.candyGold.top, Th.candyGold.bottom, Th.candyGold.outline, edge);
    }
}

// A stick well: a sunken disc with a crosshair, the dashed deadzone circle, the shaped dot
// and the raw dot.
void Stick(Graphics& g, const RectF& box, const wchar_t* label, float x, float y) {
    const REAL tagW = TextWidth(g, label, g_fontTiny) + SF(20);
    const RectF tag(box.X + (box.Width - tagW) / 2, box.Y - SF(22), tagW, SF(18));
    const RectF tagFace = CandyBody(g, tag, SF(9), Th.candyGold, false, false, SF(2.0f));
    Text(g, label, g_fontTiny, Th.candyGold.text, tagFace, StringAlignmentCenter);

    const REAL radius = box.Width / 2;
    const PointF centre(box.X + radius, box.Y + radius);
    SoftShadow(g, box, radius, SF(2.0f));
    // The well is darker at the top, so it reads as a dip rather than a bump.
    ChunkyRound(g, box, radius, Th.wellTop, Th.wellBottom, Th.panelOutline, Edge());
    Pen cross(Th.wellLine, 1.0f);
    g.DrawLine(&cross, centre.X - radius + SF(8), centre.Y, centre.X + radius - SF(8), centre.Y);
    g.DrawLine(&cross, centre.X, centre.Y - radius + SF(8), centre.X, centre.Y + radius - SF(8));
    // The circle the runtime's deadzone covers, and the dot the stick reports. The dot
    // follows the shaped value, so the deadzone is visible when the stick rests.
    Pen inner(Th.stickDeadzone, 1.2f);
    inner.SetDashStyle(DashStyleDash);
    const REAL dead = static_cast<REAL>(radius * g_testDeadzone);
    g.DrawEllipse(&inner, centre.X - dead, centre.Y - dead, dead * 2, dead * 2);

    float size = std::sqrt(x * x + y * y);
    float shaped = 0;
    if (size > g_testDeadzone) {
        shaped = static_cast<float>((size - g_testDeadzone) / (1.0 - g_testDeadzone) * g_testSpeed);
        if (shaped > 1) shaped = 1;
    }
    const float dx = size > 0 ? x / size * shaped : 0;
    const float dy = size > 0 ? y / size * shaped : 0;
    const REAL px = centre.X + dx * (radius - SF(7));
    const REAL py = centre.Y - dy * (radius - SF(7));

    const REAL rawSize = SF(7);
    const REAL rawX = centre.X + x * (radius - SF(7));
    const REAL rawY = centre.Y - y * (radius - SF(7));
    SolidBrush dot(Th.stickDot);
    const REAL dotSize = SF(12);
    g.FillEllipse(&dot, px - dotSize / 2, py - dotSize / 2, dotSize, dotSize);
    Pen ring(Th.stickDotRing, 1.5f);
    g.DrawEllipse(&ring, px - dotSize / 2, py - dotSize / 2, dotSize, dotSize);

    // The raw position goes on last, so it stays visible when the shaped dot sits close.
    SolidBrush rawDot(Th.stickRaw);
    g.FillEllipse(&rawDot, rawX - rawSize / 2, rawY - rawSize / 2, rawSize, rawSize);
    Pen rawEdge(Th.stickRawEdge, 1.0f);
    g.DrawEllipse(&rawEdge, rawX - rawSize / 2, rawY - rawSize / 2, rawSize, rawSize);
}

void DrawPadCard(Graphics& g, const RectF& card, const TestPad& test, int index) {
    Panel(g, card, SF(14));

    // Header: a numbered medallion and the pad's name.
    const RectF medal(card.X + SF(14), card.Y + SF(10), SF(32), SF(32));
    const RectF medalFace = CandyBody(g, medal, SF(16), Th.candyGold, false, false, SF(3.0f));
    const std::wstring number = std::to_wstring(index + 1);
    OutlinedText(g, number, g_familyHeavy, SF(17.0f), g_heavyStyle, Th.btnLabel, Th.candyGold.outline, medalFace,
                 StringAlignmentCenter, 0.0f);
    Text(g, test.name, g_fontBody, Th.cardText, RectF(card.X + SF(54), card.Y + SF(10), card.Width - SF(70), SF(32)),
         StringAlignmentNear, true);

    struct Named { int code; const wchar_t* label; };
    static const Named rows[4][4] = {
        {{0, L"B  south"}, {1, L"A  east"}, {2, L"Y  west"}, {3, L"X  north"}},
        {{4, L"-  back"}, {6, L"+  start"}, {5, L"Home"}, {15, L"Capture"}},
        {{7, L"L3"}, {8, L"R3"}, {9, L"LB"}, {10, L"RB"}},
        {{11, L"D-up"}, {12, L"D-down"}, {13, L"D-left"}, {14, L"D-right"}},
    };
    const float inner = card.Width - SF(32);
    const float gap = SF(6);
    const float chipW = (inner - 3 * gap) / 4;

    Slot(g, RectF(card.X + SF(10), card.Y + SF(48), card.Width - SF(20), SF(128)), SF(10));
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const RectF chip(card.X + SF(16) + column * (chipW + gap), card.Y + SF(54) + row * SF(30), chipW, SF(24));
            const bool on = g_sdl.button && g_sdl.button(test.handle, rows[row][column].code);
            Chip(g, chip, rows[row][column].label, on);
        }
    }

    Slot(g, RectF(card.X + SF(10), card.Y + SF(182), card.Width - SF(20), SF(50)), SF(10));
    const float triggerY = card.Y + SF(187);
    TriggerBar(g, RectF(card.X + SF(16), triggerY, inner, SF(18)), L"LT", Trigger01(test.handle, 4));
    TriggerBar(g, RectF(card.X + SF(16), triggerY + SF(22), inner, SF(18)), L"RT", Trigger01(test.handle, 5));

    Slot(g, RectF(card.X + SF(10), card.Y + SF(238), card.Width - SF(20), SF(146)), SF(10));
    const float stickSize = SF(92);
    const float stickY = card.Y + SF(268);
    const float sticksWidth = stickSize * 2 + SF(24);
    const float leftX = card.X + (card.Width - sticksWidth) / 2;
    const float rightX = leftX + stickSize + SF(24);
    const float leftAxisX = Axis01(test.handle, 0), leftAxisY = -Axis01(test.handle, 1);
    const float rightAxisX = Axis01(test.handle, 2), rightAxisY = -Axis01(test.handle, 3);
    Stick(g, RectF(leftX, stickY, stickSize, stickSize), L"LEFT", leftAxisX, leftAxisY);
    Stick(g, RectF(rightX, stickY, stickSize, stickSize), L"RIGHT", rightAxisX, rightAxisY);
    Text(g, L"grey raw  ·  orange shaped  ·  circle deadzone", g_fontSmall, Th.cardTextMuted,
         RectF(card.X + SF(16), stickY + stickSize + SF(8), inner, SF(14)), StringAlignmentCenter, true);
}

// Draws one frame into g, width x height pixels. The window's paint handler and the headless
// --render command both call this, so a render shows exactly what the window shows.
void RenderInput(Graphics& g, int width, int height) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    DrawBackdrop(g, width, height);

    const float margin = SF(24);
    Banner(g, L"Input check", margin - SF(4), SF(8), SF(34));
    Text(g, g_testPads.empty()
                ? L"No gamepad is connected; the game still reads the keyboard and mouse."
                : L"Press buttons and move the sticks. The inner circle is the runtime deadzone.",
         g_fontSmall, Th.bgTextDim, RectF(margin + SF(2), SF(46), static_cast<float>(width) - 2 * margin, SF(18)),
         StringAlignmentNear, true);

    if (g_testPads.empty()) {
        const RectF card(margin, SF(80), static_cast<float>(width) - 2 * margin, SF(220));
        Panel(g, card, SF(14));
        SectionTag(g, card, L"NO GAMEPAD");
        const float textWidth = card.Width - SF(250);
        Title(g, L"No gamepad detected", SF(static_cast<float>(Th.sizeH2)),
              RectF(card.X + SF(24), card.Y + SF(46), textWidth, SF(30)), Th.titleOutlineCard);
        Text(g, L"Connect one, then choose Rescan on the launcher or reopen this window.",
             g_fontBody, Th.cardTextDim, RectF(card.X + SF(24), card.Y + SF(86), textWidth, SF(22)),
             StringAlignmentNear, true);
        Paragraph(g, L"The game's keyboard controls are listed in docs/controls.md and can be changed in the [keys] section of controllers.ini.",
                  g_fontSmall, Th.cardTextMuted, RectF(card.X + SF(24), card.Y + SF(118), textWidth, SF(60)));
        DrawPadIcon(g, RectF(card.X + card.Width - SF(210), card.Y + SF(40), SF(180), SF(140)));
    } else {
        const float gap = SF(16);
        const size_t count = g_testPads.size() < 2 ? g_testPads.size() : 2;
        float cardWidth = (static_cast<float>(width) - 2 * margin - gap * (count - 1)) / count;
        if (count == 1) cardWidth = std::min(cardWidth, SF(440));
        const float cardHeight = std::min(static_cast<float>(height) - SF(92) - SF(64), SF(392));
        const float used = cardWidth * count + gap * (count - 1);
        const float startX = margin + (static_cast<float>(width) - 2 * margin - used) / 2;
        for (size_t i = 0; i < count; ++i) {
            DrawPadCard(g, RectF(startX + i * (cardWidth + gap), SF(76), cardWidth, cardHeight), g_testPads[i],
                        static_cast<int>(i));
        }
        if (g_testPads.size() > count) {
            Text(g, L"+" + std::to_wstring(g_testPads.size() - count) + L" more connected; the check shows two.",
                 g_fontSmall, Th.bgTextMuted, RectF(startX, SF(76) + cardHeight + SF(6), 400, SF(16)));
        }
    }

    wchar_t settings[160]{};
    swprintf_s(settings, L"Deadzone %.2f  ·  Stick speed %.2f  ·  controllers.ini beside the console",
               g_testDeadzone, g_testSpeed);
    Text(g, settings, g_fontSmall, Th.bgTextMuted,
         RectF(margin, static_cast<float>(height) - SF(28), static_cast<float>(width) - 2 * margin, SF(18)),
         StringAlignmentNear, true);
}

void PaintInput(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &paint);
        return;
    }

    Bitmap back(width, height, PixelFormat32bppPARGB);
    Graphics g(&back);
    RenderInput(g, width, height);

    Graphics screen(dc);
    screen.DrawImage(&back, 0, 0);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK InputProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        ApplyDarkFrame(window);
        SetTimer(window, 1, 33, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            if (g_sdl.update) g_sdl.update();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_DEVICECHANGE:
        if (g_inputWindow) {
            RefreshTestPads();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintInput(window);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, 1);
        CloseTestPads();
        g_inputWindow = nullptr;
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void OpenInputTest() {
    if (g_inputWindow) {
        ShowWindow(g_inputWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_inputWindow);
        return;
    }
    if (!LoadSdl()) {
        MessageBoxW(g_window, L"SDL3 was not found, so gamepads cannot be read.\nPut SDL3.dll in the MSYS2 folder.",
                    kInputTitle, MB_ICONWARNING | MB_OK);
        return;
    }
    if (!g_sdl.initialized) {
        if (!g_sdl.init(0x00002000u)) {
            MessageBoxW(g_window, L"SDL3 could not start the controller system.", kInputTitle,
                        MB_ICONWARNING | MB_OK);
            return;
        }
        g_sdl.initialized = true;
    }
    if (!g_sdl.button || !g_sdl.axis) {
        MessageBoxW(g_window, L"This SDL3 does not export the gamepad state functions the check needs.",
                    kInputTitle, MB_ICONWARNING | MB_OK);
        return;
    }
    ReadPadSettings();
    RefreshTestPads();

    RECT rect{0, 0, S(kInputWidth), S(kInputHeight)};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    RECT owner{};
    GetWindowRect(g_window, &owner);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = owner.left + ((owner.right - owner.left) - width) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;

    g_inputWindow = CreateWindowExW(0, kInputClass, kInputTitle,
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, x, y, width, height,
                                    g_window, nullptr, g_instance, nullptr);
    if (!g_inputWindow) {
        CloseTestPads();
        MessageBoxW(g_window, L"Could not open the input check window.", kInputTitle, MB_ICONERROR | MB_OK);
        return;
    }
    ShowWindow(g_inputWindow, SW_SHOWNORMAL);
    UpdateWindow(g_inputWindow);
    SetForegroundWindow(g_inputWindow);
}

// ---- controls editor ----

// The actions, labels and default keys mirror KEY_CONTROLS, control_names and
// default_keys in work/wii-recomp/recomp/runtime/controllers.c. The runtime is the
// authority; this table only feeds the editor's rows.
struct KeyAction {
    const wchar_t* key;
    const wchar_t* label;
    const wchar_t* defaults;
};
const KeyAction kKeyActions[] = {
    {L"up", L"D-pad up", L"up"},
    {L"down", L"D-pad down", L"down"},
    {L"left", L"D-pad left", L"left"},
    {L"right", L"D-pad right", L"right"},
    {L"a", L"A button", L"enter, j"},
    {L"b", L"B button", L"backspace, k"},
    {L"one", L"1 button", L"1"},
    {L"two", L"2 button", L"2"},
    {L"plus", L"+ button", L"equal, p"},
    {L"minus", L"- button", L"minus, o"},
    {L"home", L"Home button", L"h"},
    {L"l", L"L (Classic)", L"q"},
    {L"r", L"R (Classic)", L"e"},
    {L"zl", L"ZL (Classic)", L"z"},
    {L"zr", L"ZR (Classic)", L"x"},
    {L"c", L"C (Nunchuk)", L"c"},
    {L"z", L"Z (Nunchuk)", L"v"},
    {L"stick_up", L"Stick up", L"w"},
    {L"stick_down", L"Stick down", L"s"},
    {L"stick_left", L"Stick left", L"a"},
    {L"stick_right", L"Stick right", L"d"},
    {L"shake", L"Shake", L"f"},
    {L"next_attachment", L"Attachment", L"f2"},
    {L"motionplus", L"MotionPlus", L"f3"},
    {L"board", L"Balance board", L"space"},
    {L"lean_left", L"Lean left", L"a"},
    {L"lean_right", L"Lean right", L"d"},
    {L"lean_forward", L"Lean forward", L"w"},
    {L"lean_back", L"Lean back", L"s"},
};
constexpr int kKeyActionCount = static_cast<int>(sizeof(kKeyActions) / sizeof(kKeyActions[0]));

struct PadAction {
    const wchar_t* key;
    const wchar_t* label;
};
const PadAction kPadActions[] = {
    {L"a", L"Wii A"},
    {L"b", L"Wii B"},
    {L"one", L"Wii 1"},
    {L"two", L"Wii 2"},
    {L"plus", L"Wii +"},
    {L"minus", L"Wii -"},
    {L"home", L"Wii Home"},
    {L"up", L"Wii D-pad up"},
    {L"down", L"Wii D-pad down"},
    {L"left", L"Wii D-pad left"},
    {L"right", L"Wii D-pad right"},
    {L"c", L"Nunchuk C"},
    {L"z", L"Nunchuk Z"},
    {L"classic_l", L"Classic L"},
    {L"classic_r", L"Classic R"},
    {L"classic_zl", L"Classic ZL"},
    {L"classic_zr", L"Classic ZR"},
};
constexpr int kPadActionCount = static_cast<int>(sizeof(kPadActions) / sizeof(kPadActions[0]));

// SDL's gamepad buttons, by the controller position the runtime names them after.
struct PadCapture {
    int button;
    const wchar_t* name;
};
const PadCapture kPadCapture[] = {
    {1, L"a"},   // east
    {0, L"b"},   // south
    {3, L"x"},   // north
    {2, L"y"},   // west
    {9, L"l"},
    {10, L"r"},
    {6, L"plus"},
    {4, L"minus"},
    {5, L"home"},
    {15, L"capture"},
    {7, L"left_stick"},
    {8, L"right_stick"},
    {11, L"dpad_up"},
    {12, L"dpad_down"},
    {13, L"dpad_left"},
    {14, L"dpad_right"},
};

struct PadChoice {
    const wchar_t* value;
    const wchar_t* label;
};
const PadChoice kPadChoices[] = {
    {L"auto", L"Auto (layout default)"},
    {L"a", L"a - east (Xbox B)"},
    {L"b", L"b - south (Xbox A)"},
    {L"x", L"x - north (Xbox Y)"},
    {L"y", L"y - west (Xbox X)"},
    {L"l", L"l - left bumper"},
    {L"r", L"r - right bumper"},
    {L"zl", L"zl - left trigger"},
    {L"zr", L"zr - right trigger"},
    {L"plus", L"plus - start"},
    {L"minus", L"minus - back"},
    {L"home", L"home - guide"},
    {L"capture", L"capture"},
    {L"left_stick", L"left_stick - L3"},
    {L"right_stick", L"right_stick - R3"},
    {L"dpad_up", L"dpad_up"},
    {L"dpad_down", L"dpad_down"},
    {L"dpad_left", L"dpad_left"},
    {L"dpad_right", L"dpad_right"},
    {L"none", L"None (unused)"},
};

std::vector<std::wstring> g_mapKeyValues;
std::vector<std::wstring> g_mapPadValues;

// [mouse]: the buttons and what each presses. Mirrors mouse_names and default_mouse in
// controllers.c; the runtime stays the authority.
struct MouseAction {
    const wchar_t* key;
    const wchar_t* label;
    const wchar_t* defaults;
};
const MouseAction kMouseActions[] = {
    {L"left", L"Left button", L"a"},
    {L"right", L"Right button", L"b"},
    {L"middle", L"Middle button (wheel click)", L"none"},
    {L"x1", L"Side button 1 (back)", L"none"},
    {L"x2", L"Side button 2 (forward)", L"none"},
};
constexpr int kMouseActionCount = static_cast<int>(sizeof(kMouseActions) / sizeof(kMouseActions[0]));
const PadChoice kMouseChoices[] = {
    {L"a", L"Wii A"},
    {L"b", L"Wii B"},
    {L"one", L"Wii 1"},
    {L"two", L"Wii 2"},
    {L"plus", L"Wii +"},
    {L"minus", L"Wii -"},
    {L"home", L"Wii Home"},
    {L"up", L"Wii D-pad up"},
    {L"down", L"Wii D-pad down"},
    {L"left", L"Wii D-pad left"},
    {L"right", L"Wii D-pad right"},
    {L"c", L"Nunchuk C"},
    {L"z", L"Nunchuk Z"},
    {L"l", L"Classic L"},
    {L"r", L"Classic R"},
    {L"zl", L"Classic ZL"},
    {L"zr", L"Classic ZR"},
    {L"shake", L"Shake the remote"},
    {L"none", L"None (unused)"},
};
std::vector<std::wstring> g_mapMouseValues;

// [player1]: what drives player 1, as the runtime's parser reads it. Defaults mirror
// set_defaults in controllers.c.
struct PlayerRow {
    const wchar_t* key;     // The [player1] key.
    const wchar_t* token;   // For a list: the word this row switches on or off. Null for a choice.
    const wchar_t* label;
};
const PlayerRow kPlayerRows[] = {
    {L"source", L"keyboard", L"Keyboard drives player 1"},
    {L"source", L"mouse", L"Mouse drives player 1"},
    {L"source", L"pad", L"Gamepad drives player 1"},
    {L"attachment", nullptr, L"Attachment"},
    {L"motionplus", L"yes", L"Wii MotionPlus"},
    {L"sideways", L"yes", L"Held sideways"},
    {L"pointer", L"mouse", L"Pointer: mouse"},
    {L"pointer", L"gyro", L"Pointer: gamepad gyro"},
    {L"pointer", L"stick", L"Pointer: right stick"},
    {L"recenter", nullptr, L"Recenter button"},
};
constexpr int kPlayerRowCount = static_cast<int>(sizeof(kPlayerRows) / sizeof(kPlayerRows[0]));
struct PlayerKeyDefault {
    const wchar_t* key;
    const wchar_t* value;
};
const PlayerKeyDefault kPlayerDefaults[] = {
    {L"source", L"keyboard, mouse, pad"}, {L"attachment", L"nunchuk"}, {L"motionplus", L"no"},
    {L"sideways", L"no"},                 {L"pointer", L"mouse, gyro"}, {L"gyro_speed", L"1.0"},
    {L"recenter", L"right_stick"},
};
const PadChoice kAttachmentChoices[] = {
    {L"none", L"None (remote alone)"},
    {L"nunchuk", L"Nunchuk"},
    {L"classic", L"Classic Controller"},
};
std::wstring g_mapPlayer[7];  // The values, in kPlayerDefaults order.

std::wstring Trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    const size_t last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<std::wstring> ListValues(const std::wstring& value) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t comma = value.find(L',', start);
        std::wstring part = Trim(value.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start));
        if (!part.empty()) parts.push_back(part);
        if (comma == std::wstring::npos) break;
        start = comma + 1;
    }
    return parts;
}

std::wstring PrettyKeyName(const std::wstring& name) {
    if (name.empty()) return L"";
    if (name == L"none") return L"None";
    if (name.size() == 1) {
        wchar_t c = name[0];
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
        return std::wstring(1, c);
    }
    if (name[0] == L'f' && name.size() <= 3 &&
        name.find_first_not_of(L"0123456789", 1) == std::wstring::npos) {
        std::wstring out = name;
        out[0] = L'F';
        return out;
    }
    if (name == L"home_key") return L"Home";
    std::wstring out;
    bool start = true;
    for (const wchar_t c : name) {
        if (c == L'_') {
            out += L' ';
            start = true;
        } else {
            if (start && c >= L'a' && c <= L'z') out += static_cast<wchar_t>(c - L'a' + L'A');
            else out += c;
            start = false;
        }
    }
    return out;
}

std::wstring PrettyKeyList(const std::wstring& value) {
    const std::vector<std::wstring> parts = ListValues(value);
    if (parts.empty()) return L"None";
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += L", ";
        out += PrettyKeyName(parts[i]);
    }
    return out;
}

std::wstring PrettyPadName(const std::wstring& name) {
    if (name == L"auto") return L"Auto";
    if (name == L"none") return L"None";
    if (name == L"left_stick") return L"L3";
    if (name == L"right_stick") return L"R3";
    if (name == L"plus") return L"+";
    if (name == L"minus") return L"-";
    if (name == L"home") return L"Home";
    if (name == L"capture") return L"Capture";
    if (name == L"dpad_up") return L"D-pad up";
    if (name == L"dpad_down") return L"D-pad down";
    if (name == L"dpad_left") return L"D-pad left";
    if (name == L"dpad_right") return L"D-pad right";
    std::wstring out = name;
    for (wchar_t& c : out) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    return out;
}

// A Win32 virtual key as the runtime's [keys] name. Empty means the key has no name
// there; the runtime accepts letters, digits, F1 to F12 and the names controllers.c
// lists, and nothing else.
std::wstring KeyNameFromVirtualKey(WPARAM key, LPARAM info) {
    UINT vk = static_cast<UINT>(key);
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
        const UINT scan = (static_cast<UINT>(info) >> 16) & 0xFF;
        const bool extended = (static_cast<UINT>(info) & (1u << 24)) != 0;
        if (vk == VK_SHIFT) vk = scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
        else if (vk == VK_CONTROL) vk = extended ? VK_RCONTROL : VK_LCONTROL;
        else vk = extended ? VK_RMENU : VK_LMENU;
    }
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk - 'A' + 'a'));
    if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F12) return L"f" + std::to_wstring(vk - VK_F1 + 1);
    switch (vk) {
    case VK_UP: return L"up";
    case VK_DOWN: return L"down";
    case VK_LEFT: return L"left";
    case VK_RIGHT: return L"right";
    case VK_RETURN: return L"enter";
    case VK_BACK: return L"backspace";
    case VK_SPACE: return L"space";
    case VK_TAB: return L"tab";
    case VK_OEM_MINUS: return L"minus";
    case VK_OEM_PLUS: return L"equal";
    case VK_OEM_COMMA: return L"comma";
    case VK_OEM_PERIOD: return L"period";
    case VK_OEM_2: return L"slash";
    case VK_OEM_1: return L"semicolon";
    case VK_OEM_7: return L"apostrophe";
    case VK_OEM_4: return L"left_bracket";
    case VK_OEM_6: return L"right_bracket";
    case VK_OEM_5: return L"backslash";
    case VK_OEM_3: return L"grave";
    case VK_LSHIFT: return L"left_shift";
    case VK_RSHIFT: return L"right_shift";
    case VK_LCONTROL: return L"left_control";
    case VK_RCONTROL: return L"right_control";
    case VK_LMENU: return L"left_alt";
    case VK_RMENU: return L"right_alt";
    case VK_INSERT: return L"insert";
    case VK_DELETE: return L"delete";
    case VK_HOME: return L"home_key";
    case VK_END: return L"end";
    case VK_PRIOR: return L"page_up";
    case VK_NEXT: return L"page_down";
    default: return L"";
    }
}

std::wstring ReadControllerValue(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    wchar_t buffer[512]{};
    GetPrivateProfileStringW(section, key, fallback, buffer, ARRAYSIZE(buffer), g_mapConfigPath.c_str());
    // The runtime ends a value at a # (a comment after it); the Windows reader does not.
    std::wstring value = buffer;
    const size_t hash = value.find(L'#');
    if (hash != std::wstring::npos) value.resize(hash);
    return value;
}

int PlayerKeyIndex(const wchar_t* key) {
    for (int i = 0; i < 7; ++i) {
        if (wcscmp(kPlayerDefaults[i].key, key) == 0) return i;
    }
    return 0;
}

void ReloadControls() {
    g_mapKeyValues.resize(kKeyActionCount);
    for (int i = 0; i < kKeyActionCount; ++i) {
        g_mapKeyValues[i] = Trim(ReadControllerValue(L"keys", kKeyActions[i].key, kKeyActions[i].defaults));
    }
    g_mapPadValues.resize(kPadActionCount);
    for (int i = 0; i < kPadActionCount; ++i) {
        g_mapPadValues[i] = Trim(ReadControllerValue(L"pad", kPadActions[i].key, L"auto"));
    }
    g_mapMouseValues.resize(kMouseActionCount);
    for (int i = 0; i < kMouseActionCount; ++i) {
        g_mapMouseValues[i] = Trim(ReadControllerValue(L"mouse", kMouseActions[i].key, kMouseActions[i].defaults));
    }
    for (int i = 0; i < 7; ++i) {
        g_mapPlayer[i] = Trim(ReadControllerValue(L"player1", kPlayerDefaults[i].key, kPlayerDefaults[i].value));
    }
    g_mapGyro = 1.0;
    const double gyro = _wtof(g_mapPlayer[PlayerKeyIndex(L"gyro_speed")].c_str());
    if (gyro >= 0.25 && gyro <= 4) g_mapGyro = gyro;
    g_mapDeadzone = 0.18;
    g_mapSpeed = 1.0;
    const double deadzone = _wtof(ReadControllerValue(L"pad", L"deadzone", L"0.18").c_str());
    if (deadzone >= 0 && deadzone <= 0.9) g_mapDeadzone = deadzone;
    const double speed = _wtof(ReadControllerValue(L"pad", L"stick_speed", L"1.0").c_str());
    if (speed >= 0.25 && speed <= 4) g_mapSpeed = speed;
}

bool WriteControllerValue(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    if (WritePrivateProfileStringW(section, key, value.c_str(), g_mapConfigPath.c_str())) return true;
    g_mapStatus = L"Could not write controllers.ini. Check that the console folder is writable.";
    return false;
}

std::wstring KeyOwnerWarning(int index, const std::wstring& key) {
    for (int i = 0; i < kKeyActionCount; ++i) {
        if (i == index) continue;
        for (const std::wstring& part : ListValues(g_mapKeyValues[i])) {
            if (_wcsicmp(part.c_str(), key.c_str()) == 0) {
                return std::wstring(L"Also bound to ") + kKeyActions[i].label + L".";
            }
        }
    }
    return L"";
}

bool ApplyKeyBinding(int index, const std::wstring& key) {
    if (index < 0 || index >= kKeyActionCount) return false;
    const std::wstring warning = key == L"none" ? L"" : KeyOwnerWarning(index, key);
    if (!WriteControllerValue(L"keys", kKeyActions[index].key, key)) {
        g_mapListening = -1;
        return false;
    }
    g_mapKeyValues[index] = key;
    g_mapStatus = std::wstring(L"Set ") + kKeyActions[index].label + L" to " + PrettyKeyName(key) + L".";
    if (!warning.empty()) g_mapStatus += L" " + warning;
    g_mapListening = -1;
    return true;
}

std::wstring PadOwnerWarning(int index, const std::wstring& button) {
    if (button == L"auto" || button == L"none") return L"";
    for (int i = 0; i < kPadActionCount; ++i) {
        if (i == index) continue;
        if (_wcsicmp(g_mapPadValues[i].c_str(), button.c_str()) == 0) {
            return std::wstring(L"Also presses ") + kPadActions[i].label + L".";
        }
    }
    return L"";
}

bool ApplyPadBinding(int index, const std::wstring& button) {
    if (index < 0 || index >= kPadActionCount) return false;
    const std::wstring warning = PadOwnerWarning(index, button);
    if (!WriteControllerValue(L"pad", kPadActions[index].key, button)) {
        g_mapListening = -1;
        return false;
    }
    g_mapPadValues[index] = button;
    g_mapStatus = std::wstring(L"Set ") + kPadActions[index].label + L" to " + PrettyPadName(button) + L".";
    if (!warning.empty()) g_mapStatus += L" " + warning;
    g_mapListening = -1;
    return true;
}

bool ApplyMouseBinding(int index, const std::wstring& action) {
    if (index < 0 || index >= kMouseActionCount) return false;
    if (!WriteControllerValue(L"mouse", kMouseActions[index].key, action)) return false;
    g_mapMouseValues[index] = action;
    std::wstring label = action;
    for (const PadChoice& choice : kMouseChoices) {
        if (action == choice.value) label = choice.label;
    }
    g_mapStatus = std::wstring(kMouseActions[index].label) + L" now presses: " + label + L".";
    return true;
}

// Pops a list at the pointer and returns the index chosen, or -1. The current value is ticked.
int ChooseFromList(const PadChoice* choices, int count, const std::wstring& current, int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return -1;
    for (int i = 0; i < count; ++i) {
        UINT flags = MF_STRING | (current == choices[i].value ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(i + 1), choices[i].label);
    }
    POINT point{x, y};
    ClientToScreen(g_mapWindow, &point);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0,
                                       g_mapWindow, nullptr);
    DestroyMenu(menu);
    return command > 0 && command <= count ? command - 1 : -1;
}

void ChooseMouseBinding(int index, int x, int y) {
    constexpr int count = static_cast<int>(sizeof(kMouseChoices) / sizeof(kMouseChoices[0]));
    const int chosen = ChooseFromList(kMouseChoices, count, g_mapMouseValues[static_cast<size_t>(index)], x, y);
    if (chosen >= 0) ApplyMouseBinding(index, kMouseChoices[chosen].value);
}

bool PlayerListHas(const std::wstring& list, const wchar_t* token) {
    const bool pad = wcscmp(token, L"pad") == 0;
    for (const std::wstring& part : ListValues(list)) {
        if (part == token || (pad && part.rfind(L"pad:", 0) == 0)) return true;
    }
    return false;
}

// The list with one word switched on or off; every other word, such as pad:<serial>, stays.
std::wstring PlayerListWith(const std::wstring& list, const wchar_t* token, bool on) {
    const bool pad = wcscmp(token, L"pad") == 0;
    std::vector<std::wstring> kept;
    for (const std::wstring& part : ListValues(list)) {
        if (part == L"none") continue;
        if (part == token || (pad && part.rfind(L"pad:", 0) == 0)) continue;
        kept.push_back(part);
    }
    if (on) kept.push_back(token);
    std::wstring out;
    for (size_t i = 0; i < kept.size(); ++i) out += (i ? L", " : L"") + kept[i];
    return out.empty() ? L"none" : out;
}

bool ApplyPlayerValue(const wchar_t* key, const std::wstring& value) {
    if (!WriteControllerValue(L"player1", key, value)) return false;
    g_mapPlayer[PlayerKeyIndex(key)] = value;
    g_mapStatus = std::wstring(L"Player 1 ") + key + L" = " + value + L".";
    return true;
}

bool PlayerRowOn(int index) {
    const PlayerRow& row = kPlayerRows[index];
    const std::wstring& value = g_mapPlayer[PlayerKeyIndex(row.key)];
    return wcscmp(row.token, L"yes") == 0 ? value == L"yes" : PlayerListHas(value, row.token);
}

std::wstring PlayerRowText(int index) {
    const PlayerRow& row = kPlayerRows[index];
    const std::wstring& value = g_mapPlayer[PlayerKeyIndex(row.key)];
    if (!row.token) {
        if (wcscmp(row.key, L"recenter") == 0) return PrettyPadName(value);
        for (const PadChoice& choice : kAttachmentChoices) {
            if (value == choice.value) return choice.value == std::wstring(L"none") ? L"None" : choice.label;
        }
        return value;
    }
    return PlayerRowOn(index) ? L"On" : L"Off";
}

void ActivatePlayerRow(int index, int x, int y) {
    if (index < 0 || index >= kPlayerRowCount) return;
    const PlayerRow& row = kPlayerRows[index];
    const std::wstring& value = g_mapPlayer[PlayerKeyIndex(row.key)];
    if (!row.token) {
        if (wcscmp(row.key, L"attachment") == 0) {
            constexpr int count = static_cast<int>(sizeof(kAttachmentChoices) / sizeof(kAttachmentChoices[0]));
            const int chosen = ChooseFromList(kAttachmentChoices, count, value, x, y);
            if (chosen >= 0) ApplyPlayerValue(row.key, kAttachmentChoices[chosen].value);
        } else {
            // The buttons a gamepad has, after the auto entry that [pad] needs but this does not.
            constexpr int count = static_cast<int>(sizeof(kPadChoices) / sizeof(kPadChoices[0]));
            const int chosen = ChooseFromList(kPadChoices + 1, count - 1, value, x, y);
            if (chosen >= 0) ApplyPlayerValue(row.key, kPadChoices[chosen + 1].value);
        }
        return;
    }
    if (wcscmp(row.token, L"yes") == 0) {
        ApplyPlayerValue(row.key, value == L"yes" ? L"no" : L"yes");
        return;
    }
    const bool now = !PlayerListHas(value, row.token);
    ApplyPlayerValue(row.key, PlayerListWith(value, row.token, now));
}

void StartListening(int index, bool pad) {
    g_mapListening = index;
    g_mapTab = pad ? 1 : 0;
    g_mapStatus = pad ? L"Press a gamepad button. Right-click the row for a list; Escape cancels."
                      : L"Press a key. Escape cancels.";
    if (pad) {
        for (bool& state : g_mapPadWas) state = false;
        for (bool& state : g_mapTriggerWas) state = false;
        if (!g_mapPads.empty() && g_sdl.button) {
            void* handle = g_mapPads[0].handle;
            for (const PadCapture& capture : kPadCapture) {
                g_mapPadWas[capture.button] = g_sdl.button(handle, capture.button);
            }
            g_mapTriggerWas[0] = Trigger01(handle, 4) > 0.5f;
            g_mapTriggerWas[1] = Trigger01(handle, 5) > 0.5f;
        }
    }
    if (g_mapWindow) InvalidateRect(g_mapWindow, nullptr, FALSE);
}

void PollPadCapture() {
    if (g_mapTab != 1 || g_mapListening < 0 || g_mapPads.empty() || !g_sdl.button) return;
    void* handle = g_mapPads[0].handle;
    for (const PadCapture& capture : kPadCapture) {
        const bool down = g_sdl.button(handle, capture.button);
        if (down && !g_mapPadWas[capture.button]) {
            ApplyPadBinding(g_mapListening, capture.name);
            return;
        }
        g_mapPadWas[capture.button] = down;
    }
    const bool left = Trigger01(handle, 4) > 0.5f;
    const bool right = Trigger01(handle, 5) > 0.5f;
    if (left && !g_mapTriggerWas[0]) {
        ApplyPadBinding(g_mapListening, L"zl");
        return;
    }
    g_mapTriggerWas[0] = left;
    if (right && !g_mapTriggerWas[1]) {
        ApplyPadBinding(g_mapListening, L"zr");
        return;
    }
    g_mapTriggerWas[1] = right;
}

void ChoosePadBinding(int index, int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    constexpr int count = static_cast<int>(sizeof(kPadChoices) / sizeof(kPadChoices[0]));
    for (int i = 0; i < count; ++i) {
        if (i == 1) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(i + 1), kPadChoices[i].label);
    }
    POINT point{x, y};
    ClientToScreen(g_mapWindow, &point);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0,
                                       g_mapWindow, nullptr);
    DestroyMenu(menu);
    if (command > 0 && command <= count) {
        ApplyPadBinding(index, kPadChoices[command - 1].value);
    }
}

void OpenControlGuide() {
    const std::wstring path = Join(g_root, L"docs\\controls.md");
    if (!Exists(path)) {
        ShowMissing(L"controls file", path);
        return;
    }
    const std::wstring arguments = L"\"" + path + L"\"";
    ShellExecuteW(g_mapWindow, L"open", L"notepad.exe", arguments.c_str(), g_root.c_str(), SW_SHOWNORMAL);
}

void RestoreControlDefaults() {
    if (MessageBoxW(g_mapWindow,
                    L"Remove every keyboard, gamepad, mouse and player 1 override from controllers.ini?\n\n"
                    L"The runtime's built-in defaults will apply the next time the game starts.",
                    L"Restore defaults", MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }
    if (!Exists(g_mapConfigPath)) {
        ReloadControls();
        g_mapStatus = L"controllers.ini does not exist yet; the built-in defaults are already in use.";
        return;
    }
    bool written = true;
    for (const KeyAction& action : kKeyActions) {
        if (!WritePrivateProfileStringW(L"keys", action.key, nullptr, g_mapConfigPath.c_str())) written = false;
    }
    for (const PadAction& action : kPadActions) {
        if (!WritePrivateProfileStringW(L"pad", action.key, nullptr, g_mapConfigPath.c_str())) written = false;
    }
    for (const MouseAction& action : kMouseActions) {
        if (!WritePrivateProfileStringW(L"mouse", action.key, nullptr, g_mapConfigPath.c_str())) written = false;
    }
    for (const PlayerKeyDefault& entry : kPlayerDefaults) {
        if (!WritePrivateProfileStringW(L"player1", entry.key, nullptr, g_mapConfigPath.c_str())) written = false;
    }
    if (!WritePrivateProfileStringW(L"pad", L"deadzone", nullptr, g_mapConfigPath.c_str())) written = false;
    if (!WritePrivateProfileStringW(L"pad", L"stick_speed", nullptr, g_mapConfigPath.c_str())) written = false;
    ReloadControls();
    g_mapListening = -1;
    g_mapStatus = written ? L"Restored the built-in defaults."
                          : L"Could not write controllers.ini. Check that the console folder is writable.";
}

void CloseMapPads() {
    if (g_sdl.close) {
        for (TestPad& pad : g_mapPads) {
            if (pad.handle) g_sdl.close(pad.handle);
        }
    }
    g_mapPads.clear();
}

void RefreshMapPads() {
    CloseMapPads();
    if (!g_sdl.initialized || !g_sdl.gamepads || !g_sdl.open) return;
    g_sdl.pump();
    int count = 0;
    unsigned* ids = g_sdl.gamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count && g_mapPads.empty(); ++i) {
        void* handle = g_sdl.open(ids[i]);
        if (!handle) continue;
        TestPad pad;
        pad.handle = handle;
        pad.name = Widen(g_sdl.name(handle));
        if (pad.name.empty()) pad.name = L"Gamepad";
        g_mapPads.push_back(pad);
    }
    g_sdl.release(ids);
}

struct MapRects {
    RectF tabs[4];
    RectF guide, defaults, close;
    RectF card;
    RectF deadzoneTrack, deadzoneHit, speedTrack, speedHit;
    RectF sliderSlot;   // The panel behind the slider labels and tracks (tabs 1 and 3).
    RectF info, art;    // The note card and the drawn illustration under the rows (tabs 1 to 3).
    std::vector<RectF> rows;
    std::vector<RectF> clears;
};

int MapRowCount() {
    switch (g_mapTab) {
    case 0: return kKeyActionCount;
    case 1: return kPadActionCount;
    case 2: return kMouseActionCount;
    default: return kPlayerRowCount;
    }
}

MapRects BuildMapRects(float width, float height) {
    MapRects rects{};
    const float margin = SF(26);
    // The tabs sit on the card's top edge: they end 4 px inside it, so a selected tab can
    // cover the card's outline. The first one starts past the card's rounded corner.
    const float tabX = margin + SF(18);
    for (int i = 0; i < 4; ++i) rects.tabs[i] = RectF(tabX + i * SF(118), SF(84), SF(110), SF(36));
    rects.card = RectF(margin, SF(116), width - 2 * margin, height - SF(116) - SF(72));

    const float buttonsY = height - SF(26) - SF(32);
    rects.close = RectF(width - margin - SF(76), buttonsY, SF(76), SF(32));
    rects.defaults = RectF(rects.close.X - SF(10) - SF(136), buttonsY, SF(136), SF(32));
    rects.guide = RectF(rects.defaults.X - SF(10) - SF(104), buttonsY, SF(104), SF(32));

    const int count = MapRowCount();
    const int leftCount = (count + 1) / 2;
    const float pad = SF(14);
    const float gap = SF(18);
    const float columnWidth = (rects.card.Width - 2 * pad - gap) / 2;
    const float rowHeight = SF(26);
    const float rowGap = SF(2);
    const float top = rects.card.Y + SF(30);
    float rowsBottom = top;

    for (int i = 0; i < count; ++i) {
        const int column = i < leftCount ? 0 : 1;
        const int row = column == 0 ? i : i - leftCount;
        const float x = rects.card.X + pad + column * (columnWidth + gap);
        const float y = top + row * (rowHeight + rowGap);
        rects.rows.emplace_back(x, y, columnWidth, rowHeight);
        rowsBottom = std::max(rowsBottom, y + rowHeight);
        // The player tab's rows switch or choose; there is nothing to clear on them.
        if (g_mapTab == 3) rects.clears.emplace_back(0.0f, 0.0f, 0.0f, 0.0f);
        else rects.clears.emplace_back(x + columnWidth - SF(25), y + SF(3), SF(20), rowHeight - SF(6));
    }

    float bandBottom = rects.card.Y + rects.card.Height - SF(12);
    if (g_mapTab == 1 || g_mapTab == 3) {
        const float areaTop = rects.card.Y + rects.card.Height - SF(92);
        const float left = rects.card.X + SF(16);
        const float half = (rects.card.Width - SF(32) - SF(24)) / 2;
        rects.deadzoneTrack = RectF(left, areaTop + SF(42), half, SF(10));
        rects.deadzoneHit = RectF(left - SF(8), areaTop + SF(28), half + SF(16), SF(30));
        rects.speedTrack = RectF(left + half + SF(24), areaTop + SF(42), half, SF(10));
        rects.speedHit = RectF(left + half + SF(16), areaTop + SF(28), half + SF(16), SF(30));
        rects.sliderSlot = RectF(rects.card.X + SF(10), areaTop + SF(12), rects.card.Width - SF(20), SF(72));
        bandBottom = rects.sliderSlot.Y - SF(8);
    }

    // Under the rows: the note card, and for the mouse and player tabs an illustration.
    const float bandTop = rowsBottom + SF(12);
    const float fullWidth = rects.card.Width - SF(32);
    if (g_mapTab == 1) {
        rects.info = RectF(rects.card.X + SF(16), bandTop, fullWidth, SF(80));
    } else if (g_mapTab == 2) {
        rects.info = RectF(rects.card.X + SF(16), bandTop, fullWidth, SF(78));
        rects.art = RectF(rects.card.X + SF(16), rects.info.Y + rects.info.Height + SF(10), fullWidth,
                          bandBottom - (rects.info.Y + rects.info.Height + SF(10)));
    } else if (g_mapTab == 3) {
        const float infoWidth = fullWidth * 0.56f;
        rects.info = RectF(rects.card.X + SF(16), bandTop, infoWidth, bandBottom - bandTop);
        rects.art = RectF(rects.info.X + infoWidth + SF(12), bandTop, fullWidth - infoWidth - SF(12), bandBottom - bandTop);
    }
    return rects;
}

enum class MapHitKind { None, Tab, Row, Clear, Guide, Defaults, Close, Deadzone, Speed, Gyro };
struct MapHit {
    MapHitKind kind = MapHitKind::None;
    int index = 0;
};

MapHit HitTestMap(const MapRects& rects, int x, int y) {
    for (int i = 0; i < 4; ++i) {
        if (rects.tabs[i].Contains(static_cast<REAL>(x), static_cast<REAL>(y))) {
            return {MapHitKind::Tab, i};
        }
    }
    if (g_mapTab == 1) {
        if (rects.deadzoneHit.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) return {MapHitKind::Deadzone, 0};
        if (rects.speedHit.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) return {MapHitKind::Speed, 0};
    }
    if (g_mapTab == 3 && rects.deadzoneHit.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) {
        return {MapHitKind::Gyro, 0};
    }
    for (size_t i = 0; i < rects.rows.size(); ++i) {
        if (rects.clears[i].Contains(static_cast<REAL>(x), static_cast<REAL>(y))) {
            return {MapHitKind::Clear, static_cast<int>(i)};
        }
        if (rects.rows[i].Contains(static_cast<REAL>(x), static_cast<REAL>(y))) {
            return {MapHitKind::Row, static_cast<int>(i)};
        }
    }
    if (rects.guide.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) return {MapHitKind::Guide, 0};
    if (rects.defaults.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) return {MapHitKind::Defaults, 0};
    if (rects.close.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) return {MapHitKind::Close, 0};
    return {};
}

void MapSlider(Graphics& g, const RectF& track, float fraction, bool hot) {
    SliderBody(g, track, fraction, true, hot, SF(18));
}

// Slider values are rounded to what the runtime's own parser shows in the guide:
// deadzone in hundredths, speed in twentieths. Dragging updates memory only; the write
// happens once on release so the disk is not hit per pixel.
void SetMapSliderValue(const MapRects& rects, int x) {
    if (g_mapSlider == 3) {
        const float fraction = std::clamp(
            (static_cast<float>(x) - rects.deadzoneTrack.X) / rects.deadzoneTrack.Width, 0.0f, 1.0f);
        g_mapGyro = std::round((0.25f + fraction * 3.75f) * 20.0f) / 20.0;
        wchar_t text[64]{};
        swprintf_s(text, L"Gyro speed %.2fx. Release to save.", g_mapGyro);
        g_mapStatus = text;
    } else if (g_mapSlider == 1) {
        const float fraction = std::clamp(
            (static_cast<float>(x) - rects.deadzoneTrack.X) / rects.deadzoneTrack.Width, 0.0f, 1.0f);
        g_mapDeadzone = std::round(fraction * 90.0f) / 100.0;
        wchar_t text[64]{};
        swprintf_s(text, L"Stick deadzone %.2f. Release to save.", g_mapDeadzone);
        g_mapStatus = text;
    } else if (g_mapSlider == 2) {
        const float fraction = std::clamp(
            (static_cast<float>(x) - rects.speedTrack.X) / rects.speedTrack.Width, 0.0f, 1.0f);
        g_mapSpeed = std::round((0.25f + fraction * 3.75f) * 20.0f) / 20.0;
        wchar_t text[64]{};
        swprintf_s(text, L"Stick speed %.2fx. Release to save.", g_mapSpeed);
        g_mapStatus = text;
    }
}

void CommitMapSliders() {
    if (g_mapSlider == 0) return;
    wchar_t value[32]{};
    if (g_mapSlider == 3) {
        swprintf_s(value, L"%.2f", g_mapGyro);
        if (ApplyPlayerValue(L"gyro_speed", value)) g_mapStatus = std::wstring(L"Gyro speed saved as ") + value + L"x.";
    } else if (g_mapSlider == 1) {
        swprintf_s(value, L"%.2f", g_mapDeadzone);
        if (WriteControllerValue(L"pad", L"deadzone", value)) {
            g_mapStatus = std::wstring(L"Stick deadzone saved as ") + value + L".";
        }
    } else {
        swprintf_s(value, L"%.2f", g_mapSpeed);
        if (WriteControllerValue(L"pad", L"stick_speed", value)) {
            g_mapStatus = std::wstring(L"Stick speed saved as ") + value + L"x.";
        }
    }
    g_mapSlider = 0;
}

// Draws one frame into g, width x height pixels. The window's paint handler and the headless
// --render command both call this, so a render shows exactly what the window shows.
// What a mouse row shows: the label of the Wii button it presses, as the picker lists it.
std::wstring MouseValueLabel(int index) {
    const std::wstring& value = g_mapMouseValues[static_cast<size_t>(index)];
    std::wstring shown = L"none";
    for (const PadChoice& choice : kMouseChoices) {
        if (value == choice.value) shown = choice.label;
    }
    if (value == L"none") shown = L"None";
    return shown;
}

void RenderControls(Graphics& g, int width, int height) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    DrawBackdrop(g, width, height);

    const float margin = SF(26);
    Banner(g, L"Controls", margin - SF(4), SF(8), SF(36));
    Text(g, g_mapTab == 0   ? L"Click a row, then press a key. Every change is saved to controllers.ini."
            : g_mapTab == 1 ? L"Click a row, then press a pad button, or right-click a row to choose from the list."
            : g_mapTab == 2 ? L"Click a row to choose what that mouse button presses. Saved to controllers.ini at once."
                            : L"Click a row to switch it or choose a value. Saved to controllers.ini at once.",
         g_fontSmall, Th.bgTextDim,
         RectF(margin + SF(2), SF(50), static_cast<float>(width) - 2 * margin, SF(18)), StringAlignmentNear, true);

    const MapRects rects = BuildMapRects(static_cast<float>(width), static_cast<float>(height));
    const wchar_t* tabLabels[4] = {L"Keyboard", L"Gamepad", L"Mouse", L"Player"};
    // Idle tabs go behind the card, the selected one in front of it.
    for (int i = 0; i < 4; ++i) {
        if (i != g_mapTab) Tab(g, rects.tabs[i], tabLabels[i], false, g_mapHotTab == i);
    }

    std::wstring padStatus;
    if (g_mapTab != 1) {
        padStatus = L"controllers.ini beside the console";
    } else if (g_mapPads.empty()) {
        padStatus = L"No gamepad connected - right-click a row to choose";
    } else {
        padStatus = L"Pad: " + g_mapPads[0].name;
    }
    const float statusX = rects.tabs[3].X + rects.tabs[3].Width + SF(20);
    Text(g, padStatus, g_fontSmall, Th.bgTextMuted,
         RectF(statusX, rects.tabs[0].Y + SF(8), static_cast<float>(width) - margin - statusX, SF(24)),
         StringAlignmentFar, true);

    Panel(g, rects.card, SF(14));
    Tab(g, rects.tabs[g_mapTab], tabLabels[g_mapTab], true, false);

    const bool keyboard = g_mapTab == 0;
    const int count = MapRowCount();
    const int leftCount = (count + 1) / 2;
    if (!rects.rows.empty()) {
        const float columnX[2] = {rects.rows[0].X, rects.rows[static_cast<size_t>(leftCount < count ? leftCount : 0)].X};
        const int columns = leftCount < count ? 2 : 1;
        for (int c = 0; c < columns; ++c) {
            Text(g, g_mapTab == 3 ? L"SETTING" : g_mapTab == 2 ? L"MOUSE BUTTON" : L"ACTION", g_fontTiny, Th.cardTextMuted,
                 RectF(columnX[c] + SF(10), rects.card.Y + SF(9), SF(120), SF(16)));
            Text(g, keyboard ? L"KEYS" : g_mapTab == 2 ? L"PRESSES" : g_mapTab == 3 ? L"VALUE" : L"HOST BUTTON", g_fontTiny, Th.cardTextMuted,
                 RectF(columnX[c] + SF(130), rects.card.Y + SF(9), rects.rows[0].Width - SF(140), SF(16)),
                 StringAlignmentFar);
        }
    }

    for (int i = 0; i < count; ++i) {
        const RectF& row = rects.rows[static_cast<size_t>(i)];
        const bool listening = g_mapListening == i;
        const bool hot = g_mapHotRow == i;
        if (listening) {
            LinearGradientBrush fill(row, Th.rowListenA, Th.rowListenB, LinearGradientModeHorizontal);
            GraphicsPath path;
            RoundRectPath(path, row, SF(8));
            g.FillPath(&fill, &path);
            StrokeRound(g, Deflate(row, SF(1.0f)), SF(8), Th.rowListenBorder, std::max(2.0f, SF(2.0f)));
        } else {
            Slot(g, row, SF(8), hot);
        }

        const wchar_t* rowLabel = g_mapTab == 0   ? kKeyActions[i].label
                                  : g_mapTab == 1 ? kPadActions[i].label
                                  : g_mapTab == 2 ? kMouseActions[i].label
                                                  : kPlayerRows[i].label;
        Text(g, rowLabel, g_fontBody, Th.rowText, RectF(row.X + SF(12), row.Y, row.Width * 0.42f, row.Height),
             StringAlignmentNear, true);

        std::wstring value, shown;
        if (g_mapTab == 2) {
            value = g_mapMouseValues[static_cast<size_t>(i)];
            shown = MouseValueLabel(i);
        } else if (g_mapTab == 3) {
            shown = PlayerRowText(i);
            value = shown == L"Off" || shown == L"None" ? L"none" : shown;
        } else {
            value = keyboard ? g_mapKeyValues[static_cast<size_t>(i)] : g_mapPadValues[static_cast<size_t>(i)];
            shown = listening ? (keyboard ? L"Press a key..." : L"Press a pad button...")
                              : (keyboard ? PrettyKeyList(value) : PrettyPadName(value));
        }

        // The value is a key cap (a keyboard list becomes one cap per key); On and Off are
        // green and grey caps, and an empty binding is a pale cap.
        Cap style = listening ? Cap::Listening : (value == L"none" ? Cap::None : Cap::Plain);
        if (g_mapTab == 3 && !listening) {
            if (shown == L"On") style = Cap::On;
            else if (shown == L"Off") style = Cap::Off;
        }
        std::vector<std::wstring> parts;
        if (keyboard && !listening && style != Cap::None) {
            size_t start = 0;
            while (true) {
                const size_t cut = shown.find(L", ", start);
                if (cut == std::wstring::npos) {
                    parts.push_back(shown.substr(start));
                    break;
                }
                parts.push_back(shown.substr(start, cut - start));
                start = cut + 2;
            }
        } else {
            parts.push_back(shown);
        }
        const float capHeight = row.Height - SF(6);
        const float capY = row.Y + SF(3);
        float capX = g_mapTab == 3 ? row.X + row.Width - SF(10) : rects.clears[static_cast<size_t>(i)].X - SF(6);
        for (size_t k = parts.size(); k-- > 0;) {
            const float w = CapWidth(g, parts[k]);
            capX -= w;
            KeyCap(g, RectF(capX, capY, w, capHeight), parts[k], style);
            capX -= SF(5);
        }

        if (g_mapTab == 3) continue;
        const RectF& clear = rects.clears[static_cast<size_t>(i)];
        const bool clearHot = g_mapHotClear == i;
        const float d = std::min(clear.Width, clear.Height);
        const RectF disc(clear.X + (clear.Width - d) / 2, clear.Y + (clear.Height - d) / 2, d, d);
        FillRound(g, disc, d / 2, clearHot ? Th.clearHotFill : Th.clearFill);
        StrokeRound(g, Deflate(disc, SF(0.5f)), d / 2, clearHot ? Th.rowListenBorder : Th.rowBorder, 1.0f);
        Text(g, L"×", g_fontSmall, clearHot ? Th.clearHotText : Th.clearText, clear, StringAlignmentCenter);
    }

    // ---- notes and illustrations below the rows ----
    if (g_mapTab == 1) {
        InfoCard(g, rects.info,
                 {L"Host button names follow position on a Nintendo pad: a right, b bottom, x top, y left.",
                  L"On an Xbox pad that is B, A, Y and X. Auto uses the layout; None leaves the Wii button unused.",
                  L"Left and right triggers are zl and zr."});
    } else if (g_mapTab == 2) {
        InfoCard(g, rects.info,
                 {L"Left and right press the remote's A and B unless changed. The pointer itself is set on the Player tab.",
                  L"Side buttons are the back and forward buttons of a mouse; the wheel click is the middle button.",
                  L"Changing a mouse button here does not touch the keyboard or gamepad bindings."});
        std::wstring shown[5];
        for (int i = 0; i < 5; ++i) shown[i] = MouseValueLabel(i);
        DrawMouseDiagram(g, rects.art, shown);
    } else if (g_mapTab == 3) {
        InfoCard(g, rects.info,
                 {L"Rune Factory: Tides of Destiny needs the Nunchuk (attachment: Nunchuk) for walking and menus.",
                  L"Keyboard, mouse and gamepad can drive player 1 together. Pointer lists what moves the on-screen pointer.",
                  L"The recenter button brings the gyro pointer back to the middle. F2 and F3 still change the attachment in play."});
        DrawRemoteDiagram(g, rects.art);
    }

    if (g_mapTab == 1) {
        Slot(g, rects.sliderSlot, SF(10));
        const float labelY = rects.deadzoneTrack.Y - SF(22);
        wchar_t value[64]{};
        swprintf_s(value, L"%.2f", g_mapDeadzone);
        Text(g, L"Stick deadzone", g_fontSmall, Th.cardTextDim,
             RectF(rects.deadzoneTrack.X, labelY, rects.deadzoneTrack.Width * 0.7f, SF(18)), StringAlignmentNear, true);
        Text(g, value, g_fontSmall, g_mapSlider == 1 ? Th.accentText : Th.cardTextDim,
             RectF(rects.deadzoneTrack.X + rects.deadzoneTrack.Width * 0.7f, labelY,
                   rects.deadzoneTrack.Width * 0.3f, SF(18)), StringAlignmentFar, true);
        swprintf_s(value, L"%.2fx", g_mapSpeed);
        Text(g, L"Stick speed", g_fontSmall, Th.cardTextDim,
             RectF(rects.speedTrack.X, labelY, rects.speedTrack.Width * 0.7f, SF(18)), StringAlignmentNear, true);
        Text(g, value, g_fontSmall, g_mapSlider == 2 ? Th.accentText : Th.cardTextDim,
             RectF(rects.speedTrack.X + rects.speedTrack.Width * 0.7f, labelY,
                   rects.speedTrack.Width * 0.3f, SF(18)), StringAlignmentFar, true);

        MapSlider(g, rects.deadzoneTrack, static_cast<float>(g_mapDeadzone / 0.9),
                  g_mapSlider == 1 || g_mapHotSlider == 1);
        MapSlider(g, rects.speedTrack, static_cast<float>((g_mapSpeed - 0.25) / 3.75),
                  g_mapSlider == 2 || g_mapHotSlider == 2);
        Text(g, L"Ignore stick drift, then scale how far the stick has to travel.",
             g_fontTiny, Th.cardTextMuted,
             RectF(rects.deadzoneTrack.X, rects.deadzoneTrack.Y + SF(16), rects.deadzoneTrack.Width, SF(14)),
             StringAlignmentNear, true);
        Text(g, L"Applied once before the layout and pointer see the stick.",
             g_fontTiny, Th.cardTextMuted,
             RectF(rects.speedTrack.X, rects.speedTrack.Y + SF(16), rects.speedTrack.Width, SF(14)),
             StringAlignmentNear, true);
    }

    if (g_mapTab == 3) {
        Slot(g, rects.sliderSlot, SF(10));
        const float labelY = rects.deadzoneTrack.Y - SF(22);
        wchar_t value[64]{};
        swprintf_s(value, L"%.2fx", g_mapGyro);
        Text(g, L"Gyro pointer speed", g_fontSmall, Th.cardTextDim,
             RectF(rects.deadzoneTrack.X, labelY, rects.deadzoneTrack.Width * 0.7f, SF(18)), StringAlignmentNear, true);
        Text(g, value, g_fontSmall, g_mapSlider == 3 ? Th.accentText : Th.cardTextDim,
             RectF(rects.deadzoneTrack.X + rects.deadzoneTrack.Width * 0.7f, labelY,
                   rects.deadzoneTrack.Width * 0.3f, SF(18)), StringAlignmentFar, true);
        MapSlider(g, rects.deadzoneTrack, static_cast<float>((g_mapGyro - 0.25) / 3.75),
                  g_mapSlider == 3 || g_mapHotSlider == 3);
        Text(g, L"How far the pointer moves for each turn of a gamepad's gyro (0.25 to 4).", g_fontTiny, Th.cardTextMuted,
             RectF(rects.deadzoneTrack.X, rects.deadzoneTrack.Y + SF(16), rects.deadzoneTrack.Width * 2, SF(14)),
             StringAlignmentNear, true);
    }

    Button(g, rects.guide, L"Open guide", false, g_mapHotButton == 1, false, true, false, g_fontSmall);
    Button(g, rects.defaults, L"Restore defaults", false, g_mapHotButton == 2, false, true, false, g_fontSmall);
    Button(g, rects.close, L"Close", false, g_mapHotButton == 3, false, true, false, g_fontSmall);
    Text(g, g_mapStatus, g_fontSmall, Th.bgTextDim,
         RectF(margin, rects.guide.Y, rects.guide.X - margin - SF(16), rects.guide.Height),
         StringAlignmentNear, true);
}

void PaintControls(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0) {
        EndPaint(hwnd, &paint);
        return;
    }

    Bitmap back(width, height, PixelFormat32bppPARGB);
    Graphics g(&back);
    RenderControls(g, width, height);

    Graphics screen(dc);
    screen.DrawImage(&back, 0, 0);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK ControlsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        ApplyDarkFrame(window);
        g_mapTrackingLeave = false;
        SetTimer(window, 1, 33, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam != 1) return 0;
        if (g_sdl.update) g_sdl.update();
        else if (g_sdl.pump) g_sdl.pump();
        ++g_mapTicks;
        if (g_mapTab == 1 && g_mapListening >= 0) {
            PollPadCapture();
            InvalidateRect(window, nullptr, FALSE);
        } else if (g_mapPads.empty() && g_mapTicks % 60 == 0) {
            RefreshMapPads();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_DEVICECHANGE:
        if (g_mapListening < 0) RefreshMapPads();
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintControls(window);
        return 0;

    case WM_MOUSEMOVE: {
        RECT client{};
        GetClientRect(window, &client);
        const MapRects rects = BuildMapRects(static_cast<float>(client.right), static_cast<float>(client.bottom));
        const MapHit hit = HitTestMap(rects, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

        int tab = -1, row = -1, clear = -1, button = 0, slider = 0;
        if (hit.kind == MapHitKind::Tab) tab = hit.index;
        else if (hit.kind == MapHitKind::Row) row = hit.index;
        else if (hit.kind == MapHitKind::Clear) clear = hit.index;
        else if (hit.kind == MapHitKind::Guide) button = 1;
        else if (hit.kind == MapHitKind::Defaults) button = 2;
        else if (hit.kind == MapHitKind::Close) button = 3;
        else if (hit.kind == MapHitKind::Deadzone) slider = 1;
        else if (hit.kind == MapHitKind::Speed) slider = 2;
        else if (hit.kind == MapHitKind::Gyro) slider = 3;

        if (g_mapSlider != 0 && (wParam & MK_LBUTTON)) {
            SetMapSliderValue(rects, GET_X_LPARAM(lParam));
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }

        if (tab != g_mapHotTab || row != g_mapHotRow || clear != g_mapHotClear || button != g_mapHotButton ||
            slider != g_mapHotSlider) {
            g_mapHotTab = tab;
            g_mapHotRow = row;
            g_mapHotClear = clear;
            g_mapHotButton = button;
            g_mapHotSlider = slider;
            InvalidateRect(window, nullptr, FALSE);
        }
        if (!g_mapTrackingLeave) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            g_mapTrackingLeave = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_mapHotTab = g_mapHotRow = g_mapHotClear = -1;
        g_mapHotButton = 0;
        g_mapHotSlider = 0;
        g_mapTrackingLeave = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        SetFocus(window);
        RECT client{};
        GetClientRect(window, &client);
        const MapRects rects = BuildMapRects(static_cast<float>(client.right), static_cast<float>(client.bottom));
        const MapHit hit = HitTestMap(rects, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        switch (hit.kind) {
        case MapHitKind::Tab:
            g_mapTab = hit.index;
            g_mapListening = -1;
            g_mapHotRow = g_mapHotClear = -1;
            g_mapHotSlider = 0;
            if (g_mapSlider != 0) {
                CommitMapSliders();
                ReleaseCapture();
            }
            g_mapStatus = g_mapTab == 0   ? L"Click a row, then press a key."
                          : g_mapTab == 1 ? L"Click a row, then press a pad button."
                          : g_mapTab == 2 ? L"Click a row to choose what that mouse button presses."
                                          : L"Click a row to switch it or choose a value.";
            break;
        case MapHitKind::Row:
            if (g_mapTab == 2) {
                ChooseMouseBinding(hit.index, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            } else if (g_mapTab == 3) {
                ActivatePlayerRow(hit.index, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            } else if (g_mapTab == 0) {
                StartListening(hit.index, false);
            } else if (g_mapPads.empty()) {
                ChoosePadBinding(hit.index, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            } else {
                StartListening(hit.index, true);
            }
            break;
        case MapHitKind::Clear:
            if (g_mapTab == 0) ApplyKeyBinding(hit.index, L"none");
            else if (g_mapTab == 2) ApplyMouseBinding(hit.index, L"none");
            else if (g_mapTab == 1) ApplyPadBinding(hit.index, L"none");
            g_mapListening = -1;
            break;
        case MapHitKind::Guide:
            OpenControlGuide();
            break;
        case MapHitKind::Defaults:
            RestoreControlDefaults();
            break;
        case MapHitKind::Deadzone:
        case MapHitKind::Speed:
        case MapHitKind::Gyro:
            g_mapSlider = hit.kind == MapHitKind::Deadzone ? 1 : hit.kind == MapHitKind::Speed ? 2 : 3;
            g_mapHotSlider = g_mapSlider;
            SetCapture(window);
            SetMapSliderValue(rects, GET_X_LPARAM(lParam));
            break;
        case MapHitKind::Close:
            DestroyWindow(window);
            return 0;
        default:
            if (g_mapListening >= 0) {
                g_mapListening = -1;
                g_mapStatus = L"Binding cancelled.";
            }
            break;
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_mapSlider != 0) {
            CommitMapSliders();
            ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (g_mapSlider != 0 && reinterpret_cast<HWND>(lParam) != window) {
            CommitMapSliders();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_RBUTTONDOWN: {
        if (g_mapTab != 1) return 0;
        RECT client{};
        GetClientRect(window, &client);
        const MapRects rects = BuildMapRects(static_cast<float>(client.right), static_cast<float>(client.bottom));
        const MapHit hit = HitTestMap(rects, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hit.kind == MapHitKind::Row || hit.kind == MapHitKind::Clear) {
            g_mapListening = -1;
            ChoosePadBinding(hit.index, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(window, &p);
            RECT client{};
            GetClientRect(window, &client);
            const MapRects rects = BuildMapRects(static_cast<float>(client.right), static_cast<float>(client.bottom));
            if (HitTestMap(rects, p.x, p.y).kind != MapHitKind::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (g_mapTab == 0 && g_mapListening >= 0) {
            if (wParam == VK_ESCAPE) {
                g_mapListening = -1;
                g_mapStatus = L"Binding cancelled.";
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            const std::wstring name = KeyNameFromVirtualKey(wParam, lParam);
            if (name.empty()) {
                g_mapStatus = L"That key has no name in controllers.ini. Try a letter, digit, F1 to F12, or an arrow.";
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            ApplyKeyBinding(g_mapListening, name);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, 1);
        CloseMapPads();
        g_mapWindow = nullptr;
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// The folder holding the console, where controllers.ini lives.
std::wstring ConsoleDirectory() {
    std::wstring directory = g_config.console;
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash != std::wstring::npos) directory.resize(slash);
    return directory;
}

// Headless: --dump-controls [file] loads controllers.ini the way the editor does and
// writes every value it would show, to stdout and logs\controls-dump.txt. Exit 0, or 3
// when the dump could not be written.
int DumpControls(const std::wstring& explicitPath) {
    g_mapConfigPath = explicitPath.empty() ? Join(ConsoleDirectory(), L"controllers.ini") : explicitPath;
    ReloadControls();
    std::wstring out = L"file: " + g_mapConfigPath + (Exists(g_mapConfigPath) ? L"\n" : L" (missing: built-in defaults)\n");
    out += L"[keys]\n";
    for (int i = 0; i < kKeyActionCount; ++i) out += std::wstring(kKeyActions[i].key) + L" = " + g_mapKeyValues[i] + L"\n";
    out += L"[pad]\n";
    for (int i = 0; i < kPadActionCount; ++i) out += std::wstring(kPadActions[i].key) + L" = " + g_mapPadValues[i] + L"\n";
    wchar_t number[32]{};
    swprintf_s(number, L"%.2f", g_mapDeadzone);
    out += std::wstring(L"deadzone = ") + number + L"\n";
    swprintf_s(number, L"%.2f", g_mapSpeed);
    out += std::wstring(L"stick_speed = ") + number + L"\n";
    out += L"[mouse]\n";
    for (int i = 0; i < kMouseActionCount; ++i) out += std::wstring(kMouseActions[i].key) + L" = " + g_mapMouseValues[i] + L"\n";
    out += L"[player1]\n";
    for (int i = 0; i < 7; ++i) {
        std::wstring value = g_mapPlayer[i];
        if (wcscmp(kPlayerDefaults[i].key, L"gyro_speed") == 0) {
            swprintf_s(number, L"%.2f", g_mapGyro);
            value = number;
        }
        out += std::wstring(kPlayerDefaults[i].key) + L" = " + value + L"\n";
    }
    const std::string text = Narrow(out);
    CreateDirectoryW(Join(g_root, L"logs").c_str(), nullptr);
    bool wrote = false;
    HANDLE file = CreateFileW(Join(Join(g_root, L"logs"), L"controls-dump.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        wrote = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
        CloseHandle(file);
    }
    HANDLE std_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!std_out || std_out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            std_out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (std_out && std_out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(std_out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    }
    return wrote ? 0 : 3;
}

void OpenControls() {
    if (g_mapWindow) {
        ShowWindow(g_mapWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_mapWindow);
        return;
    }
    std::wstring directory = ConsoleDirectory();
    if (!Exists(directory)) {
        MessageBoxW(g_window, L"The console folder is missing, so controllers.ini cannot be edited.\n"
                              L"Run the build steps in README.md first.",
                    kMapTitle, MB_ICONWARNING | MB_OK);
        return;
    }
    g_mapConfigPath = Join(directory, L"controllers.ini");
    ReloadControls();
    g_mapTab = 0;
    g_mapListening = -1;
    g_mapTicks = 0;
    g_mapHotTab = g_mapHotRow = g_mapHotClear = -1;
    g_mapHotButton = 0;
    g_mapSlider = 0;
    g_mapHotSlider = 0;
    g_mapStatus = L"Changes apply the next time the game starts.";

    CloseMapPads();
    if (LoadSdl()) {
        if (!g_sdl.initialized && g_sdl.init) {
            if (g_sdl.init(0x00002000u)) g_sdl.initialized = true;
        }
        if (g_sdl.initialized) RefreshMapPads();
    }

    RECT rect{0, 0, S(kMapWidth), S(kMapHeight)};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    RECT owner{};
    GetWindowRect(g_window, &owner);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = owner.left + ((owner.right - owner.left) - width) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;

    g_mapWindow = CreateWindowExW(0, kMapClass, kMapTitle, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  x, y, width, height, g_window, nullptr, g_instance, nullptr);
    if (!g_mapWindow) {
        CloseMapPads();
        MessageBoxW(g_window, L"Could not open the controls window.", kMapTitle, MB_ICONERROR | MB_OK);
        return;
    }
    ShowWindow(g_mapWindow, SW_SHOWNORMAL);
    UpdateWindow(g_mapWindow);
    SetForegroundWindow(g_mapWindow);
}

// ---- interaction ----

void BuildWidgets() {
    g_widgets.clear();
    RECT client{};
    GetClientRect(g_window, &client);
    const Layout l = MakeLayout(static_cast<float>(client.right));

    g_widgets.push_back({ID_QUIT, Kind::Button, l.quit});
    g_widgets.push_back({ID_SETTINGS, Kind::Button, l.gear});

    if (g_page == Page::Settings) {
        g_widgets.push_back({ID_FULLSCREEN, Kind::Toggle, l.fullscreenSwitch});
        g_widgets.push_back({ID_FSR, Kind::Toggle, l.fsrSwitch});
        g_widgets.push_back({ID_OVERLAY, Kind::Toggle, l.overlaySwitch});
        g_widgets.push_back({ID_SHARPNESS, Kind::Slider, l.sharpnessHit});
        for (int i = 0; i < 4; ++i) g_widgets.push_back({ID_SCALE_1 + i, Kind::Segment, l.scale[i]});
        for (int i = 0; i < 4; ++i) g_widgets.push_back({ID_SIZE_720 + i, Kind::Segment, l.size[i]});
        for (int i = 0; i < 3; ++i) g_widgets.push_back({ID_PRESENT_FIFO + i, Kind::Segment, l.present[i]});
        g_widgets.push_back({ID_CONTROLS, Kind::Button, l.controls});
        g_widgets.push_back({ID_INPUT_TEST, Kind::Button, l.testInput});
        g_widgets.push_back({ID_BACK, Kind::Button, l.back});
        g_widgets.push_back({ID_DONE, Kind::Button, l.done});
        return;
    }

    if (g_page == Page::GameFiles) {
        g_widgets.push_back({ID_USE_IMAGE, Kind::Button, l.useImage});
        g_widgets.push_back({ID_OPEN_IMAGE, Kind::Button, l.openImage});
        g_widgets.push_back({ID_USE_FOLDER, Kind::Button, l.useFolder});
        g_widgets.push_back({ID_OPEN_DISC, Kind::Button, l.openDisc});
        g_widgets.push_back({ID_OPEN_TITLE, Kind::Button, l.openTitle});
        g_widgets.push_back({ID_USE_CONSOLE, Kind::Button, l.useConsole});
        g_widgets.push_back({ID_OPEN_CONSOLE, Kind::Button, l.openConsole});
        g_widgets.push_back({ID_BACK, Kind::Button, l.back});
        g_widgets.push_back({ID_REBUILD, Kind::Button, l.rebuild});
        g_widgets.push_back({ID_PLAY, Kind::Button, l.play});
        return;
    }

    if (g_page == Page::Build) {
        g_widgets.push_back({ID_BACK, Kind::Button, l.back});
        if (g_building) g_widgets.push_back({ID_BUILD_HIDE, Kind::Button, l.buildHide});
        g_widgets.push_back({ID_BUILD_LOG, Kind::Button, l.buildLog});
        g_widgets.push_back({ID_PLAY, Kind::Button, l.buildPlay});
        return;
    }

    g_widgets.push_back({ID_PLAY, Kind::Button, l.play});
    if (Ready()) g_widgets.push_back({ID_HERO_FILES, Kind::Button, l.heroFiles});
    if (g_crash.pending) {
        g_widgets.push_back({ID_CRASH_OPEN, Kind::Button, l.crashOpen});
        g_widgets.push_back({ID_CRASH_DISMISS, Kind::Button, l.crashDismiss});
    } else if (ImageStale() && !g_building) {
        g_widgets.push_back({ID_HERO_REBUILD, Kind::Button, l.heroRebuild});
    }
    g_widgets.push_back({ID_RESCAN, Kind::Button, l.rescan});
    g_widgets.push_back({ID_INPUT_TEST, Kind::Button, l.testInput});
    g_widgets.push_back({ID_GAMEFILES, Kind::Button, l.gameFiles});
    g_widgets.push_back({ID_REBUILD, Kind::Button, l.rebuild});
    g_widgets.push_back({ID_CONTROLS, Kind::Button, l.controls});
    g_widgets.push_back({ID_SAVES, Kind::Button, l.saves});
    g_widgets.push_back({ID_LOGS, Kind::Button, l.logs});
    g_widgets.push_back({ID_CONFIG, Kind::Button, l.config});
    g_widgets.push_back({ID_PLAY_LOG, Kind::Button, l.playLog});
    g_widgets.push_back({ID_CARD_SETTINGS, Kind::Button, l.settingsButton});
}

Widget* FindWidget(int id) {
    for (Widget& w : g_widgets)
        if (w.id == id) return &w;
    return nullptr;
}

int WidgetAt(int x, int y) {
    for (const Widget& w : g_widgets)
        if (w.rect.Contains(static_cast<REAL>(x), static_cast<REAL>(y))) return w.id;
    return 0;
}

void SetSharpnessFromX(int x) {
    RECT client{};
    GetClientRect(g_window, &client);
    const Layout l = MakeLayout(static_cast<float>(client.right));
    const double t = ClampDouble((x - l.sharpnessTrack.X) / l.sharpnessTrack.Width, 0.0, 1.0);
    g_config.sharpness = std::round(t * 40.0) / 20.0;  // 0.05 steps
    RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
}

void StopBuild() {
    if (g_buildJob) {
        TerminateJobObject(g_buildJob, 1);
        CloseHandle(g_buildJob);
        g_buildJob = nullptr;
    }
    if (g_buildProcess) {
        WaitForSingleObject(g_buildProcess, 2000);
        CloseHandle(g_buildProcess);
        g_buildProcess = nullptr;
    }
    if (g_building) {
        g_building = false;
        if (g_window) KillTimer(g_window, 2);
    }
}

// The child PowerShell writes whatever its console would show. Windows PowerShell on a
// hidden handle uses UTF-16 when it prints through the host, and the scripts' python
// steps use UTF-8 or the ANSI code page; the first bytes decide which reader to use.
std::wstring DecodeBuildBytes(const std::string& bytes) {
    g_buildCarry.append(bytes);
    if (g_buildEncoding == 0) {
        if (g_buildCarry.size() >= 2) {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(g_buildCarry.data());
            if (p[0] == 0xFF && p[1] == 0xFE) {
                g_buildEncoding = 1;
                g_buildCarry.erase(0, 2);
            } else if (p[0] == 0xFE && p[1] == 0xFF) {
                g_buildEncoding = 3;
                g_buildCarry.erase(0, 2);
            } else if (g_buildCarry.size() >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
                g_buildEncoding = 2;
                g_buildCarry.erase(0, 3);
            } else if (g_buildCarry.size() >= 8) {
                int zeros = 0, pairs = 0;
                const size_t limit = std::min<size_t>(g_buildCarry.size(), 64);
                for (size_t i = 1; i < limit; i += 2) {
                    ++pairs;
                    if (p[i] == 0) ++zeros;
                }
                g_buildEncoding = pairs && zeros * 2 >= pairs ? 1 : 2;
            } else {
                return L"";
            }
        } else {
            return L"";
        }
    }

    if (g_buildEncoding == 1 || g_buildEncoding == 3) {
        const size_t count = g_buildCarry.size() / 2 * 2;
        std::wstring text(count / 2, L'\0');
        for (size_t i = 0; i < count; i += 2) {
            const unsigned char first = static_cast<unsigned char>(g_buildCarry[i]);
            const unsigned char second = static_cast<unsigned char>(g_buildCarry[i + 1]);
            text[i / 2] = g_buildEncoding == 1 ? static_cast<wchar_t>(first | (second << 8))
                                               : static_cast<wchar_t>((first << 8) | second);
        }
        g_buildCarry.erase(0, count);
        return text;
    }

    // UTF-8: decode complete lines, so a multi-byte character split across reads waits.
    size_t cut = g_buildCarry.find_last_of('\n');
    if (cut == std::string::npos) {
        if (g_buildCarry.size() < 4096) return L"";
        cut = g_buildCarry.size() - 1;
    }
    std::string usable = g_buildCarry.substr(0, cut + 1);
    g_buildCarry.erase(0, cut + 1);
    if (usable.empty()) return L"";
    const int length = static_cast<int>(usable.size());
    int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, usable.data(), length, nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (wide <= 0) {
        codePage = CP_ACP;
        flags = 0;
        wide = MultiByteToWideChar(codePage, flags, usable.data(), length, nullptr, 0);
    }
    if (wide <= 0) return L"";
    std::wstring text(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(codePage, flags, usable.data(), length, text.data(), wide);
    return text;
}

void ParseBuildLine(const std::wstring& raw) {
    const std::wstring line = Trim(raw);
    if (line.empty()) return;
    if (line.rfind(L"== ", 0) == 0) {
        std::wstring stage = Trim(line.substr(3));
        if (stage.size() > 2 && stage.compare(stage.size() - 2, 2, L"==") == 0)
            stage = Trim(stage.substr(0, stage.size() - 2));
        if (stage.empty()) return;
        g_buildStage = stage;
        if (stage == L"Translate and build") g_buildPercent = std::max(g_buildPercent, 16);
        else if (stage == L"build the console") g_buildPercent = std::max(g_buildPercent, 26);
        else if (stage == L"compile the console") g_buildPercent = std::max(g_buildPercent, 38);
        else if (stage == L"translate the program") g_buildPercent = std::max(g_buildPercent, 52);
        else if (stage == L"write the build file") g_buildPercent = std::max(g_buildPercent, 62);
        else if (stage == L"compile the title") g_buildPercent = std::max(g_buildPercent, 74);
        else if (stage.find(L"pack") != std::wstring::npos) g_buildPercent = std::max(g_buildPercent, 86);
        else if (stage == L"Result") g_buildPercent = std::max(g_buildPercent, 94);
        else g_buildPercent = std::min(90, g_buildPercent + 6);
    } else if (line.find(L"[ ok ]") != std::wstring::npos && line.find(L"shader recipes") != std::wstring::npos) {
        g_buildPercent = std::max(g_buildPercent, 98);
    }
}

void AppendBuildText(const std::wstring& text) {
    if (text.empty()) return;
    g_buildPartial += text;
    size_t start = 0;
    for (;;) {
        const size_t end = g_buildPartial.find(L'\n', start);
        if (end == std::wstring::npos) break;
        std::wstring line = g_buildPartial.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        start = end + 1;
        if (line.empty()) continue;
        g_buildLines.push_back(line);
        if (g_buildLines.size() > 300) g_buildLines.erase(g_buildLines.begin());
        ParseBuildLine(line);
    }
    g_buildPartial.erase(0, start);
    if (g_buildPartial.size() > 8192) {
        g_buildLines.push_back(g_buildPartial);
        if (g_buildLines.size() > 300) g_buildLines.erase(g_buildLines.begin());
        g_buildPartial.clear();
    }
}

void ReadBuildLog() {
    if (g_buildLogPath.empty()) return;
    HANDLE file = CreateFileW(g_buildLogPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0) {
        if (static_cast<unsigned long long>(size.QuadPart) < g_buildLogOffset) g_buildLogOffset = 0;
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(g_buildLogOffset);
        if (SetFilePointerEx(file, offset, nullptr, FILE_BEGIN)) {
            const unsigned long long available =
                static_cast<unsigned long long>(size.QuadPart) - g_buildLogOffset;
            const DWORD want = static_cast<DWORD>(std::min<unsigned long long>(available, 256 * 1024));
            if (want > 0) {
                std::string bytes(want, '\0');
                DWORD read = 0;
                if (ReadFile(file, bytes.data(), want, &read, nullptr) && read > 0) {
                    bytes.resize(read);
                    g_buildLogOffset += read;
                    const std::wstring text = DecodeBuildBytes(bytes);
                    if (!text.empty()) AppendBuildText(text);
                }
            }
        }
    }
    CloseHandle(file);
}

void FinishBuild() {
    DWORD exitCode = 0;
    if (g_buildProcess) {
        GetExitCodeProcess(g_buildProcess, &exitCode);
        CloseHandle(g_buildProcess);
        g_buildProcess = nullptr;
    }
    if (g_buildJob) {
        CloseHandle(g_buildJob);
        g_buildJob = nullptr;
    }
    g_building = false;
    if (g_window) KillTimer(g_window, 2);
    g_buildExitCode = static_cast<int>(exitCode);
    g_buildPercent = exitCode == 0 ? 100 : std::max(5, g_buildPercent);
    g_buildStage = exitCode == 0 ? L"Done" : L"Failed";
    // A long build can leave more than one read in the file; drain it now that the child
    // is gone, so the last lines and the exit summary are on screen.
    for (int pass = 0; pass < 32; ++pass) {
        const unsigned long long before = g_buildLogOffset;
        ReadBuildLog();
        if (g_buildLogOffset == before) break;
    }
    if (!g_buildPartial.empty()) {
        g_buildLines.push_back(g_buildPartial);
        if (g_buildLines.size() > 300) g_buildLines.erase(g_buildLines.begin());
        g_buildPartial.clear();
        ParseBuildLine(g_buildLines.back());
    }
    SetStatus(exitCode == 0
                  ? (ImageCurrent() ? L"Rebuild finished; the packed image is current."
                                    : L"Rebuild finished; check the build summary.")
                  : L"Rebuild failed (code " + std::to_wstring(exitCode) + L"); open the log for the reason.");
    // Recompute last so the Home notice follows the new image; a write failure replaces the
    // status because it must not pass unseen.
    RefreshSetup();
}

void StartBuild() {
    if (g_building) {
        GoTo(Page::Build);
        return;
    }
    if (g_running) {
        SetStatus(L"Close the game before rebuilding the title.");
        return;
    }
    const std::wstring script = Join(g_root, L"tools\\wii-recomp-build.ps1");
    if (!Exists(script)) {
        ShowMissing(L"build script", script);
        return;
    }

    g_buildLines.clear();
    g_buildPartial.clear();
    g_buildCarry.clear();
    g_buildEncoding = 0;
    g_buildLogOffset = 0;
    g_buildStage = L"Starting PowerShell";
    g_buildPercent = 5;
    g_buildExitCode = -1;

    CreateDirectoryW(Join(g_root, L"logs").c_str(), nullptr);
    g_buildLogPath = Join(Join(g_root, L"logs"), L"launcher-build.log");

    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof inherit;
    inherit.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(g_buildLogPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_window, L"Could not create logs\\launcher-build.log.", kWindowTitle, MB_ICONERROR | MB_OK);
        return;
    }

    std::wstring arguments = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script + L"\"";
    // The launcher's disc folder is the sys folder, while the build wants the extracted
    // root. Passing it keeps the build on the folder the Game files page shows.
    if (Exists(Join(g_config.disc, L"main.dol"))) arguments += L" -Disc \"" + g_config.disc + L"\"";
    const std::wstring commandLine = L"powershell.exe " + arguments;

    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = nullptr;

    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, g_root.c_str(), &startup,
                                        &process);
    CloseHandle(log);
    if (!started) {
        const DWORD error = GetLastError();
        wchar_t message[256]{};
        swprintf_s(message, L"Could not start PowerShell (error %lu).", error);
        MessageBoxW(g_window, message, kWindowTitle, MB_ICONERROR | MB_OK);
        return;
    }

    g_buildJob = CreateJobObjectW(nullptr, nullptr);
    if (g_buildJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_buildJob, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        AssignProcessToJobObject(g_buildJob, process.hProcess);
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    g_buildProcess = process.hProcess;
    g_building = true;
    SetTimer(g_window, 2, 300, nullptr);
    SetStatus(L"Rebuilding the title with tools\\wii-recomp-build.ps1.");
    GoTo(Page::Build);
}

void PollBuild() {
    if (!g_building) return;
    ReadBuildLog();
    if (g_buildProcess && WaitForSingleObject(g_buildProcess, 0) == WAIT_OBJECT_0) {
        FinishBuild();
    } else if (g_page == Page::Build) {
        InvalidateRect(g_window, nullptr, FALSE);
    }
}

std::wstring ParentFolder(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return path;
    return path.substr(0, slash);
}

void Activate(int id) {
    switch (id) {
    case ID_PLAY:
        if (g_running) {
            SetStatus(L"The game is already running.");
            break;
        }
        if (g_building) {
            SetStatus(L"The rebuild is still running; Play unlocks when it finishes.");
            break;
        }
        if (!Ready()) {
            GoTo(Page::GameFiles);
            SetStatus(L"Point at your game files, then build once.");
            break;
        }
        LaunchGame();
        break;
    case ID_HERO_REBUILD:
    case ID_REBUILD:
        if (g_running) {
            SetStatus(L"Close the game before rebuilding the title.");
            break;
        }
        if (g_building) {
            GoTo(Page::Build);
            break;
        }
        GoTo(Page::Build);
        StartBuild();
        break;
    case ID_CRASH_OPEN: {
        const std::wstring dir = g_crash.dir;
        if (!Exists(dir)) {
            SetStatus(L"The crash report folder is gone: " + dir);
        } else {
            OpenFolder(dir);
        }
        MarkCrashSeen();
        break;
    }
    case ID_CRASH_DISMISS:
        MarkCrashSeen();
        break;
    case ID_SETTINGS:
    case ID_CARD_SETTINGS:
        GoTo(Page::Settings);
        break;
    case ID_GAMEFILES:
    case ID_HERO_FILES:
        GoTo(Page::GameFiles);
        break;
    case ID_BACK:
    case ID_DONE:
    case ID_BUILD_HIDE:
        GoTo(Page::Home);
        break;
    case ID_USE_IMAGE:
        PickImage();
        break;
    case ID_USE_FOLDER:
        PickDisc();
        break;
    case ID_USE_CONSOLE:
        PickConsole();
        break;
    case ID_OPEN_IMAGE:
        if (Exists(g_config.image)) OpenFolder(ParentFolder(g_config.image));
        else ShowMissing(L"packed image", g_config.image);
        break;
    case ID_OPEN_DISC:
        if (Exists(g_config.disc)) OpenFolder(g_config.disc);
        else ShowMissing(L"extracted disc", g_config.disc);
        break;
    case ID_OPEN_TITLE:
        if (Exists(g_config.title)) OpenFolder(ParentFolder(g_config.title));
        else ShowMissing(L"native library", g_config.title);
        break;
    case ID_OPEN_CONSOLE:
        if (Exists(g_config.console)) OpenFolder(ParentFolder(g_config.console));
        else ShowMissing(L"game console", g_config.console);
        break;
    case ID_BUILD_LOG: {
        const std::wstring log =
            g_buildLogPath.empty() ? Join(Join(g_root, L"logs"), L"launcher-build.log") : g_buildLogPath;
        if (Exists(log)) OpenPath(log);
        else OpenFolder(Join(g_root, L"logs"));
        break;
    }
    case ID_SAVES:
        OpenFolder(g_config.saves);
        break;
    case ID_LOGS:
        OpenFolder(Join(g_root, L"logs"));
        break;
    case ID_PLAY_LOG: {
        const std::wstring log = NewestPlayLog();
        if (!log.empty()) OpenPath(log);
        else SetStatus(L"No play log yet; one is written each time the game starts.");
        break;
    }
    case ID_CONFIG:
        OpenFolder(Join(g_root, L"config"));
        break;
    case ID_CONTROLS:
        OpenControls();
        break;
    case ID_QUIT:
        // WM_CLOSE asks before stopping a running build.
        PostMessage(g_window, WM_CLOSE, 0, 0);
        break;
    case ID_RESCAN:
        RefreshPads(true);
        SetStatus(g_padsError ? std::wstring(L"SDL3 was not found; keyboard and mouse still work.")
                              : g_pads.empty() ? std::wstring(L"No gamepad found; keyboard and mouse still work.")
                                               : L"Found " + std::to_wstring(g_pads.size()) + L" gamepad(s).");
        break;
    case ID_INPUT_TEST:
        OpenInputTest();
        break;
    case ID_FULLSCREEN:
        g_config.fullscreen = !g_config.fullscreen;
        SaveOption(L"fullscreen", g_config.fullscreen ? L"1" : L"0");
        break;
    case ID_FSR:
        g_config.fsr = !g_config.fsr;
        SaveOption(L"fsr", g_config.fsr ? L"1" : L"0");
        break;
    case ID_OVERLAY:
        g_config.fpsOverlay = !g_config.fpsOverlay;
        SaveOption(L"overlay", g_config.fpsOverlay ? L"1" : L"0");
        break;
    case ID_SCALE_1:
    case ID_SCALE_15:
    case ID_SCALE_2:
    case ID_SCALE_3: {
        static const double values[4] = {1.0, 1.5, 2.0, 3.0};
        g_config.scale = values[id - ID_SCALE_1];
        SaveOption(L"scale", FormatDouble(g_config.scale, L"%.4g"));
        break;
    }
    case ID_SIZE_720:
    case ID_SIZE_960:
    case ID_SIZE_1200:
    case ID_SIZE_1440: {
        static const int heights[4] = {720, 1080, 1440, 2160};
        const int height = heights[id - ID_SIZE_720];
        g_config.windowHeight = height;
        g_config.windowWidth = height * g_aspectWidth / g_aspectHeight;
        wchar_t value[32]{};
        swprintf_s(value, L"%dx%d", g_config.windowWidth, g_config.windowHeight);
        SaveOption(L"window", value);
        break;
    }
    case ID_PRESENT_FIFO:
        g_config.present = L"fifo";
        SaveOption(L"present", g_config.present);
        break;
    case ID_PRESENT_MAILBOX:
        g_config.present = L"mailbox";
        SaveOption(L"present", g_config.present);
        break;
    case ID_PRESENT_IMMEDIATE:
        g_config.present = L"immediate";
        SaveOption(L"present", g_config.present);
        break;
    default:
        break;
    }
    RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
}

std::vector<std::wstring> BuildEnvironment(bool useImage) {
    std::vector<std::wstring> environment;
    LPWCH strings = GetEnvironmentStringsW();
    if (strings) {
        for (LPWCH p = strings; *p; p += std::wcslen(p) + 1) environment.emplace_back(p);
        FreeEnvironmentStringsW(strings);
    }

    const auto strip = [&environment](const wchar_t* prefix) {
        const size_t length = std::wcslen(prefix);
        environment.erase(std::remove_if(environment.begin(), environment.end(),
                                         [&](const std::wstring& entry) {
                                             return entry.size() >= length &&
                                                    _wcsnicmp(entry.c_str(), prefix, length) == 0;
                                         }),
                          environment.end());
    };

    const wchar_t* owned[] = {
        L"PATH=",
        L"WII_RECOMP_NATIVE=",
        L"WII_RECOMP_FULLSCREEN=",
        L"WII_RECOMP_FSR=",
        L"WII_RECOMP_FSR_SHARPNESS=",
        L"WII_RECOMP_FPS_OVERLAY=",
        L"WII_RECOMP_SCALE=",
        L"WII_RECOMP_WINDOW=",
        L"WII_RECOMP_PRESENT=",
    };
    for (const wchar_t* prefix : owned) strip(prefix);

    wchar_t pathBuffer[32768]{};
    const DWORD pathLength = GetEnvironmentVariableW(L"PATH", pathBuffer, ARRAYSIZE(pathBuffer));
    std::wstring path = g_config.msys;
    if (pathLength > 0 && pathLength < ARRAYSIZE(pathBuffer)) {
        path += L";";
        path += pathBuffer;
    }
    environment.push_back(L"PATH=" + path);
    // The image carries its own copy of the native library, so a packed launch extracts
    // that once into native-cache instead of reaching for the loose build beside it.
    if (!useImage) environment.push_back(L"WII_RECOMP_NATIVE=" + g_config.title);
    if (g_config.fullscreen) environment.push_back(L"WII_RECOMP_FULLSCREEN=1");

    int windowWidth = 0, windowHeight = 0;
    EffectiveWindow(windowWidth, windowHeight);
    wchar_t window[32]{};
    swprintf_s(window, L"%dx%d", windowWidth, windowHeight);
    environment.push_back(L"WII_RECOMP_WINDOW=" + std::wstring(window));

    // WII_RECOMP_SCALE takes whole numbers or fractions ("3/2"), not decimals.
    wchar_t quality[32]{};
    if (std::fabs(g_config.scale - 1.5) < 0.001) swprintf_s(quality, L"3/2");
    else if (std::fabs(g_config.scale - 2.5) < 0.001) swprintf_s(quality, L"5/2");
    else swprintf_s(quality, L"%.0f", g_config.scale);
    environment.push_back(L"WII_RECOMP_SCALE=" + std::wstring(quality));

    environment.push_back(std::wstring(L"WII_RECOMP_FSR=") + (g_config.fsr ? L"1" : L"0"));
    environment.push_back(L"WII_RECOMP_FSR_SHARPNESS=" + FormatDouble(g_config.sharpness, L"%.2f"));
    if (g_config.fpsOverlay) environment.push_back(L"WII_RECOMP_FPS_OVERLAY=1");
    environment.push_back(L"WII_RECOMP_PRESENT=" + g_config.present);
    return environment;
}

void LaunchGame() {
    if (g_running) return;

    if (!Exists(g_config.console)) { ShowMissing(L"wii-recomp.exe", g_config.console); return; }

    // A packed image holds the disc, the native code and the shader recipes in one file,
    // which is the fully pre-baked build. Use it when it's there and current; otherwise
    // play the loose disc beside the freshly built title.
    const bool packed = ImageCurrent();
    if (!packed && !Exists(g_config.title)) { ShowMissing(L"title.dll (run Rebuild)", g_config.title); return; }
    if (!packed && !Exists(g_config.disc)) { ShowMissing(L"extracted disc (sys folder)", g_config.disc); return; }
    const std::wstring target = packed ? g_config.image : g_config.disc;

    // The check and the editor read and redraw the pads; stop them before the game
    // wants them and the frame.
    if (g_inputWindow) DestroyWindow(g_inputWindow);
    if (g_mapWindow) DestroyWindow(g_mapWindow);

    std::wstring commandLine = L"\"" + g_config.console + L"\" \"" + target + L"\"";
    std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
    commandBuffer.push_back(L'\0');

    std::vector<std::wstring> environment = BuildEnvironment(packed);
    std::wstring block;
    for (const auto& entry : environment) {
        block += entry;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');

    std::wstring workingDirectory = g_config.console;
    const size_t slash = workingDirectory.find_last_of(L"\\/");
    if (slash != std::wstring::npos) workingDirectory.resize(slash);

    // stdout and stderr go straight to a per-session file through an inherited handle, so
    // the game never blocks on a pipe and no console window appears. If the file cannot be
    // created the game still starts, just without a log.
    CreateDirectoryW(Join(g_root, L"logs").c_str(), nullptr);
    CreateDirectoryW(PlayLogDir().c_str(), nullptr);
    PrunePlayLogs();
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[32]{};
    swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u.log", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
    const std::wstring logPath = Join(PlayLogDir(), stamp);
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof inherit;
    inherit.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT;
    if (log != INVALID_HANDLE_VALUE) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = nullptr;
        startup.hStdOutput = log;
        startup.hStdError = log;
        creationFlags |= CREATE_NO_WINDOW;
    }
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(g_config.console.c_str(), commandBuffer.data(), nullptr, nullptr,
                                        log != INVALID_HANDLE_VALUE ? TRUE : FALSE,
                                        creationFlags, static_cast<LPVOID>(block.data()),
                                        workingDirectory.c_str(), &startup, &process);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!started) {
        const std::wstring message = L"Could not start wii-recomp.\nWindows error " +
                                     std::to_wstring(GetLastError()) + L".";
        MessageBoxW(g_window, message.c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
        return;
    }

    g_process = process;
    g_running = true;
    g_sessionLog = log != INVALID_HANDLE_VALUE ? logPath : std::wstring();
    SetStatus((packed ? L"Game running from the pre-baked image (PID " : L"Game running (PID ") +
              std::to_wstring(process.dwProcessId) + L"). Log: " +
              (log != INVALID_HANDLE_VALUE ? L"logs\\play\\" + std::wstring(stamp) : std::wstring(L"unavailable")));
    SetTimer(g_window, 1, 1000, nullptr);
    RedrawWindow(g_window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
}

// ---- window ----

void CreateFonts() {
    g_family = new FontFamily(Th.fontFamily);
    if (!g_family->IsAvailable()) {
        delete g_family;
        g_family = new FontFamily(Th.fontFamilyFallback);
    }
    g_familyMono = new FontFamily(Th.fontMono);
    if (!g_familyMono->IsAvailable()) {
        delete g_familyMono;
        g_familyMono = new FontFamily(Th.fontMonoFallback);
    }
    g_familyHeavy = new FontFamily(Th.fontHeavy);
    g_heavyStyle = FontStyleRegular;
    if (!g_familyHeavy->IsAvailable() || !g_familyHeavy->IsStyleAvailable(FontStyleRegular)) {
        delete g_familyHeavy;
        g_familyHeavy = new FontFamily(Th.fontHeavyFallback);
        if (!g_familyHeavy->IsAvailable() || !g_familyHeavy->IsStyleAvailable(FontStyleRegular)) {
            // No heavy face on this system: the regular family in bold keeps the same look.
            delete g_familyHeavy;
            g_familyHeavy = new FontFamily(g_family->IsAvailable() ? Th.fontFamily : Th.fontFamilyFallback);
            g_heavyStyle = FontStyleBold;
        }
    }
    g_fontBrand = new Font(g_family, static_cast<REAL>(S(Th.sizeBrand)), FontStyleBold, UnitPixel);
    g_fontH1 = new Font(g_family, static_cast<REAL>(S(Th.sizeH1)), FontStyleBold, UnitPixel);
    g_fontH2 = new Font(g_family, static_cast<REAL>(S(Th.sizeH2)), FontStyleBold, UnitPixel);
    g_fontBody = new Font(g_family, static_cast<REAL>(S(Th.sizeBody)), FontStyleRegular, UnitPixel);
    g_fontSmall = new Font(g_family, static_cast<REAL>(S(Th.sizeSmall)), FontStyleRegular, UnitPixel);
    g_fontButton = new Font(g_family, static_cast<REAL>(S(Th.sizeButton)), FontStyleBold, UnitPixel);
    g_fontTiny = new Font(g_family, static_cast<REAL>(S(Th.sizeTiny)), FontStyleBold, UnitPixel);
    g_fontPlay = new Font(g_family, static_cast<REAL>(S(Th.sizePlay)), FontStyleBold, UnitPixel);
    g_fontMono = new Font(g_familyMono, static_cast<REAL>(S(Th.sizeMono)), FontStyleRegular, UnitPixel);
}

void DestroyFonts() {
    delete g_fontMono; g_fontMono = nullptr;
    delete g_fontPlay; g_fontPlay = nullptr;
    delete g_fontTiny; g_fontTiny = nullptr;
    delete g_fontButton; g_fontButton = nullptr;
    delete g_fontSmall; g_fontSmall = nullptr;
    delete g_fontBody; g_fontBody = nullptr;
    delete g_fontH2; g_fontH2 = nullptr;
    delete g_fontH1; g_fontH1 = nullptr;
    delete g_fontBrand; g_fontBrand = nullptr;
    delete g_familyHeavy; g_familyHeavy = nullptr;
    delete g_familyMono; g_familyMono = nullptr;
    delete g_family; g_family = nullptr;
}

void ApplyDarkFrame(HWND window) {
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    const COLORREF caption = Rgb(Th.frameCaption);
    DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof caption);
    const COLORREF border = Rgb(Th.frameBorder);
    DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof border);
    const COLORREF captionText = Rgb(Th.frameCaptionText);
    DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &captionText, sizeof captionText);
}

void ChooseScale(HWND window) {
    const double dpiScale = static_cast<double>(g_dpi) / 96.0;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const double fitW = (work.right - work.left - 48.0) / kClientWidth;
    const double fitH = (work.bottom - work.top - 96.0) / kClientHeight;
    g_scale = std::min(dpiScale, std::min(fitW, fitH));
    if (g_scale < 0.75) g_scale = 0.75;
    if (g_scale > 2.5) g_scale = 2.5;
    (void)window;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_window = window;
        g_dpi = GetDpiForWindow(window);
        if (g_dpi <= 0) g_dpi = 96;
        ChooseScale(window);
        ApplyDarkFrame(window);
        {
            RECT client{0, 0, S(kClientWidth), S(kClientHeight)};
            AdjustWindowRectEx(&client, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
            RECT current{};
            GetWindowRect(window, &current);
            const int width = client.right - client.left;
            const int height = client.bottom - client.top;
            const int x = current.left - (width - (current.right - current.left)) / 2;
            const int y = current.top - (height - (current.bottom - current.top)) / 2;
            SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        CreateFonts();
        BuildWidgets();
        DragAcceptFiles(window, TRUE);
        RefreshPads(false);
        SetStatus(Ready() ? L"Ready. All files found." : L"No game files yet; open Game files to point at them.");
        return 0;

    case WM_SIZE:
        BuildWidgets();
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_DEVICECHANGE:
        if (g_window) {
            RefreshPads(false);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(window);
        return 0;

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (g_slider) {
            SetSharpnessFromX(x);
        } else {
            const int id = WidgetAt(x, y);
            if (id != g_hot) {
                g_hot = id;
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
            }
        }
        if (!g_trackingLeave) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            g_trackingLeave = true;
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_trackingLeave = false;
        if (g_hot && !g_slider) {
            g_hot = 0;
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        SetFocus(window);
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        const int id = WidgetAt(x, y);
        if (id) {
            g_pressed = id;
            const Widget* w = FindWidget(id);
            if (w && w->kind == Kind::Slider) {
                g_slider = id;
                SetCapture(window);
                SetSharpnessFromX(x);
            } else {
                SetCapture(window);
            }
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        const int id = WidgetAt(x, y);
        const int pressed = g_pressed;
        g_pressed = 0;
        if (g_slider) {
            g_slider = 0;
            SaveOption(L"sharpness", FormatDouble(g_config.sharpness, L"%.2f"));
        }
        if (pressed && id == pressed) Activate(pressed);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(window, &p);
            if (WidgetAt(p.x, p.y)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            if (g_page == Page::Home) Activate(ID_PLAY);
            else if (g_page == Page::GameFiles) Activate(Ready() ? ID_PLAY : ID_REBUILD);
            else if (g_page == Page::Build) {
                if (Ready() && !g_building && !g_running) Activate(ID_PLAY);
            } else {
                Activate(ID_DONE);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE || wParam == VK_BACK) {
            if (g_page != Page::Home) {
                GoTo(Page::Home);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                // Go through WM_CLOSE so a running build gets the same confirmation.
                PostMessage(window, WM_CLOSE, 0, 0);
                return 0;
            }
        }
        if (wParam == VK_F11) {
            Activate(ID_FULLSCREEN);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == 1 && g_running) {
            if (WaitForSingleObject(g_process.hProcess, 0) == WAIT_OBJECT_0) {
                DWORD exitCode = 0;
                GetExitCodeProcess(g_process.hProcess, &exitCode);
                CloseHandle(g_process.hThread);
                CloseHandle(g_process.hProcess);
                g_process = {};
                g_running = false;
                KillTimer(window, 1);
                SetStatus(L"Game exited (code " + std::to_wstring(exitCode) + L").");
                const std::wstring session = g_sessionLog;
                g_sessionLog.clear();
                RecordSessionEnd(exitCode, session);
                RefreshSetup();
                RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
            }
        }
        if (wParam == 2) {
            PollBuild();
        }
        return 0;

    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[2048]{};
        if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path))) {
            std::wstring dropped = path;
            std::wstring lower = dropped;
            CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
            const bool isImage = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".wrd") == 0;
            const bool isExe = lower.size() > 4 && lower.compare(lower.size() - 4, 4, L".exe") == 0;
            if (isImage) {
                g_config.image = dropped;
                WriteIni(L"paths", L"image", g_config.image);
                SetStatus(ImageCurrent() ? L"Packed image dropped and current." : L"Packed image dropped; rebuild to refresh it.");
                GoTo(Page::GameFiles);
            } else if (Exists(Join(dropped, L"sys\\main.dol"))) {
                g_config.disc = Join(dropped, L"sys");
                WriteIni(L"paths", L"disc", g_config.disc);
                SetStatus(L"Extracted disc dropped.");
                GoTo(Page::GameFiles);
            } else if (Exists(Join(dropped, L"main.dol"))) {
                g_config.disc = dropped;
                WriteIni(L"paths", L"disc", g_config.disc);
                SetStatus(L"Extracted disc dropped.");
                GoTo(Page::GameFiles);
            } else if (isExe) {
                g_config.console = dropped;
                WriteIni(L"paths", L"console", g_config.console);
                SetStatus(L"Game console dropped.");
                GoTo(Page::GameFiles);
            } else {
                SetStatus(L"Drop a .wrd image, an extracted disc folder, or wii-recomp.exe.");
            }
            RefreshSetup();
        }
        DragFinish(drop);
        return 0;
    }

    case WM_DESTROY:
        if (g_running) {
            // The game keeps running; the launcher only drops its handles.
            CloseHandle(g_process.hThread);
            CloseHandle(g_process.hProcess);
            g_process = {};
            g_running = false;
            g_sessionLog.clear();
        }
        StopBuild();
        if (g_inputWindow) DestroyWindow(g_inputWindow);  // Before the fonts it draws with go.
        if (g_mapWindow) DestroyWindow(g_mapWindow);
        DestroyFonts();
        ClosePads();
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (g_building) {
            const int answer = MessageBoxW(window, L"A rebuild is still running.\n\nStop it and close the launcher?",
                                           kWindowTitle, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
            if (answer != IDYES) return 0;
            StopBuild();
        }
        DestroyWindow(window);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

// ---- headless render ----
//
// SeaOfStar-ToD.exe --render <surface> <out.png> [--state <name>] [--scale <f>] draws one
// surface offscreen with the same drawing code the windows use, at 96 dpi unless a scale is
// given, saves a PNG and exits. State
// overrides touch globals only inside this path; nothing is written to the ini.

// A debug sheet with one of every drawing primitive, in each of its states.
void RenderWidgets(Graphics& g, int width, int height) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    DrawBackdrop(g, width, height);

    const float x0 = SF(32);
    Panel(g, RectF(SF(16), SF(66), SF(968), SF(620)), SF(18));
    Title(g, L"Widget sheet", SF(static_cast<float>(Th.sizeH2)), RectF(x0, SF(12), SF(400), SF(32)), Th.titleOutlineBg);
    Text(g, L"normal  ·  hot  ·  pressed  ·  disabled", g_fontSmall, Th.bgTextDim, RectF(x0, SF(46), SF(400), SF(18)));

    // Buttons: primary, secondary, then the Play button with its icon.
    const wchar_t* names[4] = {L"Normal", L"Hot", L"Pressed", L"Disabled"};
    for (int i = 0; i < 4; ++i) {
        const bool hot = i == 1, pressed = i == 2, enabled = i != 3;
        Button(g, RectF(x0 + i * SF(150), SF(76), SF(130), SF(40)), names[i], true, hot, pressed, enabled, false,
               g_fontButton);
        Button(g, RectF(x0 + i * SF(150), SF(132), SF(130), SF(38)), names[i], false, hot, pressed, enabled, false,
               g_fontButton);
    }
    Button(g, RectF(x0 + 4 * SF(150), SF(76), SF(150), SF(48)), L"Play", true, false, false, true, true, g_fontPlay);
    Button(g, RectF(x0 + 4 * SF(150), SF(132), SF(150), SF(48)), L"Play", true, true, false, true, true, g_fontPlay);
    Button(g, RectF(x0 + 4 * SF(150), SF(188), SF(150), SF(48)), L"Play", true, false, false, false, true, g_fontPlay);
    Button(g, RectF(x0, SF(188), SF(80), SF(24)), L"Tiny", false, false, false, true, false, g_fontTiny);
    Button(g, RectF(x0 + SF(90), SF(188), SF(80), SF(24)), L"Tiny", false, true, false, true, false, g_fontTiny);
    Button(g, RectF(x0 + SF(180), SF(188), SF(80), SF(24)), L"Tiny", false, false, true, true, false, g_fontTiny);
    Button(g, RectF(x0 + SF(270), SF(188), SF(80), SF(24)), L"Tiny", false, false, false, false, false, g_fontTiny);

    // Segment pills and switches.
    SegmentPill(g, RectF(x0, SF(252), SF(80), SF(30)), L"Selected", true, false, true);
    SegmentPill(g, RectF(x0 + SF(90), SF(252), SF(80), SF(30)), L"Plain", false, false, true);
    SegmentPill(g, RectF(x0 + SF(180), SF(252), SF(80), SF(30)), L"Hot", false, true, true);
    SegmentPill(g, RectF(x0 + SF(270), SF(252), SF(80), SF(30)), L"Off", true, false, false);
    Switch(g, RectF(x0 + SF(380), SF(255), SF(44), SF(24)), true, false, true);
    Switch(g, RectF(x0 + SF(440), SF(255), SF(44), SF(24)), false, false, true);
    Switch(g, RectF(x0 + SF(500), SF(255), SF(44), SF(24)), true, true, true);
    Switch(g, RectF(x0 + SF(560), SF(255), SF(44), SF(24)), false, true, true);
    Switch(g, RectF(x0 + SF(620), SF(255), SF(44), SF(24)), true, false, false);

    // Sliders and progress.
    Slider(g, RectF(x0, SF(316), SF(200), SF(10)), 0.4, true);
    Slider(g, RectF(x0 + SF(240), SF(316), SF(200), SF(10)), 1.2, false);
    MapSlider(g, RectF(x0 + SF(480), SF(316), SF(200), SF(10)), 0.35f, false);
    MapSlider(g, RectF(x0 + SF(720), SF(316), SF(200), SF(10)), 0.7f, true);
    Progress(g, RectF(x0, SF(346), SF(200), SF(18)), 0);
    Progress(g, RectF(x0 + SF(240), SF(346), SF(200), SF(18)), 45);
    Progress(g, RectF(x0 + SF(480), SF(346), SF(200), SF(18)), 100);

    // Input-check pieces.
    Chip(g, RectF(x0, SF(392), SF(80), SF(24)), L"A  east", true);
    Chip(g, RectF(x0 + SF(90), SF(392), SF(80), SF(24)), L"B  south", false);
    TriggerBar(g, RectF(x0 + SF(200), SF(392), SF(220), SF(18)), L"LT", 0.0f);
    TriggerBar(g, RectF(x0 + SF(200), SF(416), SF(220), SF(18)), L"RT", 0.6f);
    Stick(g, RectF(x0 + SF(470), SF(400), SF(70), SF(70)), L"LEFT", 0.0f, 0.0f);
    Stick(g, RectF(x0 + SF(570), SF(400), SF(70), SF(70)), L"RIGHT", 0.6f, 0.4f);
    Dot(g, x0 + SF(700), SF(400), Th.good);
    Dot(g, x0 + SF(716), SF(400), Th.warn);
    Dot(g, x0 + SF(732), SF(400), Th.bad);
    Gear(g, RectF(x0 + SF(780), SF(392), SF(36), SF(32)), false, false);
    Gear(g, RectF(x0 + SF(824), SF(392), SF(36), SF(32)), true, false);
    Gear(g, RectF(x0 + SF(868), SF(392), SF(36), SF(32)), false, true);

    // The flow steps and the header pill in its three states.
    DrawSteps(g, SF(1000) - SF(32), SF(580), 1, 1);
    StatusPill(g, RectF(x0 + SF(470), SF(486), SF(112), SF(28)), L"READY", Th.pillOk);
    StatusPill(g, RectF(x0 + SF(600), SF(486), SF(112), SF(28)), L"RUNNING", Th.pillBusy);
    StatusPill(g, RectF(x0 + SF(730), SF(486), SF(112), SF(28)), L"SET UP", Th.pillBad);

    // A card with its title.
    const RectF card(x0, SF(470), SF(420), SF(190));
    Panel(g, card, SF(16));
    CardTitle(g, card, L"SAMPLE CARD");
    Text(g, L"Body text on a card", g_fontBody, Th.cardText, RectF(card.X + SF(24), card.Y + SF(44), SF(300), SF(20)));
    Text(g, L"Small secondary text on a card", g_fontSmall, Th.cardTextDim,
         RectF(card.X + SF(24), card.Y + SF(70), SF(300), SF(16)));
    Text(g, L"Small muted text on a card", g_fontSmall, Th.cardTextMuted, RectF(card.X + SF(24), card.Y + SF(92), SF(300), SF(16)));
    Text(g, L"Tiny bold label", g_fontTiny, Th.cardTextMuted, RectF(card.X + SF(24), card.Y + SF(114), SF(300), SF(16)));
    Text(g, L"Heading style", g_fontH2, Th.cardText, RectF(card.X + SF(24), card.Y + SF(138), SF(300), SF(30)));
    // Page furniture: banner, key caps, mark badges, tabs, a slot.
    Banner(g, L"Banner", x0 + SF(436), SF(528), SF(34));
    const wchar_t* capNames[5] = {L"Plain", L"On", L"Off", L"None", L"Press..."};
    const Cap capStyles[5] = {Cap::Plain, Cap::On, Cap::Off, Cap::None, Cap::Listening};
    float capX = x0 + SF(670);
    for (int i = 0; i < 5; ++i) {
        const float w = CapWidth(g, capNames[i]);
        KeyCap(g, RectF(capX, SF(436), w, SF(22)), capNames[i], capStyles[i]);
        capX += w + SF(6);
    }
    const Mark marks[5] = {Mark::Tick, Mark::Bang, Mark::Cross, Mark::Dots, Mark::Plain};
    for (int i = 0; i < 5; ++i) MarkBadge(g, RectF(x0 + SF(670) + i * SF(30), SF(462), SF(22), SF(22)), marks[i]);
    Tab(g, RectF(x0 + SF(670), SF(524), SF(100), SF(36)), L"Idle", false, false);
    Tab(g, RectF(x0 + SF(780), SF(524), SF(100), SF(36)), L"Selected", true, false);
}

bool PngEncoder(CLSID& clsid) {
    UINT count = 0, bytes = 0;
    if (GetImageEncodersSize(&count, &bytes) != Ok || bytes == 0) return false;
    std::vector<unsigned char> buffer(bytes);
    ImageCodecInfo* codecs = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, bytes, codecs) != Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (codecs[i].MimeType && wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// Exit codes: 0 rendered, 2 bad arguments, 3 the PNG could not be written.
int RenderCli(int argc, wchar_t** argv, int flag) {
    if (flag + 2 >= argc) return 2;
    const std::wstring surface = argv[flag + 1];
    const std::wstring outPath = argv[flag + 2];
    std::wstring state = L"ready";
    double scale = 1.0;
    for (int i = flag + 3; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--state") == 0) state = argv[i + 1];
        if (wcscmp(argv[i], L"--scale") == 0) scale = _wtof(argv[i + 1]);
    }
    if (scale < 0.75 || scale > 2.5) return 2;

    const wchar_t* states[] = {L"ready", L"notready", L"building", L"running", L"crash", L"stale"};
    bool knownState = false;
    for (const wchar_t* s : states) knownState = knownState || state == s;
    if (!knownState) return 2;

    // The logical size at 96 dpi (or at --scale), whatever the monitor says.
    g_dpi = static_cast<int>(std::lround(96 * scale));
    g_scale = scale;
    g_page = Page::Home;
    int width = S(kClientWidth), height = S(kClientHeight);
    enum class Kind2 { Main, Input, Controls, Widgets } kind = Kind2::Main;
    if (surface == L"home") g_page = Page::Home;
    else if (surface == L"settings") g_page = Page::Settings;
    else if (surface == L"gamefiles") g_page = Page::GameFiles;
    else if (surface == L"build") g_page = Page::Build;
    else if (surface == L"input" || surface == L"input-nopad") {
        kind = Kind2::Input;
        width = S(kInputWidth);
        height = S(kInputHeight);
    } else if (surface.rfind(L"controls-", 0) == 0) {
        kind = Kind2::Controls;
        width = S(kMapWidth);
        height = S(kMapHeight);
        if (surface == L"controls-keyboard" || surface == L"controls-listening") g_mapTab = 0;
        else if (surface == L"controls-gamepad") g_mapTab = 1;
        else if (surface == L"controls-mouse") g_mapTab = 2;
        else if (surface == L"controls-player") g_mapTab = 3;
        else return 2;
    } else if (surface == L"widgets") {
        kind = Kind2::Widgets;
    } else {
        return 2;
    }

    // Fixed inputs so two renders of the same build compare equal.
    g_status = L"Ready.";
    g_padsTried = true;
    g_padsError = false;
    g_pads.clear();
    g_crash = CrashNotice{};
    g_hot = g_pressed = 0;

    if (state == L"notready") {
        g_config.console = L"C:\\render-missing\\wii-recomp.exe";
        g_config.title = L"C:\\render-missing\\title.dll";
        g_config.disc = L"C:\\render-missing\\disc";
        g_config.image = L"C:\\render-missing\\game.wrd";
    } else if (state == L"stale") {
        g_renderImage = static_cast<int>(ImageState::StaleTitle);
    } else if (state == L"crash") {
        g_crash.pending = true;
        g_crash.code = 0xC0000005ul;
        g_crash.stamp = L"20261124-000000";
        g_crash.dir = L"logs\\crash\\20261124-000000";
    } else if (state == L"running") {
        g_running = true;
    } else if (state == L"building") {
        g_building = true;
        g_buildPercent = 42;
        g_buildStage = L"Compiling the title";
    }
    ComputeSetupState();

    if (kind == Kind2::Main && g_page == Page::Build) {
        g_buildLines = {L"== Translate the disc", L"[ ok ] main.dol read", L"[warn] 3 unresolved calls skipped",
                        L"[ ok ] title.dll compiled", L"[fail] example failure line"};
        if (!g_building) g_buildExitCode = 0;
    }
    if (kind == Kind2::Input) {
        g_testPads.clear();
        if (surface == L"input") g_testPads.push_back(TestPad{nullptr, L"Demo gamepad"});
    }
    if (kind == Kind2::Controls) {
        g_mapConfigPath = L"C:\\render-missing\\controllers.ini";  // Missing: the built-in defaults.
        ReloadControls();
        g_mapListening = surface == L"controls-listening" ? 2 : -1;
        g_mapHotTab = g_mapHotRow = g_mapHotClear = -1;
        if (surface == L"controls-listening") {
            g_mapHotRow = 1;
            g_mapHotClear = 3;
        }
        g_mapHotButton = 0;
        g_mapSlider = g_mapHotSlider = 0;
    }

    CreateFonts();
    Bitmap bitmap(width, height, PixelFormat32bppPARGB);
    {
        Graphics g(&bitmap);
        switch (kind) {
        case Kind2::Main: RenderMain(g, width, height); break;
        case Kind2::Input: RenderInput(g, width, height); break;
        case Kind2::Controls: RenderControls(g, width, height); break;
        case Kind2::Widgets: RenderWidgets(g, width, height); break;
        }
    }
    DestroyFonts();

    CLSID png{};
    if (!PngEncoder(png)) return 3;
    return bitmap.Save(outPath.c_str(), &png, nullptr) == Ok ? 0 : 3;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    g_instance = instance;
    g_dpi = GetDpiForSystem();
    if (g_dpi <= 0) g_dpi = 96;
    g_scale = static_cast<double>(g_dpi) / 96.0;

    GdiplusStartupInput gdiplusInput;
    if (GdiplusStartup(&g_gdiplus, &gdiplusInput, nullptr) != Ok) return 1;

    g_root = FindRepoRoot();
    g_ini = Join(Join(g_root, L"config"), L"launcher.ini");
    LoadConfig();
    DetectAspect();

    // Headless: --render <surface> <out.png> [--state <name>] draws one surface offscreen and
    // exits. Checked first, on exact arguments, and before anything writes to the ini.
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        int flag = -1;
        for (int i = 0; argv && i < argc; ++i)
            if (wcscmp(argv[i], L"--render") == 0) { flag = i; break; }
        if (flag >= 0) {
            const int code = RenderCli(argc, argv, flag);
            LocalFree(argv);
            GdiplusShutdown(g_gdiplus);
            return code;
        }
        if (argv) LocalFree(argv);
    }

    WriteDefaultsIfMissing();

    // Headless: --check-session <play log> <exit code> classifies one finished session, writes
    // the crash report when it was unexpected, prints the verdict and exits 0.
    if (commandLine && std::wcsstr(commandLine, L"--check-session")) {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        int flag = -1;
        for (int i = 0; argv && i < argc; ++i)
            if (wcscmp(argv[i], L"--check-session") == 0) flag = i;
        if (argv && flag >= 0 && flag + 2 < argc) {
            CheckSessionCli(argv[flag + 1], std::wcstoul(argv[flag + 2], nullptr, 0));
            LocalFree(argv);
            GdiplusShutdown(g_gdiplus);
            return 0;
        }
        if (argv) LocalFree(argv);
        GdiplusShutdown(g_gdiplus);
        return 2;  // Missing arguments: usage is --check-session <log> <exit code>.
    }

    // Headless: --dump-controls [controllers.ini] prints what the Controls editor would show.
    if (commandLine && std::wcsstr(commandLine, L"--dump-controls")) {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring path;
        for (int i = 0; argv && i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--dump-controls") == 0 && wcsncmp(argv[i + 1], L"--", 2) != 0) path = argv[i + 1];
        }
        if (argv) LocalFree(argv);
        const int code = DumpControls(path);
        GdiplusShutdown(g_gdiplus);
        return code;
    }

    // Headless: write config\setup-state.json and leave, for scripts and CI. Exit code 0 on
    // success, 3 when the file could not be written.
    if (commandLine && std::wcsstr(commandLine, L"--write-setup-state")) {
        const bool written = RefreshSetup();
        GdiplusShutdown(g_gdiplus);
        return written ? 0 : 3;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) {
        GdiplusShutdown(g_gdiplus);
        return 1;
    }

    WNDCLASSEXW inputClass{};
    inputClass.cbSize = sizeof(inputClass);
    inputClass.lpfnWndProc = InputProc;
    inputClass.hInstance = instance;
    inputClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    inputClass.hbrBackground = nullptr;
    inputClass.lpszClassName = kInputClass;
    inputClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    inputClass.hIconSm = inputClass.hIcon;
    if (!RegisterClassExW(&inputClass)) {
        GdiplusShutdown(g_gdiplus);
        return 1;
    }

    WNDCLASSEXW mapClass{};
    mapClass.cbSize = sizeof(mapClass);
    mapClass.lpfnWndProc = ControlsProc;
    mapClass.hInstance = instance;
    mapClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mapClass.hbrBackground = nullptr;
    mapClass.lpszClassName = kMapClass;
    mapClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    mapClass.hIconSm = mapClass.hIcon;
    if (!RegisterClassExW(&mapClass)) {
        GdiplusShutdown(g_gdiplus);
        return 1;
    }

    RECT rect{0, 0, S(kClientWidth), S(kClientHeight)};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    const HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle,
                                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, x, y,
                                        width, height, nullptr, nullptr, instance, nullptr);
    if (!window) {
        GdiplusShutdown(g_gdiplus);
        return 1;
    }

    LoadCrashNotice();
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    RefreshSetup();

    // A shortcut for testing, and for opening the check or the editor before a controller
    // is connected.
    if (commandLine && std::wcsstr(commandLine, L"--input-test")) OpenInputTest();
    if (commandLine && std::wcsstr(commandLine, L"--controls")) OpenControls();
    if (commandLine && std::wcsstr(commandLine, L"--settings")) GoTo(Page::Settings);
    if (commandLine && std::wcsstr(commandLine, L"--gamefiles")) GoTo(Page::GameFiles);
    if (commandLine && std::wcsstr(commandLine, L"--build-page")) {
        GoTo(Page::Build);
    } else if (commandLine && std::wcsstr(commandLine, L"--build")) {
        GoTo(Page::Build);
        StartBuild();
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    GdiplusShutdown(g_gdiplus);
    return static_cast<int>(message.wParam);
}
