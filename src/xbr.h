// xbr.h - Hyllian's 2xBR pixel-art upscaler (applied twice for 4x), on premultiplied ARGB.
#pragma once

#include <cstdint>
#include <vector>

namespace xbr {

// in: w x h premultiplied 0xAARRGGBB (fully transparent = 0). out: (2w) x (2h), premultiplied.
void Scale2x(const uint32_t* in, int w, int h, std::vector<uint32_t>& out);
// Twice: (4w) x (4h).
void Scale4x(const uint32_t* in, int w, int h, std::vector<uint32_t>& out);

}  // namespace xbr
