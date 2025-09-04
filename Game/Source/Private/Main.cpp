#include <Module.h>

int main(int argc, char** argv) {
    qmod::ModuleManager& M = qmod::ModuleManager::Get();

    // Load all statically-registered modules (Engine, Game, etc.)
    M.StartupAll();

    // Optionally ensure the primary game module is active
    if (const char* primary = M.PrimaryModule()) {
        (void)M.EnsureLoaded(primary);
    }

    // TODO: minimal engine loop placeholder
    // while (running) { tick(); }

    M.ShutdownAll();
    return 0;
}