// Included inside tempest_native by TempestPreparation.h.
struct PreparationShapeWriter{
    CommandMemory& m;uint32_t base,destination,used=0;
    bool flush(unsigned count){if(!preparation_copy(m,base,destination,count))return false;destination+=count*4;used=0;return true;}
    bool word(uint32_t value){if(!m.write32(base+used*4,value))return false;return ++used!=64||flush(64);}
    bool pair(uint32_t a,uint32_t b){return word(a)&&word(b);}
    bool point(uint32_t x,uint32_t y){return word(((x>>16|1)<<16)|(y>>16));}
    bool colour(uint32_t a,uint32_t b){return word(0x80000004)&&pair(b,a);}
    bool finish(){
        if(!word(0x80000001)||(used&&!flush(used+1)))return false;
        return m.write32(base,destination)&&m.write32(0x800DACB0,destination);
    }
};
inline bool preparation_resources(CommandMemory& m,bool cached=false){
    uint32_t resource,flags;if(!m.read32(0x20100020,resource)||!m.read32(0x2010000C,flags))return false;
    if((!cached||!(flags&1))&&!preparation_copy(m,resource,0x201000F0,128))return false;
    if((!cached||!(flags&2))&&!preparation_copy(m,resource+512,0x201002F0,256))return false;
    return m.write32(0x2010000C,(flags|3)&~4U);
}
inline uint32_t preparation_rng(uint32_t v){return (v>>1)^((v&1)?0xA3000000:0);}
inline bool prepare_shape_boxes(CommandMemory& m,PreparationShapeWriter& out,uint32_t context,uint32_t position,uint32_t colour){
    unsigned count=(context>>8)&255;if(!count)return true;
    if(!out.pair(0x80000006,colour))return false;uint32_t angle=0;
    for(unsigned k=0;k<count;++k){
        uint32_t a=uint32_t(int32_t(int16_t(position))),b=uint32_t(int32_t(position)>>16);
        for(unsigned i=0;i<5;++i){
            uint32_t x=uint32_t(int32_t(preparation_sincos(a)[0])>>14),y=uint32_t(int32_t(preparation_sincos(b)[0])>>14);
            a-=0x800;b-=0x666;x=command_mul(x,0x400000,16);y=command_mul(y,0x400000,16);auto cs=preparation_sincos(angle);
            uint32_t px=command_mul(y,cs[0],30)+command_mul(x,0U-cs[1],30),py=command_mul(y,cs[1],30)+command_mul(x,cs[0],30);
            if(!i){if(!out.word(0x80000407))return false;
                for(auto delta:std::array<std::array<uint32_t,2>,4>{{{0xFFF00000,0xFFF00000},{0x100000,0xFFF00000},{0x100000,0x100000},{0xFFF00000,0x100000}}})
                    if(!out.point(px,py)||!out.point(px+delta[0],py+delta[1])||!out.word(0x80000002))return false;
            }
            if(!out.word(0x80000007|(((i<<4)|(i+1))<<8))||!out.point(px,py))return false;
        }
        if(!out.word(0x80000002))return false;angle+=0x5555;
    }
    return true;
}
struct PreparationLightning{
    CommandMemory& m;PreparationShapeWriter& out;uint32_t seedA=0,seedB=0;
    bool seeds(){
        if(!m.read32(0x2010080C,seedA)||!m.read32(0x20100810,seedB))return false;
        seedA=preparation_rng(seedA);seedB=preparation_rng(seedB);
        return m.write32(0x2010080C,seedA)&&m.write32(0x20100810,seedB)&&preparation_copy(m,0x20100808,0x8010CFC0,4);
    }
    bool segment(uint32_t x,uint32_t y,uint32_t endX,uint32_t endY,uint32_t depth){
        uint32_t dx=endX-x,dy=endY-y,extent=uint32_t(std::max(std::abs(int64_t(int32_t(dx))),std::abs(int64_t(int32_t(dy)))));
        if(!seeds())return false;
        if(!depth)return out.point(x,y)&&out.point(endX,endY)&&out.word(0x80000002);
        struct Node{uint32_t x,y,endX,endY,depth;};std::array<Node,128> stack{};unsigned used=0;
        for(unsigned budget=0;budget<65536;++budget){
            uint32_t midX=x+uint32_t(int32_t(endX-x)>>1),midY=y+uint32_t(int32_t(endY-y)>>1);
            seedA=preparation_rng(seedA);seedB=preparation_rng(seedB);
            midX+=command_mul(depth,command_mul(extent,uint32_t(int32_t(seedA)>>16),16),4);
            midY+=command_mul(depth,command_mul(extent,uint32_t(int32_t(seedB)>>16),16),4);
            --depth;
            if(int32_t(depth)<=0){
                if(!out.point(x,y)||!out.point(midX,midY)||!out.point(endX,endY)||!out.word(0x80000002))return false;
                if(!used)return true;auto n=stack[--used];x=n.x;y=n.y;endX=n.endX;endY=n.endY;depth=n.depth;
            }else{
                if(used+2>stack.size())return false;
                if((seedB&255)<=depth*70){uint32_t a=midX-x,b=midY-y;if(seedB&1)a=uint32_t(int32_t(a)>>1);else b=uint32_t(int32_t(b)>>1);stack[used++]={midX,midY,midX+a,midY+b,depth};}
                stack[used++]={midX,midY,endX,endY,depth};endX=midX;endY=midY;
            }
        }
        return false;
    }
    bool bolt(uint32_t x,uint32_t y,uint32_t endX,uint32_t endY){
        uint32_t colour;if(!preparation_copy(m,0x800B6A88,0x20100844,1)||!m.read32(0x20100844,colour)||!out.colour(colour,colour))return false;
        uint32_t extent=uint32_t(std::max(std::abs(int64_t(int32_t(endX-x))),std::abs(int64_t(int32_t(endY-y)))));
        if(!segment(x,y,endX,endY,4)||!m.read32(0x20100844,colour)||!out.colour(colour&0xFFFFFF00,colour))return false;
        for(unsigned i=0;i<8;++i){
            uint32_t a=endX+command_mul(extent,uint32_t(int32_t(int16_t(seedA))),16),b=endY+command_mul(extent,uint32_t(int32_t(int16_t(seedB))),16);
            if(!segment(endX,endY,a,b,0))return false;
        }
        return true;
    }
    bool run(uint32_t pointPointer,uint32_t targetPointer){
        if(!preparation_copy(m,0x8010CFC0,0x20100808,4))return false;
        for(uint32_t at=0x80110B14,budget=0;budget<4096;++budget,at+=8){
            if(!preparation_copy(m,at,0x20100870,2))return false;uint32_t a,b;
            if(!m.read32(0x20100870,a)||!m.read32(0x20100874,b))return false;
            if(a==0x0F10551E)break;
            uint32_t seed;if(!m.read32(0x2010080C,seed))return false;uint32_t other;if(!m.read32(0x20100810,other))return false;
            if(!m.write32(0x2010080C,preparation_rng(seed))||!m.write32(0x20100810,preparation_rng(other))||!preparation_copy(m,0x20100808,0x8010CFC0,4)||!bolt(a,a<<16,b,b<<16))return false;
            if(budget==4095)return false;
        }
        if(!m.write32(0x20100870,0x80110B14)||!m.write32(0x20100874,0x0F10551E)||!preparation_copy(m,0x20100870,0x80110B10,2))return false;
        if(!pointPointer)return true;
        if(!preparation_copy(m,pointPointer,0x20100870,1))return false;uint32_t a;if(!m.read32(0x20100870,a))return false;
        uint32_t x=(a<<17)-0xF00000,y=(uint32_t(int32_t(a)>>16)<<17)-0x1680000;
        if(!targetPointer)return true;
        if(targetPointer==0x80000000){if(!seeds())return false;return true;}
        uint32_t b;if(!preparation_copy(m,targetPointer,0x20100874,1)||!m.read32(0x20100874,b)||!m.write32(0x20100808,b))return false;
        return bolt(x,y,(b<<17)-0xF00000,(uint32_t(int32_t(b)>>16)<<17)-0x1680000);
    }
};
inline bool prepare_shape_text(CommandMemory& m,PreparationShapeWriter& out,uint32_t object){
    if(!preparation_copy(m,object+32,0x20100860,4)||!preparation_copy(m,0x80106590,0x20100870,2))return false;
    uint32_t spacing,lineHeight,flags,detail,text;
    if(!m.read32(0x20100870,spacing)||!m.read32(0x20100874,lineHeight)||!m.read32(0x20100864,flags)||!m.read32(0x20100868,detail)||!m.read32(0x2010086C,text)||!preparation_copy(m,text,0x20100AF0,200))return false;
    lineHeight=command_mul(0xC000,lineHeight,0);uint32_t dynamicStep;
    if(!m.write32(0x2010083C,detail)||!m.read32(0x20100840,dynamicStep))return false;
    unsigned lineCount=0,dynamicCount=0;bool dynamic=false;
    for(unsigned i=0;i<800;++i){uint8_t c;if(!m.read8(0x20100AF0+i,c))return false;if(!c)break;
        if(c=='_'){dynamic=!dynamic;continue;}
        if(dynamic&&c>=32&&c!='/'&&c!=';'&&c!='\\'&&(c<=0x5A||c>0x60))++dynamicCount;
        else if(dynamic&&c=='^')++i;
        if(c=='/'||c=='\\')++lineCount;if(i==799)return false;
    }
    uint32_t offset=0U-command_mul(dynamicCount,dynamicStep,0),magnitude=uint32_t(std::abs(int64_t(int32_t(offset))));
    uint32_t dynamicDetail=command_mul((magnitude+0x10000)>>16,detail,0);if(int32_t(dynamicStep)>=0)dynamicDetail+=offset;
    if(!m.write32(0x20100838,dynamicDetail))return false;
    uint32_t y=0U-uint32_t(int32_t(command_mul(lineHeight,lineCount+1,0))>>1),at=0x20100AF0,bracket=0;
    for(unsigned lines=0;lines<800;++lines){
        flags&=~1U;uint32_t width=0,count=0,style=0,previousStyle=0x80000000;
        for(unsigned chars=0;chars<800;++chars){
            uint8_t c;if(!m.read8(at++,c))return false;if(!c)break;if(c=='/'){flags|=1;break;}
            if(c=='^'){
                uint8_t next;if(!m.read8(at,next))return false;if(next=='@'){++at;style|=0x20000000;continue;}
                if(!(flags&2)){uint32_t colour;if(!m.read32(0x20100860,colour)||!out.pair(0x80000006,colour))return false;flags|=2;}
                ++at;if(flags&8)continue;uint32_t index=uint32_t(next)-'A';style|=0x80000000|((index<<8)|(index<<12))<<8;previousStyle=style&0x9FFFFFFF;continue;
            }
            if(c=='_'){style|=0x40000000;continue;}
            if(c=='['){if((flags>>16)==bracket++){flags|=0x88;style=(style&0x60000000)|0x80FF0000;}continue;}
            if(c==']'){style=previousStyle;flags&=~8U;continue;}
            uint32_t code=c;if(code>=0x60)code-=0x20;code-=0x20;
            uint32_t local=0x20100870+count*8,pointer,descriptor;
            if(count>=800||!preparation_copy(m,0x80106598+code*4,local,1)||!m.read32(local,pointer)||!preparation_copy(m,pointer,local+4,1)||!m.read32(local+4,descriptor))return false;
            if(!m.write32(local+4,descriptor|style))return false;style=0;++count;
            unsigned advance=descriptor&255;if(advance&&advance!=1)width+=spacing+advance;
            if(chars==799)return false;
        }
        uint32_t x=command_mul(0U-(width<<15),0x6000,16);if(flags&64)x=0xFC000000;
        if(flags&128){
            flags&=~128U;if(!out.word(0x80000002)||!out.word(0x8000FF07))return false;
            uint32_t a=0U-(x+command_mul(0x6000,uint32_t(-160),0)),b=y+command_mul(0xC000,160,0),h=command_mul(0xC000,160,0),w=command_mul(0x6000,width+256,0);
            if(!out.point(a,b)||!out.point(a-w,b)||!out.point(a-w,b-h*2)||!out.point(a,b-h*2)||!out.point(a,b)||!out.word(0x80000002))return false;
        }
        if(!count)return true;
        for(unsigned glyph=0;glyph<count;++glyph){
            uint32_t pointer,descriptor;if(!m.read32(0x20100870+glyph*8,pointer)||!m.read32(0x20100874+glyph*8,descriptor))return false;
            uint32_t effect=descriptor>>16;
            if(effect&0x2000){flags^=32;effect&=~0x2000U;}if(effect&0x4000){flags^=16;effect&=~0x4000U;}
            if(effect&0x8000)if(!out.word(0x80000007|((effect&~0x8000U)<<8)))return false;
            if(!(descriptor&255))flags^=64;
            else{
                uint32_t value;if(!m.read32(flags&16?0x20100838:0x2010083C,value))return false;
                if(flags&16){value+=dynamicStep;if(!m.write32(0x20100838,value))return false;}
                // The branch at 203005A4 always selects the 80000009 form.
                if(!out.pair(0x80000009,value))return false;
            }
            unsigned vertices=(descriptor>>8)&255;
            if(vertices){
                if(!preparation_copy(m,pointer+4,0x201009B0,vertices))return false;
                unsigned iterations=vertices>1?vertices-1:1;
                for(unsigned i=0;i<iterations;++i){uint32_t p;if(!m.read32(0x201009B0+i*4,p))return false;
                    if((p>>16)==0x8000){if(!out.word(p))return false;}
                    else if(!out.point(0U-(x+command_mul(0x6000,p<<16,16)),y+command_mul(0xC000,p&0xFFFF0000,16)))return false;
                }
                if(!out.word(0x80000002))return false;
            }
            x+=command_mul(0x6000,spacing+(descriptor&255),0);
        }
        if(!(flags&1))return true;y+=lineHeight;
    }
    return false;
}
inline bool prepare_shapes(CommandMemory& m,const PreparationInput& in){
    for(unsigned i=0;i<4;++i)if(!m.write32(0x20100850+i*4,in.context[i]))return false;
    if(!m.write32(0x201007FC,0)||!preparation_copy(m,0x800DACB0,0x201006F0,1)||!preparation_copy(m,0x201006F0,in.context[0]+28,1)||!preparation_resources(m))return false;
    uint32_t destination,entry;if(!m.read32(0x201006F0,destination)||!m.read32(0x20100EC0+(in.context[1]&255)*4,entry))return false;
    PreparationShapeWriter out{m,0x201006F0,destination};bool ok=false;
    switch(entry){
    case 0x20300BA6:ok=true;break;
    case 0x20300804:{PreparationLightning lightning{m,out};ok=lightning.run(in.context[2],in.context[3]);break;}
    case 0x20300200:ok=prepare_shape_text(m,out,in.context[0]);break;
    case 0x2030066C:ok=prepare_shape_boxes(m,out,in.context[1],in.context[2],in.context[3]);break;
    default:return false;
    }
    return ok&&out.finish()&&m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
}
