// nuon_host.cpp - see nuon_host.h.
#include "nuon_host.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../third_party/NuanceResurrection/src/libretro.h"

namespace nuon {
namespace {

// The core's entry points, from the DLL.
struct Core {
  HMODULE dll = nullptr;
  void (*set_environment)(retro_environment_t);
  void (*set_video_refresh)(retro_video_refresh_t);
  void (*set_audio_sample)(retro_audio_sample_t);
  void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
  void (*set_input_poll)(retro_input_poll_t);
  void (*set_input_state)(retro_input_state_t);
  void (*init)();
  void (*deinit)();
  void (*get_system_av_info)(retro_system_av_info*);
  bool (*load_game)(const retro_game_info*);
  void (*unload_game)();
  void (*run)();
  void (*get_run_stats)(RunStats*);   // ours (llamasoft-nuon); null on another build
  void (*set_timing)(int, double);    // ours
  uint8_t* (*get_memory)(int, size_t*);   // ours
} g_core;

retro_system_av_info g_av{};
retro_hw_render_callback g_hw{};
bool g_hw_requested = false;
std::string g_system_dir, g_save_dir;
uint16_t g_buttons = 0;
Frame g_frame;
std::vector<int16_t> g_audio;

// GL: a hidden window's context, an FBO with a 720x480 RGBA8 colour texture.
SDL_Window* g_glwin = nullptr;
SDL_GLContext g_gl = nullptr;
GLuint g_fbo = 0, g_fbtex = 0;
constexpr int kFbW = 720, kFbH = 480;

PFNGLGENFRAMEBUFFERSPROC pglGenFramebuffers;
PFNGLBINDFRAMEBUFFERPROC pglBindFramebuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC pglFramebufferTexture2D;
PFNGLCHECKFRAMEBUFFERSTATUSPROC pglCheckFramebufferStatus;
PFNGLDELETEFRAMEBUFFERSPROC pglDeleteFramebuffers;
void(APIENTRY* pglGenTextures)(GLsizei, GLuint*);
void(APIENTRY* pglBindTexture)(GLenum, GLuint);
void(APIENTRY* pglTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void(APIENTRY* pglTexParameteri)(GLenum, GLenum, GLint);
void(APIENTRY* pglReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
void(APIENTRY* pglDeleteTextures)(GLsizei, const GLuint*);
void(APIENTRY* pglPixelStorei)(GLenum, GLint);

template <typename T> bool Proc(T& f, const char* name) {
  f = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
  return f != nullptr;
}

bool CreateGL(std::string* err) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
  g_glwin = SDL_CreateWindow("nuon-gl", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!g_glwin) { *err = std::string("GL window: ") + SDL_GetError(); return false; }
  g_gl = SDL_GL_CreateContext(g_glwin);
  if (!g_gl) { *err = std::string("GL context: ") + SDL_GetError(); return false; }
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  bool ok = Proc(pglGenFramebuffers, "glGenFramebuffers") && Proc(pglBindFramebuffer, "glBindFramebuffer") &&
            Proc(pglFramebufferTexture2D, "glFramebufferTexture2D") &&
            Proc(pglCheckFramebufferStatus, "glCheckFramebufferStatus") &&
            Proc(pglDeleteFramebuffers, "glDeleteFramebuffers") && Proc(pglGenTextures, "glGenTextures") &&
            Proc(pglBindTexture, "glBindTexture") && Proc(pglTexImage2D, "glTexImage2D") &&
            Proc(pglTexParameteri, "glTexParameteri") && Proc(pglReadPixels, "glReadPixels") &&
            Proc(pglDeleteTextures, "glDeleteTextures") && Proc(pglPixelStorei, "glPixelStorei");
  if (!ok) { *err = "GL: missing entry points (needs OpenGL 2.1 + framebuffer objects)"; return false; }
  pglGenTextures(1, &g_fbtex);
  pglBindTexture(GL_TEXTURE_2D, g_fbtex);
  pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFbW, kFbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  pglBindTexture(GL_TEXTURE_2D, 0);
  pglGenFramebuffers(1, &g_fbo);
  pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbtex, 0);
  GLenum st = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
  pglBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (st != GL_FRAMEBUFFER_COMPLETE) { *err = "GL: framebuffer incomplete"; return false; }
  return true;
}

void DestroyGL() {
  if (g_gl) {
    SDL_GL_MakeCurrent(g_glwin, g_gl);
    if (g_fbo) pglDeleteFramebuffers(1, &g_fbo);
    if (g_fbtex) pglDeleteTextures(1, &g_fbtex);
    g_fbo = g_fbtex = 0;
    SDL_GL_DestroyContext(g_gl);
    g_gl = nullptr;
  }
  if (g_glwin) SDL_DestroyWindow(g_glwin);
  g_glwin = nullptr;
}

uintptr_t GetFramebuffer() { return g_fbo; }
void* GetProcAddress_(const char* sym) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(sym)); }

void LogCb(enum retro_log_level level, const char* fmt, ...) {
  if (level < RETRO_LOG_INFO) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
}

bool EnvCb(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      static_cast<retro_log_callback*>(data)->log = LogCb;
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return *static_cast<const retro_pixel_format*>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *static_cast<const char**>(data) = g_system_dir.c_str();
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *static_cast<const char**>(data) = g_save_dir.c_str();
      return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
      auto* hw = static_cast<retro_hw_render_callback*>(data);
      if (hw->context_type != RETRO_HW_CONTEXT_OPENGL) return false;
      hw->get_current_framebuffer = GetFramebuffer;
      hw->get_proc_address = GetProcAddress_;
      g_hw = *hw;
      g_hw_requested = true;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
      return true;
    default:
      return false;
  }
}

void VideoCb(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (!data) return;  // dupe: keep the last picture
  g_frame.width = w;
  g_frame.height = h;
  g_frame.pixels.resize(size_t(w) * h);
  if (data == RETRO_HW_FRAME_BUFFER_VALID) {
    // BGRA bytes = XRGB8888 words; the core draws bottom-left origin, so flip the rows.
    static std::vector<uint32_t> tmp;
    tmp.resize(size_t(w) * h);
    pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    pglPixelStorei(GL_PACK_ALIGNMENT, 4);
    pglReadPixels(0, 0, GLsizei(w), GLsizei(h), GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, tmp.data());
    for (unsigned y = 0; y < h; ++y)
      std::memcpy(&g_frame.pixels[size_t(y) * w], &tmp[size_t(h - 1 - y) * w], size_t(w) * 4);
  } else {
    for (unsigned y = 0; y < h; ++y)
      std::memcpy(&g_frame.pixels[size_t(y) * w], static_cast<const uint8_t*>(data) + y * pitch, size_t(w) * 4);
  }
}

void AudioCb(int16_t l, int16_t r) {
  g_audio.push_back(l);
  g_audio.push_back(r);
}
size_t AudioBatchCb(const int16_t* d, size_t frames) {
  g_audio.insert(g_audio.end(), d, d + frames * 2);
  return frames;
}
void InputPollCb() {}
int16_t InputStateCb(unsigned port, unsigned device, unsigned, unsigned id) {
  if (port != 0 || device != RETRO_DEVICE_JOYPAD || id > 15) return 0;
  return (g_buttons >> id) & 1;
}

template <typename T> bool Sym(T& f, const char* name) {
  f = reinterpret_cast<T>(::GetProcAddress(g_core.dll, name));
  return f != nullptr;
}

}  // namespace

bool Load(const std::string& core_dll, const std::string& disc, const std::string& system_dir,
          const std::string& save_dir, std::string* err) {
  g_core.dll = LoadLibraryA(core_dll.c_str());
  if (!g_core.dll) { *err = "can't load " + core_dll; return false; }
  bool ok = Sym(g_core.set_environment, "retro_set_environment") &&
            Sym(g_core.set_video_refresh, "retro_set_video_refresh") &&
            Sym(g_core.set_audio_sample, "retro_set_audio_sample") &&
            Sym(g_core.set_audio_sample_batch, "retro_set_audio_sample_batch") &&
            Sym(g_core.set_input_poll, "retro_set_input_poll") && Sym(g_core.set_input_state, "retro_set_input_state") &&
            Sym(g_core.init, "retro_init") && Sym(g_core.deinit, "retro_deinit") &&
            Sym(g_core.get_system_av_info, "retro_get_system_av_info") && Sym(g_core.load_game, "retro_load_game") &&
            Sym(g_core.unload_game, "retro_unload_game") && Sym(g_core.run, "retro_run");
  if (!ok) { *err = core_dll + ": missing libretro entry points"; return false; }
  Sym(g_core.get_run_stats, "nuance_get_run_stats");
  Sym(g_core.set_timing, "nuance_set_timing");
  Sym(g_core.get_memory, "nuance_get_memory");
  g_system_dir = system_dir;
  g_save_dir = save_dir;
  if (!CreateGL(err)) return false;

  g_core.set_environment(EnvCb);
  g_core.set_video_refresh(VideoCb);
  g_core.set_audio_sample(AudioCb);
  g_core.set_audio_sample_batch(AudioBatchCb);
  g_core.set_input_poll(InputPollCb);
  g_core.set_input_state(InputStateCb);
  g_core.init();
  retro_game_info gi{};
  gi.path = disc.c_str();
  if (!g_core.load_game(&gi)) { *err = "core refused " + disc; g_core.deinit(); return false; }
  if (!g_hw_requested) { *err = "core didn't ask for OpenGL"; return false; }
  g_core.get_system_av_info(&g_av);
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  if (g_hw.context_reset) g_hw.context_reset();
  return true;
}

void Unload() {
  if (!g_core.dll) return;
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  if (g_hw.context_destroy) g_hw.context_destroy();
  g_core.unload_game();
  g_core.deinit();
  DestroyGL();
  // The core's threads / statics may outlive deinit: leave the DLL loaded until exit.
}

void SetButtons(uint16_t buttons) { g_buttons = buttons; }

void RunFrame() {
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  g_core.run();
}

const Frame& LastFrame() { return g_frame; }
double Fps() { return g_av.timing.fps; }
double SampleRate() { return g_av.timing.sample_rate; }
std::vector<int16_t>& Audio() { return g_audio; }

uint8_t* Memory(int which, size_t* size) {
  if (!g_core.get_memory) { if (size) *size = 0; return nullptr; }
  return g_core.get_memory(which, size);
}

void ReleaseContext() { SDL_GL_MakeCurrent(g_glwin, nullptr); }
void MakeContextCurrent() { SDL_GL_MakeCurrent(g_glwin, g_gl); }

std::string Disasm(int mpe, uint32_t address, int packets) {
  using Fn = size_t (*)(int, uint32_t, int, char*, size_t);
  auto f = reinterpret_cast<Fn>(::GetProcAddress(g_core.dll, "nuance_disasm"));
  if (!f) return {};
  std::string buf(size_t(packets) * 600 + 1024, '\0');
  buf.resize(f(mpe, address, packets, buf.data(), buf.size()));
  return buf;
}

bool SetFlipCap(bool on) {
  using Fn = void (*)(int);
  auto f = reinterpret_cast<Fn>(::GetProcAddress(g_core.dll, "nuance_set_flip_cap"));
  if (!f) return false;
  f(on ? 1 : 0);
  return true;
}

bool SetTiming(bool emulated, double rounds) {
  if (!g_core.set_timing) return false;
  g_core.set_timing(emulated ? 1 : 0, rounds);
  return true;
}

bool GetRunStats(RunStats* out) {
  if (!g_core.get_run_stats) return false;
  g_core.get_run_stats(out);
  return true;
}

}  // namespace nuon
