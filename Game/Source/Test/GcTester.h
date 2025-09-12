#pragma once

#include <vector>
#include <random>
#include "CoreMinimal.h"

class QTestObject;

// GC performance and functionality test harness.
class QGcTester : public QObject
{
public:

    QPROPERTY()
    std::vector<QObject*> Roots;

    QPROPERTY()
    bool bUseVector = true;

    // 0: own-only, 1: parents-only, 2: random between available sides
    QPROPERTY()
    int AssignMode = 0;

    QPROPERTY()
    int GetAssignMode() const { return AssignMode; }

    QFUNCTION()
    void SetAssignMode(int InMode);
    
    QFUNCTION()
    void SetUseVector(bool bUse);
    
    // Working sets (non-reflected)
    std::vector<QTestObject*> AllNodes;
    std::vector<std::vector<QTestObject*>> DepthLayers; // BFS layers from Roots
    
    // ---------- General graph patterns ----------
    QFUNCTION()
    void PatternChain(int Length, int Seed = 1);
    
    QFUNCTION()
    void PatternGrid(int Width, int Height, int Seed = 1);
    
    QFUNCTION()
    void PatternRandom(int Nodes, int BranchCount, int Seed = 1337);
    
    QFUNCTION()
    void PatternRings(int Rings, int RingSize, int Seed = 7);

    /**
     *   Example shape  *
     * L0:              A
     *                 /|\
     * L1:           B  C  D
     *             /|\ /|\ /|\
     * L2:        E..G H..J K..M
     *             \|/  \|/  \|/
     * L3:          N    O    P
     *              \    |    /
     * L4:               Q
     */
    QFUNCTION()
    void PatternDiamond(int Layers, int Breadth, int Seed = 3);

    QFUNCTION()
    void ClearAll(bool bSilent);

    // ---------- Mutations / breaks ----------
    // Break all links of selected parents at depth (existing).
    QFUNCTION()
    int BreakAtDepth(int TargetDepth, int Count /* -1 == ALL */, int Seed = 42);

    // Remove a percentage of outgoing edges. If depth<0 => all depths.
    QFUNCTION()
    int BreakPercent(double Percent, int Depth /* -1 == all */, int Seed = 24, bool bSilient = false);

    // Remove N random edges from reachable set.
    QFUNCTION()
    int BreakRandomEdges(int EdgeCount, int Seed = 99);

    // Detach some roots
    QFUNCTION()
    int DetachRoots(int Count, double Ratio /* 0...1 */);

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

    QFUNCTION()
    void RepeatRandomAndCollect(int NumSteps, int NumNodes, int NumBranches);
    
private:
    void ClearGraph();
    void LinkChild(QTestObject* Parent, QTestObject* Child, std::mt19937* RngOpt);

    // Build helpers
    void BuildLayers(QTestObject* Root, bool bClearExisting);
    std::vector<QTestObject*> GetReachable() const;

    // Edge helpers
    struct EdgeRef { QTestObject* Parent; QTestObject* Child; size_t ChildIndex; };
    void CollectEdgesReachable(std::vector<EdgeRef>& Out) const;
    bool RemoveEdge(QTestObject* Parent, QTestObject* Child);

    // Utility
    QTestObject* MakeNode();
    QTestObject* PickRandom(const std::vector<QTestObject*>& From, std::mt19937& Rng);

    // Returns non-null children respecting the current storage mode.
    void GatherChildren(QTestObject* Node, std::vector<QTestObject*>& Out) const;

    // Count of non-null children in current mode.
    size_t GetChildCount(const QTestObject* Node) const;
};
