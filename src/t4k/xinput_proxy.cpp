// xinput_proxy.cpp - Tempest 4000 helper (Steam app 688140, by Llamasoft / Atari, 2018): skips the game's start-up
// video-mode dialog and starts at the desktop's current resolution and refresh rate.
//
// Built 32-bit as xinput1_4.dll (for Win10\Tempest4000.exe) and xinput1_3.dll (for Win7-8\Tempest4000.exe), placed
// next to the game's exe. The game imports only XInputGetState (ordinal 2) from XInput; this DLL forwards XInput to
// the real system DLL. At load it points the exe's DialogBoxParamW import at a wrapper: for the "Welcome to Tempest
// 4000" dialog (resource 130) it lets the dialog fill its mode list (WM_INITDIALOG), selects the entry matching the
// desktop mode ("W x H @R Hz"; same size with the nearest rate, else the biggest), and sends the dialog the list's
// selection change and "Start Game" - so the game stores and saves that mode and the dialog closes before it is
// ever shown. Hold Shift while the game starts to get the dialog (e.g. for Configure Keyboard).
// Steam Deck / Proton: needs the launch option  WINEDLLOVERRIDES="xinput1_3,xinput1_4=n,b" %command%
// Log: t4k_autostart.log next to the exe (the modes seen and the one chosen).
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

extern "C" IMAGE_DOS_HEADER __ImageBase;   // this DLL's own module (linker-provided)

namespace {

HMODULE g_real = nullptr;
DLGPROC g_orig_proc = nullptr;
decltype(&DialogBoxParamW) g_real_dialog = nullptr;
FILE* g_log = nullptr;

void Log(const char* fmt, ...) {
  if (!g_log) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(g_log, fmt, ap);
  va_end(ap);
  std::fflush(g_log);
}

FARPROC Real(const char* name) {
  if (!g_real) {
    // The system DLL of our own name (xinput1_4.dll / xinput1_3.dll), from the system folder (32-bit: SysWOW64).
    wchar_t self[MAX_PATH], sys[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), self, MAX_PATH);
    const wchar_t* base = wcsrchr(self, L'\\');
    GetSystemDirectoryW(sys, MAX_PATH);
    swprintf(path, MAX_PATH, L"%s%s", sys, base ? base : L"\\xinput1_4.dll");
    g_real = LoadLibraryW(path);
  }
  return g_real ? GetProcAddress(g_real, name) : nullptr;
}

struct ListMode { unsigned w, h; int rate; };

// The game's own table behind the list: one DXGI_MODE_DESC (28 bytes: width, height, refresh numerator, denominator,
// format, scanline order, scaling) per list entry, in list order. Found as the one run of records in writable memory
// whose sizes and rates match every list entry (the raw DXGI list elsewhere still has the modes the game dropped).
DWORD* FindModeTable(const ListMode* modes, int n) {
  DWORD* found = nullptr;
  int hits = 0;
  MEMORY_BASIC_INFORMATION mi;
  for (auto* a = static_cast<BYTE*>(nullptr); VirtualQuery(a, &mi, sizeof(mi)) == sizeof(mi);
       a = static_cast<BYTE*>(mi.BaseAddress) + mi.RegionSize) {
    if (mi.State != MEM_COMMIT || !(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) || (mi.Protect & PAGE_GUARD))
      continue;
    auto* p = static_cast<DWORD*>(mi.BaseAddress);
    const size_t words = mi.RegionSize / 4;
    for (size_t i = 0; i + size_t(n) * 7 <= words; ++i) {
      if (p[i] != modes[0].w || p[i + 1] != modes[0].h) continue;
      bool all = true;
      for (int k = 0; k < n && all; ++k) {
        const DWORD* r = p + i + size_t(k) * 7;
        all = r[0] == modes[k].w && r[1] == modes[k].h && r[3] &&
              std::abs(double(r[2]) / r[3] - modes[k].rate) <= 1.0;
      }
      if (all) { found = p + i; ++hits; }
    }
    if (static_cast<BYTE*>(mi.BaseAddress) + mi.RegionSize < static_cast<BYTE*>(mi.BaseAddress)) break;
  }
  Log("mode table: %d match(es)%s\n", hits, hits == 1 ? "" : " - not changed");
  return hits == 1 ? found : nullptr;
}

// Picks the list entry for the desktop's current mode and starts the game with it. False: nothing suitable.
bool StartWithDesktopMode(HWND dlg) {
  HWND list = GetDlgItem(dlg, 2004);
  const int n = list ? int(SendMessageW(list, LB_GETCOUNT, 0, 0)) : 0;
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
  const int want_w = int(dm.dmPelsWidth), want_h = int(dm.dmPelsHeight), want_r = int(dm.dmDisplayFrequency);
  Log("desktop %dx%d @%d Hz, %d modes in the list\n", want_w, want_h, want_r, n);
  // Score: the desktop's size (exact), else its aspect ratio and the biggest size; then the rate nearest the desktop's.
  int best = -1;
  long long best_score = -1;
  unsigned best_w = 0, best_h = 0;
  static ListMode modes[1024];
  int parsed = 0;
  for (int i = 0; i < n; ++i) {
    wchar_t text[128] = {};
    unsigned w = 0, h = 0;
    int r = 0;
    if (SendMessageW(list, LB_GETTEXTLEN, i, 0) < 127) SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text));
    const bool ok = swscanf(text, L"%u x %u @%d", &w, &h, &r) == 3 && w && h;
    if (i < 1024) modes[i] = {w, h, r};
    if (!ok) continue;
    ++parsed;
    const bool same_size = int(w) == want_w && int(h) == want_h;
    const bool same_aspect = want_h && std::abs(double(w) / h - double(want_w) / want_h) < 0.02;
    const long long score = (same_size ? 1LL << 50 : 0) + (same_aspect ? 1LL << 49 : 0) +
                            (long long(w) * h << 10) + (1000 - std::min(1000, std::abs(r - want_r)));
    if (score > best_score) { best_score = score; best = i; best_w = w; best_h = h; }
  }
  if (best < 0) return false;
  wchar_t chosen[128] = {};
  SendMessageW(list, LB_GETTEXT, best, reinterpret_cast<LPARAM>(chosen));
  Log("chosen: %d (%ls)\n", best, chosen);
  SendMessageW(list, LB_SETCURSEL, best, 0);
  g_orig_proc(dlg, WM_COMMAND, MAKEWPARAM(2004, LBN_SELCHANGE), reinterpret_cast<LPARAM>(list));

  // The game keeps only its first 256 DXGI modes, which on a big, fast monitor cuts the desktop's own mode off the
  // list. Then the chosen entry of the game's table becomes the desktop mode before Start Game copies it (the game
  // sizes both its output and its picture from it, and sets "the closest match" of it).
  if ((int(best_w) != want_w || int(best_h) != want_h) && parsed == n && n <= 1024) {
    if (DWORD* table = FindModeTable(modes, n)) {
      DWORD* r = table + size_t(best) * 7;
      Log("table entry %d: %lux%lu @%lu/%lu -> %dx%d @%d/1\n", best, r[0], r[1], r[2], r[3], want_w, want_h, want_r);
      r[0] = DWORD(want_w);
      r[1] = DWORD(want_h);
      r[2] = DWORD(want_r);
      r[3] = 1;
    }
  }
  g_orig_proc(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dlg, IDOK)));
  return true;
}

INT_PTR CALLBACK WrapperProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  const INT_PTR r = g_orig_proc(dlg, msg, wp, lp);
  if (msg == WM_INITDIALOG) {
    // The list is filled by now: answer it before the dialog is shown (EndDialog during WM_INITDIALOG means it
    // never appears). If it is not filled yet, try once more from the message loop.
    if (!StartWithDesktopMode(dlg)) PostMessageW(dlg, WM_APP + 0x54, 0, 0);
  } else if (msg == WM_APP + 0x54) {
    if (!StartWithDesktopMode(dlg)) Log("no mode to pick: the dialog stays\n");
  }
  return r;
}

INT_PTR WINAPI HookDialogBoxParamW(HINSTANCE inst, LPCWSTR tmpl, HWND parent, DLGPROC proc, LPARAM lp) {
  const bool welcome = IS_INTRESOURCE(tmpl) && LOWORD(reinterpret_cast<ULONG_PTR>(tmpl)) == 130;
  if (!welcome || (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
    if (welcome) Log("Shift held: the dialog is shown\n");
    return g_real_dialog(inst, tmpl, parent, proc, lp);
  }
  g_orig_proc = proc;
  return g_real_dialog(inst, tmpl, parent, WrapperProc, lp);
}

// Points the exe's import of user32!DialogBoxParamW at the hook.
void HookImport() {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
    if (_stricmp(reinterpret_cast<char*>(base + imp->Name), "USER32.dll") != 0) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                                                       : imp->FirstThunk));
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<char*>(by_name->Name), "DialogBoxParamW") != 0) continue;
      DWORD old = 0;
      VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old);
      g_real_dialog = reinterpret_cast<decltype(&DialogBoxParamW)>(slots->u1.Function);
      slots->u1.Function = reinterpret_cast<ULONG_PTR>(&HookDialogBoxParamW);
      VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
      Log("DialogBoxParamW hooked\n");
      return;
    }
  }
  Log("DialogBoxParamW import not found: nothing hooked\n");
}

}  // namespace

extern "C" {

// XInput, forwarded to the system DLL (the ordinals of xinput1_4 / xinput1_3: the game imports ordinal 2).
DWORD WINAPI XInputGetState(DWORD user, void* state) {
  static auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, void*)>(Real("XInputGetState"));
  return f ? f(user, state) : ERROR_DEVICE_NOT_CONNECTED;
}
DWORD WINAPI XInputSetState(DWORD user, void* vib) {
  static auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, void*)>(Real("XInputSetState"));
  return f ? f(user, vib) : ERROR_DEVICE_NOT_CONNECTED;
}
DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, void* caps) {
  static auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, DWORD, void*)>(Real("XInputGetCapabilities"));
  return f ? f(user, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}
void WINAPI XInputEnable(BOOL enable) {
  static auto f = reinterpret_cast<void(WINAPI*)(BOOL)>(Real("XInputEnable"));
  if (f) f(enable);
}
DWORD WINAPI XInputGetBatteryInformation(DWORD user, BYTE type, void* info) {
  static auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, BYTE, void*)>(Real("XInputGetBatteryInformation"));
  return f ? f(user, type, info) : ERROR_DEVICE_NOT_CONNECTED;
}
DWORD WINAPI XInputGetKeystroke(DWORD user, DWORD reserved, void* key) {
  static auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, DWORD, void*)>(Real("XInputGetKeystroke"));
  return f ? f(user, reserved, key) : ERROR_DEVICE_NOT_CONNECTED;
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\')) wcscpy(slash + 1, L"t4k_autostart.log");
    g_log = _wfopen(path, L"w");
    // The game is not DPI aware: with Windows display scaling (e.g. 200% on a 4K screen) it would see a scaled-down
    // screen and window and lay its picture out for that inside the real-size output (a quarter of the screen). Made
    // per-monitor aware before it opens any window, it sees the real pixels. (No scaling on the Deck: no change.)
    // Process-wide first; if that is refused (already set), the main thread (our DllMain runs on it at start-up, and
    // windows take the awareness of the thread that creates them).
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    using SetCtx = BOOL(WINAPI*)(HANDLE);
    using SetThreadCtx = HANDLE(WINAPI*)(HANDLE);
    const HANDLE kPerMonitorV2 = reinterpret_cast<HANDLE>(-4);
    auto set_process = reinterpret_cast<SetCtx>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
    auto set_thread = reinterpret_cast<SetThreadCtx>(GetProcAddress(u32, "SetThreadDpiAwarenessContext"));
    if (set_process && set_process(kPerMonitorV2)) {
      Log("DPI aware: process, per monitor v2\n");
    } else {
      const DWORD err = GetLastError();
      const HANDLE prev = set_thread ? set_thread(kPerMonitorV2) : nullptr;
      Log("DPI aware: process refused (error %lu); main thread %s\n", err, prev ? "set" : "refused too");
      if (!set_process && !set_thread) Log("DPI aware (old Windows): %s\n", SetProcessDPIAware() ? "yes" : "no");
    }
    HookImport();
  }
  return TRUE;
}
