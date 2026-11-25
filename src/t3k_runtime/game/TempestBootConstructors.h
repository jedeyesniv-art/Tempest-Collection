#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct DriverFirmwareServices {
    virtual bool firmware_version(uint32_t& version,uint32_t& build)=0;
    virtual bool shutdown_media_processor()=0;
    virtual bool initialize_media_processor(uint32_t& engine)=0;
    virtual ~DriverFirmwareServices()=default;
};
// 80010160..800101B0: old firmware requires a media-processor restart. This is
// not a code patch or a generic thread allocation; audio and media share that
// firmware service. Host dependencies remain explicit.
inline bool initialize_driver_firmware_compatibility(DriverFirmwareServices& services){
    uint32_t version,build,engine;
    if(!services.firmware_version(version,build))return false;
    return version!=0x10002||build>0x3B0000||(services.shutdown_media_processor()&&services.initialize_media_processor(engine));
}
struct DriverConstructorServices {
    // Dispatch only to an implemented native function. Unknown entries must
    // fail; these original data addresses are identifiers, never guest calls.
    virtual bool invoke_constructor(uint32_t address)=0;
    virtual ~DriverConstructorServices()=default;
};
// 800338AE..80033928, including both counted and -1/null-terminated tables.
inline bool run_driver_constructor_table(CommandMemory& m,DriverConstructorServices& services,unsigned budget=4096){
    constexpr uint32_t table=0x8003C880;uint32_t count;
    if(!m.read32(table,count))return false;
    if(count==0xFFFFFFFF){count=0;uint32_t entry;
        do{if(count==budget||!m.read32(table+4+count*4,entry))return false;if(entry)++count;}while(entry);
    }
    if(count>budget)return false;
    while(count){uint32_t entry;if(!m.read32(table+count*4,entry)||!services.invoke_constructor(entry))return false;--count;}
    return true;
}
// 8003392A..8003395E. The once flag is published BEFORE invoking the table.
inline bool run_driver_constructors(CommandMemory& m,DriverConstructorServices& services){
    uint32_t initialized;if(!m.read32(0x8003C878,initialized))return false;
    return initialized|| (m.write32(0x8003C878,1)&&run_driver_constructor_table(m,services));
}
}
