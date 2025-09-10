
#include <cstdio>
#include <ios>
#include <iostream>

#include "Character.h"
#include "EngineGlobals.h"
#include "GarbageCollector.h"
#include "Module.h"
#include "TestObject.h"
#include "Runtime.h"
#include "Console/ConsoleIO.h"
#include "Core/GarbageCollector.h"
#include "CoreObjects/Public/World.h"

// Force-link the Game static library so its auto-registrar runs
Q_FORCE_LINK_MODULE(Game);

inline QWorld* CreateWorld()
{
    QWorld* World = NewObject<QWorld>();
    QWorld::SetWorldSingleton(World);

    return World;
}

inline GarbageCollector& CreateGC()
{
    GarbageCollector* GC = new GarbageCollector();
    GarbageCollector::SetGcSingleton(GC);

    return *GC;
}

int main(int argc, char* argv[])
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(&std::cout);

    qmod::ModuleManager& M = qmod::ModuleManager::Get();

    ConsoleIO::InstallDirtyCout();
    
    std::cout << "Starting QuietEngine..." << std::endl;

    // Bring up all statically registered modules (Engine, Game, etc.)
    M.StartupAll();

    // Ensure primary game module is active
    qmod::IModule* Primary = nullptr;
    if (const char* Name = M.PrimaryModule())
    {
        Primary = M.EnsureLoaded(Name);
    }

    GarbageCollector& GC = CreateGC();
    GC.SetAutoInterval(0);
    
    QWorld* World = CreateWorld();
    GC.AddRoot(World);

    QCharacter* Character = NewObject<QCharacter>();
    World->Objects.push_back(Character);
    
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
    std::chrono::milliseconds TimeStep(16);
    qruntime::RunMainLoop(TimeStep,5);

    // Cleanup
    qruntime::StopConsoleInput();
    M.ShutdownAll();
    
    return 0;
}
