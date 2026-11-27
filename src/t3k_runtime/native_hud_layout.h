#pragma once
#include "../t3k_native.h"
#include "native_draw_sources.h"
#include "game/TempestSceneTiming.h"
namespace tempest_native {
inline bool native_hud_layout_scene(SceneTimingKind kind){
 switch(kind){case SceneTimingKind::none:case SceneTimingKind::levelTravel:case SceneTimingKind::levelClear:
 case SceneTimingKind::death:case SceneTimingKind::entry:case SceneTimingKind::settle:case SceneTimingKind::pause:return true;default:return false;}
}
inline void apply_native_hud_layout(t3knative::Picture& picture,float horizontalMargin){
 if(!(horizontalMargin>0))return;
 for(auto& op:picture.ops){
  float dx=op.hudAnchor==unsigned(NativeHudAnchor::left)?-horizontalMargin:op.hudAnchor==unsigned(NativeHudAnchor::right)?horizontalMargin:0;
  if(!dx)continue;
  if(op.kind==t3knative::Op::kSegs)for(unsigned i=0;i<op.count;++i){auto& s=picture.segs.at(op.first+i);s.x0+=dx;s.x1+=dx;}
  else if(op.kind==t3knative::Op::kText)for(unsigned i=0;i<op.count;++i)picture.glyphs.at(op.first+i).x+=dx;
  else if(op.kind==t3knative::Op::kBlobs)for(unsigned i=0;i<op.count;++i)picture.blobs.at(op.first+i).x+=dx;
  else if(op.kind==t3knative::Op::kBlendRect)op.x+=dx;
 }
}
}
