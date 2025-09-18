#pragma once
#include <vector>
#include <functional>
#include "EngineGlobals.h"
#include "GarbageCollector.h" 
#include "World.h"
#include "Object.h"
#include "Test/GcTester.h"

class QGcTestManager : public QObject
{
public:

    QGcTestManager();

    
    enum class ERootAttachMode
    {
        WorldObjects,
        GarbageCollectorRoots
    };
    
    void Initialize();
    
    void SetRootAttachMode(ERootAttachMode InMode)
    {
        RootMode = InMode;
    }

    void SetNumTesters(int InCount)
    {
        if (InCount < 0) InCount = 0;
        NumTesters = InCount;
    }

    // Create testers and attach as roots according to RootMode
    void CreateOrReset()
    {
        ClearGeneratedAll();
        Testers.clear();

        QWorld* World = GetWorld();

        for (int i = 0; i < NumTesters; ++i)
        {
            QGcTester* T = NewObject<QGcTester>();
            Testers.push_back(T);

            if (RootMode == ERootAttachMode::WorldObjects)
            {
                if (World) World->Objects.push_back(T);
            }
            else
            {
                GarbageCollector::Get().AddRoot(T);
            }
        }
    }

    // Configure all testers (factory setup, assign mode, etc.)
    void ConfigureAll(const std::function<void(QGcTester&)>& Fn)
    {
        for (QGcTester* T : Testers)
        {
            Fn(*T);
        }
    }

    // Build a random graph on each tester; do NOT collect here
    void BuildGraphsRandomForAll(int NodesPerTester, int AvgOut, int SeedBase)
    {
        int Idx = 0;
        for (QGcTester* Tester : Testers)
        {
            if (Tester)
            {
                Tester->ClearGenerated();
            }
            
            if (Tester)
            {
                Tester->PatternRandom(NodesPerTester, AvgOut, SeedBase + Idx);
            }
            ++Idx;
        }
    }

    // Repeat: per iteration build graphs on all testers, then one Collect()
    void RepeatRandomAcrossTestersAndCollect(int Iterations, int NodesPerTester, int AvgOut, int SeedBase)
    {
        for (int It = 0; It < Iterations; ++It)
        {
            int Base = SeedBase + It * 10007;
            BuildGraphsRandomForAll(NodesPerTester, AvgOut, Base);
            GarbageCollector::Get().Collect();
        }
    }

    void ClearGeneratedAll()
    {
        for (QGcTester* Tester : Testers)
        {
            if (Tester) Tester->ClearGenerated();
        }
    }

    const std::vector<QGcTester*>& GetTesters() const
    {
        return Testers;
    }

    QFUNCTION()
    void Run();

    QFUNCTION()
    void PatternChain(int Length, int Seed);

    QFUNCTION()
    void PatternGrid(int W, int H, int Seed);

    QFUNCTION()
    void PatternRandom(int Nodes, int AvgOut, int Seed);

    QFUNCTION()
    void PatternRings(int Rings, int RingSize, int Seed);

    QFUNCTION()
    void BreakRandomEdges(int Count, int Seed);

    // Break at specific depth: remove up to Count outgoing edges per node at TargetDepth
    QFUNCTION()
    int BreakAtDepth(int TargetDepth, int Count, int Seed);

    // Break by percent; Depth=-1 means whole reachable graph
    QFUNCTION()
    int BreakPercent(double Percent, int Depth, int Seed, bool bOnlyRoots);

    QFUNCTION()
    void ClearAll(bool bSilent);
    
private:
    ERootAttachMode RootMode { ERootAttachMode::GarbageCollectorRoots };
    int NumTesters { 0 };
    std::vector<QGcTester*> Testers;
};
