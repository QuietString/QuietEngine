#include <Module.h>

#include "qmeta_runtime.h"
#include "Demo.h"

int main(int argc, char** argv) {
    qmod::ModuleManager& M = qmod::ModuleManager::Get();

    // Load all statically registered modules (Engine, Game, etc.)
    M.StartupAll();

    // Optionally ensure the primary game module is active
    if (const char* Primary = M.PrimaryModule()) {
        (void)M.EnsureLoaded(Primary);
    }
    
    Demo::RunSaveLoad();
    
    // TODO: minimal engine loop placeholder
    // while (running) { tick(); }

    M.ShutdownAll();
    return 0;
}
