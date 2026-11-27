#pragma once
#include "../settings.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <string>
namespace tempest_native {
inline std::string native_aspect_choice(){
 auto value=settings::Str("native_aspect","");
 if(value=="4:3"||value=="16:9"||value=="21:9")return value;
 return settings::Num("widescreen",1)==0?"4:3":"16:9";
}
inline float native_aspect_ratio(){auto value=native_aspect_choice();return value=="21:9"?21.f/9:value=="16:9"?16.f/9:4.f/3;}
inline SDL_FRect native_aspect_fit(float width,float height,float ratio){
 if(!(width>0&&height>0&&ratio>0))return {};
 float h=(std::min)(height,width/ratio),w=h*ratio;
 return {(width-w)/2,(height-h)/2,w,h};
}
}
