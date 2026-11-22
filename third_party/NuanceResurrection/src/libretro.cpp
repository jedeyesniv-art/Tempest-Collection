// NuanceResurrection libretro core
// Wraps the NUON emulator for use in RetroArch

#include "libretro.h"
#include "basetypes.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>         // _access (MSVC-ABI builds have no unistd.h)
#include <direct.h>     // _chdir
#ifndef F_OK
#define F_OK 0
#endif
#define access _access
#define chdir _chdir
#define strcasecmp _stricmp
#else
#include <unistd.h>
#endif
#include "gl_headers.h"
// GL/gl.h not needed; the header above already brings in every entry point
// this file calls, whether that is GLEW on the desktop or the platform's own
// GLES headers.
#include <mutex>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <cstdarg>
#ifdef _WIN32
#include <direct.h>     // _mkdir
#include <windows.h>    // GetModuleHandleEx / GetModuleFileName
#else
#include <dlfcn.h>      // dladdr / Dl_info
#endif

#include "byteswap.h"
#include "Utility.h"
#include "comm.h"
#include "audio.h"
#include "mpe.h"
#include "NuonEnvironment.h"
#include "NuonMemoryMap.h"
#include "joystick.h"
#include "video.h"
#include "ExecuteMEM.h"
#include "timer.h"
#include "Bios.h"
#include "archive.h"
#include "aot.h"
#include "hires.h"
#include "savestate.h"

// GLWindow stubs for libretro (no windowing needed)
#include "GLWindow.h"
GLWindow::GLWindow() : bFullScreen(false), bVisible(false), keyDownHandler(nullptr), keyUpHandler(nullptr),
    paintHandler(nullptr), resizeHandler(nullptr), applyControllerState(nullptr), inputManager(nullptr),
    clientWidth(720), clientHeight(480), x(0), y(0), width(720), height(480),
    fullScreenWidth(1920), fullScreenHeight(1080), hInstance(nullptr), hWnd(nullptr), hDC(nullptr), hRC(nullptr),
    windowStyle(0), windowExtendedStyle(0), fullScreenWindowStyle(0), fullScreenWindowExtendedStyle(0),
    threadHandle(0), threadID(0), restoreWidth(720), restoreHeight(480), restoreX(0), restoreY(0) {}
GLWindow::~GLWindow() {}
void GLWindow::UpdateRestoreValues() {}
void GLWindow::ToggleFullscreen() {}
bool GLWindow::ChangeScreenResolution(int,int) { return false; }
bool GLWindow::CreateWindowGL() { return false; }
void GLWindow::CleanUp() {}
bool GLWindow::RegisterWindowClass() { return false; }
bool GLWindow::Create() { return false; }
void GLWindow::MessagePump() {}
void GLWindow::OnResize(int,int) {}
unsigned GLWindow::GLWindowMain(void*) { return 0; }
GLWindow display;

// Globals
NuonEnvironment nuonEnv;
bool bQuit = false;
bool bRun = false;
std::string g_ISOPath;
std::string g_ISOPrefix;

extern ControllerData *controller;
extern VidChannel structMainChannel, structOverlayChannel;
extern bool bOverlayChannelActive, bMainChannelActive;
extern vidTexInfo videoTexInfo;

// Per-retro_run statistics for a frontend that knows this core (Llamasoft on the Jaguar host): what one
// retro_run did, for measuring how evenly the game runs. nuance_get_run_stats() returns the last run's.
struct NuanceRunStats {
    uint64_t iterations;       // main-loop iterations (each runs MPE 3..0 once)
    uint64_t mpe3_stalled;     // of those, iterations with MPE3 stalled in VidSync (waiting for a field)
    uint64_t run_us;           // wall time spent in the emulation loop
    uint32_t field_fired;      // 1 = the loop ended on a video field, 0 = on the time budget
    uint32_t field_counter;    // the video field counter after the run
    uint32_t vidsync_calls;    // VidSync waits started (cumulative)
    uint32_t pad;
    uint64_t mpe_packets[4];   // packets each MPE executed in this run
    uint32_t vid_flips;        // VidChangeBase + VidConfig calls (cumulative): the game's own frame rate
    uint32_t time_elapsed_calls; // TimeElapsed BIOS calls (cumulative)
};
static NuanceRunStats g_run_stats{};
extern uint32_t g_vidsync_waits, g_vid_flips, g_time_elapsed_calls;
extern "C" void nuance_get_run_stats(NuanceRunStats* out) { if (out) *out = g_run_stats; }

// Timing mode (llamasoft-nuon). Emulated: one retro_run = exactly one video field = a fixed number of rounds
// (each round runs one block on every MPE, Nuance's own interleaving - the game depends on it), and the timers,
// audio periods and the clock the game reads (TimeElapsed) follow emulated time: a field is 1/60 s of a 54 MHz
// clock, spread evenly over its rounds. The frontend paces retro_run at 60 Hz. More rounds per field = more MPE
// work per field (the game draws more pictures a second) and more PC time.
// Real time (Nuance's own): the loop runs flat out and everything follows the PC clock.
static bool g_emulated = true;
static uint64 g_rounds_per_field = 200000;   // Tempest 3000: a steady 30 pictures/s with flip cap 2 (2026-11-14)
static const bool g_debug_halts = getenv("NUANCE_DEBUG_HALTS") != nullptr;
extern FILE* g_joblog;   // comm.cpp; opened from NUANCE_JOBLOG at load
void JobLogDescriptor(FILE* f, uint32 ptr);   // comm.cpp
// NUANCE_PROFILE=1: where a field's PC time goes (TSC ticks per MPE call, comm bus + interrupts, the rest),
// printed every 300 fields.
static const bool g_profile = getenv("NUANCE_PROFILE") != nullptr;
struct Profile {
    uint64 mpe_ticks[4] = {}, mpe_calls[4] = {}, mpe_packets[4] = {}, mpe_single[4] = {}, mpe_idle[4] = {};
    uint64 mpe_self[4] = {}, mpe_pair[4] = {};   // calls starting at the same pc as the last call / the one before
    uint32 last_pc[4][2] = {};
    uint64 other_ticks = 0, field_ticks = 0, fields = 0;
};
static Profile g_prof;
static uint64 g_aot_packets_at_last[4] = {};
extern uint64 g_dispatch[4][4];   // mpe.cpp
#include <unordered_map>
extern bool g_trackUncompiled;
static const bool g_pchist = getenv("NUANCE_PCHIST") != nullptr;
static std::unordered_map<uint32, std::pair<uint64, uint64>> g_pc_packets[4];   // pc -> (calls, packets)
extern std::unordered_map<uint64, std::pair<uint64, uint32>> g_uncompiled;
// The machine's memory, for a frontend's game hooks and memory searches (llamasoft-nuon): 0 = system bus DRAM
// ($80000000, 8 MB, big-endian words), 1 = main bus DRAM ($40000000), 2..5 = MPE 0..3 local memory ($20000000).
extern "C" uint8_t* nuance_get_memory(int which, size_t* size)
{
    if (which == 0) { if (size) *size = SYSTEM_BUS_SIZE; return nuonEnv.systemBusDRAM; }
    if (which == 1) { if (size) *size = MAIN_BUS_SIZE; return nuonEnv.mainBusDRAM; }
    if (which >= 2 && which <= 5) { if (size) *size = MPE_LOCAL_MEMORY_SIZE; return nuonEnv.mpe[which - 2].dtrom; }
    if (size) *size = 0;
    return nullptr;
}

// Disassembly for a frontend's investigations (llamasoft-nuon): `packets` packets of MPE `mpe` from `address`
// (following each packet's length), one "address: instructions" line each, into buf. Returns the length written.
extern "C" size_t nuance_disasm(int mpe, uint32_t address, int packets, char* buf, size_t size)
{
    if (mpe < 0 || mpe > 3 || !buf || size < 2) return 0;
    size_t used = 0;
    buf[0] = 0;
    for (int k = 0; k < packets && used + 600 < size; ++k) {
        InstructionCacheEntry entry;
        entry.pcexec = address;
        nuonEnv.mpe[mpe].DecompressPacket(nuonEnv.mpe[mpe].GetPointerToMemoryBank(address), entry);
        char line[512];
        line[0] = 0;
        nuonEnv.mpe[mpe].PrintInstructionCachePacket(line, sizeof(line), entry);
        for (char* c = line; *c; ++c) if (*c == '\n') *c = ';';
        used += snprintf(buf + used, size - used, "%08X: %s\n", address, line);
        if (entry.pcroute == address) break;
        address = entry.pcroute;
    }
    return used;
}

// AOT module dump (llamasoft-nuon): decode `length` bytes of code at system-bus address srcAddr as if loaded at
// `base` in MPE `mpe`'s local memory, with the interpreter's decode options, and write one line per packet:
//   P pc pcroute packetInfo nuanceCount pcfetchnext ecuConditionCode
//   N handler f0 f1 f2 f3 f4        (one per nuance; a field that points into the MPE's local memory is D+offset)
// tools/t3k_aot.py turns the dump into C++.
extern "C" int nuance_aot_dump(int mpe, uint32_t srcAddr, uint32_t length, uint32_t base, const char* path)
{
    if (mpe < 0 || mpe > 3 || !path) return 0;
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    MPE& m = nuonEnv.mpe[mpe];
    const uint8* src = &nuonEnv.systemBusDRAM[srcAddr & SYSTEM_BUS_VALID_MEMORY_MASK];
    const size_t lo = (size_t)&m.dtrom[0], hi = lo + MPE_LOCAL_MEMORY_SIZE;
    int packets = 0;
    fprintf(f, "MODULE %08X %u %08X\n", srcAddr, length, base);
    for (uint32 pc = base; pc < base + length; ) {
        InstructionCacheEntry e;
        memset((void*)&e, 0, sizeof(e));   // unused fields stay 0 (they are stack garbage otherwise)
        e.pcexec = pc;
        m.DecompressPacket(src + (pc - base), e, DECOMPRESS_OPTIONS_SCHEDULE_ECU_LAST);
        fprintf(f, "P %08X %08X %08X %u %08X %08X\n", pc, e.pcroute, e.packetInfo, e.nuanceCount, e.pcfetchnext,
            e.ecuConditionCode);
        for (uint32 i = 0; i < e.nuanceCount; ++i) {
            fprintf(f, "N %u", e.handlers[i]);
            for (uint32 j = 0; j < FIELDS_PER_NUANCE; ++j) {
                const size_t v = e.nuances[FIXED_FIELD(i, j)];
                const size_t mlo = (size_t)&m, mhi = mlo + sizeof(MPE);
                const size_t elo = (size_t)&nuonEnv, ehi = elo + sizeof(NuonEnvironment);
                if (v >= lo && v < hi) fprintf(f, " D+%llX", (unsigned long long)(v - lo));
                else if (v >= mlo && v < mhi) fprintf(f, " M+%llX", (unsigned long long)(v - mlo));
                else if (v >= elo && v < ehi) fprintf(f, " E+%llX", (unsigned long long)(v - elo));
                else if (v > 0xFFFFFFFFull) fprintf(f, " ?%llX", (unsigned long long)v);   // another host pointer
                else fprintf(f, " %llX", (unsigned long long)v);
            }
            fprintf(f, "\n");
        }
        ++packets;
        if (e.pcroute <= pc) break;
        pc = e.pcroute;
    }
    fclose(f);
    // The module bytes next to it (<path minus .txt>.bin): the generator embeds them for validation.
    std::string bin = path;
    if (bin.size() > 4 && bin.compare(bin.size() - 4, 4, ".txt") == 0) bin.resize(bin.size() - 4);
    bin += ".bin";
    if (FILE* fb = fopen(bin.c_str(), "wb")) { fwrite(src, 1, length, fb); fclose(fb); }
    return packets;
}

extern double g_flip_cap;
// The fields between two pictures (video.cpp): 0 = off, 1 = 60 pictures/s at most, 2 = 30, 2.5 = 24 (a 2.5-field grid).
extern "C" void nuance_set_flip_cap(int fields) { g_flip_cap = fields > 0 ? (double)fields : 0.0; }
extern "C" void nuance_set_flip_interval(double fields) { g_flip_cap = fields > 0.0 ? fields : 0.0; }
// Tempest 3000 at 720x480 (hires.cpp); also NUANCE_HIRES=1. Call before the first frame.
extern "C" void nuance_set_hires(int on) { g_hires = on != 0; }

extern "C" void nuance_set_timing(int emulated, double rounds_per_field)
{
    g_emulated = emulated != 0;
    g_emu_time = g_emulated;
    if (rounds_per_field >= 1000) g_rounds_per_field = (uint64)rounds_per_field;
}

// Libretro callbacks
static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_hw_render_callback hw_render;

static bool gl_initialized = false;
static bool game_loaded = false;

// Audio buffer
#define AUDIO_BUFFER_SIZE 4096
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];

// Software framebuffer fallback
#define FB_WIDTH 720
#define FB_HEIGHT 480
static uint32_t framebuffer[FB_WIDTH * FB_HEIGHT];

// Into the frontend's log when it has one - stderr goes nowhere for a
// RetroArch started from a Windows desktop, so a report from there carried
// none of these lines - and to stderr otherwise.
static retro_log_printf_t log_cb = nullptr;

static void log_printf(const char* fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (log_cb)
        log_cb(RETRO_LOG_INFO, "%s", line);
    else
        fputs(line, stderr);
}

// For the rest of the core: a shader that does not compile, and the like.
void libretro_log_info(const char* text)
{
    log_printf("libretro: %s\n", text);
}

void libretro_log_message(const char* caption, const char* text)
{
    if (log_cb)
        log_cb(RETRO_LOG_ERROR, "libretro: %s: %s\n", caption ? caption : "message", text ? text : "");
    else
        fprintf(stderr, "[%s] %s\n", caption ? caption : "message", text ? text : "");
}

// Stub functions needed by other modules
void StopEmulation(int) { bRun = false; }

// InputManager stubs for libretro (input handled by retro_input_state)
#include "InputManager.h"
InputManager::~InputManager() {}
InputManager* InputManager::Create() { return nullptr; }
bool InputManager::StrToInputType(const char* str, InputType* type) {
    if (!strcasecmp("KEY", str)) { *type = KEY; return true; }
    if (!strcasecmp("JOYBUT", str)) { *type = JOYBUT; return true; }
    if (!strcasecmp("JOYAXIS", str)) { *type = JOYAXIS; return true; }
    if (!strcasecmp("JOYPOV", str)) { *type = JOYPOV; return true; }
    return false;
}
const char* InputManager::InputTypeToStr(InputType type) {
    switch (type) { case KEY: return "KEY"; case JOYBUT: return "JOYBUT"; case JOYAXIS: return "JOYAXIS"; case JOYPOV: return "JOYPOV"; default: return ""; }
}

// Analog axes (llamasoft-nuon): the frontend's stick, -128..127, written with the buttons when it has set them.
static int g_analog[4] = {};
static bool g_analog_set = false;
extern uint32 g_pad_extra_properties;
extern "C" void nuance_set_pad_properties(uint32_t extra) { g_pad_extra_properties = extra; }
extern bool g_pad_present_at_boot;
extern "C" void nuance_set_pad_present_at_boot(int on) { g_pad_present_at_boot = on != 0; }
extern "C" void nuance_set_analog(int x, int y, int x2, int y2)
{
    g_analog[0] = x; g_analog[1] = y; g_analog[2] = x2; g_analog[3] = y2;
    g_analog_set = true;
}

static void ApplyControllerState(unsigned int controllerIdx, uint16 buttons)
{
    if (controller) {
        controller[controllerIdx].buttons = SwapBytes(buttons);
        if (g_analog_set) {
            auto c = [](int v) { return (int8)(v < -128 ? -128 : v > 127 ? 127 : v); };
            controller[controllerIdx].d1.xAxis = c(g_analog[0]);
            controller[controllerIdx].d2.yAxis = c(g_analog[1]);
            controller[controllerIdx].d3.xAxis2 = c(g_analog[2]);
            controller[controllerIdx].d4.yAxis2 = (uint8)c(g_analog[3]);
        }
    }
}

static uint16 GetInputButtons()
{
    uint16 buttons = 0;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))    buttons |= CTRLR_DPAD_UP;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))  buttons |= CTRLR_DPAD_DOWN;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))  buttons |= CTRLR_DPAD_LEFT;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) buttons |= CTRLR_DPAD_RIGHT;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A))     buttons |= CTRLR_BUTTON_A;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B))     buttons |= CTRLR_BUTTON_B;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START)) buttons |= CTRLR_BUTTON_START;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT))buttons |= CTRLR_BUTTON_NUON;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L))     buttons |= CTRLR_BUTTON_L;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R))     buttons |= CTRLR_BUTTON_R;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))     buttons |= CTRLR_BUTTON_C_UP;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y))     buttons |= CTRLR_BUTTON_C_LEFT;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2))    buttons |= CTRLR_BUTTON_C_DOWN;
    if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2))    buttons |= CTRLR_BUTTON_C_RIGHT;
    return buttons;
}

static void context_reset(void)
{
#if defined(NUANCE_GLES)
    log_printf("libretro: GLES context, no loader needed\n");
#else
    glewExperimental = GL_TRUE;
    GLenum glew_err = glewInit();
    // GLEW often reports GLEW_ERROR_NO_GL_VERSION on core/forward-compatible contexts
    // because glGetString(GL_VERSION) is unavailable; the function pointers still load.
    if (glew_err != GLEW_OK && glew_err != 4 /* GLEW_ERROR_NO_GL_VERSION */) {
        log_printf("libretro: glewInit failed: %s\n", glewGetErrorString(glew_err));
        gl_initialized = false;
        return;
    }
    log_printf("libretro: GL context reset OK (GL %s)\n", (const char*)glGetString(GL_VERSION));
#endif

    gl_initialized = true;

    // The GL context was just (re)created. RetroArch can call context_reset more
    // than once per session (e.g. it tears down and rebuilds the GL context on a
    // windowed->fullscreen switch, which happens when its primary video driver is
    // Vulkan). All GL object names from the previous context are now dead, so tell
    // the video module to rebuild its textures/shaders; otherwise it keeps the
    // one-time-init guards set and renders with stale handles -> black screen.
    VideoInvalidateGLState();

    // Init CPU extensions and lookup tables (deferred from retro_load_game)
    static bool tables_initialized = false;
    if (!tables_initialized) {
        init_supported_CPU_extensions();
        GenerateMirrorLookupTable();
        GenerateSaturateColorTables();
        tables_initialized = true;
        log_printf("libretro: tables initialized in context_reset\n");
    }

    // Setup GL state
    glViewport(0, 0, FB_WIDTH, FB_HEIGHT);
#if !defined(NUANCE_GLES)
    // The matrix stack is only still here for the no-shader fallback: the
    // shader path gets the same projection through the u_mvp uniform, and GLES
    // has no fixed-function matrices at all.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
#endif
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
}

static void context_destroy(void)
{
    gl_initialized = false;
    // GL objects die with the context; make sure they are rebuilt after the next
    // context_reset rather than reused with stale handles.
    VideoInvalidateGLState();
}

// --- Libretro API ---

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
    struct retro_log_callback logging;
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
        log_cb = logging.log;
    bool no_game = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

    static struct retro_input_descriptor desc[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "NUON" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L Shoulder" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R Shoulder" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "C-Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "C-Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "C-Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "C-Right" },
        { 0, 0, 0, 0, NULL },
    };
    cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t) {}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "Nuance";
    info->library_version = "0.6.7";
    info->valid_extensions = "run|cof|nuon|cd|iso|img|mdf|chd|nuz";
    info->need_fullpath = true;
    info->block_extract = false; // let RetroArch extract ZIPs for us
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = FB_WIDTH;
    info->geometry.base_height = FB_HEIGHT;
    info->geometry.max_width = FB_WIDTH;
    info->geometry.max_height = FB_HEIGHT;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = VIDEO_HZ;
    info->timing.sample_rate = 32000.0;
}

void retro_init(void)
{
    log_printf("libretro: retro_init start\n");
    fflush(stderr);
    // Defer heavy init to retro_load_game
    log_printf("libretro: retro_init done\n");
    fflush(stderr);
}

void StopRendererWorkers();

void retro_deinit(void)
{
    StopRendererWorkers();
    if (getenv("NUANCE_AOT_STATS")) AotPrintStats();
    log_printf("libretro: retro_deinit\n");
    CleanupArchives();
}

void retro_set_controller_port_device(unsigned, unsigned) {}

void retro_reset(void)
{
    for (int i = 0; i < 4; i++)
        nuonEnv.mpe[i].Reset();
}

bool retro_load_game(const struct retro_game_info *game)
{
    if (!game || !game->path) return false;

    log_printf("libretro: loading %s\n", game->path); fflush(stderr);

    // Defer CPU init to context_reset to avoid potential issues

    // Request OpenGL context
#if defined(NUANCE_GLES)
    // Android, iOS and tvOS have OpenGL ES and nothing else. Version 3 rather
    // than 2 for one reason: the texture formats. GL_RGBA8, GL_RG8 and GL_RG
    // are what the video buffers are declared with, and ES2 has no sized
    // internal formats and no two-channel one at all.
    hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES3;
#else
    hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
#endif
    hw_render.context_reset = context_reset;
    hw_render.context_destroy = context_destroy;
    hw_render.depth = false;
    hw_render.stencil = false;
    hw_render.bottom_left_origin = true;
    hw_render.version_major = 2;
    hw_render.version_minor = 1;
    hw_render.cache_context = true;

    log_printf("libretro: requesting HW render...\n"); fflush(stderr);
    if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render)) {
        log_printf("libretro: HW render not available, using software\n"); fflush(stderr);
    } else {
        log_printf("libretro: HW render OK\n"); fflush(stderr);
    }

    // Find game file
    log_printf("libretro: ResolveGameFile(%s)...\n", game->path); fflush(stderr);
    std::string gamePath = ResolveGameFile(game->path);
    log_printf("libretro: ResolveGameFile returned: '%s'\n", gamePath.c_str()); fflush(stderr);
    if (gamePath.empty()) {
        log_printf("libretro: cannot find NUON game in %s\n", game->path);
        return false;
    }

    log_printf("libretro: game file found: %s\n", gamePath.c_str()); fflush(stderr);

    // Initialize emulator
    // Find bios files - check system directory BEFORE Init (bios.cof is loaded during Init)
    const char* sysDir = nullptr;
    environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysDir);
    if (sysDir) {
        std::string biosPath = std::string(sysDir) + "/nuance/bios.cof";
        if (access(biosPath.c_str(), F_OK) == 0) {
            chdir((std::string(sysDir) + "/nuance").c_str());
            log_printf("libretro: bios dir: %s/nuance\n", sysDir); fflush(stderr);
        }
    }
    // Also check build directory (for development)
    if (access("bios.cof", F_OK) != 0) {
        // Try the directory containing the core library
        std::string soDir;
#ifdef _WIN32
        HMODULE hm = nullptr;
        char modPath[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(void*)retro_init, &hm) &&
            GetModuleFileNameA(hm, modPath, sizeof(modPath))) {
            soDir = modPath;
            size_t pos = soDir.find_last_of("/\\");
            if (pos != std::string::npos) soDir = soDir.substr(0, pos);
            else soDir.clear();
        }
#else
        Dl_info dl_info;
        if (dladdr((void*)retro_init, &dl_info) && dl_info.dli_fname) {
            soDir = dl_info.dli_fname;
            size_t pos = soDir.rfind('/');
            if (pos != std::string::npos) soDir = soDir.substr(0, pos);
            else soDir.clear();
        }
#endif
        if (!soDir.empty() && access((soDir + "/bios.cof").c_str(), F_OK) == 0) {
            chdir(soDir.c_str());
            log_printf("libretro: bios dir (from core lib): %s\n", soDir.c_str()); fflush(stderr);
        }
    }

    log_printf("libretro: nuonEnv.Init()...\n"); fflush(stderr);
    nuonEnv.Init();
    if (const char* jl = getenv("NUANCE_JOBLOG")) g_joblog = fopen(jl, "w");
    log_printf("libretro: nuonEnv.Init() done\n"); fflush(stderr);

    // Load game
    bool ok = nuonEnv.mpe[3].LoadNuonRomFile(gamePath.c_str());
    if (!ok) ok = nuonEnv.mpe[3].LoadCoffFile(gamePath.c_str());
    if (!ok) {
        log_printf("libretro: failed to load %s\n", gamePath.c_str());
        return false;
    }

    nuonEnv.SetDVDBaseFromFileName(gamePath.c_str());
    for(int i=0;i<4;i++) log_printf("libretro: mpe%d inten1=%08X inten2sel=%u intsrc=%08X intctl=%08X mpectl=%08X\n", i, nuonEnv.mpe[i].inten1, nuonEnv.mpe[i].inten2sel, nuonEnv.mpe[i].intsrc, nuonEnv.mpe[i].intctl, nuonEnv.mpe[i].mpectl); fflush(stderr);
    nuonEnv.mpe[3].Go();
    // Process any pending comm requests from BIOS init before starting emulation
    while (nuonEnv.pendingCommRequests) {
        log_printf("libretro: processing %d pending comm requests from init\n", nuonEnv.pendingCommRequests); fflush(stderr);
        DoCommBusController();
    }
    bRun = true;
    game_loaded = true;

    log_printf("libretro: game loaded successfully\n");
    return true;
}

bool retro_load_game_special(unsigned, const struct retro_game_info*, size_t) { return false; }

void retro_unload_game(void)
{
    StopRendererWorkers();
    bRun = false;
    game_loaded = false;
    VideoCleanup();
}

// Whether the game has re-armed its audio interrupt and none is pending: then the next period can be pushed.
static bool AudioReady()
{
    return nuonEnv.pNuonAudioBuffer &&
        ((nuonEnv.nuonAudioChannelMode & (ENABLE_WRAP_INT | ENABLE_HALF_INT)) != (nuonEnv.oldNuonAudioChannelMode & (ENABLE_WRAP_INT | ENABLE_HALF_INT))) &&
        ((((nuonEnv.mpe[0].intsrc & nuonEnv.mpe[0].inten1) | (nuonEnv.mpe[1].intsrc & nuonEnv.mpe[1].inten1) | (nuonEnv.mpe[2].intsrc & nuonEnv.mpe[2].inten1) | (nuonEnv.mpe[3].intsrc & nuonEnv.mpe[3].inten1)) & INT_AUDIO) == 0);
}

static void FireVideoField()
{
    IncrementVideoFieldCounter();
    nuonEnv.TriggerVideoInterrupt();
    nuonEnv.trigger_render_video = true;
    const uint32 fieldCounter = SwapBytes(*((uint32*)&nuonEnv.systemBusDRAM[VIDEO_FIELD_COUNTER_ADDRESS & SYSTEM_BUS_VALID_MEMORY_MASK]));
    if (fieldCounter >= nuonEnv.MPE3wait_fieldCounter)
        nuonEnv.MPE3wait_fieldCounter = 0;
}


// Parking (llamasoft-nuon): known wait loops whose only job is to watch for something another MPE (or an
// interrupt) does. Executing them costs a dispatch per block every round and changes nothing, so an MPE that
// reaches the loop's head with nothing to do is parked: skipped each round until what it waits for has happened
// (or it has an enabled interrupt to take), then it resumes at the loop head and leaves the loop by itself.
// The loop is recognised by its code bytes (MPE code is swapped in as overlays). NUANCE_NOPARK=1: off.
struct ParkPoint {
    int mpe;
    uint32 pc;
    const uint8* code;       // expected bytes at pc
    uint32 codeLength;
    bool (*waiting)();       // true while the thing it waits for hasn't happened
};

static const uint8 kT3kMpe3MailboxWait[] = {   // $800150A0: jsr DCacheFlush; OR the 3 words at $8003DE50; bra ne
    0x8c,0x00,0x00,0x00,0x92,0x30,0xbc,0x01,0x94,0x90,0xb6,0x00,0xdc,0xa3,0x8c,0x00,0x1e,0xf2,0xdc,0xe0,
    0x00,0xe4,0x10,0xe7,0xc8,0x86,0x04,0x84,0x14,0x25,0xea,0x7d,0x28,0xc7,0x81,0x00,0x90,0x0e,0xb9,0xff };
static const uint8 kT3kMpe0JobPoll[] = {       // $20300000: ld intvec1; jsr (r0-12) = dequeue; cmp #3, r4; bra ne
    0xd1,0x60,0x81,0x00,0x15,0x80,0x92,0x20,0xbf,0x00,0x20,0x64,0x90,0x1a,0xb9,0xff };

static inline uint32 MemWord(int mpe, uint32 address)
{
    return *(const uint32*)nuonEnv.mpe[mpe].GetPointerToMemoryBank(address);   // (byte order irrelevant here)
}
static bool T3kMpe3Waiting()
{
    // Busy while any of the renderers' completion words is non-zero.
    return (*(const uint32*)&nuonEnv.systemBusDRAM[0x3DE50] | *(const uint32*)&nuonEnv.systemBusDRAM[0x3DE54] |
            *(const uint32*)&nuonEnv.systemBusDRAM[0x3DE58]) != 0;
}
static bool T3kMpe0Waiting()
{
    // The command queue is empty while its write and read indices are equal.
    return MemWord(0, 0x20101C18) == MemWord(0, 0x20101C1C);
}

static const ParkPoint kParkPoints[] = {
    { 3, 0x800150A0, kT3kMpe3MailboxWait, sizeof(kT3kMpe3MailboxWait), T3kMpe3Waiting },
    { 0, 0x20300000, kT3kMpe0JobPoll, sizeof(kT3kMpe0JobPoll), T3kMpe0Waiting },
};
static const bool g_park_enabled = getenv("NUANCE_NOPARK") == nullptr;
static const ParkPoint* g_parked[4] = {};
uint64 g_parked_calls[4] = {};

static inline bool InterruptPending(const MPE& m)
{
    return (m.intsrc & (m.inten1 | (1U << m.inten2sel))) != 0;
}

// After an MPE ran a block: park it if it is at a known wait loop's head with nothing to do.
static inline void MaybePark(int i)
{
    MPE& m = nuonEnv.mpe[i];
    for (const ParkPoint& pp : kParkPoints) {
        if (pp.mpe != i || m.pcexec != pp.pc || m.ecuSkipCounter != 0 || InterruptPending(m)) continue;
        if (memcmp(m.GetPointerToMemoryBank(pp.pc), pp.code, pp.codeLength) != 0) continue;
        if (pp.waiting()) g_parked[i] = &pp;
    }
}

// Rounds r .. r_end: one block per MPE each (MPE3 skipped while it waits in VidSync), then the comm bus and
// scheduled interrupts. kInstr: with the NUANCE_PROFILE / NUANCE_DEBUG_HALTS instrumentation.
template <bool kInstr>
static void RunRounds(uint64& r, const uint64 r_end, uint64_t packets[4], uint64_t& stalled)
{
    for (; r < r_end; ++r)
    {
        if (nuonEnv.MPE3wait_fieldCounter != 0) ++stalled;
        for (int i = 3; i >= 0; --i)
        {
            if (i == 3 && nuonEnv.MPE3wait_fieldCounter != 0) continue;
            if (g_parked[i]) {
                if (g_parked[i]->waiting() && !InterruptPending(nuonEnv.mpe[i])) { ++g_parked_calls[i]; continue; }
                g_parked[i] = nullptr;
            }
            const uint64 tc0 = kInstr && g_profile ? __rdtsc() : 0;
            const uint32 pc_in = nuonEnv.mpe[i].pcexec;
            // An AOT-compiled module runs the block if this MPE is in one (aot.h); else the interpreter / JIT.
            const bool ran = AotTry(nuonEnv.mpe[i]) || nuonEnv.mpe[i].FetchDecodeExecute();
            packets[i] += nuonEnv.mpe[i].cycleCounter;
            if (g_park_enabled) MaybePark(i);
            if (kInstr && g_profile) {
                g_prof.mpe_ticks[i] += __rdtsc() - tc0;
                ++g_prof.mpe_calls[i];
                g_prof.mpe_packets[i] += nuonEnv.mpe[i].cycleCounter;
                if (!ran) ++g_prof.mpe_idle[i];
                else {
                    if (pc_in == g_prof.last_pc[i][0]) ++g_prof.mpe_self[i];
                    else if (pc_in == g_prof.last_pc[i][1]) ++g_prof.mpe_pair[i];
                    g_prof.last_pc[i][1] = g_prof.last_pc[i][0];
                    g_prof.last_pc[i][0] = pc_in;
                    if (nuonEnv.mpe[i].cycleCounter == 1) ++g_prof.mpe_single[i];
                    if (g_pchist) {
                        auto& e = g_pc_packets[i][pc_in];
                        ++e.first;
                        e.second += nuonEnv.mpe[i].cycleCounter ? nuonEnv.mpe[i].cycleCounter : 1;
                    }
                }
            }
            if (kInstr && g_joblog) {   // MPE starts (with the start pc and r0-r3) and halts
                static bool was_running_j[4] = {};
                if (ran != was_running_j[i]) {
                    const MPE& m = nuonEnv.mpe[i];
                    if (ran) fprintf(g_joblog, "f%u start mpe%d pc %08X r0-3 %08X %08X %08X %08X\n", SwapBytes(*((uint32*)&nuonEnv.systemBusDRAM[VIDEO_FIELD_COUNTER_ADDRESS & SYSTEM_BUS_VALID_MEMORY_MASK])),
                        i, pc_in, m.regs[0], m.regs[1], m.regs[2], m.regs[3]);
                    if (ran) JobLogDescriptor(g_joblog, m.regs[0]);
                    if (!ran) fprintf(g_joblog, "f%u halt mpe%d pc %08X\n", SwapBytes(*((uint32*)&nuonEnv.systemBusDRAM[VIDEO_FIELD_COUNTER_ADDRESS & SYSTEM_BUS_VALID_MEMORY_MASK])), i, m.pcexec);
                    was_running_j[i] = ran;
                }
            }
            if (kInstr && g_debug_halts) {   // NUANCE_DEBUG_HALTS=1: each MPE stopping / starting
                static bool was_running[4] = {};
                if (ran != was_running[i]) {
                    fprintf(stderr, "round %llu field %u: mpe%d %s pc=%08X mpectl=%08X excepsrc=%08X\n",
                        (unsigned long long)r, (unsigned)SwapBytes(*((uint32*)&nuonEnv.systemBusDRAM[VIDEO_FIELD_COUNTER_ADDRESS & SYSTEM_BUS_VALID_MEMORY_MASK])),
                        i, ran ? "runs" : "halts", nuonEnv.mpe[i].pcexec, nuonEnv.mpe[i].mpectl, nuonEnv.mpe[i].excepsrc);
                    was_running[i] = ran;
                }
            }
        }
        const uint64 to0 = kInstr && g_profile ? __rdtsc() : 0;
        if (nuonEnv.pendingCommRequests)
            DoCommBusController();
        nuonEnv.TriggerScheduledInterrupts();
        if (kInstr && g_profile) g_prof.other_ticks += __rdtsc() - to0;
    }
}


// Parallel renderers (llamasoft-nuon; NUANCE_THREADS=0: off). In Tempest 3000 every cross-MPE effect comes from
// MPE3 (it loads the renderers' code, starts them, sends MPE0 its jobs); a renderer only touches its own state and
// shared memory. So rounds go in windows of g_window: MPE0 (this thread), MPE1 and MPE2 (two workers) each run the
// window's rounds at the same time, then MPE3 runs them with the comm bus and scheduled interrupts, the renderers
// paused. Cross-MPE latency grows to a window (256 rounds ~ 0.1% of a field).
static const bool g_threaded = getenv("NUANCE_THREADS") ? atoi(getenv("NUANCE_THREADS")) != 0 : true;
static const uint32 g_window = getenv("NUANCE_WINDOW") ? (uint32)atoi(getenv("NUANCE_WINDOW")) : 64U;   // 256 breaks a boot-time handshake

static void RunMpeRounds(const int i, uint64 count, uint64& packets)
{
    MPE& m = nuonEnv.mpe[i];
    for (; count; --count) {
        if (g_parked[i]) {
            if (g_parked[i]->waiting() && !InterruptPending(m)) { ++g_parked_calls[i]; continue; }
            g_parked[i] = nullptr;
        }
        if (AotTry(m) || m.FetchDecodeExecute())
            packets += m.cycleCounter;
        if (g_park_enabled) MaybePark(i);
    }
}

struct RendererWorker
{
    std::thread thread;
    std::atomic<uint32> go{0}, done{0};
    int mpe = 0;
    uint64 rounds = 0, packets = 0;
};
static RendererWorker g_workers[2];
static std::atomic<bool> g_workers_quit{false};
static bool g_workers_started = false;

static void WorkerMain(RendererWorker* w)
{
    uint32 seen = 0;
    for (;;) {
        uint32 g;
        int spins = 0;
        while ((g = w->go.load(std::memory_order_acquire)) == seen) {
            if (g_workers_quit.load(std::memory_order_relaxed)) return;
            if (++spins < 20000) _mm_pause(); else std::this_thread::yield();
        }
        seen = g;
        uint64 packets = 0;
        RunMpeRounds(w->mpe, w->rounds, packets);
        w->packets = packets;
        w->done.store(g, std::memory_order_release);
    }
}

static void StartWorkers()
{
    if (g_workers_started) return;
    AotPrepare();   // the module index is built before any thread looks at it
    for (int k = 0; k < 2; ++k) {
        g_workers[k].mpe = k + 1;
        g_workers[k].thread = std::thread(WorkerMain, &g_workers[k]);
    }
    g_workers_started = true;
}

void StopRendererWorkers()
{
    if (!g_workers_started) return;
    g_workers_quit = true;
    for (auto& w : g_workers) if (w.thread.joinable()) w.thread.join();
    g_workers_started = false;
    g_workers_quit = false;
}

// NUANCE_PROFILE_PAR=1: where a threaded field's time goes (renderers phase: MPE0 on this thread, then waiting for
// the workers; MPE3 phase), printed every 300 fields.
static const bool g_profile_par = getenv("NUANCE_PROFILE_PAR") != nullptr;
static uint64 g_par_mpe0 = 0, g_par_wait = 0, g_par_mpe3 = 0, g_par_fields = 0, g_par_t0 = 0;

static void RunRoundsParallel(uint64& r, const uint64 r_end, uint64_t packets[4], uint64_t& stalled)
{
    const uint64 count = r_end - r;
    const uint64 ta = g_profile_par ? __rdtsc() : 0;
    static uint32 generation = 0;
    ++generation;
    for (auto& w : g_workers) { w.rounds = count; w.go.store(generation, std::memory_order_release); }
    uint64 p0 = 0;
    RunMpeRounds(0, count, p0);
    const uint64 tb = g_profile_par ? __rdtsc() : 0;
    for (auto& w : g_workers) {
        int spins = 0;
        while (w.done.load(std::memory_order_acquire) != generation)
            if (++spins < 20000) _mm_pause(); else std::this_thread::yield();
    }
    const uint64 tc = g_profile_par ? __rdtsc() : 0;
    packets[0] += p0;
    packets[1] += g_workers[0].packets;
    packets[2] += g_workers[1].packets;
    // MPE3, with the comm bus and interrupts, the renderers paused.
    MPE& m3 = nuonEnv.mpe[3];
    for (; r < r_end; ++r) {
        if (nuonEnv.MPE3wait_fieldCounter != 0) ++stalled;
        else if (g_parked[3] && g_parked[3]->waiting() && !InterruptPending(m3)) ++g_parked_calls[3];
        else {
            g_parked[3] = nullptr;
            if (AotTry(m3) || m3.FetchDecodeExecute())
                packets[3] += m3.cycleCounter;
            if (g_park_enabled) MaybePark(3);
        }
        if (nuonEnv.pendingCommRequests)
            DoCommBusController();
        nuonEnv.TriggerScheduledInterrupts();
    }
    if (g_profile_par) {
        g_par_mpe0 += tb - ta;
        g_par_wait += tc - tb;
        g_par_mpe3 += __rdtsc() - tc;
    }
}

// Idle fast-forward (NUANCE_NOIDLESKIP=1: off): see RunEmulated.
static const bool g_idle_skip = getenv("NUANCE_NOIDLESKIP") == nullptr;
uint64 g_idle_rounds = 0;   // statistics: rounds skipped

static bool AllIdle()
{
    if (nuonEnv.MPE3wait_fieldCounter == 0 || nuonEnv.pendingCommRequests ||
        *(volatile LONG*)&nuonEnv.schedule_intsrc != 0)
        return false;
    for (int i = 0; i < 3; ++i) {
        const MPE& m = nuonEnv.mpe[i];
        if (!(m.mpectl & MPECTRL_MPEGO)) continue;   // halted
        if (g_parked[i] && g_parked[i]->waiting() && !InterruptPending(m)) continue;   // parked, nothing to wake it
        return false;
    }
    return true;
}

// One video field of emulated time: g_rounds_per_field rounds of one block per MPE (MPE3 skipped while it waits
// in VidSync), the emulated clock advancing evenly; timers and audio checked every 64 rounds (~5 us).
static uint64 next_timer0 = 0, next_timer1 = 0, next_audio = 0;   // emulated-time event schedule (save states)

static void RunEmulated(uint64_t packets[4], uint64_t& rounds, uint64_t& stalled)
{
    const double cycles_per_us = g_emu_mhz;
    const uint64 field_cycles = (uint64)(g_emu_mhz * 1e6 / VIDEO_HZ + 0.5);
    const uint64 field_start = g_emu_cycles;
    const uint64 R = g_rounds_per_field;

    const uint64 field_t0 = g_profile ? __rdtsc() : 0;
    for (uint64 r = 0; r < R && bRun; )
    {
        const bool instrumented = g_profile || g_debug_halts || g_joblog;
        const bool parallel = g_threaded && !instrumented;
        if (parallel) StartWorkers();
        // Idle fast-forward: with MPE3 stalled until the next field and every renderer halted or parked with
        // nothing to wake it, nothing can change before the next timer / audio event or the field's end: instead of
        // running a window, jump to the first 64-round check point where that event fires - the checks below then
        // run at exactly the round they would have run at anyway.
        bool skipped = false;
        if (g_idle_skip && r > 0 && AllIdle()) {
            const uint64 t0 = (uint64)(nuonEnv.timer_rate[0] * cycles_per_us);
            const uint64 t1 = (uint64)(nuonEnv.timer_rate[1] * cycles_per_us);
            const uint64 ta = (uint64)(nuonEnv.AudioPeriodIntervalUs() * cycles_per_us);
            uint64 next = field_start + field_cycles;
            if (t0 > 0 && next_timer0) next = (std::min)(next, next_timer0);
            if (t1 > 0 && next_timer1) next = (std::min)(next, next_timer1);
            if (ta > 0 && AudioReady()) next = (std::min)(next, next_audio);
            if (next > g_emu_cycles) {
                uint64 target = ((next - field_start) * R + field_cycles - 1) / field_cycles;
                target = ((target + 63) / 64) * 64;
                while (target > r + 64 && field_start + field_cycles * (target - 64) / R >= next) target -= 64;
                target = (std::min)(target, R);
                if (target > r + 64) {   // (a single window: just run it)
                    stalled += target - r;
                    g_idle_rounds += target - r;
                    r = target;
                    skipped = true;
                }
            }
        }
        const uint64 r_end = (std::min<uint64>)(r + (parallel ? g_window : 64), R);
        if (skipped)
            ;
        else if (instrumented)
            RunRounds<true>(r, r_end, packets, stalled);
        else if (parallel)
            RunRoundsParallel(r, r_end, packets, stalled);
        else
            RunRounds<false>(r, r_end, packets, stalled);
        rounds = r;
        g_emu_cycles = field_start + field_cycles * r / R;

        const uint64 t0 = (uint64)(nuonEnv.timer_rate[0] * cycles_per_us);
        if (t0 > 0) {
            if (next_timer0 == 0 || next_timer0 > g_emu_cycles + t0) next_timer0 = g_emu_cycles + t0;
            if (g_emu_cycles >= next_timer0) { nuonEnv.ScheduleInterrupt(INT_SYSTIMER0); next_timer0 += t0; if (next_timer0 <= g_emu_cycles) next_timer0 = g_emu_cycles + t0; }
        } else next_timer0 = 0;
        const uint64 t1 = (uint64)(nuonEnv.timer_rate[1] * cycles_per_us);
        if (t1 > 0) {
            if (next_timer1 == 0 || next_timer1 > g_emu_cycles + t1) next_timer1 = g_emu_cycles + t1;
            if (g_emu_cycles >= next_timer1) { nuonEnv.ScheduleInterrupt(INT_SYSTIMER1); next_timer1 += t1; if (next_timer1 <= g_emu_cycles) next_timer1 = g_emu_cycles + t1; }
        } else next_timer1 = 0;
        const uint64 ta = (uint64)(nuonEnv.AudioPeriodIntervalUs() * cycles_per_us);
        if (ta > 0) {
            if (g_emu_cycles >= next_audio && AudioReady() && nuonEnv.TryPushAudioPeriod())
                next_audio = g_emu_cycles + ta;
        } else next_audio = g_emu_cycles;

    }
    g_emu_cycles = field_start + field_cycles;
    FireVideoField();
    if (g_profile_par) {
        const uint64 now = __rdtsc();
        if (!g_par_t0) g_par_t0 = now;
        if (++g_par_fields == 300) {
            const double all = (double)(now - g_par_t0);
            fprintf(stderr, "par: MPE0 %.0f%%, waiting for MPE1/2 %.0f%%, MPE3 %.0f%%, rest %.0f%% (%.1f Mticks/field)\n",
                100.0 * g_par_mpe0 / all, 100.0 * g_par_wait / all, 100.0 * g_par_mpe3 / all,
                100.0 * (all - g_par_mpe0 - g_par_wait - g_par_mpe3) / all, all / 300 / 1e6);
            g_par_mpe0 = g_par_wait = g_par_mpe3 = g_par_fields = 0;
            g_par_t0 = now;
        }
    }
    AotCollectFlush();
    if (g_profile) {
        g_prof.field_ticks += __rdtsc() - field_t0;
        if (++g_prof.fields == 300) {
            const double tot = (double)g_prof.field_ticks;
            fprintf(stderr, "profile (300 fields, %.0f Mticks/field):", tot / 300 / 1e6);
            uint64 accounted = g_prof.other_ticks;
            for (int i = 0; i < 4; ++i) {
                accounted += g_prof.mpe_ticks[i];
                fprintf(stderr, " | mpe%d %.1f%% %.1f ns/call %.2f pk/call single %.0f%% idle %.0f%% self-loop %.0f%% 2-loop %.0f%%", i,
                    100.0 * g_prof.mpe_ticks[i] / tot,
                    g_prof.mpe_calls[i] ? g_prof.mpe_ticks[i] / 3.0 / g_prof.mpe_calls[i] : 0.0,   // ~3 GHz TSC
                    g_prof.mpe_calls[i] ? (double)g_prof.mpe_packets[i] / g_prof.mpe_calls[i] : 0.0,
                    g_prof.mpe_calls[i] ? 100.0 * g_prof.mpe_single[i] / g_prof.mpe_calls[i] : 0.0,
                    g_prof.mpe_calls[i] ? 100.0 * g_prof.mpe_idle[i] / g_prof.mpe_calls[i] : 0.0,
                    g_prof.mpe_calls[i] ? 100.0 * g_prof.mpe_self[i] / g_prof.mpe_calls[i] : 0.0,
                    g_prof.mpe_calls[i] ? 100.0 * g_prof.mpe_pair[i] / g_prof.mpe_calls[i] : 0.0);
            }
            for (int i = 0; i < 4; ++i) {
                const double n = (double)(g_dispatch[i][0] + g_dispatch[i][1] + g_dispatch[i][2] + g_dispatch[i][3]);
                if (n > 0) fprintf(stderr, " | mpe%d calls: compiled %.0f%% delay-slot %.0f%% interpNext %.0f%% uncompiled %.0f%%", i,
                    100 * g_dispatch[i][0] / n, 100 * g_dispatch[i][1] / n, 100 * g_dispatch[i][2] / n, 100 * g_dispatch[i][3] / n);
            }
            memset(g_dispatch, 0, sizeof(g_dispatch));
            if (g_pchist) {
                for (int m = 0; m < 4; ++m) {
                    std::vector<std::pair<uint32, std::pair<uint64, uint64>>> v(g_pc_packets[m].begin(), g_pc_packets[m].end());
                    uint64 total = 0;
                    for (auto& x : v) total += x.second.second;
                    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.second > b.second.second; });
                    fprintf(stderr, "\n  mpe%d packets by block pc (%zu pcs):", m, v.size());
                    for (size_t k = 0; k < v.size() && k < 14; ++k)
                        fprintf(stderr, "\n    %08X %5.1f%%  calls %llu  %.1f pk/call", v[k].first,
                            100.0 * v[k].second.second / (total ? total : 1), (unsigned long long)v[k].second.first,
                            (double)v[k].second.second / v[k].second.first);
                    g_pc_packets[m].clear();
                }
            }
            if (g_trackUncompiled) {
                std::vector<std::pair<uint64, std::pair<uint64, uint32>>> v(g_uncompiled.begin(), g_uncompiled.end());
                std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second.first > b.second.first; });
                fprintf(stderr, "\n  uncompiled hot spots (%zu pcs):", v.size());
                for (size_t k = 0; k < v.size() && k < 25; ++k)
                    fprintf(stderr, "\n    mpe%u pc %08X  %llu calls  flags %04X (8000 compiled, 4000 never) freq %u", (unsigned)(v[k].first >> 32),
                        (unsigned)v[k].first, (unsigned long long)v[k].second.first, v[k].second.second & 0xFFFF,
                        v[k].second.second >> 16);
                g_uncompiled.clear();
            }
            for (int i = 0; i < 4; ++i) {
                fprintf(stderr, " | mpe%d AOT %.0f%% of packets", i, g_prof.mpe_packets[i] ? 100.0 * (g_aot_packets[i] - g_aot_packets_at_last[i]) / g_prof.mpe_packets[i] : 0.0);
                g_aot_packets_at_last[i] = g_aot_packets[i];
            }
            fprintf(stderr, " | comm+int %.1f%% | rest %.1f%%\n", 100.0 * g_prof.other_ticks / tot,
                100.0 * (tot - accounted) / tot);
            g_prof = Profile{};
        }
    }
}

static void RunRealTime(uint64_t packets[4], uint64_t& rounds, uint64_t& stalled);
static void RenderAndAudio();

void retro_run(void)
{
    if (!game_loaded) return;

    // Input
    input_poll_cb();
    uint16 buttons = GetInputButtons();
    ApplyControllerState(1, buttons);

    nuonEnv.trigger_render_video = false;
    HiresField();   // 720x480 mode: code / data patches
    uint64_t packets[4] = {};
    uint64_t rounds = 0, stalled = 0;
    const uint64 run_start = real_useconds_since_start();
    if (g_emulated)
        RunEmulated(packets, rounds, stalled);
    else
        RunRealTime(packets, rounds, stalled);
    g_run_stats.iterations = rounds;
    g_run_stats.mpe3_stalled = stalled;
    g_run_stats.run_us = real_useconds_since_start() - run_start;
    g_run_stats.field_fired = nuonEnv.trigger_render_video ? 1 : 0;
    g_run_stats.field_counter = SwapBytes(*((uint32*)&nuonEnv.systemBusDRAM[VIDEO_FIELD_COUNTER_ADDRESS & SYSTEM_BUS_VALID_MEMORY_MASK]));
    g_run_stats.vidsync_calls = g_vidsync_waits;
    g_run_stats.vid_flips = g_vid_flips;
    g_run_stats.time_elapsed_calls = g_time_elapsed_calls;
    for (int i = 0; i < 4; ++i) g_run_stats.mpe_packets[i] = packets[i];

    RenderAndAudio();
}

// Nuance's own timing: run until the PC clock says a field is due, or a ~16 ms budget is used up.
static void RunRealTime(uint64_t packets[4], uint64_t& rounds, uint64_t& stalled)
{
    uint32 cycles = 0;
    static uint64 last_time0 = real_useconds_since_start();
    static uint64 last_time1 = real_useconds_since_start();
    static uint64 last_time2 = real_useconds_since_start();
    static uint64 last_time3 = real_useconds_since_start();
    const uint64 frame_budget_start = real_useconds_since_start();
    // During BIOS init (before video is configured), allow longer execution time
    // Use 100ms budget for first 100 frames to allow BIOS init to complete
    static int init_frames = 0;
    const uint64 frame_budget_us = (init_frames < 100) ? 50000 : 16000;
    init_frames++;
    while (bRun && !nuonEnv.trigger_render_video)
    {
        cycles++;
        if (nuonEnv.MPE3wait_fieldCounter != 0) ++stalled;

        for (int i = 3; i >= 0; --i)
            if (i != 3 || nuonEnv.MPE3wait_fieldCounter == 0) {
                nuonEnv.mpe[i].FetchDecodeExecute();
                packets[i] += nuonEnv.mpe[i].cycleCounter;
            }

        if (nuonEnv.pendingCommRequests)
            DoCommBusController();

        if ((cycles % 5000) == 0)
        {
            const uint64 new_time = real_useconds_since_start();
            // Time-based frame budget: break after ~16ms to return control to retroarch
            if ((new_time - frame_budget_start) >= frame_budget_us) {
                static int flog = 0;
                if (flog < 5) { fprintf(stderr, "FRAME[%d]: %u cycles in %lums budget=%lums\n", init_frames, cycles, (unsigned long)(new_time - frame_budget_start)/1000, (unsigned long)frame_budget_us/1000); flog++; }
                break;
            }

            if (nuonEnv.timer_rate[0] > 0 && new_time >= last_time0 + (uint64)nuonEnv.timer_rate[0]) {
                nuonEnv.ScheduleInterrupt(INT_SYSTIMER0); last_time0 = new_time;
            } else if (nuonEnv.timer_rate[0] <= 0) last_time0 = new_time;

            if (nuonEnv.timer_rate[1] > 0 && new_time >= last_time1 + (uint64)nuonEnv.timer_rate[1]) {
                nuonEnv.ScheduleInterrupt(INT_SYSTIMER1); last_time1 = new_time;
            } else if (nuonEnv.timer_rate[1] <= 0) last_time1 = new_time;

            {
                // Video field counter update: use timer_rate[2] if set, otherwise default ~60Hz (16667us)
                const uint64 vidRate = (nuonEnv.timer_rate[2] > 0) ? (uint64)nuonEnv.timer_rate[2] : 16667;
                if (new_time >= last_time2 + vidRate) {
                    FireVideoField();
                    last_time2 = new_time;
                }
            }

            // Audio: feed the host audio ring (RetroArch drains it via
            // DrainAudioRing below). Push one Nuon audio period when the game has
            // re-armed its HALF/WRAP interrupt and INT_AUDIO is not already
            // pending. The pacing interval is the period's TRUE playback duration
            // at the Nuon rate (AudioPeriodIntervalUs), not the 60Hz video field:
            // a period is half the game-chosen DMA buffer (1K..64K), so pacing on
            // the field overproduces for >4K buffers (RetroArch audio_sync then
            // throttles the whole frontend -> slowdown) and underruns for <4K.
            const uint64 audioIntervalUs = nuonEnv.AudioPeriodIntervalUs();
            if (audioIntervalUs > 0) {
                if ((new_time >= last_time3 + audioIntervalUs) && AudioReady()) {
                    if (nuonEnv.TryPushAudioPeriod())
                        last_time3 = new_time;
                }
            } else last_time3 = new_time;
        }

        nuonEnv.TriggerScheduledInterrupts();
    }
    rounds = cycles;
}

static void RenderAndAudio()
{
    // Render video
    if (gl_initialized) {
        glBindFramebuffer(GL_FRAMEBUFFER, hw_render.get_current_framebuffer());
        glViewport(0, 0, FB_WIDTH, FB_HEIGHT);
        // Once: what a black screen comes down to is in these.
        static bool reported = false;
        const GLenum fbStatus = reported ? GL_FRAMEBUFFER_COMPLETE : glCheckFramebufferStatus(GL_FRAMEBUFFER);
        RenderVideo(FB_WIDTH, FB_HEIGHT);
        if (!reported) {
            reported = true;
            log_printf("libretro: first frame: GL %s on %s, framebuffer %u (status 0x%04X), GL error 0x%04X\n",
                (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
                (unsigned)hw_render.get_current_framebuffer(), (unsigned)fbStatus, (unsigned)glGetError());
        }
        // A few times once the game runs: whether the Nuon has a picture,
        // whether drawing it raised a GL error, and what landed in
        // RetroArch's framebuffer - which of the three a black screen is
        // (#79). At about 5, 20 and 60 seconds.
        {
            static unsigned frames = 0;
            ++frames;
            if (frames == 300 || frames == 1200 || frames == 3600) {
                char state[256];
                VideoDebugState(state, sizeof(state));
                const GLenum err = glGetError();
                unsigned char px[3][4] = {};
                glReadPixels(FB_WIDTH / 2, FB_HEIGHT / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[0]);
                glReadPixels(FB_WIDTH / 4, FB_HEIGHT / 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[1]);
                glReadPixels(FB_WIDTH * 3 / 4, FB_HEIGHT * 3 / 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[2]);
                log_printf("libretro: frame %u: %s; GL error 0x%04X; framebuffer %u pixels %02X%02X%02X %02X%02X%02X %02X%02X%02X\n",
                    frames, state, (unsigned)err, (unsigned)hw_render.get_current_framebuffer(),
                    px[0][0], px[0][1], px[0][2], px[1][0], px[1][1], px[1][2], px[2][0], px[2][1], px[2][2]);
            }
        }
        video_cb(RETRO_HW_FRAME_BUFFER_VALID, FB_WIDTH, FB_HEIGHT, 0);
    } else {
        // Software fallback - black frame
        memset(framebuffer, 0, sizeof(framebuffer));
        video_cb(framebuffer, FB_WIDTH, FB_HEIGHT, FB_WIDTH * 4);
    }

    // Audio: drain the host audio ring. DrainAudioRing emits native-endian s16
    // (the ring holds little-endian samples; swapped only on big-endian hosts),
    // so what audio_batch_cb expects
    uint32 frames = nuonEnv.DrainAudioRing(audio_buffer, AUDIO_BUFFER_SIZE / 2);
    if (frames)
        audio_batch_cb(audio_buffer, frames);
}

// Save states (llamasoft-nuon). Taken between retro_run calls, when every MPE (and the renderer threads) is between
// rounds: the RAM, the four MPEs (registers, local memory), the BIOS state Nuance keeps on the host (heap, media
// callbacks, interrupt waits), video channels, emulated time and the timer schedule, parked wait loops. Compiled
// code is not saved: every code cache is flushed on load and instruction RAM re-hashed (overlays by content), and
// the AOT modules re-validated. Not saved: settings (timing, flip cap, hires - a load into another hires mode is
// refused), capture / probe logs, the host audio ring.
static void MpeState(StateIO& s, MPE& m)
{
    s.tag(0x4D504530 + m.mpeIndex);   // MPE0..3
    s.raw(m.reg_union, sizeof m.reg_union);
    s.raw(m.tempreg_union, sizeof m.tempreg_union);
    uint32* const ctl[] = { &m.mpectl, &m.excepsrc, &m.excepclr, &m.excephalten, &m.pcfetch, &m.pcroute, &m.pcexec,
        &m.intvec1, &m.intvec2, &m.intsrc, &m.intclr, &m.intctl, &m.inten1, &m.inten1set, &m.inten1clr, &m.inten2sel,
        &m.xybase, &m.uvbase, &m.linpixctl, &m.clutbase, &m.sp, &m.dabreak, &m.odmactl, &m.odmacptr, &m.mdmactl,
        &m.mdmacptr, &m.comminfo, &m.commctl, &m.dcachectl, &m.icachectl, &m.vdmactla, &m.vdmactlb, &m.vdmaptra,
        &m.vdmaptrb, &m.pcfetchnext, &m.ecuSkipCounter, &m.overlayMask, &m.interpretNextPacket, &m.ba_reg_offset,
        &m.ba_control, &m.ba_mipped_xoffset };
    for (uint32* r : ctl) s.pod(*r);
    s.raw(m.commxmit, sizeof m.commxmit);
    s.raw(m.commrecv, sizeof m.commrecv);
    s.pod(m.cycleCounter);
    s.pod(m.bInterpretedBranchTaken);
    s.pod(m.nuances_use_tempreg_union);
    static const bool full_mem = getenv("NUANCE_RA_FULLMEM") != nullptr;   // tests: every page in the snapshot
    if (!s.fast || full_mem) {
        s.raw(m.dtrom, sizeof m.dtrom);
        return;
    }
    // Run-ahead snapshot: the 8 MB array is the MPE's whole local address window, but only a few pages are memory
    // (measured on T3K: data RAM 4-8 KB at +$100000, instruction RAM 4-8 KB at +$300000; the rest is never written).
    // Saved: 64 KB from each of those two, plus every page that isn't one byte repeated (anything else the game wrote),
    // re-scanned every 256 snapshots. A page first written between two scans and then rewound would keep its write: the
    // run-ahead checks (picture byte-identical to a run without) are what guard that.
    static std::vector<uint32> pages[4];
    static uint32 scans_left[4] = {};
    std::vector<uint32>& pg = pages[m.mpeIndex];
    if (!s.loading && scans_left[m.mpeIndex]-- == 0) {
        scans_left[m.mpeIndex] = 255;
        pg.clear();
        constexpr uint32 kPage = 4096, kLive = 64 * 1024;
        for (uint32 a = 0; a < MPE_LOCAL_MEMORY_SIZE; a += kPage) {
            const bool fixed = (a >= MPE_DTRAM_OFFSET && a < MPE_DTRAM_OFFSET + kLive) ||
                               (a >= MPE_IRAM_OFFSET && a < MPE_IRAM_OFFSET + kLive);
            bool uniform = !fixed;
            for (uint32 k = 1; uniform && k < kPage; ++k) uniform = m.dtrom[a + k] == m.dtrom[a];
            if (!uniform) pg.push_back(a);
        }
    }
    uint32 n = (uint32)pg.size();
    s.pod(n);
    if (s.loading && n != pg.size()) { s.ok = false; return; }   // (only ever loaded right after its own save)
    for (uint32 a : pg) s.raw(m.dtrom + a, 4096);
}

// Run-ahead: code written outside instruction RAM since the snapshot (main / system bus) means its compiled code
// must go on a rewind; counted in MPE::UpdateInvalidateRegion.
uint32 g_code_writes_outside_iram = 0;
static uint32 g_ra_light_loads = 0, g_ra_full_loads = 0;   // run-ahead rewinds (NUANCE_RA_STATS=1 prints them)

static bool StateWalk(StateIO& s)
{
    s.tag(0x4E554F4E);   // NUON
    uint32 version = 1;
    s.pod(version);
    if (version != 1) return false;
    uint8 hires = g_hires ? 1 : 0, hires_was = hires;
    s.pod(hires);
    if (s.loading && hires != hires_was) return false;
    s.tag(0x52414D30);   // RAM0
    // (a run-ahead snapshot leaves out the main bus's upper 8 MB: only the high-resolution mode's frame buffers live there)
    s.raw(nuonEnv.mainBusDRAM, s.fast && !g_hires ? MAIN_BUS_SIZE : sizeof nuonEnv.mainBusDRAM);
    s.raw(nuonEnv.systemBusDRAM, sizeof nuonEnv.systemBusDRAM);
    nuonEnv.flashEEPROM.State(s);
    nuonEnv.nuonMemoryManager.State(s);
    s.tag(0x454E5630);   // ENV0
    NuonEnvironment& e = nuonEnv;
    s.pod(e.cycleCounter); s.pod(e.pendingCommRequests);
    s.pod(e.mainChannelUpperLimit); s.pod(e.mainChannelLowerLimit);
    s.pod(e.overlayChannelUpperLimit); s.pod(e.overlayChannelLowerLimit);
    s.pod(e.nuonAudioChannelMode); s.pod(e.oldNuonAudioChannelMode); s.pod(e.nuonAudioBufferSize);
    s.pod(e.nuonSupportedPlaybackRates); s.pod(e.nuonAudioPlaybackRate);
    s.pod(e.bInterlaced); s.pod(e.bProcessorStartStopChange); s.pod(e.whichAudioInterrupt);
    s.pod(e.cyclesPerAudioInterrupt); s.pod(e.audioInterruptCycleCount); s.pod(e.videoDisplayCycleCount);
    s.pod(e.MPE3wait_fieldCounter); s.raw(e.timer_rate, sizeof e.timer_rate);
    LONG sched = e.schedule_intsrc;
    s.pod(sched);
    bool audio_ints = e.GetAudioInterruptsEnabled();
    s.pod(audio_ints);
    // the audio buffer: a pointer into one of the two DRAM arrays -> (which, offset)
    uint32 abuf_where = 0, abuf_off = 0;
    if (!s.loading && e.pNuonAudioBuffer) {
        const uint8* p = e.pNuonAudioBuffer;
        if (p >= e.mainBusDRAM && p < e.mainBusDRAM + sizeof e.mainBusDRAM) abuf_where = 1, abuf_off = (uint32)(p - e.mainBusDRAM);
        else if (p >= e.systemBusDRAM && p < e.systemBusDRAM + sizeof e.systemBusDRAM) abuf_where = 2, abuf_off = (uint32)(p - e.systemBusDRAM);
        else return false;   // somewhere we can't name: refuse
    }
    s.pod(abuf_where); s.pod(abuf_off);
    e.AudioRingState(s);
    for (int i = 0; i < 4; ++i) MpeState(s, nuonEnv.mpe[i]);
    VideoState(s);
    MediaState(s);
    SyscallState(s);
    s.tag(0x54494D30);   // TIM0
    s.pod(g_emu_cycles); s.pod(next_timer0); s.pod(next_timer1); s.pod(next_audio);
    int8_t parked[4];
    for (int i = 0; i < 4; ++i)
        parked[i] = g_parked[i] ? (int8_t)(g_parked[i] - kParkPoints) : (int8_t)-1;
    s.raw(parked, sizeof parked);
    s.tag(0x454E4421);   // END!
    if (!s.ok) return false;
    if (s.loading) {
        e.schedule_intsrc = sched;
        e.SetAudioInterruptsEnabled(audio_ints);
        e.pNuonAudioBuffer = abuf_where == 1 ? e.mainBusDRAM + abuf_off : abuf_where == 2 ? e.systemBusDRAM + abuf_off : nullptr;
        for (int i = 0; i < 4; ++i)
            g_parked[i] = (parked[i] >= 0 && parked[i] < (int)(sizeof kParkPoints / sizeof kParkPoints[0])) ? &kParkPoints[parked[i]] : nullptr;
        extern uint32 g_ra_code_writes_at_save;
        static const bool ra_flush = getenv("NUANCE_RA_FLUSH") != nullptr;   // tests: flush all code as a file load does
        // Run-ahead rewind: the compiled code stays (flushing it every field cost ~3 ms a field in recompiling).
        // Instruction RAM is marked changed, as a code DMA does: on its next use it is re-hashed and matched to the
        // overlay compiled for that content (a slot reused meanwhile is recompiled by the overlay manager); every
        // interpreter cache is cleared (decoded packets of the hidden fields - without this the boot crashed within
        // ~2000 fields: an MPE store through a bad pointer, 2026-11-18). Code written on the main / system bus during
        // the hidden fields: the full flush below (the per-region flush of those caches crashed the boot too).
        const bool outside = g_code_writes_outside_iram != g_ra_code_writes_at_save;
        if (s.fast && !ra_flush && !outside) {
            // MPE3's compiled code goes too (the main program: kept, the boot crashed - measured by flushing each MPE's
            // in turn, 2026-11-18; ~0.4 ms a field). The renderers' (MPE0-2, the heavy part) stays.
            for (int i = 0; i < 4; ++i) {
                MPE& m = nuonEnv.mpe[i];
                if (i == 3) m.nativeCodeCache.Flush();
                m.instructionCache->Invalidate();
                const uint32 lo = MPE_IRAM_BASE, hi = MPE_IRAM_BASE + MPE::overlayLengths[i] - 1;
                if (lo < m.invalidateRegionStart) m.invalidateRegionStart = lo;
                if (hi > m.invalidateRegionEnd) m.invalidateRegionEnd = hi;
                ++g_iramVersion[i];
            }
            ++g_ra_light_loads;
            return true;
        }
        if (s.fast) ++g_ra_full_loads;
        for (int i = 0; i < 4; ++i) {   // compiled code: flush everything, re-hash instruction RAM, re-validate AOT
            MPE& m = nuonEnv.mpe[i];
            m.instructionCache->Invalidate();
            m.nativeCodeCache.Flush();
            m.invalidateRegionStart = MPE_IRAM_BASE;
            m.invalidateRegionEnd = MPE_IRAM_BASE + MPE::overlayLengths[i] - 1;
            m.interpreterInvalidateRegionStart = 0;
            m.interpreterInvalidateRegionEnd = 0xFFFFFFFFU;
            m.bInvalidateInterpreterCache = true;
            m.bInvalidateInstructionCaches = true;
            ++g_iramVersion[i];
        }
    }
    return true;
}

static size_t g_state_size = 0;

size_t retro_serialize_size(void)
{
    if (!game_loaded) return 0;
    if (!g_state_size) {   // measured once, with room for the variable parts (heap lists, media queue)
        StateIO s;
        if (!StateWalk(s)) return 0;
        g_state_size = s.out.size() + (1u << 20);
    }
    return g_state_size;
}

bool retro_serialize(void* data, size_t size)
{
    if (!game_loaded) return false;
    StateIO s;
    if (!StateWalk(s)) return false;
    if (s.out.size() + 8 > size) return false;
    const uint64 n = s.out.size();
    memcpy(data, &n, 8);
    memcpy((uint8*)data + 8, s.out.data(), s.out.size());
    memset((uint8*)data + 8 + n, 0, size - 8 - (size_t)n);
    return true;
}

bool retro_unserialize(const void* data, size_t size)
{
    if (!game_loaded || size < 8) return false;
    uint64 n = 0;
    memcpy(&n, data, 8);
    if (n > size - 8) return false;
    StateIO s;
    s.loading = true;
    s.in = (const uint8*)data + 8;
    s.size = (size_t)n;
    // walk once into scratch first? The walk writes straight into the machine; a bad file stops at its first tag
    // mismatch, so check the header and size before touching anything.
    {
        StateIO probe;
        probe.loading = true;
        probe.in = s.in;
        probe.size = s.size;
        uint32 tag = 0, version = 0;
        probe.pod(tag); probe.pod(version);
        if (!probe.ok || tag != 0x4E554F4E || version != 1) return false;
    }
    if (!StateWalk(s)) return false;
    // llamasoft-nuon: the pad block's pointer is set when the game calls ControllerInitialize at boot; a session that
    // starts from a state never ran it, so the pad would be ignored. The block lives at a fixed bus address.
    if (!controller) controller = (ControllerData *)nuonEnv.GetPointerToMemory(3, CONTROLLER_ADDRESS);
    return true;
}
// llamasoft-nuon: run-ahead snapshots (memory only; the host saves one after each real field, runs hidden fields from
// it and loads it back). StateWalk in fast mode: the MPEs' live pages, main RAM without the hires half, into a buffer
// kept from snapshot to snapshot; the load keeps the compiled code (see StateWalk). Returns the snapshot's bytes / 1.
void CaptureRaState(bool restore);   // capture.cpp
uint32 g_ra_code_writes_at_save = 0;
static StateIO g_ra;
extern "C" size_t nuance_ra_save(void)
{
    if (!game_loaded) return 0;
    g_ra.loading = false;
    g_ra.ok = true;
    g_ra.fast = true;
    g_ra.pos = 0;
    g_ra.out.clear();   // (keeps its capacity: no allocation after the first)
    if (!StateWalk(g_ra)) return 0;
    g_ra_code_writes_at_save = g_code_writes_outside_iram;
    CaptureRaState(false);
    return g_ra.out.size();
}
extern "C" int nuance_ra_load(void)
{
    if (!game_loaded || g_ra.out.empty()) return 0;
    StateIO s;
    s.loading = true;
    s.fast = true;
    s.in = g_ra.out.data();
    s.size = g_ra.out.size();
    if (!StateWalk(s)) return 0;
    CaptureRaState(true);
    static const bool stats = getenv("NUANCE_RA_STATS") != nullptr;
    if (stats && (g_ra_light_loads + g_ra_full_loads) % 600 == 0)
        fprintf(stderr, "run-ahead rewinds: %u kept the compiled code, %u flushed it (code written outside IRAM)\n",
                g_ra_light_loads, g_ra_full_loads);
    return 1;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned, bool, const char*) {}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void *retro_get_memory_data(unsigned) { return nullptr; }
size_t retro_get_memory_size(unsigned) { return 0; }

// Swap buffers stub for video.cpp
void SDL2_SwapWindow() {}
