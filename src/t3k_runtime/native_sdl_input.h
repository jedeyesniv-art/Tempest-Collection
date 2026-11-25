#pragma once
#include "native_controller_input.h"
#include "native_mouse_input.h"
#include <SDL3/SDL.h>
namespace tempest_native {
// The live host owns relative-mouse capture and calls this exactly once per
// input sample. SDL trackballs appear through the same relative mouse API.
inline bool sample_sdl_mouse(NativeMouseSpinner& spinner,bool captured,NativeControllerPort& record){
    float x=0,y=0;SDL_MouseButtonFlags buttons=SDL_GetRelativeMouseState(&x,&y);
    if(!captured){spinner.clear_motion();record={};return true;}
    if(!spinner.add_motion(x))return false;
    record=spinner.sample({true,bool(buttons&SDL_BUTTON_LMASK),bool(buttons&SDL_BUTTON_RMASK),bool(buttons&SDL_BUTTON_MMASK)});return true;
}
// Call after SDL_PollEvent/SDL_PumpEvents on the window's main thread. Device
// enumeration and port ownership belong to the live host, not this sampler.
inline NativePadSample sample_sdl_pad(SDL_Gamepad* pad,const bool* keys=nullptr){
    NativePadSample result{};result.connected=(pad&&SDL_GamepadConnected(pad))||keys;
    auto set=[&](PadButton button,bool held){if(held)result.held|=pad_button(button);};
    if(pad&&SDL_GamepadConnected(pad)){
        constexpr SDL_GamepadButton buttons[]{SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,SDL_GAMEPAD_BUTTON_INVALID,SDL_GAMEPAD_BUTTON_INVALID,
            SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_BACK,SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK,SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,
            SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        for(unsigned i=0;i<unsigned(PadButton::count);++i)if(buttons[i]!=SDL_GAMEPAD_BUTTON_INVALID)set(PadButton(i),SDL_GetGamepadButton(pad,buttons[i]));
        set(PadButton::leftTrigger,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)>16383);
        set(PadButton::rightTrigger,SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>16383);
        result.leftX=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX);
        result.leftY=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY);
    }
    if(keys){
        constexpr SDL_Scancode mapping[]{SDL_SCANCODE_Z,SDL_SCANCODE_X,SDL_SCANCODE_J,SDL_SCANCODE_I,
            SDL_SCANCODE_Q,SDL_SCANCODE_E,SDL_SCANCODE_K,SDL_SCANCODE_L,SDL_SCANCODE_RETURN,
            SDL_SCANCODE_TAB,SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_UNKNOWN,SDL_SCANCODE_UP,
            SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT};
        for(unsigned i=0;i<unsigned(PadButton::count);++i)if(mapping[i]!=SDL_SCANCODE_UNKNOWN)set(PadButton(i),keys[mapping[i]]);
    }
    return result;
}
}
