#include "Test/GcPerfTest.h"

#include <algorithm>
#include <numeric>
#include <chrono>
#include <iostream>
#include <queue>
#include <unordered_set>
#include <unordered_map>

#include "EngineGlobals.h"
#include "GarbageCollector.h"
#include "Object_GcTest.h"
#include "World.h"

using namespace QGC;

void QGcPerfTest::ClearGraph()
{
    Roots.clear();
    AllNodes.clear();
    Levels.clear();
}

QObject_GcTest* QGcPerfTest::MakeNode()
{
    auto* n = NewObject<QObject_GcTest>();
    AllNodes.push_back(n);
    return n;
}

void QGcPerfTest::LinkChild(QObject_GcTest* Parent, QObject_GcTest* Child)
{
    if (!Parent || !Child) return;

    Parent->Children.push_back(Child);
}

QObject_GcTest* QGcPerfTest::PickRandom(const std::vector<QObject_GcTest*>& From, std::mt19937& Rng)
{
    if (From.empty()) return nullptr;
    std::uniform_int_distribution<size_t> dist(0, From.size() - 1);
    return From[dist(Rng)];
}

void QGcPerfTest::RebuildLevelsFromRoots()
{
    Levels.clear();
    std::unordered_map<QObject_GcTest*, int> depth;
    std::queue<QObject_GcTest*> q;
    for (QObject* r : Roots)
    {
        auto* gr = static_cast<QObject_GcTest*>(r);
        if (!gr) continue;
        depth[gr] = 0;
        q.push(gr);
    }
    int maxD = 0;
    while (!q.empty())
    {
        auto* u = q.front(); q.pop();
        int d = depth[u];
        maxD = std::max(maxD, d);
        for (auto* v : u->Children)
        {
            if (!v) continue;
            if (depth.find(v) == depth.end())
            {
                depth[v] = d + 1;
                q.push(v);
            }
        }
    }
    Levels.resize(static_cast<size_t>(maxD) + 1);
    for (auto& kv : depth)
    {
        QObject_GcTest* node = kv.first;
        int d = kv.second;
        if (d >= 0) Levels[(size_t)d].push_back(node);
    }
}

std::vector<QObject_GcTest*> QGcPerfTest::GetReachable() const
{
    std::vector<QObject_GcTest*> Out;
    std::unordered_set<QObject_GcTest*> Vis;
    std::queue<QObject_GcTest*> q;
    for (QObject* r : Roots)
    {
        auto* gr = static_cast<QObject_GcTest*>(r);
        if (!gr || Vis.count(gr)) continue;
        Vis.insert(gr);
        q.push(gr);
    }
    while (!q.empty())
    {
        auto* u = q.front(); q.pop();
        Out.push_back(u);
        for (auto* v : u->Children)
        {
            if (v && !Vis.count(v))
            {
                Vis.insert(v);
                q.push(v);
            }
        }
    }
    return Out;
}

void QGcPerfTest::CollectEdgesReachable(std::vector<EdgeRef>& Out) const
{
    Out.clear();
    std::unordered_set<QObject_GcTest*> vis;
    std::queue<QObject_GcTest*> q;
    for (QObject* r : Roots)
    {
        auto* gr = static_cast<QObject_GcTest*>(r);
        if (!gr || vis.count(gr)) continue;
        vis.insert(gr);
        q.push(gr);
    }
    while (!q.empty())
    {
        auto* u = q.front(); q.pop();
        // vector children
        for (size_t i = 0; i < u->Children.size(); ++i)
        {
            QObject_GcTest* v = u->Children[i];
            if (v) Out.push_back({u, v, i});
            if (v && !vis.count(v)) { vis.insert(v); q.push(v); }
        }
    }
}

bool QGcPerfTest::RemoveEdge(QObject_GcTest* Parent, QObject_GcTest* Child)
{
    if (!Parent) return false;
    bool removed = false;
    // remove from vector
    auto& vec = Parent->Children;
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (vec[i] == Child)
        {
            vec[i] = vec.back();
            vec.pop_back();
            removed = true;
            break;
        }
    }

    return removed;
}

// ---------------- Tree builder (legacy) ----------------

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
    if (!World) { std::cout << "[GcPerfTest] World not found.\n"; return; }

    Levels.resize(static_cast<size_t>(Depth) + 1);

    for (int i = 0; i < RootCount; ++i)
    {
        auto* R = MakeNode();
        Levels[0].push_back(R);
        Roots.push_back(R);
    }

    for (int d = 0; d < Depth; ++d)
    {
        const auto& Cur = Levels[(size_t)d];
        auto& Next = Levels[(size_t)(d + 1)];
        for (QObject_GcTest* Parent : Cur)
        {
            for (int k = 0; k < Branching; ++k)
            {
                auto* C = MakeNode();
                LinkChild(Parent, C);
                Next.push_back(C);
            }
        }
    }

    World->Objects.push_back(this);
    std::cout << "[GcPerfTest] Tree built: roots=" << RootCount
              << " depth=" << Depth << " branching=" << Branching
              << " total=" << AllNodes.size() << "\n";
}

// ---------------- Pattern builders ----------------

void QGcPerfTest::PatternChain(int Length, int Seed)
{
    ClearGraph();
    if (Length <= 0) { std::cout << "[GcPerfTest] length>0 required\n"; return; }
    QWorld* World = GetWorld(); if (!World) { std::cout << "World not found.\n"; return; }

    auto* head = MakeNode();
    Roots.push_back(head);
    auto* cur = head;
    for (int i = 1; i < Length; ++i)
    {
        auto* nxt = MakeNode();
        LinkChild(cur, nxt);
        cur = nxt;
    }
    World->Objects.push_back(this);
    RebuildLevelsFromRoots();
    std::cout << "[GcPerfTest] Chain built: length=" << Length
              << " total=" << AllNodes.size() << "\n";
}

void QGcPerfTest::PatternGrid(int Width, int Height, int Seed)
{
    ClearGraph();
    if (Width <= 0 || Height <= 0) { std::cout << "[GcPerfTest] width/height>0 required\n"; return; }
    QWorld* World = GetWorld(); if (!World) { std::cout << "World not found.\n"; return; }

    std::vector<std::vector<QObject_GcTest*>> grid(Height, std::vector<QObject_GcTest*>(Width, nullptr));
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            grid[y][x] = MakeNode();

    // root: top-left
    Roots.push_back(grid[0][0]);

    // edges: right and down
    for (int y = 0; y < Height; ++y)
    for (int x = 0; x < Width; ++x)
    {
        if (x + 1 < Width)  LinkChild(grid[y][x], grid[y][x+1]);
        if (y + 1 < Height) LinkChild(grid[y][x], grid[y+1][x]);
    }
    World->Objects.push_back(this);
    RebuildLevelsFromRoots();
    std::cout << "[GcPerfTest] Grid built: " << Width << "x" << Height
              << " total=" << AllNodes.size() << "\n";
}

void QGcPerfTest::PatternRandom(int Nodes, int AvgOut, int Seed)
{
    ClearGraph();
    if (Nodes <= 0 || AvgOut < 0) { std::cout << "[GcPerfTest] nodes>0, avgOut>=0\n"; return; }
    QWorld* World = GetWorld(); if (!World) { std::cout << "World not found.\n"; return; }

    AllNodes.reserve((size_t)Nodes);
    for (int i = 0; i < Nodes; ++i) MakeNode();

    Roots.push_back(AllNodes.front());

    std::mt19937 rng(static_cast<uint32_t>(Seed));
    std::uniform_int_distribution<int> pick(0, Nodes - 1);

    for (int i = 0; i < Nodes; ++i)
    {
        auto* p = AllNodes[(size_t)i];
        int deg = AvgOut;
        for (int k = 0; k < deg; ++k)
        {
            auto* c = AllNodes[(size_t)pick(rng)];
            if (c == p) continue;
            LinkChild(p, c);
        }
    }
    World->Objects.push_back(this);
    RebuildLevelsFromRoots();
    std::cout << "[GcPerfTest] Random graph: nodes=" << Nodes
              << " avgOut=" << AvgOut << " total=" << AllNodes.size() << "\n";
}

void QGcPerfTest::PatternRings(int Rings, int RingSize, int Seed)
{
    ClearGraph();
    
    if (Rings <= 0 || RingSize <= 0)
    {
        std::cout << "[GcPerfTest] rings>0, ringSize>0\n";
        return;
    }
    
    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    QObject_GcTest* PrevRingFirst = nullptr;
    for (int r = 0; r < Rings; ++r)
    {
        std::vector<QObject_GcTest*> Ring;
        Ring.reserve((size_t)RingSize);
        for (int i = 0; i < RingSize; ++i) Ring.push_back(MakeNode());

        // make cycle
        for (int i = 0; i < RingSize; ++i)
            LinkChild(Ring[(size_t)i], Ring[(size_t)((i+1)%RingSize)]);

        // connect previous ring to this ring (one bridge)
        if (PrevRingFirst) LinkChild(PrevRingFirst, Ring[0]);

        if (r == 0) Roots.push_back(Ring[0]);
        PrevRingFirst = Ring[0];
    }
    World->Objects.push_back(this);
    RebuildLevelsFromRoots();
    std::cout << "[GcPerfTest] Rings: rings=" << Rings << " ringSize=" << RingSize << " total=" << AllNodes.size() << "\n";
}

void QGcPerfTest::PatternDiamond(int Layers, int Breadth, int Seed)
{
    ClearGraph();
    
    if (Layers <= 1 || Breadth <= 1)
    {
        std::cout << "[GcPerfTest] layers>1, breadth>1\n";
        return;
    }
    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    std::vector<std::vector<QObject_GcTest*>> L;
    L.resize((size_t)Layers);
    // top
    L[0].push_back(MakeNode());
    Roots.push_back(L[0][0]);

    int up = Layers / 2;
    // expand
    for (int d = 0; d < up; ++d)
    {
        for (auto* p : L[(size_t)d])
        {
            for (int i = 0; i < Breadth; ++i)
            {
                auto* c = MakeNode();
                LinkChild(p, c);
                L[(size_t)(d+1)].push_back(c);
            }
        }
    }
    // merge
    for (int d = up; d < Layers - 1; ++d)
    {
        auto& cur = L[(size_t)d];
        auto& nxt = L[(size_t)(d+1)];
        // group current layer nodes into blocks of 'breadth' and share a single child
        for (size_t i = 0; i < cur.size(); i += (size_t)Breadth)
        {
            auto* shared = MakeNode();
            nxt.push_back(shared);
            for (size_t j = i; j < std::min(cur.size(), i + (size_t)Breadth); ++j)
                LinkChild(cur[j], shared);
        }
    }
    World->Objects.push_back(this);
    RebuildLevelsFromRoots();
    std::cout << "[GcPerfTest] Diamond: layers=" << Layers << " breadth=" << Breadth << " total=" << AllNodes.size() << "\n";
}

// ---------------- Break / mutate ----------------

int QGcPerfTest::BreakAtDepth(int TargetDepth, int Count, int Seed)
{
    if (TargetDepth <= 0)
    {
        std::cout << "[GcPerfTest] TargetDepth must be > 0\n";
        return 0;
    }
    if (Levels.empty()) RebuildLevelsFromRoots();
    if (TargetDepth >= (int)Levels.size())
    {
        std::cout << "[GcPerfTest] Invalid TargetDepth " << TargetDepth << "\n";
        return 0;
    }
    auto& Parents = Levels[(size_t)(TargetDepth - 1)];
    if (Parents.empty())
    {
        std::cout << "[GcPerfTest] No parents at depth " << TargetDepth - 1 << "\n";
        return 0;
    }
    std::vector<int> idx(Parents.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(static_cast<uint32_t>(Seed));
    std::shuffle(idx.begin(), idx.end(), rng);

    if (Count < 0 || Count > (int)Parents.size()) Count = (int)Parents.size();
    idx.resize((size_t)Count);

    int cut = 0;
    for (int i : idx)
    {
        auto* p = Parents[(size_t)i];
        cut += (int)p->Children.size();
        p->Children.clear();
    }
    std::cout << "[GcPerfTest] Cut " << cut << " links at depth " << TargetDepth
              << " from " << Count << " parents.\n";
    return cut;
}

int QGcPerfTest::BreakPercent(double Percent, int Depth, int Seed)
{
    Percent = std::clamp(Percent, 0.0, 100.0);
    if (Percent <= 0.0) return 0;

    if (Levels.empty()) RebuildLevelsFromRoots();
    std::mt19937 rng(static_cast<uint32_t>(Seed));
    std::uniform_real_distribution<double> roll(0.0, 100.0);

    std::vector<QObject_GcTest*> targets;
    if (Depth < 0)
    {
        auto reach = GetReachable();
        targets = std::move(reach);
    }
    else
    {
        if (Depth >= (int)Levels.size()) { std::cout << "[GcPerfTest] Invalid depth\n"; return 0; }
        targets = Levels[(size_t)Depth];
    }

    int cut = 0;
    for (auto* p : targets)
    {
        // vector children
        auto& vec = p->Children;
        for (size_t i = 0; i < vec.size(); )
        {
            if (roll(rng) < Percent)
            {
                vec[i] = vec.back();
                vec.pop_back();
                ++cut;
            }
            else ++i;
        }
    }
    std::cout << "[GcPerfTest] BreakPercent depth=" << Depth
              << " percent=" << Percent << " cut=" << cut << "\n";
    return cut;
}

int QGcPerfTest::BreakRandomEdges(int EdgeCount, int Seed)
{
    if (EdgeCount <= 0) return 0;
    std::vector<EdgeRef> edges;
    CollectEdgesReachable(edges);
    if (edges.empty()) return 0;

    std::mt19937 rng(static_cast<uint32_t>(Seed));
    std::shuffle(edges.begin(), edges.end(), rng);
    if ((int)edges.size() > EdgeCount) edges.resize((size_t)EdgeCount);

    int cut = 0;
    for (auto& e : edges)
        if (RemoveEdge(e.Parent, e.Child)) ++cut;

    std::cout << "[GcPerfTest] BreakRandomEdges cut=" << cut << "\n";
    return cut;
}

int QGcPerfTest::DetachRoots(int Count, double Percentage)
{
    int removed = 0;
    if (!Roots.empty())
    {
        if (Count > 0)
        {
            int n = std::min<int>((int)Roots.size(), Count);
            Roots.erase(Roots.begin(), Roots.begin() + n);
            removed += n;
        }
        else if (Percentage > 0.0)
        {
            double pct = std::clamp(Percentage, 0.0, 100.0);
            int n = (int)std::round((pct / 100.0) * (double)Roots.size());
            n = std::min<int>((int)Roots.size(), std::max(0, n));
            Roots.erase(Roots.begin(), Roots.begin() + n);
            removed += n;
        }
    }
    RebuildLevelsFromRoots();
    std::cout << "[GcPerfTest] DetachRoots removed=" << removed
              << " remainingRoots=" << Roots.size() << "\n";
    return removed;
}

// ---------------- Stats / measure / GC ----------------

void QGcPerfTest::PrintDepthStats(int TargetDepth) const
{
    if (TargetDepth < 0 || TargetDepth >= (int)Levels.size())
    {
        std::cout << "[GcPerfTest] Invalid depth " << TargetDepth << "\n";
        return;
    }
    const auto& L = Levels[(size_t)TargetDepth];
    size_t N = L.size();
    size_t minC = SIZE_MAX, maxC = 0, sumC = 0;

    for (auto* Node : L)
    {
        size_t c = Node ? Node->Children.size() : 0;
        minC = std::min(minC, c);
        maxC = std::max(maxC, c);
        sumC += c;
    }
    double avgC = N ? (double)sumC / (double)N : 0.0;
    std::cout << "[GcPerfTest] Depth " << TargetDepth
              << " nodes=" << N
              << " children(min/avg/max)=(" << (N ? minC : 0) << "/"
              << avgC << "/" << maxC << ")\n";
}

void QGcPerfTest::MeasureGc(int Repeats)
{
    if (Repeats <= 0) { std::cout << "[GcPerfTest] repeats>0 required\n"; return; }
    auto& GC = GcManager::Get();
    double minv = 1e100, maxv = -1.0, sum = 0.0;

    for (int i = 0; i < Repeats; ++i)
    {
        const auto T0 = std::chrono::high_resolution_clock::now();
        GC.Collect();
        const auto T1 = std::chrono::high_resolution_clock::now();
        const double Ms = std::chrono::duration<double, std::milli>(T1 - T0).count();
        minv = std::min(minv, Ms);
        maxv = std::max(maxv, Ms);
        sum += Ms;
    }
    std::cout << "[GcPerfTest] MeasureGc repeats=" << Repeats
              << " avg=" << (sum / Repeats)
              << " min=" << minv
              << " max=" << maxv << " ms\n";
}

// ---------------- Churn ----------------

void QGcPerfTest::Churn(int Steps, int AllocPerStep, double BreakPct, int GcEveryN, int Seed)
{
    if (Steps <= 0 || AllocPerStep < 0) { std::cout << "[GcPerfTest] invalid params\n"; return; }
    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    for (int s = 1; s <= Steps; ++s)
    {
        // 1) allocate and attach to random reachable parents
        auto reach = GetReachable();
        if (reach.empty() && !Roots.empty())
        {
            // ensure at least root exists
            auto* r = static_cast<QObject_GcTest*>(Roots[0]);
            reach.push_back(r);
        }
        for (int i = 0; i < AllocPerStep; ++i)
        {
            auto* p = PickRandom(reach, Rng);
            auto* n = MakeNode();
            if (p) LinkChild(p, n);
        }

        // 2) random break by percent across all depths
        if (BreakPct > 0.0) BreakPercent(BreakPct, -1, Rng());

        // 3) optional GC
        if (GcEveryN > 0 && (s % GcEveryN == 0)) GcManager::Get().Collect();
    }
    RebuildLevelsFromRoots();
    std::cout << "[GcPerfTest] Churn done: steps=" << Steps
              << " alloc/step=" << AllocPerStep
              << " breakPct=" << BreakPct
              << " gcEveryN=" << GcEveryN << "\n";
}
