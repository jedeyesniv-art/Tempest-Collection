// launcher_sound.h - the launcher's sound stings, each game's own: its lane-switch click when the selection moves to
// it, its death / explosion when it is started. Files <dir>\<id>_move.wav and <id>_launch.wav (tools/launcher_sounds.py;
// the play folder's sounds\, else the repo's art\sounds\ - not in git). Missing files are silent.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lsound {

// Returns the number of stings loaded (0: silent).
int Init(const std::filesystem::path& dir, const std::vector<std::string>& ids);
void Shutdown();
void Move(const std::string& id);     // the selection moved to this game
void Launch(const std::string& id);   // this game is starting
void SetVolume(float v);              // 0 (off) .. 1 (as recorded); later stings play at it

}  // namespace lsound
