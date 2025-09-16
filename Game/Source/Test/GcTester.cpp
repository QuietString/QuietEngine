#include "Test/GcTester.h"

#include <algorithm>
#include <numeric>
#include <chrono>
#include <iostream>
#include <queue>
#include <unordered_set>
#include <random>

#include "GarbageCollector.h"
#include "World.h"
#include "qmeta_runtime.h"

// ---------------- local helpers ----------------
namespace
{
    inline std::string TrimSpaces(std::string s)
    {
        s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
        return s;
    }

    // Extract inner from "std::vector<T*>" -> "T*"; returns empty if not vector
    inline std::string VectorElem(const std::string& Type)
    {
        if (Type.find("std::vector") == std::string::npos) return {};
        auto lt = Type.find('<');
        auto gt = Type.rfind('>');
        if (lt == std::string::npos || gt == std::string::npos || lt >= gt) return {};
        auto inner = TrimSpaces(Type.substr(lt + 1, gt - lt - 1));
        return inner;
    }

    // From "T*" -> "T"
    inline std::string PointeeName(const std::string& PtrType)
    {
        std::string s = TrimSpaces(PtrType);
        if (!s.empty() && s.back() == '*') s.pop_back();
        return s;
    }

    // Whether DeclTypeName is base-of-or-equal to ChildTi
    inline bool IsAssignableTo(const std::string& DeclTypeName, const qmeta::TypeInfo& ChildTi)
    {
        if (DeclTypeName.empty()) return false;
        // Fast path exact match
        if (DeclTypeName == ChildTi.name) return true;

        const qmeta::TypeInfo* B = ChildTi.base;
        while (B)
        {
            if (B->name == DeclTypeName) return true;
            B = B->base;
        }
        return false;
    }

    // Enumerate properties with control over local/parents
    template <class F>
    void EnumerateProps(const qmeta::TypeInfo& Ti, bool bLocal, bool bParents, F&& Fn)
    {
        if (bLocal)
        {
            for (auto& p : Ti.properties) Fn(p);
        }
        if (bParents)
        {
            const qmeta::TypeInfo* Base = Ti.base;
            while (Base)
            {
                for (auto& p : Base->properties) Fn(p);
                Base = Base->base;
            }
        }
    }
}

// ---------------- QGcTester core ----------------

void QGcTester::ClearGraph()
{
    AllNodes.clear();
    DepthLayers.clear();
}

QObject* QGcTester::MakeNode()
{
    // Default: round-robin; if you prefer random: create rng outside and use Factory.CreateRandom(Rng)
    if (Factory.GetPoolCount() == 0)
    {
        // Ensure at least QTestObject is available if compiled in your Game module.
        // You can call FactoryAddType("QTestObject") from console (after Register<T> in C++).
        // For safety, register QObject itself cannot be constructed; so early out.
        std::cout << "[GcTester] Factory has no registered types/pool.\n";
        return nullptr;
    }
    QObject* Node = Factory.CreateRoundRobin();
    if (Node) AllNodes.push_back(Node);
    return Node;
}

QObject* QGcTester::PickRandom(const std::vector<QObject*>& From, std::mt19937& Rng)
{
    if (From.empty()) return nullptr;
    std::uniform_int_distribution<size_t> Dist(0, From.size() - 1);
    return From[Dist(Rng)];
}

void QGcTester::LinkChild(QObject* Parent, QObject* Child, std::mt19937* RngOpt)
{
    if (!Parent || !Child) return;

    using qmeta::TypeInfo;
    using qmeta::MetaProperty;

    auto& GC = GarbageCollector::Get();
    const TypeInfo* Ti = GC.GetTypeInfo(Parent);
    const TypeInfo* ChildTi = GC.GetTypeInfo(Child);

    if (!Ti || !ChildTi)
    {
        std::cout << "[GcTester] Missing TypeInfo (parent or child)\n";
        return;
    }

    unsigned char* Base = GarbageCollector::BytePtr(Parent);

    auto TryAssignInList = [&](const std::vector<const MetaProperty*>& Props) -> bool
    {
        for (const MetaProperty* P : Props)
        {
            const std::string& T = P->type;
            const bool bVec = GarbageCollector::IsVectorOfPointer(T);
            const bool bRaw = GarbageCollector::IsPointerType(T);

            if (bUseVector && bVec)
            {
                // vector<T*>
                const std::string ElemPtr = VectorElem(T); // "T*"
                const std::string ElemName = PointeeName(ElemPtr); // "T"
                if (!IsAssignableTo(ElemName, *ChildTi)) continue;

                // Reinterpret as vector<QObject*>; we'll store QObject* uniformly.
                auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + P->offset);
                if (std::find(Vec->begin(), Vec->end(), Child) == Vec->end())
                {
                    Vec->push_back(Child);
                    return true;
                }
            }
            else if (!bUseVector && bRaw && !bVec) // ensure not vector
            {
                const std::string DeclName = PointeeName(T);
                if (!IsAssignableTo(DeclName, *ChildTi)) continue;

                auto** Slot = reinterpret_cast<QObject**>(Base + P->offset);
                if (*Slot == nullptr)
                {
                    *Slot = Child;
                    return true;
                }
            }
        }
        return false;
    };

    // Build candidate property lists honoring AssignMode
    std::vector<const MetaProperty*> Local, Parents;
    Local.reserve(Ti->properties.size());
    for (auto& p : Ti->properties) Local.push_back(&p);
    const qmeta::TypeInfo* B = Ti->base;
    while (B)
    {
        for (auto& p : B->properties) Parents.push_back(&p);
        B = B->base;
    }

    auto HasAssignable = [&](const std::vector<const MetaProperty*>& Props)->bool
    {
        for (const MetaProperty* P : Props)
        {
            const std::string& T = P->type;
            if (bUseVector && GarbageCollector::IsVectorOfPointer(T)) return true;
            if (!bUseVector && GarbageCollector::IsPointerType(T) && !GarbageCollector::IsVectorOfPointer(T))
            {
                auto** Slot = reinterpret_cast<QObject**>(Base + P->offset);
                if (*Slot == nullptr) return true;
            }
        }
        return false;
    };

    int Origin = 0; // 0 local, 1 parents
    if (AssignMode == 0) Origin = 0;
    else if (AssignMode == 1) Origin = 1;
    else
    {
        const bool bHasLocal = HasAssignable(Local);
        const bool bHasParents = HasAssignable(Parents);
        if (bHasLocal && bHasParents)
        {
            if (!RngOpt) { static std::mt19937 Tmp(1337); RngOpt = &Tmp; }
            std::uniform_int_distribution<int> Pick01(0, 1);
            Origin = Pick01(*RngOpt);
        }
        else Origin = bHasParents ? 1 : 0;
    }

    bool bLinked = false;
    if (Origin == 0)
    {
        bLinked = TryAssignInList(Local);
        if (!bLinked) bLinked = TryAssignInList(Parents);
    }
    else
    {
        bLinked = TryAssignInList(Parents);
        if (!bLinked) bLinked = TryAssignInList(Local);
    }

    if (!bLinked)
    {
        std::cout << "[GcTester] No suitable slot for link\n";
    }
}

void QGcTester::GatherChildren(QObject* Node, std::vector<QObject*>& Out) const
{
    Out.clear();
    if (!Node) return;

    auto& GC = GarbageCollector::Get();
    const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Node);
    if (!Ti) return;

    const unsigned char* Base = GarbageCollector::BytePtr(const_cast<QObject*>(Node));

    auto VisitProp = [&](const qmeta::MetaProperty& P)
    {
        if (bUseVector && GarbageCollector::IsVectorOfPointer(P.type))
        {
            auto* Vec = reinterpret_cast<const std::vector<QObject*>*>(Base + P.offset);
            for (QObject* C : *Vec)
            {
                if (C && GC.IsManaged(C)) Out.push_back(C);
            }
        }
        else if (!bUseVector && GarbageCollector::IsPointerType(P.type) && !GarbageCollector::IsVectorOfPointer(P.type))
        {
            auto* Slot = reinterpret_cast<QObject* const*>(Base + P.offset);
            if (Slot && *Slot && GC.IsManaged(*Slot)) Out.push_back(*Slot);
        }
    };

    // Include bases as well for traversal
    Ti->ForEachProperty(VisitProp);
}

size_t QGcTester::GetChildCount(const QObject* Node) const
{
    if (!Node) return 0;
    std::vector<QObject*> Tmp;
    GatherChildren(const_cast<QObject*>(Node), Tmp);
    return Tmp.size();
}

void QGcTester::CollectEdgesReachable(std::vector<EdgeRef>& Out) const
{
    Out.clear();
    std::unordered_set<const QObject*> Vis;
    std::queue<QObject*> Q;

    for (QObject* R : Roots)
    {
        if (R && !Vis.count(R)) { Vis.insert(R); Q.push(R); }
    }

    auto& GC = GarbageCollector::Get();

    while (!Q.empty())
    {
        QObject* U = Q.front(); Q.pop();
        const qmeta::TypeInfo* Ti = GC.GetTypeInfo(U);
        if (!Ti) continue;

        unsigned char* Base = GarbageCollector::BytePtr(U);

        // Enumerate edges out of U and enqueue reachable nodes
        Ti->ForEachProperty([&](const qmeta::MetaProperty& P)
        {
            if (bUseVector && GarbageCollector::IsVectorOfPointer(P.type))
            {
                auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + P.offset);
                for (size_t i = 0; i < Vec->size(); ++i)
                {
                    QObject* V = (*Vec)[i];
                    if (V)
                    {
                        Out.push_back({ U, V, P.name, i, true });
                        if (!Vis.count(V)) { Vis.insert(V); Q.push(V); }
                    }
                }
            }
            else if (!bUseVector && GarbageCollector::IsPointerType(P.type) && !GarbageCollector::IsVectorOfPointer(P.type))
            {
                auto** Slot = reinterpret_cast<QObject**>(Base + P.offset);
                if (Slot && *Slot)
                {
                    QObject* V = *Slot;
                    Out.push_back({ U, V, P.name, (size_t)-1, false });
                    if (!Vis.count(V)) { Vis.insert(V); Q.push(V); }
                }
            }
        });
    }
}

bool QGcTester::RemoveEdge(QObject* Parent, QObject* Child)
{
    if (!Parent) return false;

    auto& GC = GarbageCollector::Get();
    const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Parent);
    if (!Ti) return false;

    unsigned char* Base = GarbageCollector::BytePtr(Parent);
    bool bRemoved = false;

    Ti->ForEachProperty([&](const qmeta::MetaProperty& P)
    {
        if (bRemoved) return;

        if (bUseVector && GarbageCollector::IsVectorOfPointer(P.type))
        {
            auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + P.offset);
            for (size_t i = 0; i < Vec->size(); ++i)
            {
                if ((*Vec)[i] == Child)
                {
                    (*Vec)[i] = Vec->back();
                    Vec->pop_back();
                    bRemoved = true;
                    return;
                }
            }
        }
        else if (!bUseVector && GarbageCollector::IsPointerType(P.type) && !GarbageCollector::IsVectorOfPointer(P.type))
        {
            auto** Slot = reinterpret_cast<QObject**>(Base + P.offset);
            if (Slot && *Slot == Child)
            {
                *Slot = nullptr;
                bRemoved = true;
                return;
            }
        }
    });

    return bRemoved;
}

// ---------------- Layers & patterns ----------------

void QGcTester::BuildLayers(QObject* Head, bool bFromRootsOnly)
{
    DepthLayers.clear();

    std::vector<QObject*> Starts;
    if (bFromRootsOnly || !Head)
    {
        for (QObject* r : Roots) if (r) Starts.push_back(r);
    }
    else
    {
        Starts.push_back(Head);
    }

    std::unordered_set<QObject*> Vis;
    std::queue<std::pair<QObject*, int>> Q;
    for (QObject* s : Starts)
    {
        if (!s || Vis.count(s)) continue;
        Vis.insert(s);
        Q.emplace(s, 0);
    }

    auto EnsureDepth = [&](int d)
    {
        if (d >= (int)DepthLayers.size()) DepthLayers.resize((size_t)d + 1);
    };

    while (!Q.empty())
    {
        auto [u, d] = Q.front(); Q.pop();
        EnsureDepth(d);
        DepthLayers[(size_t)d].push_back(u);

        std::vector<QObject*> Children;
        GatherChildren(u, Children);
        for (QObject* v : Children)
        {
            if (!v || Vis.count(v)) continue;
            Vis.insert(v);
            Q.emplace(v, d + 1);
        }
    }
}

// ---------------- Public (console) API ----------------

void QGcTester::PatternChain(int Length, int Seed)
{
    if (Length <= 0)
    {
        std::cout << "[GcTester] length>0 required\n";
        return;
    }

    if (!GetWorld())
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    QObject* Head = MakeNode();
    if (!Head) return;
    Roots.push_back(Head);

    QObject* Cur = Head;
    for (int i = 1; i < Length; ++i)
    {
        QObject* Nxt = MakeNode();
        if (!Nxt) break;
        LinkChild(Cur, Nxt, &Rng);
        Cur = Nxt;
    }

    BuildLayers(Head);
    std::cout << "[GcTester] Chain built: length=" << Length << " total=" << AllNodes.size() << "\n";
}

void QGcTester::PatternGrid(int W, int H, int Seed)
{
    if (W <= 0 || H <= 0)
    {
        std::cout << "[GcTester] w>0, h>0\n";
        return;
    }
    if (!GetWorld())
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    // Make nodes
    std::vector<std::vector<QObject*>> Grid((size_t)H, std::vector<QObject*>((size_t)W, nullptr));
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            Grid[(size_t)y][(size_t)x] = MakeNode();
        }
    }

    if (Grid[0][0]) Roots.push_back(Grid[0][0]);

    // 4-neighborhood links
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            QObject* P = Grid[(size_t)y][(size_t)x];
            if (!P) continue;
            if (x + 1 < W) LinkChild(P, Grid[(size_t)y][(size_t)(x + 1)], &Rng);
            if (y + 1 < H) LinkChild(P, Grid[(size_t)(y + 1)][(size_t)x], &Rng);
        }
    }

    BuildLayers(Grid[0][0]);
    std::cout << "[GcTester] Grid built: " << W << "x" << H << " total=" << AllNodes.size() << "\n";
}

void QGcTester::PatternRandom(int Nodes, int AvgOut, int Seed)
{
    if (Nodes <= 0 || AvgOut < 0)
    {
        std::cout << "[GcTester] nodes>0, avgOut>=0\n";
        return;
    }
    if (!GetWorld())
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    AllNodes.reserve((size_t)Nodes);
    for (int i = 0; i < Nodes; ++i) MakeNode();

    if (AllNodes.empty()) { std::cout << "[GcTester] No nodes created.\n"; return; }

    QObject* Head = AllNodes.front();
    Roots.push_back(Head);

    std::uniform_int_distribution<int> Pick(0, Nodes - 1);

    for (int i = 0; i < Nodes; ++i)
    {
        QObject* Parent = AllNodes[(size_t)i];
        for (int k = 0; k < AvgOut; ++k)
        {
            QObject* Child = AllNodes[(size_t)Pick(Rng)];
            if (Child == Parent) continue;
            LinkChild(Parent, Child, &Rng);
        }
    }

    BuildLayers(Head);
    std::cout << "[GcTester] Random graph: nodes=" << Nodes << " avgOut=" << AvgOut
              << " total=" << AllNodes.size() << "\n";
}

void QGcTester::PatternRings(int Rings, int RingSize, int Seed)
{
    if (Rings <= 0 || RingSize <= 0)
    {
        std::cout << "[GcTester] rings>0, ringSize>0\n";
        return;
    }
    if (!GetWorld())
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    QObject* PrevRingFirst = nullptr;
    for (int r = 0; r < Rings; ++r)
    {
        QObject* First = MakeNode();
        QObject* Cur = First;
        for (int i = 1; i < RingSize; ++i)
        {
            QObject* Nxt = MakeNode();
            LinkChild(Cur, Nxt, &Rng);
            Cur = Nxt;
        }
        // close the ring
        LinkChild(Cur, First, &Rng);

        if (r == 0) Roots.push_back(First);
        if (PrevRingFirst) LinkChild(PrevRingFirst, First, &Rng);
        PrevRingFirst = First;
    }

    BuildLayers(Roots.empty() ? nullptr : Roots.front());
    std::cout << "[GcTester] Rings built: rings=" << Rings << " ringSize=" << RingSize
              << " total=" << AllNodes.size() << "\n";
}

int QGcTester::BreakAtDepth(int TargetDepth, int Count, int Seed)
{
    if (TargetDepth <= 0) { std::cout << "[GcTester] TargetDepth must be > 0\n"; return 0; }
    if (DepthLayers.empty()) { std::cout << "[GcTester] Depth layer is empty.\n"; return 0; }
    if (TargetDepth >= (int)DepthLayers.size()) { std::cout << "[GcTester] Invalid depth\n"; return 0; }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));
    std::uniform_int_distribution<int> Flip01(0, 1);

    auto& Layer = DepthLayers[(size_t)TargetDepth];
    int Removed = 0;

    for (QObject* P : Layer)
    {
        if (!P) continue;

        auto& GC = GarbageCollector::Get();
        const qmeta::TypeInfo* Ti = GC.GetTypeInfo(P);
        if (!Ti) continue;

        unsigned char* Base = GarbageCollector::BytePtr(P);

        // Remove up to Count out-edges of the selected kind
        int Left = Count;

        Ti->ForEachProperty([&](const qmeta::MetaProperty& Meta)
        {
            if (Left <= 0) return;

            if (bUseVector && GarbageCollector::IsVectorOfPointer(Meta.type))
            {
                auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + Meta.offset);
                for (size_t i = 0; i < Vec->size() && Left > 0; )
                {
                    if ((*Vec)[i] && Flip01(Rng)) // simple 50% within the Count budget
                    {
                        (*Vec)[i] = Vec->back();
                        Vec->pop_back();
                        --Left;
                        ++Removed;
                    }
                    else ++i;
                }
            }
            else if (!bUseVector && GarbageCollector::IsPointerType(Meta.type) && !GarbageCollector::IsVectorOfPointer(Meta.type))
            {
                auto** Slot = reinterpret_cast<QObject**>(Base + Meta.offset);
                if (Slot && *Slot && Flip01(Rng) && Left > 0)
                {
                    *Slot = nullptr;
                    --Left;
                    ++Removed;
                }
            }
        });
    }

    BuildLayers(nullptr, true);
    std::cout << "[GcTester] BreakAtDepth removed=" << Removed << " at depth=" << TargetDepth << "\n";
    return Removed;
}

int QGcTester::BreakPercent(double Percent, int Depth, int Seed, bool /*bOnlyRoots*/)
{
    Percent = std::clamp(Percent, 0.0, 100.0);
    std::mt19937 Rng(static_cast<uint32_t>(Seed));
    auto Roll = [&](std::mt19937& G) -> bool
    {
        std::uniform_real_distribution<double> Dist(0.0, 100.0);
        return Dist(G) < Percent;
    };

    std::vector<QObject*> Targets;
    if (Depth < 0)
    {
        // All reachable from roots
        std::unordered_set<QObject*> Vis;
        std::queue<QObject*> Q;
        for (QObject* r : Roots) if (r && !Vis.count(r)) { Vis.insert(r); Q.push(r); }
        while (!Q.empty())
        {
            QObject* u = Q.front(); Q.pop();
            Targets.push_back(u);
            std::vector<QObject*> Ch; GatherChildren(u, Ch);
            for (QObject* v : Ch)
            {
                if (v && !Vis.count(v)) { Vis.insert(v); Q.push(v); }
            }
        }
    }
    else
    {
        if (DepthLayers.empty()) BuildLayers(nullptr, true);
        if (Depth >= (int)DepthLayers.size()) { std::cout << "[GcTester] Invalid depth\n"; return 0; }
        Targets = DepthLayers[(size_t)Depth];
    }

    int Cut = 0;
    for (QObject* P : Targets)
    {
        if (!P) continue;

        auto& GC = GarbageCollector::Get();
        const qmeta::TypeInfo* Ti = GC.GetTypeInfo(P);
        if (!Ti) continue;
        unsigned char* Base = GarbageCollector::BytePtr(P);

        Ti->ForEachProperty([&](const qmeta::MetaProperty& Meta)
        {
            if (bUseVector && GarbageCollector::IsVectorOfPointer(Meta.type))
            {
                auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + Meta.offset);
                for (size_t i = 0; i < Vec->size(); )
                {
                    if (Roll(Rng))
                    {
                        (*Vec)[i] = Vec->back();
                        Vec->pop_back();
                        ++Cut;
                    }
                    else ++i;
                }
            }
            else if (!bUseVector && GarbageCollector::IsPointerType(Meta.type) && !GarbageCollector::IsVectorOfPointer(Meta.type))
            {
                auto** Slot = reinterpret_cast<QObject**>(Base + Meta.offset);
                if (Slot && *Slot && Roll(Rng))
                {
                    *Slot = nullptr;
                    ++Cut;
                }
            }
        });
    }

    BuildLayers(nullptr, true);
    std::cout << "[GcTester] BreakPercent " << Percent << "% removed=" << Cut << "\n";
    return Cut;
}

void QGcTester::BreakRandomEdges(int Count, int Seed)
{
    if (Count <= 0) return;

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    std::vector<EdgeRef> Edges;
    CollectEdgesReachable(Edges);
    if (Edges.empty()) { std::cout << "[GcTester] No edges.\n"; return; }

    std::shuffle(Edges.begin(), Edges.end(), Rng);
    int Cut = 0;
    for (auto& E : Edges)
    {
        if (Cut >= Count) break;
        if (RemoveEdge(E.Parent, E.Child)) ++Cut;
    }

    BuildLayers(nullptr, true);
    std::cout << "[GcTester] BreakRandomEdges removed=" << Cut << "\n";
}

void QGcTester::DetachRoots(int Count, double Percent)
{
    int Removed = 0;

    if (Count > 0)
    {
        int n = std::min<int>((int)Roots.size(), Count);
        Roots.erase(Roots.begin(), Roots.begin() + n);
        Removed += n;
    }
    else if (Percent > 0.0)
    {
        int PickedNum = (int)std::round((Percent / 100.0) * (double)Roots.size());
        PickedNum = std::min<int>((int)Roots.size(), std::max(0, PickedNum));
        Roots.erase(Roots.begin(), Roots.begin() + PickedNum);
        Removed += PickedNum;
    }

    BuildLayers(nullptr, true);
    std::cout << "[GcTester] DetachRoots removed=" << Removed << " remaining=" << Roots.size() << "\n";
}

void QGcTester::ClearAll(bool bSilent)
{
    ClearGraph();
    Roots.clear();
    GarbageCollector::Get().Collect(true);
    if (!bSilent) std::cout << "[GcTester] Cleared all test objects.\n";
}

// ------------- batch test -------------
void QGcTester::RepeatRandomAndCollect(int NumSteps, int NumNodes, int NumBranches)
{
    if (NumSteps <= 0 || NumNodes <= 0 || NumBranches < 0)
    {
        std::cout << "[GcTester] RepeatRandomAndCollect: invalid args\n";
        return;
    }

    for (int i = 0; i < NumSteps; ++i)
    {
        PatternRandom(NumNodes, NumBranches, i);
        GarbageCollector::Get().Collect(false);
        ClearAll(true);
    }

    std::cout << "[GcTester] RepeatRandomAndCollect done: steps=" << NumSteps << "\n";
}

// ------------- config -------------
void QGcTester::SetAssignMode(int InMode)
{
    if (InMode < 0 || InMode > 2)
    {
        std::cout << "[GcTester] Invalid AssignMode " << InMode << "\n";
        return;
    }
    AssignMode = InMode;
    const char* ModeNames[] = { "OwnedOnly", "ParentsOnly", "Random" };
    std::cout << "[GcTester] AssignMode: " << ModeNames[InMode] << "\n";
}

void QGcTester::SetUseVector(bool bUse)
{
    bUseVector = bUse;
    std::cout << "[GcTester] bUseVector: " << (bUseVector ? "true" : "false") << "\n";
}

// ------------- factory helpers -------------
void QGcTester::FactoryClear()
{
    Factory.Clear();
    std::cout << "[GcTester] Factory cleared.\n";
}

void QGcTester::FactoryAddType(const std::string& TypeName)
{
    // TypeName must have been registered from C++ via Factory.Register<T>()
    if (!Factory.HasType(TypeName))
    {
        std::cout << "[GcTester] Factory has no registered creator for type: " << TypeName << "\n";
        return;
    }

    // Extend pool if not present
    auto Pool = Factory.GetPool();
    if (std::find(Pool.begin(), Pool.end(), TypeName) == Pool.end())
    {
        Pool.push_back(TypeName);
        Factory.SetTypePool(Pool);
    }
    std::cout << "[GcTester] FactoryAddType: " << TypeName << "\n";
}

void QGcTester::FactoryUseTypes(const std::vector<std::string>& TypeNames)
{
    Factory.SetTypePool(TypeNames);
    std::cout << "[GcTester] FactoryUseTypes: ";
    for (auto& n : TypeNames) std::cout << n << " ";
    std::cout << "\n";
}
