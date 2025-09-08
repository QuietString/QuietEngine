
#include <cstdio>
#include <ios>
#include <iostream>

#include "GarbageCollector.h"
#include "Module.h"
#include "Object_GcTest.h"
#include "Runtime.h"
#include "CoreObjects/Public/World.h"

// Force-link the Game static library so its auto-registrar runs
Q_FORCE_LINK_MODULE(Game);

int main(int argc, char* argv[])
{
    // Optional stream tweaks (safe defaults)
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    qmod::ModuleManager& M = qmod::ModuleManager::Get();

    std::cout << "Starting QuietEngine..." << std::endl;

    // Bring up all statically registered modules (Engine, Game, etc.)
    M.StartupAll();

    // Ensure primary game module is active
    qmod::IModule* Primary = nullptr;
    if (const char* Name = M.PrimaryModule())
    {
        Primary = M.EnsureLoaded(Name);
    }

    QGC::GcManager& GC = QGC::GcManager::Get();
    GC.Initialize();
    GC.SetAutoInterval(0);

    auto* A = GC.NewObject<QObject_GcTest>();
    auto* B = GC.NewObject<QObject_GcTest>();
    auto* C = GC.NewObject<QObject_GcTest>();
    QWorld* World = static_cast<QWorld*>(GC.GetRoot());
    if (World)
    {
        World->SingleObject = A;
    }
    A->ChildObject = B;
    B->ChildObject = C;
    
    // BeginPlay() all modules.
    M.BeginPlayAll();
    
    // Bind game tick if the primary implements ITickableModule
    qmod::ITickableModule* Tickable = dynamic_cast<qmod::ITickableModule*>(Primary);
    qruntime::SetExternalTick([Tickable](double Dt)
    {
        if (Tickable)
        {
            Tickable->Tick(Dt);
        }
    });

    // Start background console input
    qruntime::StartConsoleInput();

    
    // Run the main loop (fixed 16ms ~60Hz)
    qruntime::RunMainLoop(std::chrono::milliseconds(16),5);

    // Cleanup
    qruntime::StopConsoleInput();
    M.ShutdownAll();
    
    return 0;
}
