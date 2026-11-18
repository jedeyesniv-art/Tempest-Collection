// jagsurvey.h - game-independent Jaguar survey (no symbols needed): every GPU start (with a hash of the GPU code),
// every blit launch and every object the OP reads, per frame. Headless: --survey=F0:F1:<file>.
// Made for Defender 2000 (no source), works on any cartridge. Summarise with tools/jag_survey.py.
#pragma once

namespace jagsurvey {
bool Install(int f0, int f1, const char* path);
void SetFrame(int f);   // before each emulated frame
}  // namespace jagsurvey
