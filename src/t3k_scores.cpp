#include "t3k_scores.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace t3kscores {
namespace {

constexpr uint32_t kTable = 0x113420;          // 8 x 32-bit big-endian scores (system bus offset)
constexpr uint32_t kText = 0x10C3A0;           // the high-score screen's text
constexpr uint32_t kTextEnd = 0x10C540;
constexpr char kMark[] = "tempest 3000/ /^Phigh scorers";
constexpr uint32_t kMarkAt = kText + 3;
constexpr int kEntries = 8, kNameLen = 14, kDigitsLen = 10;
constexpr uint32_t kDefaults[kEntries] = {10000, 9123, 8456, 7789, 6123, 5456, 4789, 3123};

struct Entry {
  uint32_t score = 0;
  std::string name;   // trailing spaces trimmed
  bool operator==(const Entry& o) const { return score == o.score && name == o.name; }
};
using Table = std::array<Entry, kEntries>;

std::string g_file;
bool g_enabled = false;
enum class State { kWaiting, kRunning, kOff } g_state = State::kWaiting;
Table g_saved;
uint64_t g_fields = 0;

uint32_t Get(const uint8_t* m, uint32_t a) {
  const uint8_t* q = m + a;
  return uint32_t(q[0]) << 24 | uint32_t(q[1]) << 16 | uint32_t(q[2]) << 8 | q[3];
}
void Put(uint8_t* m, uint32_t a, uint32_t v) {
  uint8_t* q = m + a;
  q[0] = uint8_t(v >> 24); q[1] = uint8_t(v >> 16); q[2] = uint8_t(v >> 8); q[3] = uint8_t(v);
}

// Offsets of each entry's 12-character name field in the text ("^Pn:^X " before it, "^M" after); false if the
// text isn't the high-score block (not loaded yet).
bool FindNames(const uint8_t* m, std::array<uint32_t, kEntries>& at) {
  if (std::memcmp(m + kMarkAt, kMark, sizeof kMark - 1) != 0) return false;
  uint32_t from = kMarkAt;
  for (int i = 0; i < kEntries; ++i) {
    const char tag[4] = {'^', 'P', char('1' + i), ':'};
    uint32_t a = from;
    while (a + 7 + kNameLen + 2 + kDigitsLen <= kTextEnd && std::memcmp(m + a, tag, 4) != 0) ++a;
    if (a + 7 + kNameLen + 2 + kDigitsLen > kTextEnd) return false;
    if (m[a + 4] != '^' || m[a + 6] != ' ') return false;
    at[i] = a + 7;
    if (m[at[i] + kNameLen] != '^' || m[at[i] + kNameLen + 1] != 'M') return false;
    from = at[i] + kNameLen;
  }
  return true;
}

bool ValidName(const std::string& s) {
  if (s.size() > size_t(kNameLen)) return false;
  for (unsigned char c : s)
    if (c < 0x20 || c > 0x7E || c == '^' || c == '/' || c == '|') return false;
  return true;
}

Table Read(const uint8_t* m, const std::array<uint32_t, kEntries>& at) {
  Table t;
  for (int i = 0; i < kEntries; ++i) {
    t[i].score = Get(m, kTable + 4 * i);
    std::string name(reinterpret_cast<const char*>(m + at[i]), kNameLen);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    t[i].name = name;
  }
  return t;
}

void Write(uint8_t* m, const std::array<uint32_t, kEntries>& at, const Table& t) {
  for (int i = 0; i < kEntries; ++i) {
    Put(m, kTable + 4 * i, t[i].score);
    char name[kNameLen + 1], digits[32];
    std::snprintf(name, sizeof name, "%-14s", t[i].name.c_str());
    std::memcpy(m + at[i], name, kNameLen);
    std::snprintf(digits, sizeof digits, "%-10u", t[i].score);
    if (std::strlen(digits) <= size_t(kDigitsLen)) std::memcpy(m + at[i] + kNameLen + 2, digits, kDigitsLen);
  }
}

// The file: one "score|name" line per entry, best first; lines starting with # are comments.
bool Load(Table& t) {
  std::ifstream f(g_file);
  if (!f) return false;
  std::string line;
  int n = 0;
  while (std::getline(f, line) && n < kEntries) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const size_t bar = line.find('|');
    if (bar == std::string::npos) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(line.substr(0, bar).c_str(), &end, 10);
    std::string name = line.substr(bar + 1);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (v > 999999999UL || !ValidName(name)) return false;
    t[n].score = uint32_t(v);
    t[n].name = name;
    ++n;
  }
  return n == kEntries;
}

bool Save(const Table& t) {
  std::ostringstream s;
  s << "# Tempest 3000 high scores (score|name), kept by t3k.exe - the game itself saves nothing.\n";
  for (auto& e : t) s << e.score << '|' << e.name << '\n';
  std::error_code ec;
  fs::create_directories(fs::path(g_file).parent_path(), ec);
  const std::string tmp = g_file + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << s.str();
    if (!f) return false;
  }
  fs::rename(tmp, g_file, ec);
  if (ec) {
    std::fprintf(stderr, "high scores: can't write %s\n", g_file.c_str());
    return false;
  }
  return true;
}

}  // namespace

void Init(const std::string& file, bool enabled) {
  g_file = file;
  g_enabled = enabled;
  g_state = enabled ? State::kWaiting : State::kOff;
}

void AfterField(uint8_t* m, size_t n) {
  if (g_state == State::kOff || !m || n < kTextEnd || n < kTable + 4 * kEntries) return;
  ++g_fields;
  std::array<uint32_t, kEntries> at;
  if (g_state == State::kWaiting) {
    // Only from the game's own defaults: a run started from a save state (other scores) neither restores nor saves.
    if (!FindNames(m, at)) return;
    for (int i = 0; i < kEntries; ++i)
      if (Get(m, kTable + 4 * i) != kDefaults[i]) {
        std::fprintf(stderr, "high scores: the table isn't the game's defaults (a save state?) - not kept\n");
        g_state = State::kOff;
        return;
      }
    Table t;
    if (Load(t)) {
      Write(m, at, t);
      std::fprintf(stderr, "high scores: restored from %s\n", g_file.c_str());
    } else if (fs::exists(g_file)) {
      std::fprintf(stderr, "high scores: %s unreadable - left alone, not saving\n", g_file.c_str());
      g_state = State::kOff;
      return;
    }
    g_saved = Read(m, at);
    g_state = State::kRunning;
    return;
  }
  if (g_fields % 60 != 0) return;
  if (!FindNames(m, at)) return;   // (the text is always there once loaded; just in case)
  const Table now = Read(m, at);
  for (auto& e : now)
    if (!ValidName(e.name)) return;   // mid-update
  if (now != g_saved && Save(now)) g_saved = now;
}

void Restart() {
  if (g_state == State::kRunning) g_state = State::kWaiting;
}

void Flush(const uint8_t* m, size_t n) {
  if (g_state != State::kRunning || !m || n < kTextEnd) return;
  std::array<uint32_t, kEntries> at;
  if (!FindNames(m, at)) return;
  const Table now = Read(m, at);
  for (auto& e : now)
    if (!ValidName(e.name)) return;
  if (now != g_saved && Save(now)) g_saved = now;
}

}  // namespace t3kscores
