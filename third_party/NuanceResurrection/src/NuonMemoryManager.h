#ifndef NUONMEMORYMANAGER_H
#define NUONMEMORYMANAGER_H

#include "basetypes.h"
#include "MemoryManager.h"

class NuonMemoryManager final
{
public:
  NuonMemoryManager() {}
  ~NuonMemoryManager() {}

  uint32 Alloc(const uint32 requestedBytes, const uint32 requestedAlignment, const uint32 flags);
  void Free(uint32 address);
  void Init();
  template <class S> void State(S& s) { mainBusMemoryManager.State(s); otherBusMemoryManager.State(s); }

private:
  MemoryManager mainBusMemoryManager;
  MemoryManager otherBusMemoryManager;
};

#endif
