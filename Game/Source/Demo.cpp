#include "Demo.h"
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
    void* ptr = qmeta::GetPropertyPtr(&P, *TI, "Health");
    if (ptr) *static_cast<int*>(ptr) = 150;


    // Call function by name
    qmeta::Variant ret = qmeta::CallByName(&P, *TI, "AddHealth", { qmeta::Variant(25) });
    int new_health = ret.as<int>();

    printf("New Health: %d\n", new_health);
}
