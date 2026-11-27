#pragma once
#include "../settings.h"
#include "game/TempestSceneTiming.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <numeric>
namespace tempest_native {
struct TimingField {const char* label;const char* key;unsigned initial;};
// One table owns defaults, menu rows, snapshots and restart comparison.
inline constexpr std::array<TimingField,17> timingFields{{
 {"Gameplay","native_gameplay_speed",22},
 {"Menus / level select","native_menu_speed",24},
 {"Opening title","native_opening_speed",24},
 {"Level-end travel","native_travel_speed",22},
 {"Level clear","native_clear_speed",48},
 {"Bonus sequence","native_bonus_speed",36},
 {"Death","native_death_speed",24},
 {"Entry","native_entry_speed",30},
 {"Shared transitions","native_transition_speed",24},
 {"Attract / assets","native_asset_speed",24},
 {"Game over","native_gameover_speed",24},
 {"Name / password entry","native_name_speed",24},
 {"Tools / calibration","native_tool_speed",24},
 {"Pause","native_pause_speed",24},
 {"Interactive warp game","native_warpgame_speed",24},
 {"Life settling","native_settle_speed",24},
 {"Other scenes","native_other_speed",24}
}};
struct NativeTimingProfile {
 bool highFramerate=true;unsigned presentationHz=0;std::array<unsigned,timingFields.size()> rates{};
 NativeTimingProfile(){for(size_t i=0;i<rates.size();++i)rates[i]=timingFields[i].initial;}
 static NativeTimingProfile read(){
  NativeTimingProfile p;double mode=settings::Num("native_high_framerate",1);
  // Preserve saved On=1 / Off=0 choices; 60 adds capped presentation while
  // retaining the same high-rate simulation and effect integration.
  p.highFramerate=mode!=0;p.presentationHz=mode==60?60:0;
  for(size_t i=0;i<timingFields.size();++i){auto& f=timingFields[i];double v=settings::Num(f.key,f.initial);
   p.rates[i]=std::isfinite(v)?unsigned(std::lround((std::clamp)(v,15.0,60.0))):f.initial;}
  return p;
 }
 bool operator==(const NativeTimingProfile& other)const{return highFramerate==other.highFramerate&&presentationHz==other.presentationHz&&rates==other.rates;}
 SceneCadence cadence(SceneTimingKind kind,bool gameplay=false)const{
  unsigned index=16;
  if(gameplay)index=0;else switch(kind){
   case SceneTimingKind::animatedMenu:case SceneTimingKind::particleCrawl:case SceneTimingKind::levelSelect:index=1;break;
   case SceneTimingKind::opening:index=2;break;
   case SceneTimingKind::levelTravel:index=3;break;
   case SceneTimingKind::levelClear:index=4;break;
   case SceneTimingKind::bonus:index=5;break;
   case SceneTimingKind::death:index=6;break;
   case SceneTimingKind::entry:index=7;break;
   case SceneTimingKind::transition:index=8;break;
   case SceneTimingKind::asset:index=9;break;
   case SceneTimingKind::gameOver:index=10;break;
   case SceneTimingKind::nameEntry:index=11;break;
   case SceneTimingKind::tool:index=12;break;
   case SceneTimingKind::pause:index=13;break;
   case SceneTimingKind::warpGame:index=14;break;
   case SceneTimingKind::settle:index=15;break;
   default:break;
  }
  unsigned hz=rates[index],g=std::gcd(120u,hz);
  return {kind,hz,highFramerate?120/g:1,highFramerate?hz/g:1};
 }
};
// Immutable throughout a run. Settings changes take effect only on restart.
inline NativeTimingProfile activeTiming;
inline bool timing_changes_pending(){return !(NativeTimingProfile::read()==activeTiming);}
}
