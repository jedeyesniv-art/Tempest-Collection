// vita_host.cpp - see vita_host.h.
#include "vita_host.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "../third_party/vita3k-libretro/vita3k/libretro/libretro.h"

namespace vita {
namespace {

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
} g_core;

retro_system_av_info g_av{};
retro_hw_render_callback g_hw{};
bool g_hw_requested = false;
std::string g_system_dir, g_save_dir;
std::map<std::string, std::string> g_options;
uint16_t g_buttons = 0;
int g_sticks[4] = {};
Frame g_frame;
std::vector<int16_t> g_audio;
std::string g_message;
unsigned g_message_serial = 0;
uint64_t g_new_frames = 0;   // pictures the core handed over (not dupes)

// GL: a hidden window's 4.4 core context, an FBO with an RGBA8 colour texture as big as the core's largest picture.
SDL_Window* g_glwin = nullptr;
SDL_GLContext g_gl = nullptr;
GLuint g_fbo = 0, g_fbtex = 0;
int g_fbw = 0, g_fbh = 0;

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
const GLubyte*(APIENTRY* pglGetString)(GLenum);

template <typename T> bool Proc(T& f, const char* name) {
  f = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
  return f != nullptr;
}

bool CreateContext(std::string* err) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  g_glwin = SDL_CreateWindow("vita-gl", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!g_glwin) { *err = std::string("GL window: ") + SDL_GetError(); return false; }
  g_gl = SDL_GL_CreateContext(g_glwin);
  if (!g_gl) { *err = std::string("OpenGL 4.4 core context: ") + SDL_GetError(); return false; }
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  bool ok = Proc(pglGenFramebuffers, "glGenFramebuffers") && Proc(pglBindFramebuffer, "glBindFramebuffer") &&
            Proc(pglFramebufferTexture2D, "glFramebufferTexture2D") &&
            Proc(pglCheckFramebufferStatus, "glCheckFramebufferStatus") &&
            Proc(pglDeleteFramebuffers, "glDeleteFramebuffers") && Proc(pglGenTextures, "glGenTextures") &&
            Proc(pglBindTexture, "glBindTexture") && Proc(pglTexImage2D, "glTexImage2D") &&
            Proc(pglTexParameteri, "glTexParameteri") && Proc(pglReadPixels, "glReadPixels") &&
            Proc(pglDeleteTextures, "glDeleteTextures") && Proc(pglPixelStorei, "glPixelStorei") &&
            Proc(pglGetString, "glGetString");
  if (!ok) { *err = "GL: missing entry points"; return false; }
  std::fprintf(stderr, "vita host: GL %s on %s\n", reinterpret_cast<const char*>(pglGetString(GL_VERSION)),
               reinterpret_cast<const char*>(pglGetString(GL_RENDERER)));
  return true;
}

// The FBO the core draws into, at least w x h (grown when the core reports a bigger picture).
bool SizeFramebuffer(int w, int h) {
  if (w <= g_fbw && h <= g_fbh && g_fbo) return true;
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  if (!g_fbtex) pglGenTextures(1, &g_fbtex);
  pglBindTexture(GL_TEXTURE_2D, g_fbtex);
  pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  pglBindTexture(GL_TEXTURE_2D, 0);
  if (!g_fbo) pglGenFramebuffers(1, &g_fbo);
  pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbtex, 0);
  const GLenum st = pglCheckFramebufferStatus(GL_FRAMEBUFFER);
  pglBindFramebuffer(GL_FRAMEBUFFER, 0);
  g_fbw = w;
  g_fbh = h;
  return st == GL_FRAMEBUFFER_COMPLETE;
}

void DestroyContext() {
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
retro_proc_address_t GetProcAddress_(const char* sym) {
  return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(sym));
}

void LogCb(enum retro_log_level level, const char* fmt, ...) {
  if (level < RETRO_LOG_INFO) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
}

void Message(const char* msg) {
  if (!msg) return;
  g_message = msg;
  ++g_message_serial;
  std::fprintf(stderr, "vita message: %s\n", msg);
}

bool EnvCb(unsigned cmd, void* data) {
  switch (cmd) {   // (the constants carry RETRO_ENVIRONMENT_EXPERIMENTAL where theirs do)
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
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
      return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
      auto* hw = static_cast<retro_hw_render_callback*>(data);
      if (hw->context_type != RETRO_HW_CONTEXT_OPENGL_CORE) return false;
      hw->get_current_framebuffer = GetFramebuffer;
      hw->get_proc_address = GetProcAddress_;
      g_hw = *hw;
      g_hw_requested = true;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      auto* v = static_cast<retro_variable*>(data);
      auto it = g_options.find(v->key ? v->key : "");
      if (it == g_options.end()) return false;   // the core's default
      v->value = it->second.c_str();
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *static_cast<bool*>(data) = false;
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *static_cast<unsigned*>(data) = 2;
      return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
      return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *static_cast<bool*>(data) = true;
      return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      *static_cast<int*>(data) = 3;   // video and audio
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      if (data) *static_cast<bool*>(data) = true;
      return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
      *static_cast<unsigned*>(data) = 1;
      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
      Message(static_cast<const retro_message*>(data)->msg);
      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
      Message(static_cast<const retro_message_ext*>(data)->msg);
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
      const auto* g = static_cast<const retro_game_geometry*>(data);
      g_av.geometry.base_width = g->base_width;
      g_av.geometry.base_height = g->base_height;
      g_av.geometry.aspect_ratio = g->aspect_ratio;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      g_av = *static_cast<const retro_system_av_info*>(data);
      if (g_gl) SizeFramebuffer(int(g_av.geometry.max_width), int(g_av.geometry.max_height));
      return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      return true;
    default: {
      static std::set<unsigned> told;   // what the core asked for that we don't give (once each)
      if (told.insert(cmd).second)
        std::fprintf(stderr, "vita host: environment %u%s not handled\n", cmd & 0xFFFF,
                     (cmd & RETRO_ENVIRONMENT_EXPERIMENTAL) ? " (experimental)" : "");
      return false;
    }
  }
}

void VideoCb(const void* data, unsigned w, unsigned h, size_t pitch) {
  if (!data || !w || !h) return;  // dupe: keep the last picture
  ++g_new_frames;
  g_frame.width = w;
  g_frame.height = h;
  g_frame.pixels.resize(size_t(w) * h);
  if (data == RETRO_HW_FRAME_BUFFER_VALID) {
    // BGRA bytes = XRGB8888 words; bottom-left origin, so flip the rows.
    static std::vector<uint32_t> tmp;
    tmp.resize(size_t(w) * h);
    pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    pglPixelStorei(GL_PACK_ALIGNMENT, 4);
    pglReadPixels(0, 0, GLsizei(w), GLsizei(h), GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, tmp.data());
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    const bool flip = g_hw.bottom_left_origin;
    for (unsigned y = 0; y < h; ++y)
      std::memcpy(&g_frame.pixels[size_t(y) * w], &tmp[size_t(flip ? h - 1 - y : y) * w], size_t(w) * 4);
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
int16_t InputStateCb(unsigned port, unsigned device, unsigned index, unsigned id) {
  if (port != 0) return 0;
  if (device == RETRO_DEVICE_JOYPAD) {
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return int16_t(g_buttons);
    return id < 16 ? (g_buttons >> id) & 1 : 0;
  }
  if (device == RETRO_DEVICE_ANALOG && index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT && id <= RETRO_DEVICE_ID_ANALOG_Y)
    return int16_t(g_sticks[index * 2 + id]);
  return 0;
}

template <typename T> bool Sym(T& f, const char* name) {
  f = reinterpret_cast<T>(::GetProcAddress(g_core.dll, name));
  return f != nullptr;
}

}  // namespace

void SetOption(const std::string& key, const std::string& value) { g_options[key] = value; }

bool Load(const std::string& core_dll, const std::string& game, const std::string& system_dir,
          const std::string& save_dir, std::string* err) {
  g_core.dll = LoadLibraryA(core_dll.c_str());
  if (!g_core.dll) { *err = "can't load " + core_dll + " (error " + std::to_string(GetLastError()) + ")"; return false; }
  bool ok = Sym(g_core.set_environment, "retro_set_environment") &&
            Sym(g_core.set_video_refresh, "retro_set_video_refresh") &&
            Sym(g_core.set_audio_sample, "retro_set_audio_sample") &&
            Sym(g_core.set_audio_sample_batch, "retro_set_audio_sample_batch") &&
            Sym(g_core.set_input_poll, "retro_set_input_poll") && Sym(g_core.set_input_state, "retro_set_input_state") &&
            Sym(g_core.init, "retro_init") && Sym(g_core.deinit, "retro_deinit") &&
            Sym(g_core.get_system_av_info, "retro_get_system_av_info") && Sym(g_core.load_game, "retro_load_game") &&
            Sym(g_core.unload_game, "retro_unload_game") && Sym(g_core.run, "retro_run");
  if (!ok) { *err = core_dll + ": missing libretro entry points"; return false; }
  g_system_dir = system_dir;
  g_save_dir = save_dir;
  if (!CreateContext(err)) return false;

  g_core.set_environment(EnvCb);
  g_core.set_video_refresh(VideoCb);
  g_core.set_audio_sample(AudioCb);
  g_core.set_audio_sample_batch(AudioBatchCb);
  g_core.set_input_poll(InputPollCb);
  g_core.set_input_state(InputStateCb);
  g_core.init();
  retro_game_info gi{};
  gi.path = game.c_str();
  if (!g_core.load_game(&gi)) { *err = "core refused " + game; return false; }
  if (!g_hw_requested) { *err = "core didn't ask for OpenGL 4.4 core"; return false; }
  g_core.get_system_av_info(&g_av);
  if (!SizeFramebuffer(int(g_av.geometry.max_width), int(g_av.geometry.max_height))) {
    *err = "GL: framebuffer incomplete";
    return false;
  }
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  if (g_hw.context_reset) g_hw.context_reset();
  return true;
}

void Unload() {
  if (!g_core.dll) return;
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  g_core.unload_game();
  if (g_hw.context_destroy) g_hw.context_destroy();
  g_core.deinit();
  DestroyContext();
  // The core's threads / statics may outlive deinit: leave the DLL loaded until exit.
}

void SetButtons(uint16_t buttons) { g_buttons = buttons; }
void SetSticks(int lx, int ly, int rx, int ry) {
  g_sticks[0] = lx; g_sticks[1] = ly; g_sticks[2] = rx; g_sticks[3] = ry;
}

void RunFrame() {
  SDL_GL_MakeCurrent(g_glwin, g_gl);
  g_core.run();
}

const Frame& LastFrame() { return g_frame; }
uint64_t NewFrames() { return g_new_frames; }
double Fps() { return g_av.timing.fps; }
double SampleRate() { return g_av.timing.sample_rate; }
std::vector<int16_t>& Audio() { return g_audio; }
const std::string& LastMessage(unsigned* serial) {
  if (serial) *serial = g_message_serial;
  return g_message;
}

}  // namespace vita
