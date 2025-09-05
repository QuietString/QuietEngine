#pragma once
#include <chrono>
#include <string>

namespace qruntime
{
    // Drives time-based systems (e.g., GC auto run). Call this periodically.
    void Tick(double DeltaSeconds);

    // Execute one command line. Returns true if command was recognized.
    bool ExecuteCommand(const std::string& Line);

    // Optional: set default GC interval in seconds (0 disables auto).
    void SetGcInterval(double Seconds);

    // Start/stop background console input thread (reads lines, enqueues to process on the main thread).
    void StartConsoleInput();
    void StopConsoleInput();

    // Pull and execute any pending console commands.
    void ProcessPendingCommands();

    // Request/Query quit (set by 'quit' command or externally).
    void RequestQuit();
    bool IsRunning();

    // Run a fixed-step loop (e.g., 16ms). Returns when quit requested.
    void RunMainLoop(std::chrono::milliseconds Step, int MaxCatchUpSteps = 5);
}
