// steam_shortcut.h - the launcher's "add to Steam": TempestCollection.exe as a non-Steam game with its library art.
//
// Steam keeps its non-Steam games in userdata\<account>\config\shortcuts.vdf (binary VDF) and their art in
// userdata\<account>\config\grid\<appid>p.png (portrait), <appid>.png (wide), <appid>_hero.png, <appid>_logo.png.
// Steam holds shortcuts.vdf in memory and writes it back when it exits, so a new entry may only be written while Steam
// is closed (the launcher closes it with steam.exe -shutdown and starts it again). The art can be written any time
// (Steam shows it after a restart). The account = the userdata folder used most recently.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace steamsc {

struct Shortcut {
  bool found = false;         // our exe is in this account's shortcuts.vdf
  uint32_t appid = 0;         // its app id (the grid art's file names)
  std::filesystem::path config;   // userdata\<account>\config (empty: no Steam account found)
};

// Looks for our program (by file name, e.g. TempestCollection.exe) among the shortcuts of the most recent account.
// roots = Steam's install folders (the launcher's SteamRoots).
Shortcut Find(const std::vector<std::filesystem::path>& roots, const std::wstring& exe_name);

// Adds the shortcut (Steam must be closed): name, the exe and its folder, the icon. Returns "" or an error.
std::string Add(Shortcut& sc, const std::string& name, const std::filesystem::path& exe,
                const std::filesystem::path& icon);

// Copies the art (art_dir: capsule_600x900.png, wide_capsule_920x430.png, hero_1920x620.png, logo.png) into the
// account's grid folder under the shortcut's app id. Returns "" or an error.
std::string WriteArt(const Shortcut& sc, const std::filesystem::path& art_dir);

}  // namespace steamsc
