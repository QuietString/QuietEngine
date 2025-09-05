#pragma once
#include <string>

namespace qruntime
{
    // Drives time-based systems (e.g., GC auto run). Call this periodically.
    void Tick(double DeltaSeconds);

    // Execute one command line. Returns true if command was recognized.
    bool ExecuteCommand(const std::string& Line);

    // Optional: set default GC interval in seconds (0 disables auto).
    void SetGcInterval(double Seconds);
}
