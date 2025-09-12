#include "Demo.h"

#include <filesystem>
#include <iostream>

#include "qmeta_runtime.h"
#include "World.h"
#include "GarbageCollector.h"
#include "Classes/Monster.h"
#include "Classes/Player.h"
#include "Test/GcTester.h"
#include "Test/TestObject.h"

void Demo::RunDemo()
{
    QPlayer P;
    const qmeta::TypeInfo* TI = qmeta::GetRegistry().find("Player");

    if (!TI)
    {
        std::cout << "No Player Info\n";
        return;
    }

    // Set property by name
    void* Ptr = qmeta::GetPropertyPtr(&P, *TI, "Health");
    if (Ptr) *static_cast<int*>(Ptr) = 150;


    // Call function by name
    qmeta::Variant Ret = qmeta::CallByName(&P, *TI, "AddHealth", { qmeta::Variant(25) });
    int New_Health = Ret.as<int>();

    std::cout << "New Health: " << New_Health << "\n";
}

void Demo::RunSaveLoad()
{
    // qmeta::Registry& R = qmeta::GetRegistry();
    // const qmeta::TypeInfo* TI = R.find("Player");
    // if (!TI) return;
    //
    // QPlayer P{};
    // P.Health = 150;
    // P.SetWalkSpeed(720.0f);
    //
    // // Decide the default dir from module meta (Game/Contents or Engine/Contents)
    // std::filesystem::path Dir = qasset::DefaultAssetDirFor(*TI);
    //
    // // Save as Game/Contents/PlayerSample.qasset (relative to working dir)
    // qasset::SaveOrThrow(&P, *TI, Dir, "PlayerSample.qasset");
    //
    // // Load into another instance
    // QPlayer Loaded{};
    // qasset::LoadOrThrow(&Loaded, *TI, Dir / "PlayerSample.qasset");
    //
    // if (!TI)
    // {
    //     printf("No Player Info");
    //     return;
    // }
    //
    // // Set property by name
    // void* Ptr = qmeta::GetPropertyPtr(&P, *TI, "Health");
    // if (Ptr) *static_cast<int*>(Ptr) = 150;
    //
    // // Call function by name
    // qmeta::Variant Ret = qmeta::CallByName(&P, *TI, "AddHealth", { qmeta::Variant(25) });
    // int New_Health = Ret.as<int>();
    //
    // const qmeta::TypeInfo* ControllerInfo = R.find("Controller");
    // if (!ControllerInfo) return;
    //
    // Controller C{};
    // printf("Original ID: %d\n", C.ControllerID);
    //
    // // Decide the default dir from module meta (Game/Contents or Engine/Contents)
    // std::filesystem::path Dir2 = qasset::DefaultAssetDirFor(*ControllerInfo);
    //
    // // Save as Game/Contents/PlayerSample.qasset (relative to working dir)
    // qasset::SaveOrThrow(&C, *ControllerInfo, Dir2, "ControllerSample.qasset");
    // // Load into another instance
    // Controller Loaded2{};
    // qasset::LoadOrThrow(&Loaded2, *ControllerInfo, Dir2 / "ControllerSample.qasset");
    // printf("Loaded ID: %d\n", Loaded2.ControllerID);
}

void Demo::RunTester()
{
    QWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Create the GC tester and make it reachable
    QGcTester* Tester = NewObject<QGcTester>();
    World->Objects.push_back(Tester);
    
    // ---- Factory setup ----
    // Clear previous registrations/pool if any
    Tester->FactoryClear();

    // 1) Register test types here (QObject-derived)
    Tester->FactoryRegisterType<QTestObject>();
    // Tester->FactoryRegisterType<QPlayer>();
    
    // 2) Choose which types the factory will actually use (the pool)
    Tester->FactoryUseTypes(std::vector<std::string>{
        "QTestObject",
        // "QPlayer",
    });

    // Optional: default config for assignment strategy
    Tester->SetAssignMode(2);     // 0: own-only, 1: parents-only, 2: random
    Tester->SetUseVector(true);   // prefer std::vector<T*> slots

    std::cout << "[GcTester] Ready. Instance name: " << Tester->GetDebugName()
              << " (use commands: gctest ...  or  call " << Tester->GetDebugName() << " <Func> ...)" << std::endl;
}

void Demo::RunReflectionTest()
{
    QWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    QPlayer* Player1 = NewObject<QPlayer>();
    World->Objects.push_back(Player1);
    
    QPlayer* Player2 = NewObject<QPlayer>();
    World->Objects.push_back(Player2);

    //Player1->SetOwner(Player2);

    QMonster* Monster = NewObject<QMonster>();
    World->Objects.push_back(Monster);

    std::cout << "[Demo] Created reflection test instances: " << Player1->GetDebugName() << ", " << Player2->GetDebugName() << ", " << Monster->GetDebugName() << std::endl;
}
