#include "CoreObjects/Public/World.h"

namespace 
{
    QWorld* WorldSingleton;
}
QWorld::QWorld()
    : SingleObject(nullptr)
    , SingleObject2(nullptr)
{}

void QWorld::SetWorldSingleton(QWorld* World)
{
    WorldSingleton = World;
}

QWorld* GetWorld()
{
    return WorldSingleton;
}
