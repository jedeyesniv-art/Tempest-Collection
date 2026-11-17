// d3d9_proxy.cpp - Space Giraffe helper (Steam app 27800, Llamasoft 2008): starts the game straight into the chosen
// flavour, in English, as a borderless window covering the monitor at the desktop's resolution.
//
// Built 32-bit as d3d9.dll and placed next to SpaceGiraffePC.exe; forwards Direct3D 9 to the system DLL. The exe is
// Steam-DRM wrapped (.bind section), so nothing is patched on disk: at load this DLL points the exe's imports of
// user32!DialogBoxParamA at a wrapper that answers the two start-up dialogs before they are shown -
//   131 "Choose Your Language": English (button 1008),
//   135 "Choose Flavour": the button whose label matches the flavour setting (labels are filled in at run time from
//       the *.chunk files: Acid-Mix.chunk = Acid, chunk.chunk = Nuxx).
// Direct3D: IDirect3D9::CreateDevice and IDirect3DDevice9::Reset are hooked (vtable) to log the game's presentation
// parameters and apply the window / size / vsync settings; Present is counted for a frames-per-second log.
// Settings: sg_helper.txt next to the exe (key = value lines; written by the Tempest Collection launcher):
//   flavour = acid | nuxx | ask      (ask: the game's own dialog)
//   borderless = 1                   (window without a frame covering the monitor)
//   vsync = 1                        (present once per display refresh; 0 = as fast as possible; -1 = the game's)
//   max_fps = 0                      (frame cap, 0 = none; e.g. 60 on a 120 Hz screen)
//   stick_deadzone = 20              (percent; the game sets none, so the slightest touch moves its menus)
//   input_log = 0                    (1: log the pad / mouse states the game reads)
// Hold Shift while the game starts to get the game's own dialogs.
// Steam Deck / Proton: launch option  WINEDLLOVERRIDES="d3d9=n,b" %command%
// Log: sg_helper.log next to the exe.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

namespace {

HMODULE g_real = nullptr;
FILE* g_log = nullptr;
DLGPROC g_orig_proc = nullptr;
decltype(&DialogBoxParamA) g_real_dialog = nullptr;

std::string g_flavour = "acid";
int g_borderless = 1;
int g_vsync = 1;
int g_dpi_aware = 1;
int g_max_fps = 0;
int g_deadzone = 20;   // stick deadzone, percent (DirectInput DIPROP_DEADZONE; the game sets none)
int g_input_log = 0;

HWND g_window = nullptr;

void Log(const char* fmt, ...) {
  if (!g_log) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(g_log, fmt, ap);
  va_end(ap);
  std::fflush(g_log);
}

std::wstring ExeDir() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (wchar_t* slash = wcsrchr(path, L'\\')) slash[1] = 0;
  return path;
}

void LoadSettings() {
  FILE* f = _wfopen((ExeDir() + L"sg_helper.txt").c_str(), L"r");
  if (!f) { Log("no sg_helper.txt: defaults\n"); return; }
  char line[256];
  while (std::fgets(line, sizeof line, f)) {
    char key[64] = {}, val[128] = {};
    if (line[0] == '#' || std::sscanf(line, " %63[a-z_] = %127s", key, val) != 2) continue;
    if (!std::strcmp(key, "flavour")) g_flavour = val;
    else if (!std::strcmp(key, "borderless")) g_borderless = std::atoi(val);
    else if (!std::strcmp(key, "vsync")) g_vsync = std::atoi(val);
    else if (!std::strcmp(key, "dpi_aware")) g_dpi_aware = std::atoi(val);
    else if (!std::strcmp(key, "max_fps")) g_max_fps = std::atoi(val);
    else if (!std::strcmp(key, "stick_deadzone")) g_deadzone = std::atoi(val);
    else if (!std::strcmp(key, "input_log")) g_input_log = std::atoi(val);
  }
  std::fclose(f);
  Log("settings: flavour %s, borderless %d, vsync %d, dpi_aware %d, max_fps %d, stick_deadzone %d, input_log %d\n",
      g_flavour.c_str(), g_borderless, g_vsync, g_dpi_aware, g_max_fps, g_deadzone, g_input_log);
}

HMODULE Real() {
  if (!g_real) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);   // 32-bit process: SysWOW64
    g_real = LoadLibraryW((std::wstring(sys) + L"\\d3d9.dll").c_str());
    if (!g_real) Log("real d3d9.dll not loaded (error %lu)\n", GetLastError());
  }
  return g_real;
}

// ---- start-up dialogs ----

bool Contains(const char* text, const char* word) {
  std::string t(text);
  for (auto& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
  return t.find(word) != std::string::npos;
}

void Click(HWND dlg, int id) {
  g_orig_proc(dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dlg, id)));
}

// True when it answered the dialog.
bool Answer(HWND dlg, int tmpl) {
  if (tmpl == 131) {
    Log("language dialog: English\n");
    Click(dlg, 1008);
    return true;
  }
  if (tmpl == 135) {
    static bool listed = false;
    if (!listed) {
      listed = true;
      for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        char cls[64] = {}, text[128] = {};
        GetClassNameA(c, cls, sizeof cls);
        GetWindowTextA(c, text, sizeof text);
        RECT r;
        GetWindowRect(c, &r);
        Log("  control %d %s \"%s\" visible %d style %08lx at %ld,%ld %ldx%ld\n", GetDlgCtrlID(c), cls, text,
            IsWindowVisible(c) ? 1 : 0, GetWindowLongA(c, GWL_STYLE), r.left, r.top, r.right - r.left, r.bottom - r.top);
      }
    }
    int pick = 0;
    for (int id = 1013; id <= 1017; ++id) {
      HWND b = GetDlgItem(dlg, id);
      char text[128] = {};
      // Own visible flag (the dialog itself is not shown yet, so IsWindowVisible is false for all of them).
      if (!b || !(GetWindowLongA(b, GWL_STYLE) & WS_VISIBLE) || !GetWindowTextA(b, text, sizeof text)) continue;
      const bool acid = Contains(text, "acid");
      Log("flavour button %d: \"%s\"\n", id, text);
      if (!pick && (g_flavour == "acid" ? acid : !acid)) pick = id;
    }
    if (!pick) { Log("flavour %s not found: the dialog stays\n", g_flavour.c_str()); return false; }
    Log("flavour: button %d\n", pick);
    PostMessageA(GetDlgItem(dlg, pick), BM_CLICK, 0, 0);   // a real click once the dialog runs
    return true;
  }
  return false;
}

int g_tmpl = 0;

INT_PTR CALLBACK WrapperProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
  const INT_PTR r = g_orig_proc(dlg, msg, wp, lp);
  if (msg == WM_INITDIALOG) {
    // Buttons set up by now (else once more from the message loop); EndDialog here means it is never shown.
    if (!Answer(dlg, g_tmpl)) PostMessageA(dlg, WM_APP + 0x53, 0, 0);
  } else if (msg == WM_APP + 0x53) {
    Answer(dlg, g_tmpl);
  }
  return r;
}

INT_PTR WINAPI HookDialogBoxParamA(HINSTANCE inst, LPCSTR tmpl, HWND parent, DLGPROC proc, LPARAM lp) {
  const int id = IS_INTRESOURCE(tmpl) ? LOWORD(reinterpret_cast<ULONG_PTR>(tmpl)) : -1;
  Log("dialog %d\n", id);
  const bool ours = (id == 131) || (id == 135 && g_flavour != "ask");
  if (!ours || (GetAsyncKeyState(VK_SHIFT) & 0x8000)) return g_real_dialog(inst, tmpl, parent, proc, lp);
  g_orig_proc = proc;
  g_tmpl = id;
  return g_real_dialog(inst, tmpl, parent, WrapperProc, lp);
}

bool HookImport(const char* dll, const char* name, void* hook, void** real) {
  auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
    if (_stricmp(reinterpret_cast<char*>(base + imp->Name), dll) != 0) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk
                                                                                       : imp->FirstThunk));
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<char*>(by_name->Name), name) != 0) continue;
      DWORD old = 0;
      VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old);
      *real = reinterpret_cast<void*>(slots->u1.Function);
      slots->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
      VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
      Log("%s hooked\n", name);
      return true;
    }
  }
  Log("%s import not found\n", name);
  return false;
}

// ---- Direct3D ----

void** Vtbl(void* com) { return *static_cast<void***>(com); }

void PatchVtbl(void* com, int index, void* hook, void** real) {
  void** v = Vtbl(com);
  if (v[index] == hook) return;
  DWORD old = 0;
  VirtualProtect(&v[index], sizeof(void*), PAGE_READWRITE, &old);
  *real = v[index];
  v[index] = hook;
  VirtualProtect(&v[index], sizeof(void*), old, &old);
}

RECT MonitorRect(HWND w) {
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY), &mi);
  return mi.rcMonitor;
}

void LogParams(const char* what, const D3DPRESENT_PARAMETERS* p) {
  Log("%s: %ux%u fmt %d count %u ms %d swap %d win %p windowed %d depth %d/%d flags %lx rate %u interval %x\n", what,
      p->BackBufferWidth, p->BackBufferHeight, p->BackBufferFormat, p->BackBufferCount, p->MultiSampleType,
      p->SwapEffect, p->hDeviceWindow, p->Windowed, p->EnableAutoDepthStencil, p->AutoDepthStencilFormat, p->Flags,
      p->FullScreen_RefreshRateInHz, p->PresentationInterval);
}

void Adjust(D3DPRESENT_PARAMETERS* p, HWND focus) {
  HWND w = p->hDeviceWindow ? p->hDeviceWindow : focus;
  if (w) g_window = w;
  if (g_borderless && g_window) {
    const RECT m = MonitorRect(g_window);
    SetWindowLongA(g_window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongA(g_window, GWL_EXSTYLE, 0);
    SetWindowPos(g_window, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    // To the front: started by Steam, the game is not the foreground program (the launcher allows any to take it).
    const BOOL front = SetForegroundWindow(g_window);
    Log("to the front: %s\n", front ? "ok" : "refused");
    p->Windowed = TRUE;
    p->FullScreen_RefreshRateInHz = 0;
    // The picture at the window's own size (the game may ask for a mode the monitor doesn't have, e.g. 4096x2160).
    p->BackBufferWidth = UINT(m.right - m.left);
    p->BackBufferHeight = UINT(m.bottom - m.top);
    Log("borderless: window %ldx%ld at %ld,%ld\n", m.right - m.left, m.bottom - m.top, m.left, m.top);
  }
  if (g_vsync >= 0) p->PresentationInterval = g_vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
}

using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                                   IDirect3DDevice9**);
ResetFn g_reset = nullptr;
PresentFn g_present = nullptr;
CreateDeviceFn g_create = nullptr;

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* p) {
  LogParams("Reset (game)", p);
  Adjust(p, nullptr);
  LogParams("Reset (used)", p);
  const HRESULT hr = g_reset(dev, p);
  Log("Reset -> %08lx\n", hr);
  return hr;
}

// Frame cap (max_fps): each present waits until 1 / max_fps after the previous one's slot (Sleep to ~1 ms before,
// then spin). Windowed Direct3D 9 refuses an interval of two refreshes, so this is how 60 a second is done on a fast
// screen; with vsync on the present then lands on the next refresh.
void Cap() {
  if (g_max_fps <= 0) return;
  static LARGE_INTEGER freq{}, next{};
  if (!freq.QuadPart) {
    QueryPerformanceFrequency(&freq);
    timeBeginPeriod(1);
  }
  const LONGLONG period = freq.QuadPart / g_max_fps;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (!next.QuadPart || now.QuadPart - next.QuadPart > period) next.QuadPart = now.QuadPart;   // first, or fell behind
  for (;;) {
    QueryPerformanceCounter(&now);
    const LONGLONG left = next.QuadPart - now.QuadPart;
    if (left <= 0) break;
    const LONGLONG ms = left * 1000 / freq.QuadPart;
    if (ms >= 2) Sleep(DWORD(ms - 1)); else YieldProcessor();
  }
  next.QuadPart += period;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND w, const RGNDATA* rgn) {
  static LARGE_INTEGER freq, t0;
  static int frames = 0;
  if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&t0); }
  ++frames;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const double s = double(now.QuadPart - t0.QuadPart) / double(freq.QuadPart);
  if (s >= 5.0) {
    Log("%.1f presents/s%s%s\n", frames / s, src ? " (src rect)" : "", dst ? " (dst rect)" : "");
    frames = 0;
    t0 = now;
  }
  Cap();
  return g_present(dev, src, dst, w, rgn);
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                           D3DPRESENT_PARAMETERS* p, IDirect3DDevice9** out) {
  LogParams("CreateDevice (game)", p);
  Adjust(p, focus);
  LogParams("CreateDevice (used)", p);
  const HRESULT hr = g_create(d3d, adapter, type, focus, flags, p, out);
  Log("CreateDevice -> %08lx\n", hr);
  if (SUCCEEDED(hr) && out && *out) {
    PatchVtbl(*out, 16, reinterpret_cast<void*>(&HookReset), reinterpret_cast<void**>(&g_reset));
    PatchVtbl(*out, 17, reinterpret_cast<void*>(&HookPresent), reinterpret_cast<void**>(&g_present));
  }
  return hr;
}

// ---- DirectInput (investigation: how the game reads the pad) ----
// Vtable indices: IDirectInput8 CreateDevice 3; IDirectInputDevice8 SetProperty 6, Acquire 7, GetDeviceState 9,
// GetDeviceData 10, SetDataFormat 11, Poll 25.

void* g_joy = nullptr;   // the game's joystick device (not keyboard / mouse)
using CreateDevFn = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, void**, void*);
using SetPropFn = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, const void*);
using GetStateFn = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, void*);
using GetDataFn = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, void*, DWORD*, DWORD);
using SetFmtFn = HRESULT(STDMETHODCALLTYPE*)(void*, const void*);
CreateDevFn g_di_create = nullptr;
SetPropFn g_di_setprop = nullptr;
GetStateFn g_di_getstate = nullptr;
GetDataFn g_di_getdata = nullptr;
SetFmtFn g_di_setfmt = nullptr;

const GUID kSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
const GUID kSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

HRESULT STDMETHODCALLTYPE HookSetProp(void* dev, REFGUID prop, const void* hdr) {
  const auto* h = static_cast<const DWORD*>(hdr);   // DIPROPHEADER: size, header size, obj, how; then data
  const uintptr_t id = reinterpret_cast<uintptr_t>(&prop);
  Log("SetProperty dev %p prop %u obj %lu how %lu data %ld %ld\n", dev, unsigned(id < 0x10000 ? id : 0), h[2], h[3],
      long(h[4]), long(h[0] > 20 ? h[5] : 0));
  return g_di_setprop(dev, prop, hdr);
}

HRESULT STDMETHODCALLTYPE HookSetFmt(void* dev, const void* fmt) {
  const auto* f = static_cast<const DWORD*>(fmt);   // DIDATAFORMAT: size, objsize, flags, datasize, numobjs, objs
  Log("SetDataFormat dev %p flags %lx datasize %lu objects %lu\n", dev, f[2], f[3], f[4]);
  const HRESULT hr = g_di_setfmt(dev, fmt);
  if (dev == g_joy && SUCCEEDED(hr) && g_deadzone > 0) {
    // DIPROPDWORD for the whole device: DirectInput reports 0 inside the deadzone and rescales the rest to the full
    // range, so the game keeps its whole travel. DIPROP_DEADZONE is MAKEDIPROP(5): a GUID "reference" of value 5.
    struct { DWORD size, header, obj, how, value; } dz = {20, 16, 0, 0 /*DIPH_DEVICE*/, DWORD(g_deadzone) * 100};
    const HRESULT r = g_di_setprop(dev, *reinterpret_cast<const GUID*>(uintptr_t(5)), &dz);
    Log("stick deadzone %d%% -> %08lx\n", g_deadzone, r);
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE HookGetState(void* dev, DWORD size, void* data) {
  const HRESULT hr = g_di_getstate(dev, size, data);
  if (g_input_log && dev != g_joy && SUCCEEDED(hr) && size >= 12) {   // mouse (shares the vtable): log any movement / button
    const auto* l = static_cast<const LONG*>(data);
    const BYTE* b = static_cast<const BYTE*>(data) + 12;
    bool any = l[0] || l[1] || l[2];
    for (DWORD i = 12; i < size; ++i) any |= b[i - 12] != 0;
    static int logged = 0;
    if (any && logged < 400) {
      ++logged;
      Log("mouse state dev %p: dx %ld dy %ld dz %ld b %02x %02x %02x\n", dev, l[0], l[1], l[2], b[0], b[1], b[2]);
    }
  }
  if (g_input_log && dev == g_joy) {
    static BYTE last[64];
    static int calls = 0;
    ++calls;
    const DWORD n = size < 64 ? size : 64;
    if (SUCCEEDED(hr) && std::memcmp(last, data, n) != 0) {
      std::memcpy(last, data, n);
      const auto* l = static_cast<const LONG*>(data);
      Log("joy state (call %d, size %lu): X %ld Y %ld Z %ld Rx %ld Ry %ld Rz %ld pov %ld\n", calls, size, l[0], l[1],
          l[2], l[3], l[4], l[5], size >= 36 ? l[8] : -2);
    } else if (FAILED(hr)) {
      static int fails = 0;
      if (++fails <= 20) Log("joy GetDeviceState -> %08lx\n", hr);
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE HookGetData(void* dev, DWORD objsize, void* data, DWORD* count, DWORD flags) {
  const DWORD want = count ? *count : 0;
  const HRESULT hr = g_di_getdata(dev, objsize, data, count, flags);
  if (dev == g_joy) {
    static int logged = 0;
    if (logged < 200 && count && *count) {
      ++logged;
      Log("joy GetDeviceData want %lu got %lu hr %08lx\n", want, *count, hr);
    }
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateDev(void* di, REFGUID guid, void** out, void* outer) {
  const HRESULT hr = g_di_create(di, guid, out, outer);
  const bool kb = IsEqualGUID(guid, kSysKeyboard), mouse = IsEqualGUID(guid, kSysMouse);
  Log("DirectInput CreateDevice %s -> %08lx dev %p\n", kb ? "keyboard" : mouse ? "mouse" : "joystick", hr,
      out ? *out : nullptr);
  if (SUCCEEDED(hr) && out && *out && !kb && !mouse && !g_joy) {   // the first joystick only (one saved original)
    g_joy = *out;
    PatchVtbl(*out, 6, reinterpret_cast<void*>(&HookSetProp), reinterpret_cast<void**>(&g_di_setprop));
    PatchVtbl(*out, 9, reinterpret_cast<void*>(&HookGetState), reinterpret_cast<void**>(&g_di_getstate));
    PatchVtbl(*out, 10, reinterpret_cast<void*>(&HookGetData), reinterpret_cast<void**>(&g_di_getdata));
    PatchVtbl(*out, 11, reinterpret_cast<void*>(&HookSetFmt), reinterpret_cast<void**>(&g_di_setfmt));
  }
  return hr;
}

using DI8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, void*);
DI8CreateFn g_di8_create = nullptr;

HRESULT WINAPI HookDirectInput8Create(HINSTANCE inst, DWORD ver, REFIID iid, void** out, void* outer) {
  const HRESULT hr = g_di8_create(inst, ver, iid, out, outer);
  Log("DirectInput8Create(%lx) -> %08lx\n", ver, hr);
  if (SUCCEEDED(hr) && out && *out)
    PatchVtbl(*out, 3, reinterpret_cast<void*>(&HookCreateDev), reinterpret_cast<void**>(&g_di_create));
  return hr;
}

}  // namespace

extern "C" {

IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
  using Fn = IDirect3D9*(WINAPI*)(UINT);
  static auto f = Real() ? reinterpret_cast<Fn>(GetProcAddress(Real(), "Direct3DCreate9")) : nullptr;
  IDirect3D9* d3d = f ? f(sdk) : nullptr;
  Log("Direct3DCreate9(%u) -> %p\n", sdk, static_cast<void*>(d3d));
  if (d3d) PatchVtbl(d3d, 16, reinterpret_cast<void*>(&HookCreateDevice), reinterpret_cast<void**>(&g_create));
  return d3d;
}

// Everything else the system d3d9.dll exports by name, forwarded by jump stubs (same arguments, same stack): D3DX
// looks some up at run time (Direct3DShaderValidatorCreate9 when it assembles shaders - missing, the game's engine
// set-up fails with "Impossible to initialise the Neon engine").
#define SG_FORWARDS(X) X(Direct3DCreate9Ex) X(Direct3DCreate9On12) X(Direct3DCreate9On12Ex) \
  X(Direct3DShaderValidatorCreate9) X(Direct3D9EnableMaximizedWindowedModeShim) X(DebugSetLevel) X(DebugSetMute) \
  X(PSGPError) X(PSGPSampleTexture) X(D3DPERF_BeginEvent) X(D3DPERF_EndEvent) X(D3DPERF_GetStatus) \
  X(D3DPERF_QueryRepeatFrame) X(D3DPERF_SetMarker) X(D3DPERF_SetOptions) X(D3DPERF_SetRegion)
#define SG_PTR(n) void* g_fwd_##n = nullptr;
SG_FORWARDS(SG_PTR)
#define SG_STUB(n) __declspec(naked) void fwd_##n() { __asm { jmp dword ptr [g_fwd_##n] } }
SG_FORWARDS(SG_STUB)

void ResolveForwards() {
  if (!Real()) return;
#define SG_RESOLVE(n) g_fwd_##n = reinterpret_cast<void*>(GetProcAddress(Real(), #n));
  SG_FORWARDS(SG_RESOLVE)
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    g_log = _wfopen((ExeDir() + L"sg_helper.log").c_str(), L"w");
    LoadSettings();
    // Not DPI aware on its own: with display scaling it would see a scaled-down desktop (see the T4K helper).
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    using SetCtx = BOOL(WINAPI*)(HANDLE);
    auto set_process = reinterpret_cast<SetCtx>(GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
    if (g_dpi_aware) Log("DPI aware: %s\n", set_process && set_process(reinterpret_cast<HANDLE>(-4)) ? "yes" : "no");
    ResolveForwards();
    HookImport("USER32.dll", "DialogBoxParamA", reinterpret_cast<void*>(&HookDialogBoxParamA),
               reinterpret_cast<void**>(&g_real_dialog));
    HookImport("DINPUT8.dll", "DirectInput8Create", reinterpret_cast<void*>(&HookDirectInput8Create),
               reinterpret_cast<void**>(&g_di8_create));
  }
  return TRUE;
}
