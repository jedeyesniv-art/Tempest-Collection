#pragma once
#include <cstdint>
namespace tempest_native {
// Header fields consumed by 80011B76..80011C5E. Zero pointers select inline
// storage, while the global parameter table can be inherited across objects.
struct ObjectLayoutInput {
    uint32_t address,descriptor,valuesPointer,parametersPointer,scriptPointer,globalPointer;
    uint32_t inheritedGlobals,skipUpdate,controlFlags;
};
struct ObjectLayout {
    uint32_t valueCount,values,parameters,globals,storageEnd,script;
    bool updateValues;
};
inline ObjectLayout resolve_object_layout(const ObjectLayoutInput& in) {
    ObjectLayout out{};
    out.valueCount=(in.descriptor>>16)&255;
    out.values=in.valuesPointer?in.valuesPointer:in.address+0x40;
    uint32_t end=in.address+0x40+out.valueCount*16;
    out.parameters=in.parametersPointer;
    if(!out.parameters){out.parameters=end;end+=0x40;}
    out.globals=in.inheritedGlobals;
    unsigned globals=(in.descriptor>>24)&63;
    if(globals){out.globals=in.globalPointer;if(!out.globals){out.globals=end;end+=globals*4;}}
    out.storageEnd=end;
    out.script=in.scriptPointer?in.scriptPointer:end;
    bool inhibited=(in.controlFlags&0x2cU) && (in.descriptor==0x0E02048CU||in.descriptor==0x0502318CU);
    out.updateValues=!in.skipUpdate&&out.valueCount&&!inhibited;
    return out;
}
}
