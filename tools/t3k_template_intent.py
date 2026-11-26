#!/usr/bin/env python3
"""Inspect template consumers from external reference RAM; ship no fixtures."""
import argparse,hashlib,json,struct
from pathlib import Path
def inspect(folder,index,initializer_override=None):
 banks={0x80000000:(folder/'base-system.bin').read_bytes(),0x40000000:(folder/'base-main.bin').read_bytes()}
 if any(len(b)!=0x800000 for b in banks.values()):raise ValueError('Expected complete reference banks')
 def read(a):
  base=a&0xFF800000
  if base not in banks or a&3:raise ValueError(f'Invalid reference pointer {a:08X}')
  return struct.unpack_from('>I',banks[base],a&0x7fffff)[0]
 def callback(slot):return (0x800113B4+read(0x801131E4+slot*4))&0xffffffff
 if not 0<=index<256:raise ValueError('Template index out of range')
 address=read(0x800B30E0+index*4);words=[read(address+i*4) for i in range(16)];descriptor=words[2];action=words[9]
 initializer=action>>24
 if initializer_override is not None:
  if not 0<=initializer_override<256:raise ValueError('Initializer override out of range')
  # A zero selector is consumed/reset but chooses the template default.
  if initializer_override:initializer=initializer_override
 result={'template_index':index,'source':hex(address),'descriptor':hex(descriptor),'values':(descriptor>>16)&255,'draw_source':hex(words[6]),'interest':hex(words[12]),'collision_status':hex(words[14]),'collision_capable':bool(words[14]&32),'initializer_index':initializer,'initializer_entry':hex((0x800105BC+read(0x80112CC0+initializer*4))&0xffffffff),'fixture_system_sha256':hashlib.sha256(banks[0x80000000]).hexdigest()}
 if descriptor&128:
  result['primary_callback']=hex(callback((action>>16)&255));result['secondary_callback']=hex(callback((descriptor>>8)&127));result['worker']=descriptor&127
 else:result['primary_callback']=hex(callback(descriptor&255))
 result['template_initializer_index']=action>>24
 result['initializer_override']=initializer_override
 # Resolve precisely the same inline storage ownership as ObjectLayout. Keep
 # inherited globals explicit: this fixture cannot determine a future caller.
 value_count=(descriptor>>16)&255
 values=words[3] or address+0x40
 end=address+0x40+value_count*16
 parameters=words[4]
 if not parameters:parameters=end;end+=0x40
 globals_count=(descriptor>>24)&63
 globals_pointer=words[7] if globals_count else None
 if globals_count and not globals_pointer:globals_pointer=end;end+=globals_count*4
 result['layout']={'values':hex(values),'parameters':hex(parameters),'globals':hex(globals_pointer) if globals_pointer is not None else 'inherited','script':hex(words[5] or end),'storage_end':hex(end)}
 result['value_records']=[{'position':hex(read(values+i*16)),'velocity':hex(read(values+i*16+4)),'scale':hex(read(values+i*16+8)),'flags':hex(read(values+i*16+12))} for i in range(value_count)]
 return result
def main():
 p=argparse.ArgumentParser();p.add_argument('reference_folder',type=Path);p.add_argument('--template',type=lambda v:int(v,0),required=True);p.add_argument('--initializer-override',type=lambda v:int(v,0));args=p.parse_args()
 print(json.dumps(inspect(args.reference_folder,args.template,args.initializer_override),indent=2))
if __name__=='__main__':main()
