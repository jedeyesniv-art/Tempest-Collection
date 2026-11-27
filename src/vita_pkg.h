// vita_pkg.h - TxK as a PSN .pkg (2026-11-27, a player's report: the USA release as a No-Intro PKG didn't start).
// The Vita3K core runs / installs a .pkg itself when it is handed one as the game, but only with the title's licence
// (zRIF): the package doesn't carry it. txk.exe and the launcher find the package and check for the licence the way
// the core looks for it (third_party/vita3k-libretro vita3k/libretro/libretro_installer.cpp find_pkg_license), so
// the launcher can say what is missing. One addition: No-Intro packages have random 64-letter names, so a single
// .zrif file of ANY name beside the package is copied to <package name>.zrif (the name the core reads).
#pragma once
#include <cstdint>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace vitapkg {
namespace fs = std::filesystem;

struct Info {
  std::string content_id;   // e.g. UP4461-PCSE00397_00-LLAMASOFTTXK0001
  std::string title_id;     // e.g. PCSE00397
};

inline std::string ReadBytes(const fs::path& p, size_t max = 1 << 16) {
  std::ifstream f(p, std::ios::binary);
  std::string s(max, '\0');
  f.read(s.data(), std::streamsize(max));
  s.resize(size_t(f.gcount()));
  return s;
}

inline std::string LowerExt(const fs::path& p) {
  std::string e = p.extension().string();
  for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

// A PS Vita package (magic 7F 'PKG', type 2) and its content ID (header +0x30).
inline bool ReadInfo(const fs::path& pkg, Info* out) {
  const std::string h = ReadBytes(pkg, 0x60);
  if (h.size() < 0x54 || h.compare(0, 4, "\x7FPKG", 4) != 0 || h[6] != 0 || h[7] != 2) return false;
  out->content_id.assign(h.data() + 0x30, strnlen(h.data() + 0x30, 0x24));
  out->title_id = out->content_id.size() >= 16 ? out->content_id.substr(7, 9) : "";
  return !out->title_id.empty();
}

// TxK's packages: Europe PCSB00494, USA PCSE00397 (any content ID naming TxK counts too).
inline bool IsTxK(const Info& i) {
  return i.title_id == "PCSB00494" || i.title_id == "PCSE00397" || i.content_id.find("TXK") != std::string::npos;
}

// The first TxK package in these folders, or empty.
inline fs::path FindTxK(const std::vector<fs::path>& dirs, Info* info = nullptr) {
  for (const fs::path& d : dirs) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(d, ec)) {
      Info i;
      if (e.is_regular_file(ec) && LowerExt(e.path()) == ".pkg" && ReadInfo(e.path(), &i) && IsTxK(i)) {
        if (info) *info = i;
        return e.path();
      }
    }
  }
  return {};
}

// A licence file (.rif / work.bin / a 512-byte .bin) is this content's when its content ID (+0x10) matches.
inline bool LicenceFor(const fs::path& f, const std::string& content_id) {
  const std::string b = ReadBytes(f, 0x60);
  return b.size() >= 0x40 && content_id == std::string(b.data() + 0x10, strnlen(b.data() + 0x10, 0x30));
}

inline bool NonEmptyText(const fs::path& f) {
  const std::string t = ReadBytes(f, 4096);
  return t.find_first_not_of(" \t\r\n") != std::string::npos;
}

// Where the package's licence is (as the core looks: installed, then beside the package), or empty. `vita_dir` =
// the core's system\vita3k. With `prepare`, a lone .zrif of another name is copied to <package name>.zrif first.
inline fs::path FindLicence(const fs::path& pkg, const Info& info, const fs::path& vita_dir, bool prepare = false) {
  std::error_code ec;
  const fs::path installed = vita_dir / "ux0" / "license" / info.title_id / (info.content_id + ".rif");
  if (fs::exists(installed, ec)) return installed;
  const fs::path dir = pkg.parent_path();
  const std::string stem = pkg.stem().string();
  {   // ours: a lone .zrif under another name, kept in step with <package name>.zrif (a corrected key replaces ours)
    std::vector<fs::path> others;
    for (const auto& e : fs::directory_iterator(dir, ec))
      if (e.is_regular_file(ec) && LowerExt(e.path()) == ".zrif" && e.path().filename() != fs::path(stem + ".zrif") &&
          NonEmptyText(e.path()))
        others.push_back(e.path());
    if (others.size() == 1) {
      if (!prepare) return others.front();
      if (ReadBytes(others.front()) != ReadBytes(dir / (stem + ".zrif")))
        fs::copy_file(others.front(), dir / (stem + ".zrif"), fs::copy_options::overwrite_existing, ec);
    }
  }
  for (const char* ext : {".zrif", ".zRIF", ".txt"})
    if (fs::exists(dir / (stem + ext), ec) && NonEmptyText(dir / (stem + ext))) return dir / (stem + ext);
  for (const fs::path& f : {dir / (stem + ".rif"), dir / (info.content_id + ".rif")})
    if (fs::exists(f, ec)) return f;
  if (fs::exists(dir / "work.bin", ec) && LicenceFor(dir / "work.bin", info.content_id)) return dir / "work.bin";
  std::vector<fs::path> bins;
  for (const fs::path& folder : {dir, dir.parent_path()})
    for (const auto& e : fs::directory_iterator(folder, ec)) {
      if (!e.is_regular_file(ec)) continue;
      if (LowerExt(e.path()) == ".bin" && e.file_size(ec) == 512) {
        if (LicenceFor(e.path(), info.content_id)) return e.path();
        bins.push_back(e.path());
      }
    }
  if (bins.size() == 1) return bins.front();   // (the core takes the only one there)
  return {};
}
}  // namespace vitapkg
