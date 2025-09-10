#include "Runtime.h"
#include "GarbageCollector.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <iostream>
#include <iomanip>
#include <queue>
#include "qmeta_runtime.h"
#include <condition_variable>

#include "Console/ConsoleIO.h"
#include "Console/ConsoleManager.h"

#if defined(_WIN32)
// Optional: improve sleep granularity to ~1ms (system-wide).
// Link: winmm.lib
#define QRUNTIME_USE_WINMM 0
#if QRUNTIME_USE_WINMM
    #include <mmsystem.h>
    #pragma comment(lib, "winmm.lib")
#endif
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    std::atomic<bool> G_Running { false };

    std::mutex G_CmdMutex;
    std::queue<std::string> G_CmdQueue;

    std::thread G_InputThread;
    std::atomic<bool> G_InputRun { false };

    qruntime::TickCallback G_GameTick;

#if defined(_WIN32) && QRUNTIME_USE_WINMM
    struct TimerResolutionScope
    {
        TimerResolutionScope()
        {
            timeBeginPeriod(1);
        }
        ~TimerResolutionScope()
        {
            timeEndPeriod(1);
        }
    };
#endif
}

void qruntime::Tick(double DeltaSeconds)
{
    QGC::GcManager::Get().Tick(DeltaSeconds);
}

void qruntime::SetGcInterval(double Seconds)
{
    QGC::GcManager::Get().SetAutoInterval(Seconds);
}

void qruntime::StartConsoleInput()
{
    if (G_InputRun.load()) return;
    G_InputRun.store(true);

    G_InputThread = std::thread([]()
    {
        std::string line;
        while (G_InputRun.load())
        {
            if (!std::getline(std::cin, line))
            {
                // EOF or stream closed
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (!line.empty())
            {
                std::lock_guard<std::mutex> lock(G_CmdMutex);
                G_CmdQueue.push(line);
            }
        }
    });
}

void qruntime::StopConsoleInput()
{
    if (!G_InputRun.load()) return;
    G_InputRun.store(false);
    if (G_InputThread.joinable())
    {
        G_InputThread.join();
    }
}

void qruntime::ProcessPendingCommands()
{
    for (;;)
    {
        std::string Line;
        {
            std::lock_guard<std::mutex> Lock(G_CmdMutex);
            if (G_CmdQueue.empty()) break;
            Line = std::move(G_CmdQueue.front());
            G_CmdQueue.pop();
        }
        
        ExecuteCommand(Line);
        ConsoleIO::FlushCoutIfDirty();
    }
}

void qruntime::RequestQuit()
{
    G_Running.store(false);
}

bool qruntime::IsRunning()
{
    return G_Running.load();
}

void qruntime::RunMainLoop(std::chrono::milliseconds TimeStep, int MaxCatchUpSteps)
{
#if defined(_WIN32) && QRUNTIME_USE_WINMM
    TimerResolutionScope Res;
#endif

    G_Running.store(true);

    using namespace std::chrono;
    const double StepSec = duration<double>(TimeStep).count();
    Clock::time_point Next = Clock::now();
    double AccumulatedLate = 0.0;

    while (IsRunning())
    {
        // Pace to next tick boundary.
        Next += TimeStep;
        {
            const auto Now = Clock::now();
            if (Now < Next)
            {
                std::this_thread::sleep_until(Next);
            }
            else
            {
                // We are late; accumulate how much we are behind.
                AccumulatedLate += duration<double>(Now - Next).count();
                Next = Now;
            }
        }

        // Always process one step.
        ProcessPendingCommands();
        Tick(StepSec);

        // Catch up if we fell behind, up to MaxCatchUpSteps.
        int Catchups = 0;
        while (AccumulatedLate >= StepSec && Catchups < MaxCatchUpSteps && IsRunning())
        {
            ProcessPendingCommands();
            Tick(StepSec);
            AccumulatedLate -= StepSec;
            ++Catchups;
        }
    }
}

void qruntime::SetExternalTick(TickCallback Cb)
{
    G_GameTick = std::move(Cb);
}

bool qruntime::ExecuteCommand(const std::string& Line)
{
    return ConsoleManager::ExecuteCommand(Line);
}
