// launcher.cpp - Tempest Collection: the game picker (tempest.exe).
//
// Each game is its own program (t2k.exe, t3k.exe, ...) started as a separate process: the emulators keep a lot of
// global state, so separate processes keep them independent and a crash in one game cannot take the others down.
// The launcher hides while a game runs and comes back when it quits.
//
// Barebones for now (placeholder look, SDL debug font): the list, keyboard / pad control, start, Steam link.
// userdata\launcher_settings.txt: fullscreen.
// --shot=<file.bmp> saves the first frame and exits (tests).
// Keys: Up / Down (d-pad, left stick) choose, Enter / A start, F11 fullscreen, Esc / B quit.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "settings.h"

namespace fs = std::filesystem;

namespace {

const char kDefaults[] =
    "# Tempest Collection launcher settings (key = value; '#' starts a comment).\n"
    "\n"
    "# 1 = start fullscreen (F11 toggles and remembers).\n"
    "fullscreen = 0\n";

constexpr const char* kSteamAppT4K = "688140";   // Tempest 4000 on Steam (store.steampowered.com/app/688140)

enum class Kind { Program, Steam, NotYet };

struct Game {
  const char* title;
  const char* detail;   // year, platform
  Kind kind;
  const char* target;   // program file name, or Steam app ID
  std::string status;   // why it can't start (empty = ready)
};

std::vector<Game> MakeGames(const fs::path& base) {
  std::vector<Game> g = {
      {"TEMPEST", "1981  ARCADE", Kind::NotYet, "", ""},
      {"TEMPEST 2000", "1994  ATARI JAGUAR", Kind::Program, "t2k.exe", ""},
      {"TEMPEST 3000", "2000  NUON", Kind::Program, "t3k.exe", ""},
      {"TxK", "2014  PS VITA", Kind::NotYet, "", ""},
      {"TEMPEST 4000", "2018  STEAM", Kind::Steam, kSteamAppT4K, ""},
  };
  for (auto& x : g) {
    if (x.kind == Kind::NotYet) x.status = "coming later";
    else if (x.kind == Kind::Program && !fs::exists(base / x.target)) x.status = std::string(x.target) + " not found";
    else if (x.kind == Kind::Steam) {
      DWORD installed = 0, size = sizeof(installed);
      const std::string key = std::string("Software\\Valve\\Steam\\Apps\\") + x.target;
      if (RegGetValueA(HKEY_CURRENT_USER, key.c_str(), "Installed", RRF_RT_REG_DWORD, nullptr, &installed, &size) !=
              ERROR_SUCCESS ||
          !installed)
        x.status = "not installed on Steam";
    }
  }
  return g;
}

// Starts a game program next to the launcher; returns its process handle (nullptr on failure).
HANDLE StartProgram(const fs::path& exe) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe.wstring() + L"\"";
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

void Text(SDL_Renderer* ren, float x, float y, float scale, const char* s) {
  SDL_SetRenderScale(ren, scale, scale);
  SDL_RenderDebugText(ren, x / scale, y / scale, s);
  SDL_SetRenderScale(ren, 1.f, 1.f);
}

}  // namespace

int main(int argc, char** argv) {
  std::string shot;   // --shot=<file.bmp>: draw one frame, save it, exit (tests)
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]).rfind("--shot=", 0) == 0) shot = argv[i] + 7;
  SetProcessDPIAware();
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  const fs::path base(SDL_GetBasePath());
  const fs::path udir = base / "userdata";
  fs::create_directories(udir);
  settings::SetDefaultText(kDefaults);
  settings::Load((udir / "launcher_settings.txt").string());

  SDL_Window* win = SDL_CreateWindow("Tempest Collection", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  SDL_Renderer* ren = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
  if (!ren) {
    std::fprintf(stderr, "window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetRenderVSync(ren, 1);
  if (settings::Num("fullscreen", 0) != 0) SDL_SetWindowFullscreen(win, true);

  std::vector<Game> games = MakeGames(base);
  int sel = 1;   // Tempest 2000: the first one that runs
  std::string message;
  HANDLE child = nullptr;
  uint64_t ignore_until = 0;   // input that arrives just after a game quits is not for us
  float stick_y = 0.f;
  bool running = true;

  auto start = [&](int i) {
    const Game& g = games[i];
    message.clear();
    if (!g.status.empty()) {
      message = std::string(g.title) + ": " + g.status;
      return;
    }
    if (g.kind == Kind::Steam) {
      const std::string url = std::string("steam://rungameid/") + g.target;
      if (!SDL_OpenURL(url.c_str())) message = "could not open Steam";
      else message = "starting Tempest 4000 in Steam...";
      return;
    }
    child = StartProgram(base / g.target);
    if (!child) {
      message = std::string("could not start ") + g.target;
      return;
    }
    SDL_HideWindow(win);
  };
  auto move = [&](int d) {
    sel = (sel + d + int(games.size())) % int(games.size());
    message.clear();
  };

  while (running) {
    if (child) {   // a game is running: wait for it, then come back
      if (WaitForSingleObject(child, 50) == WAIT_OBJECT_0) {
        CloseHandle(child);
        child = nullptr;
        games = MakeGames(base);
        SDL_ShowWindow(win);
        SDL_RaiseWindow(win);
        ignore_until = SDL_GetTicksNS() + 400000000ull;
      }
      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
      }
      continue;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      const bool live = SDL_GetTicksNS() >= ignore_until;
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
      else if (e.type == SDL_EVENT_KEY_DOWN && live) {
        switch (e.key.scancode) {
          case SDL_SCANCODE_UP: move(-1); break;
          case SDL_SCANCODE_DOWN: move(1); break;
          case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: start(sel); break;
          case SDL_SCANCODE_ESCAPE: running = false; break;
          case SDL_SCANCODE_F11: {
            const bool on = !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(win, on);
            settings::Save("fullscreen", on ? "1" : "0");
            break;
          }
          default: break;
        }
      } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && live) {
        switch (e.gbutton.button) {
          case SDL_GAMEPAD_BUTTON_DPAD_UP: move(-1); break;
          case SDL_GAMEPAD_BUTTON_DPAD_DOWN: move(1); break;
          case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: start(sel); break;
          case SDL_GAMEPAD_BUTTON_EAST: running = false; break;
          default: break;
        }
      } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY && live) {
        const float v = e.gaxis.value / 32767.f;   // one step per push past half way
        if (v > 0.5f && stick_y <= 0.5f) move(1);
        if (v < -0.5f && stick_y >= -0.5f) move(-1);
        stick_y = v;
      }
    }

    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    const float s = std::max(1.f, wh / 240.f);   // the 8-pixel font, scaled to the window
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    Text(ren, 24 * s / 2, 16 * s / 2, s * 1.5f, "TEMPEST COLLECTION");
    for (int i = 0; i < int(games.size()); ++i) {
      const Game& g = games[i];
      const float y = (56 + i * 28) * s / 2;
      const bool ready = g.status.empty();
      if (i == sel) SDL_SetRenderDrawColor(ren, 255, 64, 64, 255);
      else if (ready) SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
      else SDL_SetRenderDrawColor(ren, 110, 110, 110, 255);
      Text(ren, 24 * s / 2, y, s, (std::string(i == sel ? "> " : "  ") + g.title).c_str());
      SDL_SetRenderDrawColor(ren, ready ? 120 : 80, ready ? 160 : 80, ready ? 255 : 80, 255);
      Text(ren, 24 * s / 2 + 18 * 8 * s, y, s * 0.6f, (std::string(g.detail) + (ready ? "" : "  - " + g.status)).c_str());
    }
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    if (!message.empty()) Text(ren, 24 * s / 2, (56 + 5 * 28 + 8) * s / 2, s * 0.6f, message.c_str());
    SDL_SetRenderDrawColor(ren, 140, 140, 140, 255);
    Text(ren, 24 * s / 2, wh - 16 * s / 2, s * 0.5f, "UP/DOWN choose   ENTER/A start   F11 fullscreen   ESC/B quit");
    if (!shot.empty()) {
      if (SDL_Surface* img = SDL_RenderReadPixels(ren, nullptr)) {
        SDL_SaveBMP(img, shot.c_str());
        SDL_DestroySurface(img);
      }
      running = false;
    }
    SDL_RenderPresent(ren);
  }
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
