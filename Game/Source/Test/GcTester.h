#pragma once

#include <vector>
#include <random>
#include <string>
#include "CoreMinimal.h"
#include "TestObjectFactory.h"

// GC performance and functionality test harness.
// Now generic for any QObject-derived test type.
class QGcTester : public QObject
{
public:
    QPROPERTY()
    std::vector<QObject*> Roots;

    // Config flags (exposed to console via QFUNCTIONs below)
    // 0: own-only, 1: parents-only, 2: random between available sides
    QPROPERTY() int AssignMode = 2;

    QPROPERTY() bool bUseVector = true;

    // ---------------- Console-facing API ----------------
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
    void DetachRoots(int Count, double Percent);

    QFUNCTION()
    void ClearAll(bool bSilent);

    // Requested earlier by you: run N steps of (random graph + GC collect)
    QFUNCTION()
    void RepeatRandomAndCollect(int NumSteps, int NumNodes, int NumBranches);

    // Config
    QFUNCTION() void SetAssignMode(int InMode);
    QFUNCTION() void SetUseVector(bool bUse);

    // Factory control (optional helpers; you can also manage from code)
    QFUNCTION() void FactoryClear();
    QFUNCTION() void FactoryAddType(const std::string& TypeName);   // requires the type was registered
    QFUNCTION() void FactoryUseTypes(const std::vector<std::string>& TypeNames);

private:
    // ---------- Generic internals (QObject-based) ----------
    struct EdgeRef
    {
        QObject* Parent = nullptr;
        QObject* Child = nullptr;
        std::string Property;     // property name where edge lives
        size_t Index = (size_t)-1; // index for vector properties; ignored for raw pointer
        bool bVector = false;
    };

    // Node storage
    std::vector<QObject*> AllNodes;
    // BFS layers: [depth][i]
    std::vector<std::vector<QObject*>> DepthLayers;

    TestObjectFactory Factory;

    // Build/clear graph scaffolding
    void ClearGraph();
    void BuildLayers(QObject* Head, bool bFromRootsOnly = false);

    // Creation & linking
    QObject* MakeNode();
    void LinkChild(QObject* Parent, QObject* Child, std::mt19937* RngOpt);

    // Reflection helpers
    void GatherChildren(QObject* Node, std::vector<QObject*>& Out) const;
    size_t GetChildCount(const QObject* Node) const;

    // Edge ops
    void CollectEdgesReachable(std::vector<EdgeRef>& Out) const;
    bool RemoveEdge(QObject* Parent, QObject* Child);

    // Helpers
    QObject* PickRandom(const std::vector<QObject*>& From, std::mt19937& Rng);
};
