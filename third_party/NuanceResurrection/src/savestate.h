// savestate.h (llamasoft-nuon): the archive behind retro_serialize / retro_unserialize. One routine per subsystem
// walks its state with the same calls for saving and loading (StateIO::loading tells which).
#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

struct StateIO
{
  bool loading = false;
  bool ok = true;
  bool fast = false;            // run-ahead snapshot (memory only, never a file): live pages only, no code flush on load
  std::vector<uint8_t> out;     // saving
  const uint8_t* in = nullptr;  // loading
  size_t pos = 0, size = 0;

  void raw(void* p, size_t n)
  {
    if(loading)
    {
      if(!ok || pos + n > size) { ok = false; return; }
      memcpy(p, in + pos, n);
    }
    else
      out.insert(out.end(), (const uint8_t*)p, (const uint8_t*)p + n);
    pos += n;
  }
  template <class T> void pod(T& v)
  {
    static_assert(std::is_trivially_copyable<T>::value, "pod: trivially copyable only");
    raw((void*)&v, sizeof v);
  }
  template <class T> void vec(std::vector<T>& v)
  {
    static_assert(std::is_trivially_copyable<T>::value, "vec: trivially copyable elements only");
    uint32_t n = (uint32_t)v.size();
    pod(n);
    if(loading)
    {
      if(!ok || n > (size - pos) / (sizeof(T) ? sizeof(T) : 1)) { ok = false; return; }
      v.resize(n);
    }
    if(n) raw(v.data(), n * sizeof(T));
  }
  // a marker between sections: a load that drifted out of step fails here instead of loading garbage
  void tag(uint32_t t)
  {
    uint32_t v = t;
    pod(v);
    if(loading && v != t) ok = false;
  }
};

// Per subsystem (video.cpp, media.cpp, Syscall.cpp); libretro.cpp walks the rest.
void VideoState(StateIO& s);
void MediaState(StateIO& s);
void SyscallState(StateIO& s);

#endif
