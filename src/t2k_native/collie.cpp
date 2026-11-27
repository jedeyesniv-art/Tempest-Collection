// collie.cpp - Tempest 2000's collie (yak.s ~12920, $80C604) and rannum ($8100D2) ported to C++, line for line.
// collie: "Collision detect the current object (a6) with all currently active bulls. Return with non-0 for a hit,
// and kill the bull." Superzapper first (zappit), then the bullet table (colok / dog), then what the hit does to
// the bullet: stop it (jstop / kbull), or spatter it off a spike (xle / dxle, a random number from rannum).
// Checked against the emulator by the oracle (oracle.cpp, t2k.exe --oracle=collie).
#include "t2k_native.h"
#include "t2k_symbols.h"

namespace t2knative {
namespace {
using namespace t2ksym;

// rannum ($8100D2): d0.b = rantab[++ranptr & $FF]; a3 = rantab. d0's upper word is left alone (move / and are word
// ops, the move.b only replaces the low byte of a word that is already 0-255).
void rannum(Cpu& c, Mem& m) {
  c.a[3] = rantab;                                      // lea rantab,a3
  m.w16(ranptr, uint16_t(m.r16(ranptr) + 1));           // add #1,ranptr
  Cpu::setw(c.d[0], m.r16(ranptr));                     // move ranptr,d0
  Cpu::setw(c.d[0], c.d[0] & 0xFF);                     // and #$ff,d0
  Cpu::setb(c.d[0], m.r8(c.a[3] + Cpu::sw(c.d[0])));    // move.b 0(a3,d0.w),d0
}

// xle2: return a hit.
void xle2(Cpu& c) {
  Cpu::setw(c.d[0], 1);                                 // move #1,d0
  c.z = false;
}

// kbull: kill the bullet in a0 - clear its collision table entry, its update routine becomes kill_shot (4).
void kbull(Cpu& c, Mem& m) {
  c.a[1] = m.r32(c.a[0] + 24);                          // move.l 24(a0),a1
  m.w32(c.a[1], 0);                                     // clr.l (a1)
  m.w16(c.a[0] + 54, 4);                                // move #4,54(a0)
  xle2(c);
}

void jstop(Cpu& c, Mem& m) {
  c.a[0] = c.a[4];                                      // move.l a4,a0
  kbull(c, m);
}

// dxle: the bullet in a4 spatters (xshot) with a random spread.
void dxle(Cpu& c, Mem& m) {
  c.a[1] = m.r32(c.a[4] + 24);                          // move.l 24(a4),a1
  m.w32(c.a[1], 0);                                     // move.l #0,(a1)
  m.w32(c.a[4], uint32_t(-16));                         // move.l #-16,(a4)   solids index: dxshot
  m.w16(c.a[4] + 54, 26);                               // move #26,54(a4)    run_vex: xshot
  m.w16(c.a[4] + 30, 8);                                // move #8,30(a4)
  m.w16(c.a[4] + 34, 1);                                // move #1,34(a4)     draw_vex: draw
  rannum(c, m);                                         // bsr rannum
  m.w16(c.a[4] + 46, uint16_t(c.d[0]));                 // move d0,46(a4)
  xle2(c);                                              // bra xle2
}

void xle(Cpu& c, Mem& m) {
  if (m.r16(c.a[6] + 20) == 0) return dxle(c, m);       // tst 20(a6) / beq dxle   (not a Super Spike)
  dxle(c, m);                                           // bsr dxle
  Cpu::setw(c.d[0], 0);                                 // move #0,d0   spatter, but no hit for a super spike
  c.z = true;
}

void xle3(Cpu& c, Mem& m) {
  if (m.r16(c.a[6] + 54) != 6) return xle2(c);          // cmp #6,54(a6) / bne xle2   (not run_spike)
  if (m.r16(c.a[6] + 20) == 0) return xle2(c);          // tst 20(a6) / beq xle2      (not super)
  dxle(c, m);                                           // bra dxle
}

// zappit ($80C61A): the Superzapper kills the object; the bolt goes to it.
void zappit(Cpu& c, Mem& m) {
  if (m.r16(inf_zap) == 0) m.w16(_sz, 0xFFFF);          // tst inf_zap / bne zizzit / move #-1,_sz
  m.w32(boltx, m.r32(c.a[6] + 4));                      // zizzit: move.l 4(a6),boltx
  m.w32(bolty, m.r32(c.a[6] + 8));                      // move.l 8(a6),bolty
  m.w32(boltz, m.r32(c.a[6] + 12));                     // move.l 12(a6),boltz
  m.w16(bolt_lock, 1);                                  // move #1,bolt_lock
  m.w16(wave_speed, 4);                                 // move #4,wave_speed
  xle2(c);                                              // move #1,d0 / rts
}

// colok1: d6 = 0 means "kill the bullet we hit"; non-0 returns the bullet in a4 untouched.
void colok1(Cpu& c, Mem& m) {
  Cpu::setw(c.d[0], m.r16(c.a[6] + 16));                // move 16(a6),d0   our web lane
  Cpu::setw(c.d[1], m.r16(c.a[6] + 12));                // move 12(a6),d1   our Z
  Cpu::setw(c.d[7], m.r16(bullmax));                    // move bullmax,d7
  c.a[5] = bulls;                                       // lea bulls,a5
  for (;;) {
    c.d[2] = m.r32(c.a[5]);                             // dog: move.l (a5)+,d2
    c.a[5] += 4;
    if (c.d[2] != 0) {                                  // beq yap
      c.a[4] = c.d[2];                                  // move.l d2,a4
      if (m.r16(c.a[4] + 16) == uint16_t(c.d[0])) {     // cmp 16(a4),d0 / bne yap
        Cpu::setw(c.d[3], m.r16(c.a[4] + 12));          // move 12(a4),d3
        Cpu::setw(c.d[3], c.d[3] - c.d[1]);             // sub d1,d3
        if (Cpu::sw(c.d[3]) < 0) Cpu::setw(c.d[3], 0u - c.d[3]);   // bpl bone / neg d3
        if (Cpu::sw(c.d[3]) < 6) {                      // bone: cmp #6,d3 / blt bite
          // bite
          if (uint16_t(c.d[6]) != 0) { c.z = false; return; }        // tst d6 / bne rrts
          if (m.r16(h2h) != 0) {                        // tst h2h / bne h2h_special
            m.w16(h2h_sign, m.r16(c.a[4] + 36));        // move 36(a4),h2h_sign
            return jstop(c, m);                         // bra jstop
          }
          if (int16_t(m.r16(c.a[4] + 34)) >= 10) return xle3(c, m);  // cmp #10,34(a4) / bge xle3   (ring bullet)
          if (m.r16(blanka) == 0) return jstop(c, m);   // tst blanka / beq jstop   (vectors: bullets just stop)
          if (m.r16(c.a[6] + 54) == 6) return xle(c, m); // cmp #6,54(a6) / beq xle   (off a spike: spatter)
          return jstop(c, m);
        }
      }
    }
    Cpu::setw(c.d[7], c.d[7] - 1);                      // yap: dbra d7,dog
    if (uint16_t(c.d[7]) == 0xFFFF) break;
  }
  Cpu::setw(c.d[0], 0);                                 // clr d0 / rts: no collision
  c.z = true;
}

}  // namespace

void collie(Cpu& c, Mem& m) {
  const int16_t sz = int16_t(m.r16(_sz));               // tst _sz / beq colok / bmi colok
  if (sz > 0) return zappit(c, m);
  Cpu::setw(c.d[6], 0);                                 // colok: clr d6
  colok1(c, m);
}

}  // namespace t2knative
