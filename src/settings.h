// settings.h - userdata\settings.txt: key = value lines, '#' comments. F5 reloads it in game.
#pragma once
#include <string>

namespace settings {

void SetDefaultText(const char* text); // before Load: another program's defaults (default: Tempest 2000's)
void Load(const std::string& path);   // missing file: written with the defaults
bool Reload();                          // true if the file changed
double Num(const std::string& key, double def);
std::string Str(const std::string& key, const std::string& def);
void Set(const std::string& key, const std::string& value);  // in memory only (--set= on the command line)
void Save(const std::string& key, const std::string& value); // in memory and written to the file (comments kept)

}  // namespace settings
