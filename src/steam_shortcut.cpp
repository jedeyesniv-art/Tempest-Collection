// steam_shortcut.cpp - see steam_shortcut.h.
#include "steam_shortcut.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace fs = std::filesystem;

namespace steamsc {
namespace {

// ---- binary VDF (shortcuts.vdf): 0 = map, 1 = string, 2 = int32, 3 = float, 6 = colour, 7 = uint64, 10 = int64,
// 8 = end of map ----
struct Node {
  uint8_t type = 0;
  std::string key;
  std::string str;        // type 1
  std::string raw;        // fixed-size values, as stored (types 2 3 6 7 10)
  std::vector<Node> kids; // type 0
};

bool ParseMap(const std::string& d, size_t& at, std::vector<Node>& out) {
  while (at < d.size()) {
    const uint8_t type = uint8_t(d[at++]);
    if (type == 8) return true;
    Node n;
    n.type = type;
    const size_t z = d.find('\0', at);
    if (z == std::string::npos) return false;
    n.key = d.substr(at, z - at);
    at = z + 1;
    switch (type) {
      case 0:
        if (!ParseMap(d, at, n.kids)) return false;
        break;
      case 1: {
        const size_t e = d.find('\0', at);
        if (e == std::string::npos) return false;
        n.str = d.substr(at, e - at);
        at = e + 1;
        break;
      }
      case 2: case 3: case 6:
        if (at + 4 > d.size()) return false;
        n.raw = d.substr(at, 4);
        at += 4;
        break;
      case 7: case 10:
        if (at + 8 > d.size()) return false;
        n.raw = d.substr(at, 8);
        at += 8;
        break;
      default:
        return false;   // a type we don't know: never rewrite what we can't read
    }
    out.push_back(std::move(n));
  }
  return false;
}

void WriteMap(std::string& o, const std::vector<Node>& m) {
  for (const Node& n : m) {
    o += char(n.type);
    o += n.key;
    o += '\0';
    if (n.type == 0) WriteMap(o, n.kids);
    else if (n.type == 1) { o += n.str; o += '\0'; }
    else o += n.raw;
  }
  o += char(8);
}

Node Str(const char* key, const std::string& v) { Node n; n.type = 1; n.key = key; n.str = v; return n; }
Node Int(const char* key, uint32_t v) {
  Node n;
  n.type = 2;
  n.key = key;
  n.raw.assign(4, '\0');
  std::memcpy(n.raw.data(), &v, 4);   // little-endian (x86 / the Deck)
  return n;
}
Node Map(const char* key) { Node n; n.type = 0; n.key = key; return n; }

const Node* Child(const Node& n, const char* key) {
  for (const Node& k : n.kids)
    if (_stricmp(k.key.c_str(), key) == 0) return &k;
  return nullptr;
}

std::string ReadAll(const fs::path& p) {
  std::string s;
  if (FILE* f = _wfopen(p.c_str(), L"rb")) {
    char buf[8192];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) s.append(buf, n);
    std::fclose(f);
  }
  return s;
}

uint32_t Crc32(const std::string& s) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char b : s) {
    c ^= b;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// The file name at the end of a shortcut's Exe ("\"C:\\...\\TempestCollection.exe\"" or a Linux path), lower case.
std::string ExeName(std::string exe) {
  exe.erase(std::remove(exe.begin(), exe.end(), '"'), exe.end());
  const size_t s = exe.find_last_of("/\\");
  return Lower(s == std::string::npos ? exe : exe.substr(s + 1));
}

// The most recently used account's config folder (by its localconfig.vdf's time).
fs::path AccountConfig(const std::vector<fs::path>& roots) {
  fs::path best;
  fs::file_time_type best_t{};
  for (const fs::path& root : roots) {
    std::error_code ec;
    for (fs::directory_iterator it(root / "userdata", ec), end; !ec && it != end; it.increment(ec)) {
      const fs::path cfg = it->path() / "config";
      std::error_code e2;
      const auto t = fs::last_write_time(cfg / "localconfig.vdf", e2);
      if (e2) continue;
      if (best.empty() || t > best_t) { best = cfg; best_t = t; }
    }
  }
  return best;
}

bool CopyFile(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

}  // namespace

Shortcut Find(const std::vector<fs::path>& roots, const std::wstring& exe_name) {
  Shortcut sc;
  sc.config = AccountConfig(roots);
  if (sc.config.empty()) return sc;
  const std::string data = ReadAll(sc.config / "shortcuts.vdf");
  std::vector<Node> top;
  size_t at = 0;
  if (data.empty() || !ParseMap(data, at, top)) return sc;
  std::string want;
  for (wchar_t c : exe_name) want += char(c);
  want = Lower(want);
  for (const Node& root : top) {
    if (root.type != 0) continue;
    for (const Node& e : root.kids) {
      const Node* exe = Child(e, "Exe");
      if (!exe || ExeName(exe->str) != want) continue;
      sc.found = true;
      if (const Node* id = Child(e, "appid"); id && id->raw.size() == 4) {
        std::memcpy(&sc.appid, id->raw.data(), 4);
      } else {   // older entries: Steam's legacy id, from the exe and the name
        const Node* name = Child(e, "AppName");
        sc.appid = Crc32(exe->str + (name ? name->str : "")) | 0x80000000u;
      }
      return sc;
    }
  }
  return sc;
}

std::string Add(Shortcut& sc, const std::string& name, const fs::path& exe, const fs::path& icon) {
  if (sc.config.empty()) return "no Steam account found";
  const fs::path file = sc.config / "shortcuts.vdf";
  std::vector<Node> top;
  const std::string data = ReadAll(file);
  if (!data.empty()) {
    size_t at = 0;
    if (!ParseMap(data, at, top)) return "could not read Steam's shortcuts.vdf";
  }
  Node* list = nullptr;
  for (Node& n : top)
    if (n.type == 0 && _stricmp(n.key.c_str(), "shortcuts") == 0) list = &n;
  if (!list) {
    top.push_back(Map("shortcuts"));
    list = &top.back();
  }
  int next = 0;   // entries are keyed "0", "1", ...
  for (const Node& e : list->kids) next = std::max(next, std::atoi(e.key.c_str()) + 1);
  const std::string exe_q = "\"" + exe.string() + "\"";
  const std::string dir_q = "\"" + exe.parent_path().string() + "\\\"";
  const uint32_t appid = Crc32(exe_q + name) | 0x80000000u;
  Node e = Map(std::to_string(next).c_str());
  // the fields and spelling current Steam writes itself (appname / exe lower case, StartDir with a trailing \)
  e.kids = {Int("appid", appid), Str("appname", name), Str("exe", exe_q), Str("StartDir", dir_q),
            Str("icon", icon.empty() ? "" : icon.string()), Str("ShortcutPath", ""), Str("LaunchOptions", ""),
            Int("IsHidden", 0), Int("AllowDesktopConfig", 1), Int("AllowOverlay", 1), Int("OpenVR", 0),
            Int("Devkit", 0), Str("DevkitGameID", ""), Int("DevkitOverrideAppID", 0), Int("LastPlayTime", 0),
            Str("FlatpakAppID", ""), Str("sortas", ""), Map("tags")};
  list->kids.push_back(std::move(e));
  std::string out;
  WriteMap(out, top);
  // keep the old file once, then write the new one
  std::error_code ec;
  if (fs::exists(file, ec)) fs::copy_file(file, fs::path(file).concat(".bak"), fs::copy_options::overwrite_existing, ec);
  FILE* f = _wfopen(file.c_str(), L"wb");
  if (!f) return "could not write Steam's shortcuts.vdf";
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
  std::fclose(f);
  if (!ok) return "could not write Steam's shortcuts.vdf";
  sc.found = true;
  sc.appid = appid;
  return "";
}

std::string WriteArt(const Shortcut& sc, const fs::path& art_dir) {
  if (!sc.found || sc.config.empty()) return "not in Steam yet";
  const fs::path grid = sc.config / "grid";
  std::error_code ec;
  fs::create_directories(grid, ec);
  const std::string id = std::to_string(sc.appid);
  const std::pair<const char*, std::string> files[] = {
      {"capsule_600x900.png", id + "p.png"}, {"wide_capsule_920x430.png", id + ".png"},
      {"hero_1920x620.png", id + "_hero.png"}, {"logo.png", id + "_logo.png"}};
  for (const auto& [from, to] : files) {
    if (!fs::exists(art_dir / from)) return std::string("missing ") + from;
    if (!CopyFile(art_dir / from, grid / to)) return "could not write Steam's grid folder";
  }
  return "";
}

}  // namespace steamsc
