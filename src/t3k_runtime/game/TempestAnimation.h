// Included by TempestPreparation.h inside tempest_native. This interprets the
// game's animation data language, not NUON instructions.
struct PreparationAnimation{
    CommandMemory& m;uint32_t source=0,cursor=0,left=0,destination=0,words=0;
    uint32_t oscillators=0,phase=0,overrideAddress=0,lastAddress=0,repeatLeft=0,repeatCursor=0,repeatWords=0,repeatStep=0;
    bool vector=false;
    SimulationStep step{};
    bool fractional_oscillators(unsigned units=0xFFFFFFFF){
        if(units==0xFFFFFFFF)units=step.index+step.numerator;
        for(unsigned k=0;k<oscillators;++k){uint32_t v;if(!m.read32(0x20100B60+k*4,v))return false;
            uint32_t increment=uint32_t(uint64_t(v&65535)*units/step.divisions);
            if(!m.write32(0x20100B60+k*4,(v+(increment<<16)&0xFFFF0000)|(v&65535)))return false;}
        return true;
    }
    bool reload(){cursor=0x20100A20;left=16;return preparation_copy(m,source,cursor,16);}
    void advance(){cursor+=4;--left;}
    bool flush(){return !destination||preparation_copy(m,0x20100A60,destination,words);}
    bool oscillator(uint32_t command,uint32_t& value){
        uint32_t v;if(!m.read32(0x20100B60+((command>>8)&255)*4,v))return false;
        auto cs=preparation_sincos(v>>16);uint32_t factor=command>>16;value=command_mul(factor,cs[factor&1?0:1],30);advance();return true;
    }
    bool channels(uint32_t command,unsigned channel){
        uint32_t saved=phase;phase+=0x10000;
        if(!vector){if(int32_t(phase)<=0)phase=0;else if(int32_t(phase)>0x10000)phase=0x10000;}
        uint32_t a=command>>24,b=(command>>16)&255;
        uint32_t value=command_mul(phase,b-a,vector?0:16)+(vector?a<<16:a);
        uint32_t address=overrideAddress?overrideAddress:0x20100A60+((command>>8)&255)*(vector?16:4),old;
        advance();lastAddress=address;
        if(!m.read32(address,old))return false;
        phase=saved;
        if(vector)return m.write32(address+channel*4,value);
        unsigned shift=24-channel*8;
        // The first-channel source uses BITS #24, retaining bit 24 as well.
        uint32_t mask=channel==0?0x01FFFFFFU:~(255U<<shift);
        return m.write32(address,(old&mask)|(value<<shift));
    }
    bool rotate(uint32_t command){
        advance();unsigned start=command>>24,end=(command>>16)&255,count=end-start;if(!count||count>255)return false;
        if(!step.legacy_boundary())return true;
        uint32_t mask=0;unsigned bits=(command>>12)&15;for(unsigned k=0;k<4;++k)if(bits&(8>>k))mask|=255U<<(24-k*8);
        uint32_t first=0x20100A60+start*4,last=0x20100A60+end*4,saved,old,next;
        if((command>>8)&15){
            if(!m.read32(last,saved))return false;
            for(unsigned i=0;i<count;++i){uint32_t at=last-i*4;if(!m.read32(at-4,next)||!m.read32(at,old)||!m.write32(at,(old&~mask)|(next&mask)))return false;}
            return m.read32(first,old)&&m.write32(first,(old&~mask)|(saved&mask));
        }
        if(!m.read32(first,saved))return false;
        for(unsigned i=0;i<count;++i){uint32_t at=first+i*4;if(!m.read32(at+4,next)||!m.read32(at,old)||!m.write32(at,(old&~mask)|(next&mask)))return false;}
        return m.read32(last,old)&&m.write32(last,(old&~mask)|(saved&mask));
    }
    bool nibbles(uint32_t command){
        advance();uint32_t mask=0,value=0,rotated=command;
        for(unsigned i=0;i<4;++i){rotated=(rotated<<4)|(rotated>>28);unsigned n=rotated&15;mask<<=8;value<<=8;if(!n)mask|=255;else{if(n==1)--n;value|=n<<(i==3?2:4);}}
        uint32_t at=overrideAddress?overrideAddress:0x20100A60+((command>>8)&255)*4,old;lastAddress=at;
        return m.read32(at,old)&&m.write32(at,(old&mask)|value);
    }
    bool pixel(){
        advance();uint32_t p;if(!m.read32(lastAddress,p))return false;
        std::array<uint32_t,4> v{(p>>24)<<22,((p>>16)&255)<<22,((p>>8)&255)<<22,p<<24},scale{};
        auto dot=[&](uint32_t base,uint32_t& out){uint32_t sum=0;for(unsigned k=0;k<4;++k){uint32_t a;if(!m.read32(base+k*4,a))return false;sum+=uint32_t((int32_t(a)>>16)*(int32_t(v[k])>>16));}out=sum<<2;return true;};
        uint32_t first,second,third;if(!dot(0x20100C00,first)||!dot(0x20100C10,second)||!dot(0x20100C20,third))return false;
        uint32_t alpha=v[3];v={first,second,third,alpha};
        for(unsigned k=0;k<4;++k)if(!m.read32(0x20100C30+k*4,scale[k]))return false;
        std::array<uint32_t,4> offset{0x04000000,0x20000000,0x20000000,lastAddress};
        for(unsigned k=0;k<4;++k)v[k]=((uint32_t((int32_t(v[k])>>16)*(int32_t(scale[k])>>16))<<2)&0xFFFF0000)+(offset[k]&0xFFFF0000);
        v[3]=alpha;
        auto pack=[](uint32_t a){return a>>30?255U:(a>>22)&255;};
        return m.write32(lastAddress,(pack(v[0])<<24)|(pack(v[1])<<16)|(pack(v[2])<<8)|(v[3]>>24));
    }
    bool run(unsigned budget=131072){
        if(!reload())return false;
        for(unsigned steps=0;steps<budget;++steps){
            uint32_t command,entry;if(!m.read32(cursor,command))return false;
            if(command==0x80000000)return flush();
            if(!m.read32(0x20100BA8+(command&255)*4,entry))return false;
            bool jumped=false;
            switch(entry){
            case 0x20300264:{if(!flush())return false;words=((command>>8)&255)<<(vector?2:0);advance();if(!m.read32(cursor,destination)||!preparation_copy(m,destination,0x20100A60,words))return false;advance();break;}
            case 0x203002B8:{oscillators=(command>>8)&255;advance();uint32_t address;if(!oscillators||!m.read32(cursor,address)||!m.write32(0x20100BA0,address)||!preparation_copy(m,address,0x20100B60,oscillators))return false;
                if(!fractional_oscillators())return false;
                if(step.legacy_boundary()){
                    // Persist exactly one authored increment; the scratch bank
                    // retains the fractional phase, including a boundary overshoot.
                    for(unsigned k=0;k<oscillators;++k){uint32_t v;if(!m.read32(address+k*4,v)||!m.write32(address+k*4,(v+((v&65535)<<16)&0xFFFF0000)|(v&65535)))return false;}
                }advance();break;}
            case 0x20300318:case 0x2030034A:case 0x2030035A:{uint32_t old=phase,v;if(!oscillator(command,v))return false;phase=entry==0x2030034A?old+v:entry==0x2030035A?command_mul(old,v,16):v;break;}
            case 0x2030036C:case 0x20300388:case 0x203003AA:case 0x203003CC:
                if(!channels(command,entry==0x2030036C?0:entry==0x20300388?1:entry==0x203003AA?2:3))return false;break;
            case 0x2030045E:advance();repeatWords=left;repeatCursor=cursor;overrideAddress=0x20100A60+(command>>24)*(vector?16:4);repeatLeft=((command>>16)&255)-(command>>24);repeatStep=uint32_t(int32_t(int8_t(command>>8)));break;
            case 0x2030048C:
                if(int32_t(--repeatLeft)>=0){for(unsigned k=0;k<oscillators;++k){uint32_t v;if(!m.read32(0x20100B60+k*4,v)||!m.write32(0x20100B60+k*4,((v+command_mul(repeatStep,v&65535,0)*65536)&0xFFFF0000)|(v&65535)))return false;}overrideAddress+=vector?16:4;left=repeatWords;cursor=repeatCursor;}
                else{uint32_t address;overrideAddress=0;if(!m.read32(0x20100BA0,address)||!preparation_copy(m,address,0x20100B60,oscillators)||(!fractional_oscillators(step.legacy_boundary()?(step.index+step.numerator)%step.divisions:step.index+step.numerator)))return false;advance();}break;
            case 0x203004E8:if(!rotate(command))return false;break;
            case 0x20300590:if(!nibbles(command))return false;break;
            case 0x203005EE:vector=!vector;advance();break;
            case 0x20300600:if(!pixel())return false;break;
            case 0x20300232:source+=cursor+4-0x20100A20;if(!reload())return false;jumped=true;break;
            case 0x20300242:{uint32_t target;advance();if(!m.read32(cursor,target)||!m.write32(0x20100BF4,source+cursor+4-0x20100A20))return false;source=target;if(!reload())return false;jumped=true;break;}
            case 0x2030025C:if(!m.read32(0x20100BF4,source)||!reload())return false;jumped=true;break;
            default:return false;
            }
            if(!jumped&&!left){source+=64;if(!reload())return false;}
        }
        return false;
    }
};
inline bool prepare_animation(CommandMemory& m,const PreparationInput& in){
    for(unsigned i=0;i<4;++i)if(!m.write32(0x201009F0+i*4,in.context[i]))return false;
    uint32_t resource,flags;if(!m.read32(0x20100020,resource)||!m.read32(0x2010000C,flags)||!preparation_copy(m,resource,0x201000F0,128)||!preparation_copy(m,resource+512,0x201002F0,256)||!m.write32(0x2010000C,(flags|3)&~4U))return false;
    PreparationAnimation animation{m};animation.source=in.context[0];animation.step=in.step;
    bool ok=animation.run();
    return ok&&m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
}
