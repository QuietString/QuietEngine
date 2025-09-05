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

    // Simple tokenizer that honors quoted tokens.
    std::vector<std::string> Tokenize(const std::string& s)
    {
        std::vector<std::string> out;
        std::istringstream is(s);
        std::string tok;
        while (is >> std::quoted(tok))
        {
            out.push_back(tok);
        }
        return out;
    }

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
        std::string line;
        {
            std::lock_guard<std::mutex> lock(G_CmdMutex);
            if (G_CmdQueue.empty()) break;
            line = std::move(G_CmdQueue.front());
            G_CmdQueue.pop();
        }
        ExecuteCommand(line);
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

void qruntime::RunMainLoop(std::chrono::milliseconds Step, int MaxCatchUpSteps)
{
#if defined(_WIN32) && QRUNTIME_USE_WINMM
    TimerResolutionScope Res;
#endif

    G_Running.store(true);

    using namespace std::chrono;
    const double StepSec = duration<double>(Step).count();
    Clock::time_point Next = Clock::now();
    double AccumulatedLate = 0.0;

    while (IsRunning())
    {
        // Pace to next tick boundary.
        Next += Step;
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

bool qruntime::ExecuteCommand(const std::string& Line)
{
    using namespace QGC;

    auto tokens = Tokenize(Line);
    if (tokens.empty())
    {
        return false;
    }

    auto& GC = GcManager::Get();
    const std::string& cmd = tokens[0];

    try
    {
        if (cmd == "help")
        {
            std::cout <<
                "Commands:\n"
                "  new <Type> <Name>\n"
                "  link <OwnerName> <Property> <TargetName>\n"
                "  unlink <OwnerName> <Property>\n"
                "  set <Name> <Property> <Value>\n"
                "  call <Name> <Function> [args...]\n"
                "  save <Name> [FileName]\n"
                "  load <Type> <Name> [FileName]\n"
                "  gc\n"
                "  tick <seconds>\n"
                "  ls\n"
                "  props <Name>\n"
                "  funcs <Name>" << std::endl;
            return true;
        }
        else if (cmd == "tick" && tokens.size() >= 2)
        {
            double dt = std::stod(tokens[1]);
            qruntime::Tick(dt);
            return true;
        }
        else if (cmd == "gc")
        {
            GC.Collect();
            return true;
        }
        else if (cmd == "ls")
        {
            GC.ListObjects();
            return true;
        }
        else if (cmd == "props" && tokens.size() >= 2)
        {
            GC.ListProperties(tokens[1]);
            return true;
        }
        else if (cmd == "funcs" && tokens.size() >= 2)
        {
            GC.ListFunctions(tokens[1]);
            return true;
        }
        else if (cmd == "new" && tokens.size() >= 3)
        {
            GC.NewByTypeName(tokens[1], tokens[2]);
            return true;
        }
        else if (cmd == "link" && tokens.size() >= 4)
        {
            GC.Link(tokens[1], tokens[2], tokens[3]);
            return true;
        }
        else if (cmd == "unlink" && tokens.size() >= 3)
        {
            GC.Unlink(tokens[1], tokens[2]);
            return true;
        }
        else if (cmd == "set" && tokens.size() >= 4)
        {
            GC.SetPropertyFromString(tokens[1], tokens[2], tokens[3]);
            return true;
        }
        else if (cmd == "call" && tokens.size() >= 3)
        {
            std::vector<qmeta::Variant> args;
            for (size_t i = 3; i < tokens.size(); ++i)
            {
                const std::string& a = tokens[i];
                // naive parse: int/float/bool/string
                if (a == "true" || a == "false")
                {
                    args.emplace_back(a == "true");
                }
                else if (a.find('.') != std::string::npos)
                {
                    args.emplace_back(std::stod(a));
                }
                else
                {
                    // try int
                    try { args.emplace_back(std::stoll(a)); }
                    catch (...) { args.emplace_back(a); }
                }
            }
            GC.Call(tokens[1], tokens[2], args);
            return true;
        }
        else if (cmd == "save" && tokens.size() >= 2)
        {
            const std::string file = (tokens.size() >= 3 ? tokens[2] : "");
            GC.Save(tokens[1], file);
            return true;
        }
        else if (cmd == "load" && tokens.size() >= 3)
        {
            const std::string file = (tokens.size() >= 4 ? tokens[3] : "");
            GC.Load(tokens[1], tokens[2], file);
            return true;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[CommandError] " << e.what() << std::endl;
        return true;
    }

    return false;
}
