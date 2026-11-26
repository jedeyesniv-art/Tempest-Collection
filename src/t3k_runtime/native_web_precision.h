#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
namespace tempest_native {
struct WebRenderPoint {double x=0,y=0;uint32_t packed=0;bool valid=false;};
struct WebRenderPrecision {
    static constexpr uint32_t base=0x800BACF0;
    std::array<WebRenderPoint,64> points{};
    struct StreamPoint{uint32_t address;WebRenderPoint point;};
    std::vector<StreamPoint> stream,transforms;
    static bool index(uint32_t address,unsigned& index){uint32_t offset=address-base;if(offset>=16*32||(offset&7))return false;index=offset/8;return true;}
    void clear(){points.fill({});stream.clear();transforms.clear();}
    auto find(uint32_t address){return std::lower_bound(stream.begin(),stream.end(),address,[](const StreamPoint& p,uint32_t a){return p.address<a;});}
    void invalidate_range(uint32_t address,uint32_t bytes){
        uint64_t end=uint64_t(address)+bytes;
        for(size_t i=0;i<points.size();++i){uint64_t at=uint64_t(base)+i*8;if(at>=address&&at<end)points[i].valid=false;}
        for(auto* list:{&stream,&transforms}){
            auto it=std::lower_bound(list->begin(),list->end(),address,[](const StreamPoint& p,uint32_t a){return p.address<a;});
            for(;it!=list->end()&&it->address<end;++it)it->point.valid=false;
        }
    }
    void invalidate(uint32_t address){invalidate_range(address,1);}
    void record_transform(uint32_t address,uint32_t packed,double x,double y){
        auto p=std::lower_bound(transforms.begin(),transforms.end(),address,[](const StreamPoint& p,uint32_t a){return p.address<a;});
        WebRenderPoint point{x,y,packed,std::isfinite(x)&&std::isfinite(y)};
        if(p!=transforms.end()&&p->address==address)p->point=point;else transforms.insert(p,{address,point});
    }
    bool read_transform(uint32_t address,uint32_t packed,double& x,double& y)const{
        auto p=std::lower_bound(transforms.begin(),transforms.end(),address,[](const StreamPoint& p,uint32_t a){return p.address<a;});
        if(p==transforms.end()||p->address!=address||!p->point.valid||p->point.packed!=packed)return false;
        x=p->point.x;y=p->point.y;return true;
    }
    void record(uint32_t address,uint32_t packed,double x,double y){if(!std::isfinite(x)||!std::isfinite(y)){invalidate(address);return;}unsigned i;WebRenderPoint point{x,y,packed,true};if(index(address,i)){points[i]=point;return;}auto p=find(address);if(p!=stream.end()&&p->address==address)p->point=point;else stream.insert(p,{address,point});}
    bool read(uint32_t address,uint32_t packed,double& x,double& y)const{
        unsigned i;const WebRenderPoint* point=nullptr;
        if(index(address,i))point=&points[i];
        else{auto p=std::lower_bound(stream.begin(),stream.end(),address,[](const StreamPoint& p,uint32_t a){return p.address<a;});if(p!=stream.end()&&p->address==address)point=&p->point;}
        if(!point||!point->valid||point->packed!=packed)return false;x=point->x;y=point->y;return true;
    }
};
}
