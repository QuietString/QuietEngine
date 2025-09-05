#include <Module.h>

#include "qmeta_runtime.h"
#include "Demo.h"
#include "GarbageCollector.h"
#include "Runtime.h"
#include "Classes/Player.h"

int main(int argc, char** argv)
{
    
    qmod::ModuleManager& M = qmod::ModuleManager::Get();
    M.StartupAll();

    // Optionally ensure the primary game module is active
    if (const char* Primary = M.PrimaryModule())
    {
        (void)M.EnsureLoaded(Primary);
    }

    // Example: create a root Player and play with console
    auto* P = QGC::GcManager::Get().NewObject<Player>("Hero");
    QGC::GcManager::Get().AddRoot(P);
    
    for (int i = 0; i < 30; ++i)
    {
        qruntime::Tick(0.1); // 100ms
    }
    
    // TODO: minimal engine loop placeholder
    // while (running) { tick(); }
    
    M.ShutdownAll();
    return 0;
}
