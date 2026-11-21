// Defender 2000's sky stars (2000 mode): the GPU overlay $0ADB98 plots 4 parallax layers of 65 one-pixel stars (CLUT
// colour 2) into the playfield layer each update. Here the same generator from snapshots of its inputs (camera,
// horizon row, the buffer): exact (the game's pixels, to erase them from the layer before it is cut into items) or
// smooth (sub-pixel x, the camera interpolated; the 512 px star loop also past the 384 px buffer: wide screens).
// See d2k_stars.cpp.
#pragma once
#include <cstdint>
#include <vector>

namespace d2k_stars {

struct Star {
  float x;    // buffer pixel x (smooth: fractional; may lie outside 0..383)
  int y;      // buffer row
  int layer;  // 0 nearest (fastest) .. 3
};

bool Signature();                       // the overlay's code is at $0ADB98 (this game / version)
void OnOverlayStart(int frame);         // the 68K started the GPU on it: snapshot the inputs
// The snapshot shown in the buffer holding `data` since update `frame` (as d2k_bonus::ShownSnapshot), else -1.
int ShownSnapshot(uint32_t data, int frame);
uint32_t Buffer(int i);
int Latest(int frame);   // the snapshot taken during `frame` (else -1)
// Stars of snapshot i: exact (the plotted pixels) or smooth at t in [0, 1] from the snapshot before.
void Stars(int i, float t, bool exact, std::vector<Star>& out);
// Test: snapshot i's exact stars against the game's buffer (pixels holding colour 2 there / not).
void Check(int i, int frame);

}  // namespace d2k_stars
