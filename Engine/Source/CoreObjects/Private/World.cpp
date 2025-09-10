#include "CoreObjects/Public/World.h"

namespace 
{
    QWorld* WorldSingleton;
}

void QWorld::SetWorldSingleton(QWorld* World)
{
    WorldSingleton = World;
}

QWorld* GetWorld()
{
    return WorldSingleton;
}
