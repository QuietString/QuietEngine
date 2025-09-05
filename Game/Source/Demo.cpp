#include "Demo.h"

#include <filesystem>
#include <iostream>

#include "Asset.h"
#include "Controller.h"
#include "GarbageCollector.h"
#include "qmeta_runtime.h"
#include "Runtime.h"
#include "Classes/Player.h"

void Demo::RunDemo()
{
    Player P;
    const qmeta::TypeInfo* TI = qmeta::GetRegistry().find("Player");

    if (!TI)
    {
        printf("No Player Info");
        return;
    }

    // Set property by name
    void* Ptr = qmeta::GetPropertyPtr(&P, *TI, "Health");
    if (Ptr) *static_cast<int*>(Ptr) = 150;


    // Call function by name
    qmeta::Variant Ret = qmeta::CallByName(&P, *TI, "AddHealth", { qmeta::Variant(25) });
    int New_Health = Ret.as<int>();

    printf("New Health: %d\n", New_Health);
}

void Demo::RunSaveLoad()
{
    qmeta::Registry& R = qmeta::GetRegistry();
    const qmeta::TypeInfo* TI = R.find("Player");
    if (!TI) return;

    Player P{};
    P.Health = 150;
    P.SetWalkSpeed(720.0f);

    // Decide the default dir from module meta (Game/Contents or Engine/Contents)
    std::filesystem::path Dir = qasset::DefaultAssetDirFor(*TI);
    
    // Save as Game/Contents/PlayerSample.qasset (relative to working dir)
    qasset::SaveOrThrow(&P, *TI, Dir, "PlayerSample.qasset");

    // Load into another instance
    Player Loaded{};
    qasset::LoadOrThrow(&Loaded, *TI, Dir / "PlayerSample.qasset");

    if (!TI)
    {
        printf("No Player Info");
        return;
    }

    // Set property by name
    void* Ptr = qmeta::GetPropertyPtr(&P, *TI, "Health");
    if (Ptr) *static_cast<int*>(Ptr) = 150;
    
    // Call function by name
    qmeta::Variant Ret = qmeta::CallByName(&P, *TI, "AddHealth", { qmeta::Variant(25) });
    int New_Health = Ret.as<int>();
    
    const qmeta::TypeInfo* ControllerInfo = R.find("Controller");
    if (!ControllerInfo) return;

    Controller C{};
    printf("Original ID: %d\n", C.ControllerID);

    // Decide the default dir from module meta (Game/Contents or Engine/Contents)
    std::filesystem::path Dir2 = qasset::DefaultAssetDirFor(*ControllerInfo);
    
    // Save as Game/Contents/PlayerSample.qasset (relative to working dir)
    qasset::SaveOrThrow(&C, *ControllerInfo, Dir2, "ControllerSample.qasset");
    // Load into another instance
    Controller Loaded2{};
    qasset::LoadOrThrow(&Loaded2, *ControllerInfo, Dir2 / "ControllerSample.qasset");
    printf("Loaded ID: %d\n", Loaded2.ControllerID);
}

void Demo::RunGCTest()
{
    using namespace QGC;

    auto& GC = GcManager::Get();
    qruntime::SetGcInterval(0.0); // disable auto for test

    // Create a simple chain: A -> B -> C
    auto* A = GC.NewObject<Player>("A");
    auto* B = GC.NewObject<Player>("B");
    auto* C = GC.NewObject<Player>("C");

    GC.Link("A", "Friend", "B"); // assuming you add QPROPERTY(Player* Friend) to Player
    GC.Link("B", "Friend", "C");

    //GC.Collect(); // nothing freed
    GC.Unlink("A", "Friend");
    //GC.Collect(); // B and C should be collected
}