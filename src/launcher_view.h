// launcher_view.h - the Tempest Collection launcher's look (tools/launcher_mockup/index.html, ported): portrait cover
// panels scrolled horizontally, each game's own logo (animated in its flavour) and moving background (Space Giraffe
// the wildest ... Tempest 1981 the calmest), system text in a glowing pixel font (T3K style), a warp on start.
// Art: <art dir>\<id>.png (3:4 cover) and <id>_logo.png (tools/launcher_art.py; the play folder's art\).
#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

namespace lview {

struct Entry {
  std::string id;                  // t81 t2k t3k sg txk t4k: art file names and the flavour
  std::string title;               // shown when the logo is missing
  std::string line1;               // "1994 - ATARI JAGUAR"
  std::string line2;               // "LLAMASOFT"
  std::string variant;             // "ACID" / "NUXX" (UP / DOWN change it), or empty
  std::string status;              // "", "setup needed", "not installed" ...
  std::vector<std::string> help;   // what to do, shown under the info while selected
  bool ready = true;
};

// Loads the art (missing files are drawn as placeholders); false only without a renderer.
bool Init(SDL_Renderer* ren, const std::filesystem::path& art_dir);
void Shutdown();
void SetEntries(const std::vector<Entry>& entries);

// One frame of the picker. sel = the selected entry, dt = seconds since the last frame; message = a one-line note.
void Draw(SDL_Renderer* ren, int sel, double dt, const std::string& message);

// The start warp for entry i (about 0.7 s); Draw shows it. Warping() is true until it has finished.
void StartWarp(int i);
bool Warping();

// An extra button in the hint row (e.g. "Y  ADD TO STEAM"); empty = none.
void SetExtraHint(const std::string& hint);

// A question over the picker (after Draw): the screen dimmed, a panel in entry i's colour with a title, lines, an
// optional progress bar (progress < 0: none) and the buttons ("A  COMPRESS     B  NO").
void DrawPrompt(SDL_Renderer* ren, int i, const std::string& title, const std::vector<std::string>& lines,
                const std::string& buttons, float progress = -1.f);

// The screen under a starting game: its logo and "STARTING ..." on black.
void DrawStarting(SDL_Renderer* ren, int i, const std::string& text);

}  // namespace lview
