#include <iostream>
#include <Module.h>
#include <ostream>
#include <qmeta_runtime.h>

#include "Demo.h"
#include "World.h"
#include "Test/GcPerfTest.h"

Q_FORCE_LINK_MODULE(Engine);

void RegisterGameReflections(qmeta::Registry&);

class FGameModule final : public qmod::IModule, public qmod::ITickableModule
{
public:
    virtual const char* GetName() const override
    {
        return "Game";
    }
    
    virtual void StartupModule() override
    {
        qmeta::Registry& R = qmeta::GetRegistry();
        RegisterGameReflections(R);

        // Game startup init
        std::cout << "Game module started" << std::endl;
    }
    
    virtual void ShutdownModule() override
    {
        // TODO: game shutdown
    }

    virtual void BeginPlay() override
    {
        std::cout << "Game begin play" << std::endl;

        //Demo::GenerateObjectsForGcTest();

        // Create the GC perf tester and make it reachable
        QGcPerfTest* Tester = NewObject<QGcPerfTest>();
        QWorld* World = GetWorld();
        if (World)
        {
            World->Objects.push_back(Tester);
        }

        std::cout << "[GcPerfTest] Ready. Instance name: " << Tester->GetDebugName()
                  << " (use commands: gctest ...  or  call " << Tester->GetDebugName() << " <Func> ...)" << std::endl;

        // Optional: build a small default graph
        // Tester->Build(1, 8, 3);
    }
    
    virtual void Tick(double DeltaSeconds) override
    {
    }
};

Q_IMPLEMENT_PRIMARY_GAME_MODULE(FGameModule, "Game")

// Anchor to force-link the Game module from the Engine host
extern "C" void Q_Mod_Game_Anchor()
{
}