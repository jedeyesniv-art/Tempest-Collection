// Defender 2000's bonus tunnel (2000 mode, between levels): the stars, streaks, tunnel rings and pods are dots a GPU
// overlay ($0AB008) generates every update from a few values in RAM and plots into an 8-bit 384 x 279 buffer (the
// playfield layer). Here the same generator, from snapshots of those values: exact (integer, checked against the
// game's buffer) or smooth (float projection, sub-pixel dots, the scalars interpolated between two updates), so the
// window can draw the dots itself at its own resolution and frame rate. See d2k_bonus.cpp.
#pragma once
#include <cstdint>
#include <vector>

namespace d2k_bonus {

struct Pt {
  float x, y;     // buffer pixel (exact: the plotted pixel's corner; smooth: the same before truncation)
  uint8_t col;    // CLUT index
  uint8_t size;   // 1 or 2 (a 2 x 2 blob)
};

// The overlay's code is in RAM at $0AB008 (this game / version).
bool Signature();
// The 68K copied the overlay into the GPU (a blit from $0AB008): snapshot its inputs.
void OnOverlayLoad(int frame);
// The snapshot shown in the buffer holding `data` (a shown layer's DATA) since `frame` (the update the screen flipped
// to it): the newest drawn there before that frame - the GPU starts the next picture into a buffer while it is still
// shown. Its index for Points (else -1).
int ShownSnapshot(uint32_t data, int frame);
// The snapshot taken during `frame` (else -1).
int Latest(int frame);
// The buffer address of snapshot i, and the buffer row the shown layer starts at for `data`.
uint32_t Buffer(int i);
// The dots of snapshot i: exact (t ignored) or smooth at t in [0, 1] between the snapshot before it and i.
void Points(int i, float t, bool exact, std::vector<Pt>& out);
// Test: compare snapshot i's exact dots with the game's buffer: mismatching bytes (and the first few) to stderr.
int Check(int i, const char* tag);

}  // namespace d2k_bonus
