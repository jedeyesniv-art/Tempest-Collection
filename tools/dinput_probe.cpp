// dinput_probe.cpp - lists the DirectInput 8 game controllers (what old games like Space Giraffe see) and prints
// their axes / POV / pressed buttons a few times a second, to find a pad or axis that drifts or rests off-centre.
// Build (x86 or x64): cl /nologo /O2 /EHsc tools\dinput_probe.cpp dinput8.lib dxguid.lib user32.lib
// Run: dinput_probe.exe [seconds]   (default 6)
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

static IDirectInput8W* g_di = nullptr;
static std::vector<IDirectInputDevice8W*> g_devs;

static BOOL CALLBACK OnDevice(const DIDEVICEINSTANCEW* inst, void*) {
  IDirectInputDevice8W* dev = nullptr;
  if (FAILED(g_di->CreateDevice(inst->guidInstance, &dev, nullptr))) return DIENUM_CONTINUE;
  dev->SetDataFormat(&c_dfDIJoystick2);
  dev->SetCooperativeLevel(GetConsoleWindow() ? GetConsoleWindow() : GetDesktopWindow(),
                           DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
  dev->Acquire();
  std::wprintf(L"device %zu: \"%ls\" (product \"%ls\") type %08lx\n", g_devs.size(), inst->tszInstanceName,
               inst->tszProductName, inst->dwDevType);
  g_devs.push_back(dev);
  return DIENUM_CONTINUE;
}

int main(int argc, char** argv) {
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 6;
  if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                reinterpret_cast<void**>(&g_di), nullptr))) {
    std::printf("DirectInput8Create failed\n");
    return 1;
  }
  g_di->EnumDevices(DI8DEVCLASS_GAMECTRL, OnDevice, nullptr, DIEDFL_ATTACHEDONLY);
  std::printf("%zu game controller(s)\n", g_devs.size());
  for (int t = 0; t < seconds * 4; ++t) {
    for (size_t i = 0; i < g_devs.size(); ++i) {
      DIJOYSTATE2 s{};
      g_devs[i]->Poll();
      if (FAILED(g_devs[i]->GetDeviceState(sizeof(s), &s))) { g_devs[i]->Acquire(); continue; }
      std::printf("t%5.2f dev%zu X %6ld Y %6ld Z %6ld Rx %6ld Ry %6ld Rz %6ld S0 %6ld S1 %6ld POV %6ld btn", t / 4.0, i,
                  s.lX, s.lY, s.lZ, s.lRx, s.lRy, s.lRz, s.rglSlider[0], s.rglSlider[1], long(s.rgdwPOV[0]));
      for (int b = 0; b < 32; ++b)
        if (s.rgbButtons[b] & 0x80) std::printf(" %d", b);
      std::printf("\n");
    }
    Sleep(250);
  }
  return 0;
}
