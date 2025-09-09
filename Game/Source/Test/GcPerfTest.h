#pragma once
#include <vector>
#include <random>
#include <string>
#include <queue>
#include <unordered_map>
#include <chrono>
#include <iostream>

#include "Object.h"
#include "qmeta_macros.h"

// Forward declare
class QObject_GcTest;

class QGcPerfTest : public QObject
{
public:
    // Config (reflected for convenience)
    QPROPERTY()
    int RootCount = 1;
    QPROPERTY()
    int Depth = 10;            // Depth 0 = roots
    QPROPERTY()
    int Branching = 3;         // Children per node
    QPROPERTY()
    std::vector<QObject*> Roots; // GC roots the test keeps intentionally

    // Non-reflected working sets
    std::vector<QObject_GcTest*> AllNodes;
    std::vector<std::vector<QObject_GcTest*>> Levels;

    // Rebuild a fresh forest with given parameters
    QFUNCTION()
    void Build(int InRootCount, int InDepth, int InBranching, int Seed = 1337);

    // Cut links that lead into TargetDepth.
    // If Count < 0 => cut for ALL parents at depth-1.
    // Returns number of cut links.
    QFUNCTION()
    int BreakAtDepth(int TargetDepth, int Count /* -1 == ALL */, int Seed = 42);

    // Print node count at depth and per-node direct children stats (min/avg/max)
    QFUNCTION()
    void PrintDepthStats(int TargetDepth) const;

    // Force a GC and print elapsed milliseconds (also returned)
    QFUNCTION()
    double ForceGc();

private:
    void ClearGraph();
    void LinkChild(QObject_GcTest* Parent, QObject_GcTest* Child);
};
