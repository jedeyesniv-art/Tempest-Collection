#pragma once
#include <cstdint>
namespace tempest_native {
// Interpreter for the GAME'S object command language, not Nuon instructions.
// Memory is an explicit data interface; a standalone corpus can supply it.
struct CommandMemory {
    virtual bool read8(uint32_t address,uint8_t& value)=0;
    virtual bool read32(uint32_t address,uint32_t& value)=0;
    virtual bool write32(uint32_t address,uint32_t value)=0;
    virtual ~CommandMemory()=default;
};
struct CommandInput {uint32_t object,destination,cursor,values,parameters,accumulator;};
struct CommandOutput {uint32_t cursor,accumulator,armed,operand,operation,addressBase;};
// The original caller subsequently forwards these two scratch words to event
// payloads. Keep their data contract without retaining an instruction machine.
struct CommandScratch {uint32_t second=0,third=0;};
inline uint32_t command_mul(uint32_t a,uint32_t b,unsigned shift){return uint32_t((int64_t(int32_t(a))*int32_t(b))>>shift);}
inline bool evaluate_attribute(CommandMemory& m,uint32_t object,uint32_t address,uint32_t& result){
    uint32_t record[4],picture,birth;
    for(unsigned k=0;k<4;++k)if(!m.read32(address+k*4,record[k]))return false;
    if(!m.read32(0x80110AA0,picture)||!m.read32(object+0x3c,birth))return false;
    uint32_t elapsed=(record[0]-(picture+birth))&65535U;
    uint32_t phase=(elapsed*record[2]+(record[1]<<8))<<8;
    phase=uint32_t(int32_t(phase)>>16)+1;
    unsigned mode=record[3]&255;
    if(mode==0)result=phase<<16;
    else if(mode<=3){uint32_t a=int32_t(phase)<0?0U-phase:phase;result=((a<<1)-0x8000U)<<16;}
    else return false;
    return true;
}
inline bool execute_commands(CommandMemory& m,const CommandInput& in,CommandOutput& out,unsigned budget=4096,CommandScratch* scratch=nullptr){
    uint32_t cursor=in.cursor,acc=in.accumulator,armed=0,operand=0,operation=0,addressBase=0;
    unsigned used=0;
    auto byte=[&](uint32_t a,uint8_t& b){return ++used<=budget&&m.read8(a,b);};
    auto take=[&](uint8_t& b){return byte(cursor++,b);};
    auto global=[&](uint32_t& a){return m.read32(0x80113164,a);};
    auto apply=[&](){
        if(operation=='%'){if(!m.write32(operand,acc))return false;}
        else if(operation=='#'){operand<<=2;uint32_t address=acc+operand;if(scratch)scratch->second=address;if(!m.read32(address,acc))return false;}
        else if(operation=='+')acc+=operand;
        else if(operation=='/'){acc=command_mul(operand,acc,16);uint32_t a=int32_t(acc)<0?0U-acc:acc;if(int32_t(a)<255)acc=0;}
        else if(operation=='*')acc=command_mul(operand,acc,31);
        operation=0;return true;
    };
    auto assign=[&](uint32_t address,uint8_t suffix){
        uint32_t old;
        if(suffix=='<'||suffix=='>'||(suffix>='0'&&suffix<='4')){
            if(!m.read32(address,old))return false;
            if(suffix=='<'){acc<<=16;old=(old&65535U)|acc;}
            else if(suffix=='>'){acc&=65535U;old=(old&0xffff0000U)|acc;}
            else if(suffix=='4'){acc=0;} // Nuon variable left-shift by 32 clears the source
            else {unsigned shift=(suffix-'0')*8;uint32_t mask=0xff000000U>>shift;acc=(acc&255U)<<(24-shift);old=(old&~mask)|acc;}
            if(!m.write32(address,old))return false;++cursor;
        }else if(!m.write32(address,acc))return false;
        armed=0;return true;
    };
    while(used<budget){
        uint8_t c;if(!take(c))return false;
        if(c==':'){out={cursor,acc,armed,operand,operation,addressBase};return true;}
        if(c=='&'){
            uint32_t src;if(!m.read32(in.object+0x18,src))return false;
            for(unsigned v=0;v<4;++v){uint32_t words[4];for(unsigned k=0;k<4;++k)if(!m.read32(src+v*16+k*4,words[k]))return false;for(unsigned k=0;k<4;++k)if(!m.write32(in.destination+v*16+k*4,words[k]))return false;}
            addressBase=0;continue;
        }
        bool lower=c>='a'&&c<='z';uint32_t base=in.destination;
        if(c=='$'){if(!global(addressBase))return false;continue;}
        if(c=='~'||c=='^'||c=='_'){
            base=c=='~'?in.object:in.parameters;if(c=='_'&&!global(base))return false;
            if(!take(c))return false;lower=true;
        }
        if(lower){
            uint32_t offset=(uint32_t(c)-'a')*4;uint8_t suffix;if(!byte(cursor,suffix))return false;
            uint32_t address=base+offset;
            if(armed){if(!assign(address,suffix))return false;addressBase=0;continue;}
            if(addressBase){acc=addressBase+offset;}
            else {
                if(!m.read32(address,acc))return false;
                if(suffix=='<'){acc>>=16;++cursor;}else if(suffix=='>'){acc&=65535U;++cursor;}
            }
            if(!apply())return false;addressBase=0;continue;
        }
        if(c>='A'&&c<='Z'){
            uint32_t address=in.values+(c-'A')*16;uint8_t suffix;if(!byte(cursor,suffix))return false;
            if(suffix>='0'&&suffix<='3'){
                ++cursor;address+=(suffix-'0')*4;
                if(armed){if(!m.write32(address,acc))return false;armed=0;addressBase=0;continue;}
                if(!m.read32(address,acc))return false;
            }else {if(!evaluate_attribute(m,in.object,address,acc))return false;if(scratch)scratch->second=0x8000;}
            if(!apply())return false;addressBase=0;continue;
        }
        if(c=='@'){
            uint8_t key;if(!take(key))return false;
            if(key=='x'||key=='y'){if(!m.read32(key=='x'?0x80113158:0x8011315C,acc))return false;}
            else{uint8_t bit;uint32_t input;if(!byte(0x80113178+uint32_t(key)-'0',bit)||!m.read32(0x80113160,input))return false;acc=((input>>(bit&31))&1)?0:0x7fffffff;}
            if(!apply())return false;addressBase=0;continue;
        }
        if(c=='?'){
            if(!acc){do{if(!take(c))return false;}while(c!=';');addressBase=0;continue;}
            armed=1;addressBase=0;continue;
        }
        if(c=='='){armed=1;addressBase=0;continue;}
        if(c=='`'){acc=0U-acc;addressBase=0;continue;}
        if(c=='!'){acc=uint32_t(int32_t(acc)>>16);addressBase=0;continue;}
        if(c=='['||c=='{'){
            uint32_t base=in.parameters;if(c=='{'&&!global(base))return false;
            uint8_t a,b;if(!take(a)||!take(b))return false;++cursor;
            uint32_t x,y;if(!m.read32(base+(uint32_t(a)-'0')*4,x)||!m.read32(base+(uint32_t(b)-'0')*4,y))return false;
            uint32_t diff=y-x,sum=(x<<1)+diff;acc=uint32_t(int32_t(command_mul(diff,acc,31)+sum)>>1);
            if(scratch){scratch->second=diff;scratch->third=base;}
            addressBase=0;continue;
        }
        operand=acc;operation=c;addressBase=0;
    }
    return false;
}
}
