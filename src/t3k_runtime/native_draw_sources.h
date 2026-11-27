#pragma once
#include <cstdint>
namespace tempest_native {
enum class NativeHudAnchor:unsigned {none,left,right};
struct NativeDrawSource {uint32_t draw=0,object=0,source=0,parameters=0,globals=0,content=0;NativeHudAnchor anchor=NativeHudAnchor::none;};
// Semantic identities from the reconstructed HUD objects, not screen corners
// or renderer types (which are shared by gameplay and title/menu objects).
inline NativeHudAnchor native_hud_anchor(uint32_t source,uint32_t parameters,uint32_t globals,uint32_t content){
 if(source==0x800B2FD0&&parameters==0x800B94F0)return NativeHudAnchor::left; // score
 if(source!=0x800B3010)return NativeHudAnchor::none;
 if(globals==0x800B6750)return NativeHudAnchor::left; // jump/resource gauge
 if(parameters==0x800B6A90){
  if(content==0x8010CDF0)return NativeHudAnchor::left; // lives
  if(content==0x8010CE10)return NativeHudAnchor::right; // web number
 }
 if(globals==0x800B4F50||globals==0x800B4E70)return NativeHudAnchor::right; // power/status icon + gauge
 return NativeHudAnchor::none;
}
}
