// t3k_main.cpp - Tempest 3000 (Nuon, 2000) on PC: SDL3 front end for the Nuance libretro core (stage 1).
//
//   t3k.exe [disc]                        play in a window (disc: Tempest 3000 .iso next to the program, in game\,
//                                         or in Nuance_067\)
//   --headless --frames=N                 run N core frames without showing a window (the core still runs on the
//                                         PC clock: one frame takes a real 60th of a second)
//   --shot=F:<file.bmp>                   save the picture after frame F (repeatable)
//   --press=F:<btn>[:len]                 hold a RetroPad button from frame F (default 6 frames)
//   --wav=<file.wav>                      dump the audio
//   --perflog=<file.csv>                  one line per frame: timing and the core's run statistics
//   --userdata=<dir>                      save folder (default userdata next to the program)
//   (--frames and --press work in the window too: scripted timing tests)
//   --novsync                             present without waiting for the display
//   T3K_PERF=1                            a timing summary every 5 s on stderr
// Keys: arrows d-pad, Z = A, X = B, Enter = Start, Tab = Nuon, Q / E = L / R, I J K L = C up / left / down / right,
// F11 fullscreen, Esc quits (no settings menu yet).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "nuon_host.h"
#include "settings.h"

namespace fs = std::filesystem;

namespace {

const char* kDefaults = R"(# Tempest 3000 settings.

# 1 = start fullscreen (F11 toggles and saves it).
fullscreen = 1
# 1 = wait for the display when presenting (no tearing).
vsync = 1
)";

struct Shot { int frame; std::string path; };
struct Press { int frame; int btn; int len; };

int BtnByName(const std::string& n) {
  static const char* names[] = {"b", "y", "select", "start", "up", "down", "left", "right",
                                "a", "x", "l", "r", "l2", "r2", "l3", "r3"};
  for (int i = 0; i < 16; ++i)
    if (n == names[i]) return i;
  return -1;
}

bool WriteBmp(const std::string& path, const nuon::Frame& f) {
  if (f.width == 0) return false;
  std::ofstream o(path, std::ios::binary);
  if (!o) return false;
  uint32_t w = f.width, h = f.height, size = 54 + w * h * 4;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { std::memcpy(hdr + at, &v, 4); };
  put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, uint32_t(-int32_t(h)));
  hdr[26] = 1; hdr[28] = 32;
  o.write(reinterpret_cast<char*>(hdr), 54);
  o.write(reinterpret_cast<const char*>(f.pixels.data()), std::streamsize(w) * h * 4);
  return true;
}

bool WriteWav(const std::string& path, const std::vector<int16_t>& s, uint32_t rate) {
  std::ofstream o(path, std::ios::binary);
  if (!o) return false;
  uint32_t data = uint32_t(s.size() * 2), v;
  uint16_t v16;
  o.write("RIFF", 4); v = 36 + data; o.write((char*)&v, 4); o.write("WAVEfmt ", 8);
  v = 16; o.write((char*)&v, 4); v16 = 1; o.write((char*)&v16, 2); v16 = 2; o.write((char*)&v16, 2);
  o.write((char*)&rate, 4); v = rate * 4; o.write((char*)&v, 4); v16 = 4; o.write((char*)&v16, 2);
  v16 = 16; o.write((char*)&v16, 2); o.write("data", 4); o.write((char*)&data, 4);
  o.write(reinterpret_cast<const char*>(s.data()), data);
  return true;
}

std::string FindDisc(const std::string& given) {
  if (!given.empty()) return given;
  fs::path base(SDL_GetBasePath());
  std::vector<fs::path> dirs = {base, base / "game", fs::current_path(), fs::current_path() / "Nuance_067",
                                base.parent_path().parent_path() / "Nuance_067"};
  for (auto& d : dirs) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(d, ec)) {
      auto name = e.path().filename().string();
      auto ext = e.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if ((ext == ".iso" || ext == ".chd") && name.find("Tempest") != std::string::npos) return e.path().string();
    }
  }
  return {};
}

// The owner's Xbox layout (as in their Nuance setup): A = Nuon A, B = B, X = C-left, Y = C-up, LB / RB = L / R,
// LT / L3 = C-down, RT / R3 = C-right, Start = Start, Back = Nuon. D-pad and left stick = Nuon d-pad.
uint16_t ReadPad(SDL_Gamepad* pad, const bool* keys) {
  uint16_t b = 0;
  auto set = [&](int bit, bool on) { if (on) b |= uint16_t(1u << bit); };
  if (pad) {
    auto btn = [&](SDL_GamepadButton x) { return SDL_GetGamepadButton(pad, x); };
    set(nuon::A, btn(SDL_GAMEPAD_BUTTON_SOUTH));
    set(nuon::B, btn(SDL_GAMEPAD_BUTTON_EAST));
    set(nuon::Y, btn(SDL_GAMEPAD_BUTTON_WEST));    // C-left
    set(nuon::X, btn(SDL_GAMEPAD_BUTTON_NORTH));   // C-up
    set(nuon::L, btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    set(nuon::R, btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    set(nuon::L2, btn(SDL_GAMEPAD_BUTTON_LEFT_STICK) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000);
    set(nuon::R2, btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000);
    set(nuon::START, btn(SDL_GAMEPAD_BUTTON_START));
    set(nuon::SELECT, btn(SDL_GAMEPAD_BUTTON_BACK));
    const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    set(nuon::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -16000);
    set(nuon::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 16000);
    set(nuon::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -16000);
    set(nuon::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 16000);
  }
  if (keys) {
    set(nuon::UP, keys[SDL_SCANCODE_UP]);
    set(nuon::DOWN, keys[SDL_SCANCODE_DOWN]);
    set(nuon::LEFT, keys[SDL_SCANCODE_LEFT]);
    set(nuon::RIGHT, keys[SDL_SCANCODE_RIGHT]);
    set(nuon::A, keys[SDL_SCANCODE_Z]);
    set(nuon::B, keys[SDL_SCANCODE_X]);
    set(nuon::START, keys[SDL_SCANCODE_RETURN]);
    set(nuon::SELECT, keys[SDL_SCANCODE_TAB]);
    set(nuon::L, keys[SDL_SCANCODE_Q]);
    set(nuon::R, keys[SDL_SCANCODE_E]);
    set(nuon::X, keys[SDL_SCANCODE_I]);
    set(nuon::Y, keys[SDL_SCANCODE_J]);
    set(nuon::L2, keys[SDL_SCANCODE_K]);
    set(nuon::R2, keys[SDL_SCANCODE_L]);
  }
  return b;
}

// Per-frame timing (--perflog / T3K_PERF).
struct Perf {
  FILE* csv = nullptr;
  bool summary = false;
  uint64_t start = 0, last = 0, from = 0;
  uint64_t n = 0, run_sum = 0, run_max = 0, frame_max = 0, budget_breaks = 0, iters_sum = 0, stall_sum = 0;
  uint32_t last_field = 0, fields_skipped = 0;

  void Frame(uint64_t frame, uint64_t t_run0, uint64_t t_run1, uint64_t t_end) {
    nuon::RunStats s{};
    const bool have = nuon::GetRunStats(&s);
    const uint64_t frame_ns = last ? t_end - last : 0;
    last = t_end;
    if (csv) {
      std::fprintf(csv, "%llu,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%u,%u,%u,%llu,%llu,%llu,%llu,%u\n",
                   (unsigned long long)frame, (t_end - start) / 1e6, frame_ns / 1e6, (t_run1 - t_run0) / 1e6,
                   (t_end - t_run1) / 1e6, (unsigned long long)s.iterations, (unsigned long long)s.mpe3_stalled,
                   s.field_fired, s.field_counter, s.vidsync_calls, (unsigned long long)s.mpe_packets[0],
                   (unsigned long long)s.mpe_packets[1], (unsigned long long)s.mpe_packets[2],
                   (unsigned long long)s.mpe_packets[3], s.vid_flips);
    }
    if (!summary || !have) return;
    ++n;
    run_sum += t_run1 - t_run0;
    run_max = std::max<uint64_t>(run_max, t_run1 - t_run0);
    frame_max = std::max(frame_max, frame_ns);
    if (!s.field_fired) ++budget_breaks;
    if (last_field && s.field_counter > last_field + 1) fields_skipped += s.field_counter - last_field - 1;
    last_field = s.field_counter;
    iters_sum += s.iterations;
    stall_sum += s.mpe3_stalled;
    if (t_end - from >= 5000000000ull) {
      const double secs = (t_end - from) / 1e9;
      std::fprintf(stderr,
                   "perf: %.1f frames/s, run avg %.2f ms max %.2f ms, frame max %.2f ms, %llu budget ends, "
                   "%u fields skipped, %.0f k iterations/frame, MPE3 waiting %.0f%%\n",
                   n / secs, run_sum / 1e6 / n, run_max / 1e6, frame_max / 1e6, (unsigned long long)budget_breaks,
                   fields_skipped, iters_sum / 1e3 / n, iters_sum ? 100.0 * stall_sum / iters_sum : 0.0);
      n = run_sum = run_max = frame_max = budget_breaks = iters_sum = stall_sum = 0;
      fields_skipped = 0;
      from = t_end;
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  std::string disc, wav, perflog, userdata;
  bool headless = false, novsync = false;
  int frames = -1;   // headless default 600; the window runs until closed unless given
  std::vector<Shot> shots;
  std::vector<Press> presses;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    if (a == "--headless") headless = true;
    else if (a == "--novsync") novsync = true;
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--perflog=", 0) == 0) perflog = val("--perflog=");
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata=");
    else if (a.rfind("--shot=", 0) == 0) {
      auto v = val("--shot="), c = v.substr(0, v.find(':'));
      shots.push_back({std::atoi(c.c_str()), v.substr(v.find(':') + 1)});
    } else if (a.rfind("--press=", 0) == 0) {
      auto v = val("--press=");
      auto c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      int btn = BtnByName(v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1));
      if (btn < 0) { std::fprintf(stderr, "unknown button in %s\n", a.c_str()); return 1; }
      presses.push_back({std::atoi(v.substr(0, c1).c_str()), btn,
                         c2 == std::string::npos ? 6 : std::atoi(v.substr(c2 + 1).c_str())});
    } else if (a.rfind("--", 0) == 0) { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
    else disc = a;
  }
  // The core changes the working folder: make every path absolute first.
  if (!disc.empty()) disc = fs::absolute(disc).string();
  if (!wav.empty()) wav = fs::absolute(wav).string();
  if (!perflog.empty()) perflog = fs::absolute(perflog).string();
  for (auto& s : shots) s.path = fs::absolute(s.path).string();

  if (!SDL_Init(SDL_INIT_VIDEO | (headless ? 0 : SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const fs::path base(SDL_GetBasePath());
  const fs::path udir = userdata.empty() ? base / "userdata" : fs::absolute(userdata);
  fs::create_directories(udir);
  settings::SetDefaultText(kDefaults);
  settings::Load((udir / "settings.txt").string());
  disc = FindDisc(disc);
  if (disc.empty()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tempest 3000",
                             "Put your Tempest 3000 disc image (.iso) next to the program.", nullptr);
    return 1;
  }
  std::string err;
  if (!nuon::Load((base / "nuance_libretro.dll").string(), disc, (base / "system").string(), udir.string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Tempest 3000", err.c_str(), nullptr);
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", disc.c_str(), nuon::Fps(), nuon::SampleRate());

  Perf perf;
  perf.summary = std::getenv("T3K_PERF") != nullptr;
  if (!perflog.empty()) {
    perf.csv = std::fopen(perflog.c_str(), "w");
    if (perf.csv) std::fprintf(perf.csv, "frame,t_ms,frame_ms,run_ms,rest_ms,iterations,mpe3_stalled,field_fired,"
                                         "field_counter,vidsync_calls,mpe0,mpe1,mpe2,mpe3,vid_flips\n");
  }
  perf.start = perf.from = SDL_GetTicksNS();
  std::vector<int16_t> wav_samples;

  auto scripted = [&](uint64_t f) {
    uint16_t b = 0;
    for (auto& p : presses)
      if (int64_t(f) >= p.frame && int64_t(f) < p.frame + p.len) b |= uint16_t(1u << p.btn);
    return b;
  };
  if (headless) {
    if (frames < 0) frames = 600;
    for (int f = 0; f < frames; ++f) {
      nuon::SetButtons(scripted(f));
      const uint64_t t0 = SDL_GetTicksNS();
      nuon::RunFrame();
      const uint64_t t1 = SDL_GetTicksNS();
      auto& au = nuon::Audio();
      if (!wav.empty()) wav_samples.insert(wav_samples.end(), au.begin(), au.end());
      au.clear();
      for (auto& s : shots)
        if (s.frame == f && !WriteBmp(s.path, nuon::LastFrame())) std::fprintf(stderr, "shot %s failed\n", s.path.c_str());
      perf.Frame(f, t0, t1, SDL_GetTicksNS());
    }
    if (!wav.empty()) WriteWav(wav, wav_samples, uint32_t(nuon::SampleRate()));
    if (perf.csv) std::fclose(perf.csv);
    nuon::Unload();
    SDL_Quit();
    return 0;
  }

  SDL_Window* win = SDL_CreateWindow("Tempest 3000", 1440, 1080, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
  if (!ren) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
  if (settings::Num("fullscreen", 1) != 0) SDL_SetWindowFullscreen(win, true);
  SDL_SetRenderVSync(ren, (novsync || settings::Num("vsync", 1) == 0) ? 0 : 1);
  SDL_HideCursor();
  SDL_Texture* tex = nullptr;
  unsigned tw = 0, th = 0;

  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(nuon::SampleRate())};
  SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (audio) SDL_ResumeAudioStreamDevice(audio);

  SDL_Gamepad* pad = nullptr;
  bool running = true;
  uint64_t frame = 0;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) running = false;
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11) {
        const bool on = !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN);
        SDL_SetWindowFullscreen(win, on);
        settings::Save("fullscreen", on ? "1" : "0");
      } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) SDL_HideCursor();
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_ShowCursor();
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
      else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
      }
    }
    nuon::SetButtons(ReadPad(pad, SDL_GetKeyboardState(nullptr)) | scripted(frame));
    const uint64_t t0 = SDL_GetTicksNS();
    nuon::RunFrame();
    const uint64_t t1 = SDL_GetTicksNS();

    auto& au = nuon::Audio();
    if (audio && !au.empty()) SDL_PutAudioStreamData(audio, au.data(), int(au.size() * 2));
    au.clear();

    const nuon::Frame& f = nuon::LastFrame();
    if (f.width) {
      if (!tex || tw != f.width || th != f.height) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, int(f.width), int(f.height));
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        tw = f.width;
        th = f.height;
      }
      SDL_UpdateTexture(tex, nullptr, f.pixels.data(), int(f.width * 4));
    }
    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (tex) {   // 4:3, as big as fits
      float h = float(wh), w = h * 4.f / 3.f;
      if (w > ww) { w = float(ww); h = w * 3.f / 4.f; }
      SDL_FRect dst{(ww - w) / 2, (wh - h) / 2, w, h};
      SDL_RenderTexture(ren, tex, nullptr, &dst);
    }
    SDL_RenderPresent(ren);
    perf.Frame(frame++, t0, t1, SDL_GetTicksNS());
    if (frames >= 0 && int64_t(frame) >= frames) running = false;
  }
  if (perf.csv) std::fclose(perf.csv);
  if (audio) SDL_DestroyAudioStream(audio);
  if (tex) SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  nuon::Unload();
  SDL_Quit();
  return 0;
}
