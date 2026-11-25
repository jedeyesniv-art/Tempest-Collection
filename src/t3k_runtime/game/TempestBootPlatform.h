#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct BootWorkerServices {
    virtual bool local_scratch(uint32_t sizeOutput,uint32_t& address)=0;
    virtual bool load_worker(uint32_t engine,uint32_t destination,uint32_t source,uint32_t bytes)=0;
    virtual bool start_worker(uint32_t engine,uint32_t entry)=0;
    virtual ~BootWorkerServices()=default;
};
// C-main 80020740..8002082C. Descriptor data belongs to the original caller.
// The native service resolves recognized worker identities to native code.
inline bool begin_boot_worker(CommandMemory& m,BootWorkerServices& services,uint32_t entryStack){
    if((entryStack&15)||entryStack<0x80000200||entryStack>0x80800000)return false;
    const uint32_t sample[]{0x8031B5D0,0x53F8,0,0x53F8,0x1000,1};
    for(unsigned i=0;i<6;++i)if(!m.write32(entryStack-0x18C+i*4,sample[i]))return false;
    for(unsigned i=0;i<3;++i)if(!m.write32(entryStack-0x160+i*4,0))return false;
    uint32_t scratch;if(!services.local_scratch(0x800B2AD8,scratch)||!m.write32(0x800B2AD4,scratch))return false;
    uint32_t destination,source,bytes;
    if(!m.read32(0x800B1714,destination)||!m.read32(0x800B1710,source)||!m.read32(0x800B1718,bytes)||!services.load_worker(0,destination,source,bytes))return false;
    if(!m.read32(0x800B1704,destination)||!m.read32(0x800B1700,source)||!m.read32(0x800B1708,bytes)||!services.load_worker(0,destination,source,bytes))return false;
    return services.start_worker(0,0x20300000);
}
// C-main 8002082C..800209C0. Unlike the later interactive discovery routine,
// this startup scan selects at most two matches. The byte-clear target is set
// in a branch delay slot even when the player-port mirroring is skipped.
inline bool select_boot_controllers(CommandMemory& m,uint32_t& count){
    uint32_t table,word;if(!m.read32(0x806B9910,table)||(table&15)||!m.read32(table+4,word)||!m.write32(table+4,word&65535))return false;
    unsigned selected=0;
    for(unsigned port=1;port<=8;++port){uint32_t config;if(!m.read32(table+port*16,config))return false;
        if((config&0x40000000)&&(config&0x1012)){if(!m.write32(0x801133E8+selected*4,port))return false;if(++selected==2)break;}}
    if(!selected&&!m.write32(0x801133E8,0))return false;
    if(selected<2){uint32_t first;if(!m.read32(0x801133E8,first)||!m.write32(0x801133EC,first))return false;}
    uint32_t clear=0x8010BEF0;
    if(!m.read32(clear,word)||!m.write32(clear,word&0xFFFFFF))return false;
    for(unsigned player=0;player<2;++player){uint32_t port,config;uint8_t x,y;
        if(!m.read32(0x801133E8+player*4,port)||port>8||!m.read32(table+port*16,config))return false;
        if(config&0x1000){
            if(!m.read8(table+port*16+10,x)||!m.write32(0x806B9944+player*4,uint32_t(int32_t(int8_t(x))))||!m.write32(0x801133B4,0)||
               !m.read32(0x8010BEF0,word)||!m.write32(0x8010BEF0,(word&0xFFFFFF)|0x20000000))return false;
        }else if(!m.read8(table+port*16+6,x)||!m.write32(0x806B9944+player*4,uint32_t(int32_t(int8_t(x))))||
                  !m.read8(table+port*16+7,y)||!m.write32(0x806B994C+player*4,uint32_t(int32_t(int8_t(y)))))return false;
    }
    count=selected;return true;
}
}
