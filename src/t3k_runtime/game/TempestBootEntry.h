#pragma once
#include "TempestCommands.h"
namespace tempest_native {
struct BootControllerServices {
    virtual bool initialize_controllers(uint32_t& table)=0;
    virtual ~BootControllerServices()=default;
};
struct BootMainArguments {uint32_t argc,argv;};
// CRT 80010128..80010152, stopping at the C-main call. The earlier patch/cache
// compatibility logic and final exit remain separate contracts.
inline bool prepare_boot_main(CommandMemory& m,BootControllerServices& services,BootMainArguments& output){
    uint32_t table,argc;if(!services.initialize_controllers(table)||!m.write32(0x806B9910,table)||!m.read32(0x804FB71C,argc))return false;
    output={argc,0x804FB714};return true;
}
}
