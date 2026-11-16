// Tempest 3000 high scores kept between sessions (userdata\t3k_highscores.txt).
// The game saves nothing itself (it has level passwords instead). Its table: eight 32-bit scores at system bus
// $80113420 and the names only inside the high-score screen's text at $8010C3A0 ("^Pn:^X " + a 14-character name +
// "^M" + a 10-character score field). The host restores both once the game's data is loaded (the defaults are in
// place) and saves whenever they change (checked once a second) and at exit.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace t3kscores {

// file: the save file; enabled: false = neither restore nor save (setting keep_high_scores 0).
void Init(const std::string& file, bool enabled);
// After every emulated field (emulation thread). m / n: the system bus (nuon::Memory(0)).
void AfterField(uint8_t* m, size_t n);
// At exit: a last save if anything changed.
void Flush(const uint8_t* m, size_t n);

}  // namespace t3kscores
