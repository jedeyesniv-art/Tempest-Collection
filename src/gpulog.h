// gpulog.h - survey tool: logs every gpurun call in a frame range (see gpulog.cpp).
#pragma once

namespace gpulog {
bool Install(int f0, int f1, const char* path);
void SetFrame(int f);
}  // namespace gpulog
