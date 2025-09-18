#include "GcTestManager.h"

#include <iostream>

#include "TestObject.h"

QGcTestManager::QGcTestManager()
{
    bGcIgnoredSelfAndBelow = true;
}


void QGcTestManager::Initialize()
{
    SetRootAttachMode(ERootAttachMode::GarbageCollectorRoots); // or WorldObjects
    SetNumTesters(19);
    CreateOrReset();

    // 2) Setup factory and defaults for all testers
    ConfigureAll([](QGcTester& T)
    {
        T.FactoryClear();
        T.FactoryRegisterType<QTestObject>();
        T.FactoryUseTypes(std::vector<std::string>{
            "QTestObject"
        });

        T.SetAssignMode(2);    // 0: own-only, 1: parents-only, 2: random
        T.SetUseVector(true);  // prefer std::vector<T*> slots
    });

    std::cout << "[GcTestManager] Created " << (int)GetTesters().size() << " testers" << std::endl;

    // // 3) Build graphs for every tester, then run GC ONCE
    // const int NodesPerTester = 10000;
    // const int AvgOut = 3;
    // const int SeedBase = 42;
    //
    // BuildGraphsRandomForAll(NodesPerTester, AvgOut, SeedBase);
    // GarbageCollector::Get().Collect();
}

void QGcTestManager::Run()
{
    RepeatRandomAcrossTestersAndCollect(5, 10000, 3, 42);
}

void QGcTestManager::PatternChain(int Length, int Seed)
{
    for (auto& Tester : Testers)
    {
        Tester->PatternChain(Length, Seed);
    }
}

void QGcTestManager::PatternGrid(int W, int H, int Seed)
{
    for (auto& Tester : Testers)
    {
        Tester->PatternGrid(W, H, Seed);
    }
}

void QGcTestManager::PatternRandom(int Nodes, int AvgOut, int Seed)
{
    for (auto& Tester : Testers)
    {
        Tester->PatternRandom(Nodes, AvgOut, Seed);
    }
}

void QGcTestManager::PatternRings(int Rings, int RingSize, int Seed)
{
    for (auto& Tester : Testers)
    {
        Tester->PatternRings(Rings, RingSize, Seed);
    }
}

void QGcTestManager::BreakRandomEdges(int Count, int Seed)
{
    for (auto& Tester : Testers)
    {
        Tester->BreakRandomEdges(Count, Seed);
    }
}

int QGcTestManager::BreakAtDepth(int TargetDepth, int Count, int Seed)
{
    int Removed = 0;
    for (auto& Tester : Testers)
    {
        Removed += Tester->BreakAtDepth(TargetDepth, Count, Seed);
    }

    return Removed;
}

int QGcTestManager::BreakPercent(double Percent, int Depth, int Seed, bool bOnlyRoots)
{
    int Removed = 0;
    for (auto& Tester : Testers)
    {
        Removed += Tester->BreakPercent(Percent, Depth, Seed, bOnlyRoots);
    }

    return Removed;
}

void QGcTestManager::ClearAll(bool bSilent)
{
    for (auto& Tester : Testers)
    {
        Tester->ClearAll(bSilent);
    }
}

