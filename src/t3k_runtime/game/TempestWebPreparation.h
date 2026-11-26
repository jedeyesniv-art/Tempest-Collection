// Included inside tempest_native by TempestPreparation.h.
struct PreparationWeb{
    CommandMemory& m;PreparationShapeWriter& out;bool good=true;
    uint32_t oldColour=0,oldAlpha=0,nextColour=0,nextAlpha=0,phase=0,lastPoint=0,lastDetail=0,previousPoint=0,previousDetail=0;
    bool precise=false;std::array<double,2> lastPrecise{},previousPrecise{};
    static double wrap(double value,double period){return value-std::floor((value+period/2)/period)*period;}
    uint32_t read(uint32_t a){uint32_t v=0;good=m.read32(a,v)&&good;return v;}
    void put(uint32_t a,uint32_t v){good=m.write32(a,v)&&good;}
    void copy(uint32_t a,uint32_t b,unsigned n){good=preparation_copy(m,a,b,n)&&good;}
    void word(uint32_t v){good=out.word(v)&&good;}
    void colours(uint32_t a,uint32_t b){good=out.colour(a,b)&&good;}
    void select(uint32_t a,uint32_t b,uint32_t remaining){
        uint32_t count=read(0x201009F4)>>16,index=count-remaining,lane=read(0x20100910)>>16;
        bool close=(read(0x20100A18)&((read(0x20100A64)>>1)^1)&1)!=0;
        if((close&&!lane&&index==count)||lane==index-1||lane==index)a=b=read(0x201009E4);colours(a,b);
    }
    void advance_colour(){uint32_t n=read(0x20100928);put(0x20100928,n+1);uint32_t at=0x20100940+(n&15)*4,next=0x20100940+((n+1)&15)*4,width=read(0x201008DC)>>8;
        oldColour=read(at)|width;nextColour=read(next)|width;oldAlpha=(read(at+64)&0xFFFFFF00)|15;nextAlpha=(read(next+64)&0xFFFFFF00)|15;
    }
    void point(uint32_t x,uint32_t y,uint32_t detail,double fullX,double fullY){
        uint32_t address=out.destination+out.used*4;
        x+=int32_t(x)<0?0U-0x800:0x800;y+=int32_t(y)<0?0U-0x800:0x800;
        previousPoint=lastPoint;previousDetail=lastDetail;lastPoint=(((x>>12)&65535)<<16)|0x10000|((y>>12)&65535);lastDetail=detail|(phase<<16);word(lastPoint);
        if(precise){previousPrecise=lastPrecise;lastPrecise={wrap(fullY/4096,65536),wrap(fullX/4096,65536)};m.web_render_point(address,lastPoint,lastPrecise[0],lastPrecise[1]);}
    }
    void scaled(uint32_t x,uint32_t y,uint32_t scale,uint32_t detail,double fullX,double fullY){point(command_mul(scale,x,19),command_mul(scale,y,19),detail,precise?int32_t(scale)*fullX/524288:0,precise?int32_t(scale)*fullY/524288:0);}
    void precise_quad(uint32_t address){if(precise){m.web_render_point(address,lastPoint,lastPrecise[0],lastPrecise[1]);m.web_render_point(address+8,previousPoint,previousPrecise[0],previousPrecise[1]);}}
    void geometry(){
        uint32_t count=read(0x201009F4)>>16,angle=read(0x201009F4)&65535;
        if(!count||count>16){good=false;return;}
        put(0x20100A60,out.destination);advance_colour();select(nextColour,nextAlpha,count);
        put(0x201008EC,read(0x201008F4));auto rotation=preparation_sincos(read(0x201008F4));put(0x20100934,read(0x20100934)<<9);uint32_t step=0;
        good=preparation_reciprocal(m,count,0,16,step)&&good;put(0x20100904,step);
        uint32_t x=0,y=0,minX=0,minY=0,maxX=0,maxY=0;precise=m.precise_web_geometry();std::array<std::array<double,2>,17> full{};
        auto vertex=[&](uint32_t a,uint32_t b,unsigned i){if(int32_t(a)<int32_t(minX))minX=a;if(int32_t(a)>int32_t(maxX))maxX=a;if(int32_t(b)<int32_t(minY))minY=b;if(int32_t(b)>int32_t(maxY))maxY=b;if(precise)full[i]={double(int32_t(a)),double(int32_t(b))};put(0x20100730+i*4,(((a>>12)&65535)<<16)|((b>>12)&65535));};
        vertex(0,0,0);
        bool closed=(read(0x20100A18)&((read(0x20100A64)>>1)^1)&1)!=0;
        for(unsigned i=0;i<count&&good;++i){
            uint32_t extra=read(0x20100A68+i*4);put(0x201006F0+i*4,angle+extra+read(0x201008EC));auto cs=preparation_sincos(angle+extra);angle+=(read(0x201009F8+(i/2)*4)>>(i%2?0:16))&65535;
            x+=uint32_t(int32_t(cs[0])>>12);y+=uint32_t(int32_t(cs[1])>>12);
            uint32_t clock=read(0x20100934);put(0x20100934,clock+step);uint32_t factor=0x10000+command_mul(preparation_sincos(clock)[1],0x2000,30);
            uint32_t a=command_mul(x,factor,16),b=command_mul(y,factor,16);
            uint32_t rx=command_mul(rotation[0],a,30)+command_mul(0U-rotation[1],b,30),ry=command_mul(rotation[1],a,30)+command_mul(rotation[0],b,30);
            if(i+1==count&&closed){put(0x20100730+(i+1)*4,read(0x20100730));if(precise)full[i+1]=full[0];}else vertex(rx,ry,i+1);
        }
        uint32_t centreY=((maxY-minY)>>1)-maxY,centreX=((maxX-minX)>>1)-maxX,origin=read(0x201009F0);
        if(precise)for(unsigned i=0;i<=count;++i){full[i][0]=wrap(full[i][0]+int32_t(centreX)+int16_t(origin)*4096.0,268435456);full[i][1]=wrap(full[i][1]+int32_t(centreY)+int16_t(origin>>16)*4096.0,268435456);}
        centreY=(centreY<<4)+(origin&0xFFFF0000);centreX=(centreX<<4)+(origin<<16);
        for(unsigned i=0;i<=count;++i){uint32_t p=read(0x20100730+i*4);put(0x20100730+i*4,(((p&0xFFFF0000)+centreX)&0xFFFF0000)|(((p<<16)+centreY)>>16));}
        put(0x201007B0,0U-read(0x2010091C));put(0x201007B4,0U-read(0x20100918));put(0x201007B8,read(0x20100920));copy(0x201006F0,0x800DACC0,51);
        uint32_t cameraX=read(0x20100918),cameraY=read(0x2010091C),p=read(0x20100730);
        if(precise)for(unsigned i=0;i<=count;++i){full[i][0]=wrap(full[i][0]-int32_t(cameraY),4294967296.0);full[i][1]=wrap(full[i][1]-int32_t(cameraX),4294967296.0);}
        double fullX=full[0][0],fullY=full[0][1];
        x=uint32_t(int32_t(p)>>4)-cameraY;y=uint32_t(int32_t(p<<16)>>4)-cameraX;
        if(!closed){scaled(x,y,read(0x201008D4),0x201006F0,fullX,fullY);scaled(x,y,read(0x201008D0),0x201006F0,fullX,fullY);}
        for(unsigned i=0;i<count&&good;++i){
            uint32_t end=read(0x20100734+i*4),endX=uint32_t(int32_t(end)>>4)-cameraY,endY=uint32_t(int32_t(end<<16)>>4)-cameraX,remaining=count-i-1;
            uint32_t a=nextColour,b=oldColour,mask=read(0x20100A5C);if(mask&(1U<<i))a=b=read(0x201009EC);colours(a,b);
            double fullEndX=full[i+1][0],fullEndY=full[i+1][1];
            scaled(x,y,read(0x201008D0),0x1FFF,fullX,fullY);phase+=0x800;scaled(endX,endY,read(0x201008D0),0x1FFF,fullEndX,fullEndY);
            for(unsigned k=0;k<4;++k)put(0x201007B0+k*4,std::array<uint32_t,4>{lastPoint,lastDetail,previousPoint,previousDetail}[k]);copy(0x201007B0,0x800BACF0+i*32,4);precise_quad(0x800BACF0+i*32);
            select(nextAlpha,nextColour,remaining);scaled(endX,endY,read(0x201008D4),read(0x201008E4)+0x1FFF,fullEndX,fullEndY);
            colours(oldAlpha,nextAlpha);phase-=0x800;scaled(x,y,read(0x201008D4),read(0x201008E4)+0x1FFF,fullX,fullY);
            for(unsigned k=0;k<4;++k)put(0x201007B0+k*4,std::array<uint32_t,4>{lastPoint,lastDetail,previousPoint,previousDetail}[k]);copy(0x201007B0,0x800BAD00+i*32,4);precise_quad(0x800BAD00+i*32);word(0x80000002);
            uint32_t serial=read(0x20100908)+1;put(0x20100908,serial);uint32_t objectDepth=read(0x20100A1C+i*4);
            if(objectDepth){uint32_t denominator=objectDepth+uint32_t(int32_t(read(0x20100920))>>16);if(int32_t(denominator)<=10)denominator=10;uint32_t inverse=0;good=preparation_reciprocal(m,denominator+128,0,28,inverse)&&good;uint32_t scale=command_mul(0xC000,inverse,16);put(0x201008D8,scale);
                uint32_t colour=read(0x201009E0);if((read(0x201008F4)>>16)&(1U<<((serial-1)&15)))colour=read(0x201009E4);colours(colour|47,oldAlpha);
                uint32_t mx=endX+uint32_t(int32_t(x-endX)>>1),my=endY+uint32_t(int32_t(y-endY)>>1);scaled(mx,my,read(0x201008D4),0x3FFF,(fullX+fullEndX)/2,(fullY+fullEndY)/2);scaled(mx,my,scale,0x1000,(fullX+fullEndX)/2,(fullY+fullEndY)/2);colours(oldAlpha,nextAlpha);word(0x80000002);
            }
            word(0x80000002);phase+=0x800;advance_colour();x=endX;y=endY;fullX=fullEndX;fullY=fullEndY;
        }
    }
    bool run(const PreparationInput& in){
        for(unsigned i=0;i<4;++i)put(0x20100930+i*4,in.context[i]);put(0x201008CC,0);copy(0x800DACB0,0x201007C0,1);copy(0x8010DC00,0x20100940,32);copy(0x800B6A8C,0x201009E4,1);copy(0x201007C0,0x800B7D30,1);
        copy(read(0x20100020)+0x900,0x201009F0,1);copy(read(0x201009F0),0x201009F0,28);copy(0x800DC254,0x201008F4,1);copy(0x8010F170,0x20100A64,17);copy(0x800DC100,0x20100910,5);
        good=preparation_resources(m,true)&&good;
        for(unsigned i=0;i<2;++i){uint32_t depth=(i?0x5000000:0x1000000)+read(0x20100920);if(int32_t(depth)<=0xA0000)depth=0xA0000;put(0x201008E4+i*4,depth);uint32_t scale=0;good=preparation_projection_scale(m,depth,scale)&&good;put(0x201008D0+i*4,scale);uint32_t width=command_mul(scale,0x2FFF,19);if(int32_t(width)>0x2FFF)width=0x2FFF;put(i?0x201008E0:0x201008DC,width);}
        uint32_t distance=uint32_t(std::abs(int64_t(int32_t(read(0x201008E8)-read(0x201008E4)))));put(0x201008E4,command_mul(distance>>16,0x40000,18)<<3);put(0x201008C0,0x82747820);
        out.destination=read(0x201007C0);geometry();return good&&out.finish()&&m.write32(0x20100030,0)&&m.write32(0x8003DE50+in.worker*4,0);
    }
};
inline bool prepare_web(CommandMemory& m,const PreparationInput& in){PreparationShapeWriter out{m,0x201007C0,0};PreparationWeb web{m,out};return web.run(in);}
