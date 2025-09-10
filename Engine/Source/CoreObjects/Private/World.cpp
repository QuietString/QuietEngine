#include "CoreObjects/Public/World.h"

#include "EngineGlobals.h"
#include "GarbageCollector.h"
#include "TestObject.h"

namespace 
{
    QWorld* WorldSingleton;
}

void QWorld::SpawnObject()
{
    NewObject<QTestObject>();
}

void QWorld::AddObject(const std::string& ObjName)
{
    GarbageCollector& GC = GarbageCollector::Get();
    QObject* Obj = GC.FindByDebugName(ObjName);
    Objects.push_back(Obj);
}

void QWorld::RemoveObject(const std::string& ObjName)
{
    GarbageCollector& GC = GarbageCollector::Get();
    QObject* Obj = GC.FindByDebugName(ObjName);
    Objects.erase(std::remove(Objects.begin(), Objects.end(), Obj), Objects.end());
}

void QWorld::SetWorldSingleton(QWorld* World)
{
    WorldSingleton = World;
}

QWorld* GetWorld()
{
    return WorldSingleton;
}
