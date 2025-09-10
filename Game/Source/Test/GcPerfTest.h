#pragma once

#include <vector>
#include <random>
#include "CoreMinimal.h"

class QObject_GcTest;

// GC performance and functionality test harness.
class QGcPerfTest : public QObject
{
public:

    QPROPERTY()
    std::vector<QObject*> Roots;

    // Working sets (non-reflected)
    std::vector<QObject_GcTest*> AllNodes;
    std::vector<std::vector<QObject_GcTest*>> Levels; // BFS layers from Roots

    // ---------- General graph patterns ----------
    QFUNCTION()
    void PatternChain(int Length, int Seed = 1);
    
    QFUNCTION()
    void PatternGrid(int Width, int Height, int Seed = 1);
    
    QFUNCTION()
    void PatternRandom(int Nodes, int AvgOut, int Seed = 1337);
    
    QFUNCTION()
    void PatternRings(int Rings, int RingSize, int Seed = 7);
    
    QFUNCTION()
    void PatternDiamond(int Layers, int Breadth, int Seed = 3);

    // ---------- Mutations / breaks ----------
    // Break all links of selected parents at depth (existing).
    QFUNCTION()
    int BreakAtDepth(int TargetDepth, int Count /* -1 == ALL */, int Seed = 42);

    // Remove a percentage of outgoing edges. If depth<0 => all depths.
    QFUNCTION()
    int BreakPercent(double Percent, int Depth /* -1 == all */, int Seed = 24);

    // Remove N random edges from reachable set.
    QFUNCTION()
    int BreakRandomEdges(int EdgeCount, int Seed = 99);

    // Detach some roots (count >=0 => count; count<0 and percentage>0 => by percent).
    QFUNCTION()
    int DetachRoots(int Count, double Percentage /* 0..100 */);

    // ---------- Stats / measure ----------
    QFUNCTION() void PrintDepthStats(int TargetDepth) const;
    QFUNCTION() void MeasureGc(int Repeats);

    // ---------- Dynamic churn ----------
    // steps: iteration count
    // allocPerStep: number of new nodes allocated at each step
    // breakPct: percentage of edges randomly removed after allocation at each step
    // gcEveryN: run GC every N steps (<=0 to disable, 1 to run every step)
    QFUNCTION()
    void Churn(int Steps, int AllocPerStep, double BreakPct, int GcEveryN, int Seed = 2025);

private:
    void ClearGraph();
    void LinkChild(QObject_GcTest* Parent, QObject_GcTest* Child);

    // Build helpers
    void RebuildLevelsFromRoots();
    std::vector<QObject_GcTest*> GetReachable() const;

    // Edge helpers
    struct EdgeRef { QObject_GcTest* Parent; QObject_GcTest* Child; size_t ChildIndex; };
    void CollectEdgesReachable(std::vector<EdgeRef>& Out) const;
    bool RemoveEdge(QObject_GcTest* Parent, QObject_GcTest* Child);

    // Utility
    QObject_GcTest* MakeNode();
    QObject_GcTest* PickRandom(const std::vector<QObject_GcTest*>& From, std::mt19937& Rng);
};
