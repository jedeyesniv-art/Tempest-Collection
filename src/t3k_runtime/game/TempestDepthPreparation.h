// Included inside tempest_native by TempestPreparation.h.
struct PreparationDepth{
    CommandMemory& m;PreparationShapeWriter& out;
    bool good=true;SimulationStep simulation{};
    uint32_t read(uint32_t address){uint32_t value=0;good=m.read32(address,value)&&good;return value;}
    void store(uint32_t address,uint32_t value){good=m.write32(address,value)&&good;}
    void copy(uint32_t from,uint32_t to,unsigned count){good=preparation_copy(m,from,to,count)&&good;}
    static uint32_t height(uint32_t phase,uint32_t frequency,bool positive){return !positive?0U-(phase<<6):command_mul(preparation_sincos(command_mul(phase,frequency,16)+0x4000)[0],0x180,14);}
    void circle(uint32_t x,uint32_t y,uint32_t depth,unsigned count,uint32_t radius,uint32_t angle){
        uint32_t step=0;good=preparation_reciprocal(m,count,0,16,step)&&good;
        if(!count||count>1024){good=false;return;}
        for(unsigned i=0;i<count&&good;++i){auto cs=preparation_sincos(angle);angle+=step;good=line(x+command_mul(radius,cs[0],14),y+command_mul(radius,cs[1],14),depth,0,0,0x100000)&&good;}
    }
    void bonus_slot(uint32_t phase,uint32_t y,uint32_t depth,uint32_t rowSpacing,uint32_t frequency,unsigned row){
        uint32_t index=int32_t(phase)>0?(phase>>1)&127:0,address=0x201009C0+index*4,word=read(address);store(0x2010082C,0);store(0x20100874,address);store(0x20100834,word);
        int32_t velocity=int32_t((word>>8)&15)-8;
        uint32_t visiblePosition=word&0xFF000000,collisionWord=word;
        if(velocity){uint32_t previous=uint32_t(int32_t(word)>>24),next=(previous+uint32_t(velocity))<<24;
            if((previous>>31)!=(next>>31)&&((int32_t(next)<0&&velocity>=0)||(int32_t(next)>=0&&velocity<0))){velocity=-velocity;next+=uint32_t(velocity)<<24;}
            uint32_t endWord=(word&0x00FFF0FF)|next|((uint32_t(velocity+8)&15)<<8);
            int64_t displacement=int64_t(int32_t(next))-int32_t(word&0xFF000000);
            visiblePosition=uint32_t(int64_t(int32_t(word&0xFF000000))+displacement*(simulation.index+simulation.numerator)/simulation.divisions);
            // The original tests shots against the position read before this
            // sample's movement, then draws the advanced position. Preserve
            // that ordering, including the packed coordinate's low bits.
            collisionWord=uint32_t(int64_t(int32_t(word&0xFF000000))+displacement*simulation.index/simulation.divisions)+(word&0x00FFFFFF);
            if(simulation.legacy_boundary()){word=endWord;store(address,word);}
        }
        if(!(word&255))return;
        for(unsigned i=0;i<8;++i){uint32_t bulletAddress=0x201009A0+i*4,bullet=read(bulletAddress);if(!(bullet&255))continue;
            uint32_t target=read(0x20100820)+(rowSpacing<<3)-command_mul(uint32_t(int32_t(rowSpacing)>>2),bullet&255,0);
            if(std::abs(int64_t(int32_t(target-depth)))>0x80000)continue;
            uint32_t observed=read(0x20100834);if(std::abs(int64_t(int32_t(collisionWord)>>23)-int64_t(int32_t(bullet)>>16))>int64_t((observed>>16)&255))continue;
            if(word&0x1000){unsigned width=(word>>16)&255;if(width<=128)width+=5;word=(word&0xFF00FFFF)|(width<<16);store(address,word);}
            if(word&0x2000){word=(word&0xFFFFF0FF)|0x800;store(address,word);}
            store(0x20100834,word);store(0x201008B0,read(0x201008B0)|0x80000000);store(bulletAddress,bullet|0x100);break;
        }
        uint32_t shade=63-(((depth>>16)-128)>>2);store(0x20100824,read(0x20100924)|shade);
        uint32_t x=read(0x20100828)+uint32_t(int32_t(visiblePosition)>>7)+((word&0x00FFFFFF)>>7);
        if(row==8&&(read(0x201008B4)&65535)<read(0x201008AC)){store(0x2010082C,x|1);store(0x20100830,read(0x20100834));}
        uint32_t h=int32_t(phase)<0?0U-(phase<<22):command_mul(preparation_sincos(command_mul(phase,frequency,0)+0x4000)[0],0x180,14);
        circle(x,y+h,depth,32,(read(0x20100834)>>16)&255,(read(0x201008B4)&0xFFFFF)>>4);
    }
    bool bonus(const PreparationInput& in){
        simulation=in.step;
        copy(0x8010F550,0x201009A0,8);copy(in.context[2],0x201008A0,16);store(0x201008B0,0);
        uint32_t old=read(0x201008D8);for(unsigned i=0;i<(simulation.legacy_boundary()?7u:0u);++i)store(0x201008DC-i*4,read(0x201008D8-i*4));
        uint32_t startX=0U-(old&0xFFFF0000);store(0x20100828,startX);store(0x2010082C,0);
        copy(0x8010F570,0x201009C0,200);copy(0x8010D2E4,0x20100920,16);store(0x20100900,0x4000-read(0x201008A8));
        uint32_t reciprocal=0;good=preparation_reciprocal(m,8,0,30,reciprocal)&&good;
        uint32_t columnSpacing=command_mul(0x300,reciprocal,14),rowSpacing=0U-command_mul(0x100,reciprocal,14),baseDepth=0x500000-rowSpacing*8;store(0x20100820,baseDepth);
        startX-=command_mul(columnSpacing,8,1);
        uint32_t stage=read(0x201008B4),frequency=read(0x201008B8),phase=uint32_t(int32_t(stage)>>16),depth=baseDepth+command_mul(rowSpacing,stage&65535,16),y;
        uint32_t shifted=stage-0xA0000;
        y=int32_t(shifted)<0?shifted<<6:0U-command_mul(preparation_sincos(command_mul(frequency,shifted,16)+0x4000)[0],0x180,14);
        store(0x201008A4,uint32_t(int32_t(y)>>16));
        for(unsigned row=0;row<9&&good;++row){
            store(0x20100824,0xF0808000|(63-(((depth>>16)-128)>>2)));
            for(unsigned col=0;col<9&&good;++col)good=line(startX+col*columnSpacing,y,depth,0,0,0x100000)&&good;
            bool even=!(phase&1);--phase;if(even)bonus_slot(phase,y,depth,rowSpacing,frequency,row);
            depth+=rowSpacing;
        }
        depth=baseDepth+rowSpacing*8;uint32_t x=read(0x201008BC)+read(0x20100828);phase=stage-0x90000;
        for(unsigned i=0;i<8&&good;++i){uint32_t address=0x201009A0+i*4,bullet=read(address),age=bullet&127;if(!bullet)continue;uint32_t next=bullet+unsigned(simulation.legacy_boundary());store(address,next);if((next&127)>=65){store(address,0);continue;}
            uint32_t fractionalAge=(age<<16)-(age?65536-uint32_t(uint64_t(simulation.index+simulation.numerator)*65536/simulation.divisions):0);
            phase=stage-0x90000+(fractionalAge>>2);uint32_t h=height(phase,frequency,int32_t(phase)>0),detail=uint32_t(std::abs(int32_t((phase>>6)&255)-128))<<24|0x50000;
            if(bullet&0x100){store(address,0);detail=0xF0100000;}
            good=line(x+next,y+h,depth-command_mul(fractionalAge,uint32_t(int32_t(rowSpacing)>>2),16),0,0,0xFFF00000,detail)&&good;
        }
        depth=baseDepth+rowSpacing*8;uint32_t player=read(0x201008BC),angle=player<<10; x=player<<16;phase=stage-0x90000;
        uint32_t candidate=read(0x2010082C);
        if(candidate){uint32_t selected=read(0x20100830),difference=uint32_t(std::abs(int64_t(int32_t(candidate-(x+read(0x20100828))))))>>16,width=(selected>>16)&255;
            if(difference>width)store(0x201008B0,0xFE);
            else if(simulation.legacy_boundary()){store(0x201008B0,(selected&0x00FF00FF)|((width-difference)<<24)|((read(0x201008B0)>>16)&255));store(0x2010082C,0);}
        }
        store(0x201008C0,(player<<16)|(angle&65535));
        for(unsigned ring=0;ring<6&&good;++ring){
            uint32_t position=read(0x201008C0+ring*4),count=read(0x20100854+ring*4);store(0x20100824,read(0x20100928+ring*4)|63);
            circle((position&0xFFFF0000)+read(0x20100828),y+height(phase,frequency,int32_t(phase)>0),depth,count,4+ring*4,position);
            depth+=uint32_t(int32_t(rowSpacing)>>2);phase-=0x4000;
        }
        copy(0x201008A0,in.context[2],16);copy(0x201009A0,0x8010F550,8);copy(0x201009C0,0x8010F570,200);return good;
    }
    bool point(uint32_t x,uint32_t y,uint32_t depth){
        uint32_t angle,scale;if(!m.read32(0x20100900,angle)||!preparation_reciprocal(m,depth,16,30,scale))return false;
        auto cs=preparation_sincos(angle);
        uint32_t rx=command_mul(cs[0],x,30)+command_mul(cs[1],y,30),ry=command_mul(0U-cs[1],x,30)+command_mul(cs[0],y,30);
        return out.point(command_mul(rx,scale,24),command_mul(ry,scale,24));
    }
    bool line(uint32_t x,uint32_t y,uint32_t depth,uint32_t dx,uint32_t dy,uint32_t dz,uint32_t detail=0){
        uint32_t colour;if(!m.write32(0x20100838,detail)||!m.read32(0x20100824,colour)||!out.colour(colour,0x306EF000)||!point(x+dx,y+dy,depth+dz)||!point(x,y,depth)||!out.word(0x80000002))return false;
        unsigned count=(detail>>16)&255;if(!count)return true;
        uint32_t angle,scale,step;if(!m.read32(0x20100900,angle)||!preparation_reciprocal(m,depth,16,30,scale)||!preparation_reciprocal(m,count,0,16,step))return false;
        auto cs=preparation_sincos(angle);
        uint32_t px=command_mul(command_mul(cs[0],x,30)+command_mul(cs[1],y,30),scale,24),py=command_mul(command_mul(0U-cs[1],x,30)+command_mul(cs[0],y,30),scale,24);
        uint32_t radius=command_mul(scale,detail>>24,8),phase=detail;
        for(unsigned i=0;i<count;++i){uint32_t c;if(!m.read32(0x20100824,c)||!out.colour(0x306EF000,c)||!out.point(px,py))return false;auto unit=preparation_sincos(phase);phase+=step;
            if(!out.point(px+command_mul(radius,unit[0],30),py+command_mul(radius,unit[1],30))||!out.word(0x80000002))return false;
        }
        return true;
    }
    bool mirrored(uint32_t x,uint32_t y,uint32_t depth,uint32_t dz,uint32_t detail){
        uint32_t flags,offset;if(!m.read32(0x201008A4,flags)||!m.read32(0x201008B0,offset))return false;
        x+=(offset>>24)<<16;y+=((offset>>16)&255)<<16;
        for(unsigned i=0;i<4;++i){uint32_t d=(detail&0xFFFF0000)|((uint32_t(int32_t(x)>>4))&65535);if((flags&(1U<<i))&&!line(x,y,depth,0,0,dz,d))return false;if(i%2==0)x=0U-x;else y=0U-y;}
        return true;
    }
    bool particles(const PreparationInput& in){
        if(!preparation_copy(m,0x8010D2E4,0x20100920,16)||!preparation_copy(m,in.context[0]+40,0x201008A0,5)||!m.write32(0x20100900,in.context[3]))return false;
        uint32_t seedA=0x36F49EDC,seedB=0xA845FD3D,count=in.context[1]>>17;
        for(unsigned i=0;i<(count?count:1);++i){
            if(i>32768)return false;seedA=preparation_rng(seedA);seedB=preparation_rng(seedB);uint32_t config,size,shade;
            if(!m.read32(0x201008A4,config)||!m.read32(0x201008A0,size)||!m.read32(0x20100924,shade))return false;
            uint32_t x=seedA<<8,y=seedB<<8;if(config>>24)x=(config>>24)<<16;if((config>>16)&255)y=((config>>16)&255)<<16;
            uint32_t z=((seedA^seedB)+in.context[2])&0xFFFFFF,detail=size&0xFFFF0000;
            if(!m.write32(0x20100824,shade|(63-(z>>18)))||!mirrored((uint32_t(int32_t(x)>>6))|0x400000,(uint32_t(int32_t(y)>>6))|0x400000,z+0xA0000,(size&65535)<<8,detail))return false;
        }
        return true;
    }
    bool rings(uint32_t pointer){
        if(!preparation_copy(m,pointer,0x201008A0,16)||!preparation_copy(m,0x8010D2E4,0x20100920,16))return false;uint32_t shade,phase,step;
        if(!m.read32(0x20100924,shade)||!m.write32(0x20100824,shade|31)||!m.read32(0x201008B4,phase)||!preparation_reciprocal(m,11,0,16,step))return false;
        phase=(phase&0xFFFFF)>>4;
        for(unsigned i=0;i<11;++i){auto cs=preparation_sincos(phase);phase+=step;if(!line(command_mul(96,cs[0],14),command_mul(96,cs[1],14),0x400000,0,0,0xFFE00000))return false;}return true;
    }
    bool tunnel(const PreparationInput& in){
        if(!preparation_copy(m,0x8010D2E4,0x20100920,16)||!preparation_copy(m,in.context[0]+40,0x201008A0,5)||!m.write32(0x20100900,in.context[3]))return false;
        uint32_t config;if(!m.read32(0x201008A4,config))return false;unsigned bits=(config>>8)&255;if(bits>=15)return false;
        uint32_t mask=((1U<<(bits+1))-1)<<16|65535,depth=0xFF0000+(in.context[2]&mask),layer=in.context[2]>>(bits+17);
        uint32_t fixedX=(config>>24)<<16,fixedY=((config>>16)&255)<<16;
        for(unsigned i=0;i<4096;++i){
            auto cs=preparation_sincos(command_mul(layer,in.context[1]>>16,0));++layer;uint32_t offsets,shade,size;
            if(!m.read32(0x201008B0,offsets)||!m.read32(0x20100924,shade)||!m.read32(0x201008A0,size)||!m.write32(0x20100824,shade|(63-(depth>>18))))return false;
            uint32_t x=fixedX?fixedX:command_mul(cs[0],(offsets>>8)&255,14),y=fixedY?fixedY:command_mul(cs[1],offsets&255,14);
            if(!mirrored(x,y,depth,(size&65535)<<8,size&0xFFFF0000))return false;
            depth-=mask+1;if(int32_t(depth)<=0x100000)return true;
        }
        return false;
    }
};
inline bool prepare_depth(CommandMemory& m,const PreparationInput& in){
    for(unsigned i=0;i<4;++i)if(!m.write32(0x201008E0+i*4,in.context[i]))return false;
    if(!m.write32(0x201007FC,0)||!preparation_copy(m,0x800DACB0,0x201006F0,1)||!preparation_copy(m,0x201006F0,in.context[0]+28,1)||!preparation_resources(m))return false;
    uint32_t destination,entry;if(!m.read32(0x201006F0,destination)||!m.read32(0x20100CF0+(in.context[1]&255)*4,entry))return false;
    // Packed obstacle/bullet state advances at authored boundaries while
    // current geometry and spatial checks are evaluated at each fine sample.

    PreparationShapeWriter out{m,0x201006F0,destination};PreparationDepth depth{m,out};bool ok=false;
    switch(entry){case 0x20300DDA:ok=true;break;case 0x203004E6:ok=depth.bonus(in);break;case 0x20300452:ok=depth.rings(in.context[2]);break;case 0x20300200:ok=depth.particles(in);break;case 0x203002D2:ok=depth.tunnel(in);break;default:return false;}
    return ok&&out.finish()&&m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
}
