#include "CoreObjects/Public/World.h"

namespace 
{
    QWorld* WorldSingleton;
}

void QWorld::AddObject(QObject* Obj)
{
    Objects.push_back(Obj);
}

void QWorld::SetWorldSingleton(QWorld* World)
{
    WorldSingleton = World;
}

QWorld* GetWorld()
{
    return WorldSingleton;
}
