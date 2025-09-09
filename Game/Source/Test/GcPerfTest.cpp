#include "Test/GcPerfTest.h"

#include <algorithm>
#include <numeric>

#include "EngineGlobals.h"
#include "GarbageCollector.h"
#include "Object_GcTest.h"
#include "qmeta_runtime.h"
#include "World.h"

using namespace QGC;

void QGcPerfTest::ClearGraph()
{
    // Drop root references; the next GC can reclaim old nodes
    Roots.clear();
    AllNodes.clear();
    Levels.clear();
}

void QGcPerfTest::LinkChild(QObject_GcTest* Parent, QObject_GcTest* Child)
{
    if (!Parent || !Child) return;

    // Keep legacy single-child usable, but prefer vector
    if (Parent->Children.empty())
    {
        Parent->ChildObject = Child;
    }
    Parent->Children.push_back(Child);
}

void QGcPerfTest::Build(int InRootCount, int InDepth, int InBranching, int Seed)
{
    if (InRootCount <= 0 || InDepth < 0 || InBranching <= 0)
    {
        std::cout << "[GcPerfTest] Invalid params. roots>0, depth>=0, branching>0\n";
        return;
    }

    ClearGraph();
    RootCount = InRootCount;
    Depth     = InDepth;
    Branching = InBranching;

    std::mt19937 rng(static_cast<uint32_t>(Seed));

    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "[GcPerfTest] World not found.\n";
        return;
    }

    Levels.resize(static_cast<size_t>(Depth) + 1);
    for (int i = 0; i < RootCount; ++i)
    {
        auto* R = NewObject<QObject_GcTest>();
        Levels[0].push_back(R);
        AllNodes.push_back(R);
        Roots.push_back(R);
    }

    // BFS build
    for (int d = 0; d < Depth; ++d)
    {
        const auto& Cur = Levels[d];
        auto& Next = Levels[d + 1];
        for (QObject_GcTest* Parent : Cur)
        {
            for (int k = 0; k < Branching; ++k)
            {
                auto* C = NewObject<QObject_GcTest>();
                LinkChild(Parent, C);
                Next.push_back(C);
                AllNodes.push_back(C);
            }
        }
    }

    // Ensure tester is reachable (world holds tester; tester holds roots)
    World->Objects.push_back(this);

    std::cout << "[GcPerfTest] Built forest:"
              << " roots=" << RootCount
              << " depth=" << Depth
              << " branching=" << Branching
              << " total=" << AllNodes.size() << "\n";
}

int QGcPerfTest::BreakAtDepth(int TargetDepth, int Count, int Seed)
{
    if (TargetDepth <= 0 || TargetDepth >= static_cast<int>(Levels.size()))
    {
        std::cout << "[GcPerfTest] Invalid TargetDepth " << TargetDepth << "\n";
        return 0;
    }

    auto& Parents = Levels[TargetDepth - 1];
    if (Parents.empty())
    {
        std::cout << "[GcPerfTest] No parents at depth " << TargetDepth - 1 << "\n";
        return 0;
    }

    std::vector<int> indices(Parents.size());
    std::iota(indices.begin(), indices.end(), 0);

    if (Count < 0 || Count >= static_cast<int>(Parents.size()))
        Count = static_cast<int>(Parents.size());

    std::mt19937 rng(static_cast<uint32_t>(Seed));
    std::shuffle(indices.begin(), indices.end(), rng);
    indices.resize(static_cast<size_t>(Count));

    int Cut = 0;
    for (int idx : indices)
    {
        QObject_GcTest* P = Parents[static_cast<size_t>(idx)];
        if (!P) continue;

        // Remove all links from parent -> depth target
        Cut += static_cast<int>(P->Children.size());
        P->Children.clear();
        P->ChildObject = nullptr;
    }

    std::cout << "[GcPerfTest] Cut " << Cut << " links at depth " << TargetDepth
              << " from " << Count << " parent nodes.\n";
    return Cut;
}

void QGcPerfTest::PrintDepthStats(int TargetDepth) const
{
    if (TargetDepth < 0 || TargetDepth >= static_cast<int>(Levels.size()))
    {
        std::cout << "[GcPerfTest] Invalid depth " << TargetDepth << "\n";
        return;
    }

    const auto& L = Levels[TargetDepth];
    size_t N = L.size();
    size_t minC = SIZE_MAX, maxC = 0, sumC = 0;

    for (auto* Node : L)
    {
        size_t c = Node ? Node->Children.size() : 0;
        minC = std::min(minC, c);
        maxC = std::max(maxC, c);
        sumC += c;
    }
    double avgC = N ? static_cast<double>(sumC) / static_cast<double>(N) : 0.0;

    std::cout << "[GcPerfTest] Depth " << TargetDepth
              << " nodes=" << N
              << " children(min/avg/max)=(" << (N ? minC : 0) << "/"
              << avgC << "/" << maxC << ")\n";
}

double QGcPerfTest::ForceGc()
{
    auto& GC = GcManager::Get();

    const auto t0 = std::chrono::high_resolution_clock::now();
    GC.Collect();
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "[GcPerfTest] Collect() took " << ms << " ms\n";
    return ms;
}
