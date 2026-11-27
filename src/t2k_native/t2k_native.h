// t2k_native.h - Tempest 2000's 68K routines ported to C++ (a feasibility study for a native T2K, by Frogacuda's
// method from native T3K: same memory layout, port function by function, prove each one against the emulator).
// NOTES.md "Native T2K study" has the findings.
#pragma once
#include "jag_mem.h"

namespace t2knative {

// Ported routines: the CPU registers in and out, memory through Mem. Same register / flag effects as the 68K.
void collie(Cpu& c, Mem& m);   // yak.s collie, $80C604

// The oracle: t2k.exe --oracle=<routine>[,...] runs the port beside the emulated routine on every call and
// compares registers, the Z flag and all of main RAM at the return. Prints a summary at exit.
bool InstallOracle(const char* spec);
void OracleReport();

}  // namespace t2knative
