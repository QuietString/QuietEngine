#include <iostream>
#include <Module.h>

#include "qmeta_runtime.h"
#include "Demo.h"
#include "GarbageCollector.h"
#include "Runtime.h"
#include "Classes/Player.h"

int main(int argc, char** argv)
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    qmod::ModuleManager& M = qmod::ModuleManager::Get();
    M.StartupAll();

    // Optionally ensure the primary game module is active
    if (const char* Primary = M.PrimaryModule())
    {
        (void)M.EnsureLoaded(Primary);
    }

    // Optional: console input
    qruntime::StartConsoleInput();
    
    // Example: create a root Player and play with console
    auto* P = QGC::GcManager::Get().NewObject<Player>("Hero");
    QGC::GcManager::Get().AddRoot(P);

    qruntime::RunMainLoop(std::chrono::milliseconds(16), 5);

    qruntime::StopConsoleInput();
    
    M.ShutdownAll();
    return 0;
}
