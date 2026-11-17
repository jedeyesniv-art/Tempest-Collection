// runlog.h - a log file for a game started by the launcher (it has no console, so its messages - ours and the
// emulator core's - would be lost): userdata\logs\<name>.log, the previous run's kept as <name>.old.log.
#pragma once
#include <filesystem>
#include <string>

namespace runlog {

// Redirects stderr (unbuffered, so a hang or crash still leaves everything written) and the process's standard error
// handle (a core DLL with its own C runtime, loaded later, writes there too). Starts with the time and the command line.
void Start(const std::filesystem::path& userdata, const std::string& name);

}  // namespace runlog
