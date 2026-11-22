#include "Bios.h"
#include "byteswap.h"
#include "mpe.h"
#include "joystick.h"
#include "NuonEnvironment.h"

extern NuonEnvironment nuonEnv;
ControllerData *controller;
// llamasoft-nuon: extra controller properties to report (e.g. CTRLR_ANALOG1), set by the frontend.
uint32 g_pad_extra_properties = 0;
// llamasoft-nuon: the pad is there from power-on (status bit, no "changed" event): no "your controller configuration
// has changed" screen at boot (Tempest 3000). Set by the frontend before the game loads.
bool g_pad_present_at_boot = false;

#define NUM_CONTROLLER_ENTRIES (9)

void ControllerInitialize(MPE &mpe)
{
  //return the last 144 bytes of the BIOS region
  mpe.regs[0] = CONTROLLER_ADDRESS;
  controller = (ControllerData *)(nuonEnv.GetPointerToMemory(mpe.mpeIndex, CONTROLLER_ADDRESS));
  for(uint32 i = 0; i < NUM_CONTROLLER_ENTRIES; i++)
  {
    controller[i].config = 0;
    controller[i].buttons = 0;
    controller[i].remote_buttons = 0;
    controller[i].d1.xAxis = 0;
    controller[i].d2.yAxis = 0;
    controller[i].d3.xAxis2 = 0;
    controller[i].d4.yAxis2 = 0;
    controller[i].d5.quadjoyX = 0;
    controller[i].d6.quadjoyY = 0;
  }

  const uint32 properties = KEYBOARD_JOYSTICK_PROPERTIES | g_pad_extra_properties;
  controller[1].config = SwapBytes((uint32)(g_pad_present_at_boot ? CONTROLLER_STATUS_BIT : CONTROLLER_CHANGED_BIT) | properties);
}

void DeviceDetect(MPE &mpe)
{
  const uint32 slot = mpe.regs[0];

  if(slot == 1)
  {
    uint32 config = SwapBytes(controller[1].config);

    if((config & CONTROLLER_CHANGED_BIT) == 0)
    {
      mpe.regs[0] = 0xFFFFFFFFU; //unchanged: -1
      return;
    }

    if(controller)
    {
      config = SwapBytes((uint32)CONTROLLER_STATUS_BIT | (uint32)KEYBOARD_JOYSTICK_PROPERTIES | g_pad_extra_properties);

      controller[1].config = config;
    }

    mpe.regs[0] = 1;
  }
  else
  {
    controller[slot].config = 0;
    controller[slot].buttons = 0;
    controller[slot].d1.xAxis = 0;
    controller[slot].d2.yAxis = 0;
    controller[slot].d3.xAxis2 = 0;
    controller[slot].d4.yAxis2 = 0;
    controller[slot].d5.quadjoyX = 0;
    controller[slot].d6.quadjoyY = 0;

    mpe.regs[0] = 0; //No controller, for now
  }
}
