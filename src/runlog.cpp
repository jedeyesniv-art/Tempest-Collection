// runlog.cpp - see runlog.h.
#include "runlog.h"

#include <cstdio>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#endif

namespace runlog {

void Start(const std::filesystem::path& userdata, const std::string& name) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path dir = userdata / "logs";
  fs::create_directories(dir, ec);
  const fs::path log = dir / (name + ".log"), old = dir / (name + ".old.log");
  if (fs::exists(log, ec)) {
    fs::remove(old, ec);
    fs::rename(log, old, ec);
  }
  if (!std::freopen(log.string().c_str(), "w", stderr)) return;
  std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef _WIN32
  SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
  const char* cmd = GetCommandLineA();
#else
  const char* cmd = "";
#endif
  const std::time_t now = std::time(nullptr);
  char when[64] = {};
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  std::fprintf(stderr, "%s started %s\n%s\n", name.c_str(), when, cmd);
}

}  // namespace runlog
