#include "CoreObjects/Public/World.h"

static QWorld GWorldSingleton;

QWorld* GetWorld()
{
    return &GWorldSingleton;
}

QWorld::QWorld()
    : SingleObject(nullptr)
    , SingleObject2(nullptr)
{}