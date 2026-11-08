// settings.h - userdata\settings.txt: key = value lines, '#' comments. F5 reloads it in game.
#pragma once
#include <string>

namespace settings {

void Load(const std::string& path);   // missing file: written with the defaults
bool Reload();                          // true if the file changed
double Num(const std::string& key, double def);
std::string Str(const std::string& key, const std::string& def);
void Set(const std::string& key, const std::string& value);  // in memory (and --set= on the command line)

}  // namespace settings
