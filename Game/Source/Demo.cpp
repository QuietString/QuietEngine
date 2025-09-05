#include "Demo.h"

#include <filesystem>

#include "Asset.h"
#include "qmeta_runtime.h"
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
}
