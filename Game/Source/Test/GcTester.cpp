#include "Test/GcTester.h"

#include <algorithm>
#include <numeric>
#include <chrono>
#include <iostream>
#include <queue>
#include <unordered_set>
#include <unordered_map>

#include "EngineGlobals.h"
#include "GarbageCollector.h"
#include "TestObject.h"
#include "World.h"

void QGcTester::ClearGraph()
{
    Roots.clear();
    AllNodes.clear();
    DepthLayers.clear();
}

QTestObject* QGcTester::MakeNode()
{
    auto* Node = NewObject<QTestObject>();
    AllNodes.push_back(Node);
    return Node;
}

void QGcTester::LinkChild(QTestObject* Parent, QTestObject* Child, std::mt19937* RngOpt)
{
    if (!Parent || !Child) return;

    using qmeta::TypeInfo;
    using qmeta::MetaProperty;
    auto& GC = GarbageCollector::Get();

    const TypeInfo* Ti = GC.GetTypeInfo(Parent);
    if (!Ti)
    {
        std::cout << "[GcTester] Missing TypeInfo for parent\n";
        return;
    }

    unsigned char* Base = GarbageCollector::BytePtr(Parent);
    auto IsVecPtr = [&](const std::string& t){ return GarbageCollector::IsVectorOfPointer(t); };
    auto IsRawPtr = [&](const std::string& t){ return GarbageCollector::IsPointerType(t); };
    auto VecElem = [](const std::string& t)->std::string{
    auto lt = t.find('<'); auto gt = t.rfind('>');
    if (lt == std::string::npos || gt == std::string::npos || lt >= gt) return {};
    std::string inner = t.substr(lt + 1, gt - lt - 1);
    inner.erase(std::remove_if(inner.begin(), inner.end(), ::isspace), inner.end());
    return inner;

    };

    auto TryAssignInProps = [&](auto&& Enum)->bool
    {
        bool bLinked = false;
        Enum([&](const MetaProperty& p)
        {
            if (bLinked)
            {
                return;   
            }
            
            if (bUseVector && IsVecPtr(p.type))
            {
                std::string elem = VecElem(p.type);
                if (elem == "QObject*")
                {
                    auto* V = reinterpret_cast<std::vector<QObject*>*>(Base + p.offset);
                    if (std::find(V->begin(), V->end(), static_cast<QObject*>(Child)) == V->end())
                        V->push_back(static_cast<QObject*>(Child));
                    bLinked = true;
                }
                else if (elem == "QTestObject*")
                {
                    auto* V = reinterpret_cast<std::vector<QTestObject*>*>(Base + p.offset);
                    if (std::find(V->begin(), V->end(), Child) == V->end())
                        V->push_back(Child);
                    bLinked = true;
                }
            }
            else if (!bUseVector && IsRawPtr(p.type))
            {
                auto** Slot = reinterpret_cast<QObject**>(Base + p.offset);
                if (*Slot == nullptr)
                {
                    *Slot = static_cast<QObject*>(Child);
                    bLinked = true;
                }
            }
        });
        return bLinked;
    };

    auto EnumerateLocal = [&](auto&& F)
    {
        for (auto& p : Ti->properties)
        {
            F(p);   
        }
    };
    
    auto EnumerateBase  = [&](auto&& F)
    {
        if (Ti->base)
        {
            Ti->base->ForEachProperty(F);   
        }
    };

    bool bHasLocal = false;
    bool bHasBase = false;

    // Check the owner has local assignable props.
    EnumerateLocal([&](const MetaProperty& p){
        if (bUseVector ? IsVecPtr(p.type) : IsRawPtr(p.type))
        {
            if (bUseVector)
            {
                bHasLocal = true;   
            }
            else
            {
                auto** s = reinterpret_cast<QObject**>(Base + p.offset);
                if (*s == nullptr)
                {
                    bHasLocal = true;  
                }
            }
        }
    });

    // Check the parents have local assignable props.
    EnumerateBase([&](const MetaProperty& p){
        if (bUseVector ? IsVecPtr(p.type) : IsRawPtr(p.type))
        {
            if (bUseVector)
            {
                bHasBase = true;   
            }
            else
            {
                auto** s = reinterpret_cast<QObject**>(Base + p.offset);
                if (*s == nullptr)
                {
                    bHasBase = true;
                }
            }
        }
    });

    // 0: own-only, 1: parents-only, 2: random between available sides
    int Origin = 0;
    
    if (AssignMode == 0)
    {
        Origin = 0;   
    }
    else if (AssignMode == 1)
    {
        Origin = 1;   
    }
    else
    {
        // Pick a random origin
        if (bHasLocal && bHasBase)
        {
            if (!RngOpt) { static std::mt19937 Tmp(1337); RngOpt = &Tmp; }
            std::uniform_int_distribution<int> Pick01(0, 1);
            Origin = Pick01(*RngOpt);
        }
        else if (bHasBase) Origin = 1;
        else Origin = 0;
    }
    
    bool bLinked = false;
    if (Origin == 0)
    {
        bLinked = TryAssignInProps(EnumerateLocal);
        if (!bLinked && bHasBase) bLinked = TryAssignInProps(EnumerateBase);
    }
    else
    {
        bLinked = TryAssignInProps(EnumerateBase);
        if (!bLinked && bHasLocal) bLinked = TryAssignInProps(EnumerateLocal);
    }
        
    if (!bLinked)
    {
        std::cout << "[GcTester] No suitable slot for link\n";
    }
}

QTestObject* QGcTester::PickRandom(const std::vector<QTestObject*>& From, std::mt19937& Rng)
{
    if (From.empty()) return nullptr;
    std::uniform_int_distribution<size_t> dist(0, From.size() - 1);
    return From[dist(Rng)];
}

void QGcTester::GatherChildren(QTestObject* Node, std::vector<QTestObject*>& Out) const
{
    Out.clear();
    if (!Node) return;

    auto& GC = GarbageCollector::Get();

    if (bUseVector)
    {
        // This class' vector
        Out.insert(Out.end(), Node->Children.begin(), Node->Children.end());

        // Optionally include base class vector (QTestObject_Parent::Children_Parent)
        if (GC.GetAllowTraverseParents())
        {
            const auto* AsParent = static_cast<const QTestObject_Parent*>(Node);
            for (QObject* Raw : AsParent->Children_Parent)
            {
                if (auto* T = dynamic_cast<QTestObject*>(Raw))
                {
                    Out.push_back(T);
                }
            }
        }
    }
    else
    {
        QTestObject* slots[5] = { Node->Friend1, Node->Friend2, Node->Friend3, Node->Friend4, Node->Friend5 };
        for (auto* s : slots) if (s) Out.push_back(s);
    }
}

size_t QGcTester::GetChildCount(const QTestObject* Node) const
{
    if (!Node)
    {
        return 0;
    }

    auto& GC = GarbageCollector::Get();
    
    if (bUseVector)
    {
        size_t c = Node->Children.size();
        if (GC.GetAllowTraverseParents())
        {
            c += static_cast<const QTestObject_Parent*>(Node)->Children_Parent.size();
        }
        return c;
    }
    else
    {
        size_t c = 0;
        if (Node->Friend1) ++c;
        if (Node->Friend2) ++c;
        if (Node->Friend3) ++c;
        if (Node->Friend4) ++c;
        if (Node->Friend5) ++c;
        return c;
    }
}

void QGcTester::BuildLayers(QTestObject* Root, bool bClearExisting = false)
{
    if (bClearExisting)
    {
        DepthLayers.clear();
    }

    std::unordered_map<QTestObject*, int> ObjectDepthMap;
    std::queue<QTestObject*> Queue;
    
    if (Root)
    {
        ObjectDepthMap[Root] = 0;
        Queue.push(Root);
    }
    else
    {
        for (QObject* FoundRoot : Roots)
        {
            auto* RootCasted = static_cast<QTestObject*>(FoundRoot);
            if (!RootCasted) continue;

            ObjectDepthMap[RootCasted] = 0;
            Queue.push(RootCasted);
        }
    }
    
    int MaxD = 0;
    while (!Queue.empty())
    {
        auto* CurObj = Queue.front(); Queue.pop();
        int Depth = ObjectDepthMap[CurObj];
        MaxD = std::max(MaxD, Depth);

        std::vector<QTestObject*> Nexts;
        GatherChildren(CurObj, Nexts);
        
        for (auto* NextObj : Nexts)
        {
            if (!NextObj) continue;
            if (ObjectDepthMap.find(NextObj) == ObjectDepthMap.end())
            {
                ObjectDepthMap[NextObj] = Depth + 1;
                Queue.push(NextObj);
            }
        }
    }
    
    DepthLayers.resize(static_cast<size_t>(MaxD) + 1);
    
    for (auto& [Obj, Depth] : ObjectDepthMap)
    {
        QTestObject* Node = Obj;
        if (Depth >= 0)
        {
            DepthLayers[(size_t)Depth].push_back(Node);
        }
    }
}

std::vector<QTestObject*> QGcTester::GetReachable() const
{
    std::vector<QTestObject*> Out;
    std::unordered_set<QTestObject*> Vis;
    std::queue<QTestObject*> Queue;
    for (QObject* Root : Roots)
    {
        auto* RootCasted = static_cast<QTestObject*>(Root);
        if (!RootCasted || Vis.count(RootCasted)) continue;
        Vis.insert(RootCasted);
        Queue.push(RootCasted);
    }
    
    while (!Queue.empty())
    {
        auto* u = Queue.front(); Queue.pop();
        Out.push_back(u);
        for (auto* v : u->Children)
        {
            if (v && !Vis.count(v))
            {
                Vis.insert(v);
                Queue.push(v);
            }
        }
    }
    return Out;
}

void QGcTester::CollectEdgesReachable(std::vector<EdgeRef>& Out) const
{
    Out.clear();
    std::unordered_set<QTestObject*> vis;
    std::queue<QTestObject*> q;
    for (QObject* r : Roots)
    {
        auto* gr = static_cast<QTestObject*>(r);
        if (!gr || vis.count(gr)) continue;
        vis.insert(gr);
        q.push(gr);
    }
    while (!q.empty())
    {
        auto* u = q.front(); q.pop();

        if (bUseVector)
        {
            // vector children
            for (size_t i = 0; i < u->Children.size(); ++i)
            {
                QTestObject* Reached = u->Children[i];
                if (Reached)
                {
                    Out.push_back({u, Reached, i});  
                }
                
                if (Reached && !vis.count(Reached))
                {
                    vis.insert(Reached); q.push(Reached);
                }
            }   
        }
        else
        {
            QTestObject* Slots[5] = { u->Friend1, u->Friend2, u->Friend3, u->Friend4, u->Friend5 };
            for (size_t i = 0; i < 5; ++i)
            {
                auto* v = Slots[i];
                if (v)
                {
                    Out.push_back({u, v, i}); // ChildIndex = friend slot (0..4)   
                }
                if (v && !vis.count(v))
                {
                    vis.insert(v); q.push(v);
                }
            }
        }
    }
}

bool QGcTester::RemoveEdge(QTestObject* Parent, QTestObject* Child)
{
    if (!Parent)
    {
        return false;
    }

    if (bUseVector)
    {
        auto& Vec = Parent->Children;
        for (size_t i = 0; i < Vec.size(); ++i)
        {
            if (Vec[i] == Child)
            {
                Vec[i] = Vec.back();
                Vec.pop_back();
                return true;
            }
        }

        return false;
    }
    else
    {
        QTestObject** Slots[5] = { &Parent->Friend1, &Parent->Friend2, &Parent->Friend3, &Parent->Friend4, &Parent->Friend5 };
        for (auto& Slot : Slots)
        {
            if (*Slot == Child)
            {
                *Slot = nullptr;
                return true;
            }
        }
        
        return false;
    }
}

void QGcTester::SetAssignMode(int InMode)
{
    if (InMode < 0 || InMode > 3)
    {
        std::cout << "[GcTester] Invalid AssignMode " << InMode << "\n";
        return;
    }

    AssignMode = InMode;

    const std::string ModeNames[] = { "OwnedOnly", "ParentsOnly", "Random"};
    
    std::cout << "[GcTester] AssignMode: " << ModeNames[InMode] << "\n";
}

void QGcTester::SetUseVector(bool bUse)
{
    bUseVector = bUse;
    std::cout << "[GcTester] bUseVector: " << (bUseVector ? "true" : "false") << "\n";
}

// ---------------- Pattern builders ----------------

void QGcTester::PatternChain(int Length, int Seed)
{
    if (Length <= 0)
    {
        std::cout << "[GcTester] length>0 required\n";
        return;
    }
    
    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "World not found.\n";
        return;
    }

    auto* Head = MakeNode();
    Roots.push_back(Head);
    auto* Cur = Head;
    for (int i = 1; i < Length; ++i)
    {
        auto* Nxt = MakeNode();
        LinkChild(Cur, Nxt, nullptr);
        Cur = Nxt;
    }
    
    BuildLayers(Head);
    
    std::cout << "[GcTester] Chain built: length=" << Length << " total=" << AllNodes.size() << "\n";
}

void QGcTester::PatternGrid(int Width, int Height, int Seed)
{
    if (Width <= 0 || Height <= 0)
    {
        std::cout << "[GcTester] width/height>0 required\n";
        return;
    }
    
    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "World not found.\n";
        return;
    }

    std::vector<std::vector<QTestObject*>> grid(Height, std::vector<QTestObject*>(Width, nullptr));
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            grid[y][x] = MakeNode();

    // root: top-left
    QTestObject* Head = grid[0][0];
    Roots.push_back(Head);

    // edges: left, right, up, down
    for (int y = 0; y < Height; ++y)
    {
        for (int x = 0; x < Width; ++x)
        {
            if (x + 1 < Width)
            {
                LinkChild(grid[y][x], grid[y][x+1], nullptr);
            }

            if (x - 1 >= 0)
            {
                LinkChild(grid[y][x], grid[y][x - 1], nullptr);
            }
            
            if (y + 1 < Height)
            {
                LinkChild(grid[y][x], grid[y+1][x], nullptr);  
            }

            if (y - 1 >= 0)
            {
                LinkChild(grid[y][x], grid[y-1][x], nullptr);  
            }
        }
    }
    
    
    BuildLayers((grid[0][0]));
    std::cout << "[GcTester] Grid built: " << Width << "x" << Height << " total=" << AllNodes.size() << "\n";
}

void QGcTester::PatternRandom(int Nodes, int BranchCount, int Seed)
{
    if (Nodes <= 0 || BranchCount < 0)
    {
        std::cout << "[GcTester] nodes>0, branchCount>=0\n";
        return;
    }
    
    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "World not found.\n";
        return;
    }

    AllNodes.reserve((size_t)Nodes);
    for (int i = 0; i < Nodes; ++i) MakeNode();

    QTestObject* Head = AllNodes.front();
    Roots.push_back(Head);

    std::mt19937 Rng(static_cast<uint32_t>(Seed));
    std::uniform_int_distribution<int> Pick(0, Nodes - 1);

    for (int i = 0; i < Nodes; ++i)
    {
        auto* Parent = AllNodes[(size_t)i];
        for (int k = 0; k < BranchCount; ++k)
        {
            auto* Child = AllNodes[(size_t)Pick(Rng)];
            if (Child == Parent) continue;
            LinkChild(Parent, Child, &Rng);
        }
    }
    
    BuildLayers(Head);
    std::cout << "[GcTester] Random graph: nodes=" << Nodes << " branchCount=" << BranchCount << " total=" << AllNodes.size() << "\n";
}

void QGcTester::PatternRings(int Rings, int RingSize, int Seed)
{
    if (Rings <= 0 || RingSize <= 0)
    {
        std::cout << "[GcTester] rings>0, ringSize>0\n";
        return;
    }
    
    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    QTestObject* PrevRingFirst = nullptr;
    QTestObject* Head = nullptr;
    
    for (int RingIdx = 0; RingIdx < Rings; ++RingIdx)
    {
        std::vector<QTestObject*> Ring;
        Ring.reserve((size_t)RingSize);
        
        for (int i = 0; i < RingSize; ++i)
        {
            Ring.push_back(MakeNode());
        }

        // make cycle
        for (int i = 0; i < RingSize; ++i)
        {
            LinkChild(Ring[(size_t)i], Ring[(size_t)((i+1)%RingSize)], &Rng);
        }
        
        // connect the previous ring to this ring (one bridge)
        if (PrevRingFirst)
        {
            LinkChild(PrevRingFirst, Ring[0], &Rng);
        }

        if (RingIdx == 0)
        {
            Head = Ring[0];
            Roots.push_back(Head);
        }
        
        PrevRingFirst = Ring[0];
    }

    BuildLayers(Head);
    std::cout << "[GcTester] Rings: rings=" << Rings << " ringSize=" << RingSize << " total=" << AllNodes.size() << "\n";
}

void QGcTester::PatternDiamond(int Layers, int Breadth, int Seed)
{
    if (Layers <= 1 || Breadth <= 1)
    {
        std::cout << "[GcTester] layers>1, breadth>1\n";
        return;
    }
    
    QWorld* World = GetWorld();
    if (!World)
    {
        std::cout << "World not found.\n";
        return;
    }

    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    std::vector<std::vector<QTestObject*>> L;
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
                LinkChild(p, c, &Rng);
                L[(size_t)(d+1)].push_back(c);
            }
        }
    }
    
    // merge
    for (int d = up; d < Layers - 1; ++d)
    {
        auto& Cur = L[(size_t)d];
        auto& Nxt = L[(size_t)(d+1)];
        // group current layer nodes into blocks of 'breadth' and share a single child
        for (size_t i = 0; i < Cur.size(); i += (size_t)Breadth)
        {
            auto* shared = MakeNode();
            Nxt.push_back(shared);
            for (size_t j = i; j < std::min(Cur.size(), i + (size_t)Breadth); ++j)
                LinkChild(Cur[j], shared, &Rng);
        }
    }
    
    BuildLayers(L[0][0]);
    std::cout << "[GcTester] Diamond: layers=" << Layers << " breadth=" << Breadth << " total=" << AllNodes.size() << "\n";
}

void QGcTester::ClearAll(bool bSilient)
{
    ClearGraph();
    GarbageCollector::Get().Collect(true);

    if (!bSilient)
    {
        std::cout << "[GcTester] Cleared all test objects. \n";   
    }
}

// ---------------- Break / mutate ----------------

int QGcTester::BreakAtDepth(int TargetDepth, int Count, int Seed)
{
    if (TargetDepth <= 0)
    {
        std::cout << "[GcTester] TargetDepth must be > 0\n";
        return 0;
    }
    
    if (DepthLayers.empty())
    {
        std::cout << "[GcTester] Depth layer is empty.\n";
        return 0;
    }
    
    if (TargetDepth >= (int)DepthLayers.size())
    {
        std::cout << "[GcTester] Invalid TargetDepth " << TargetDepth << "\n";
        return 0;
    }
    
    auto& Parents = DepthLayers[(size_t)(TargetDepth - 1)];
    if (Parents.empty())
    {
        std::cout << "[GcTester] No parents at depth " << TargetDepth - 1 << "\n";
        return 0;
    }
    
    std::vector<int> Idx(Parents.size());
    std::iota(Idx.begin(), Idx.end(), 0);
    std::mt19937 Rng(static_cast<uint32_t>(Seed));
    std::shuffle(Idx.begin(), Idx.end(), Rng);

    if (Count < 0 || Count > (int)Parents.size())
    {
        Count = (int)Parents.size();
    }
    
    Idx.resize((size_t)Count);

    int cut = 0;
    for (int i : Idx)
    {
        auto* p = Parents[(size_t)i];
        cut += (int)p->Children.size();
        p->Children.clear();
    }
    std::cout << "[GcTester] Cut " << cut << " links at depth " << TargetDepth << " from " << Count << " parents.\n";
    return cut;
}

int QGcTester::BreakPercent(double Percent, int Depth, int Seed, bool bSilient)
{
    Percent = std::clamp(Percent, 0.0, 100.0);
    if (Percent <= 0.0)
    {
        return 0;
    }

    if (DepthLayers.empty())
    {
        std::cout << "[GcTester] Depth layer is empty.\n";
        return 0;
    }
    std::mt19937 Rng(static_cast<uint32_t>(Seed));
    std::uniform_real_distribution<double> Roll(0.0, 100.0);

    std::vector<QTestObject*> Targets;
    if (Depth < 0)
    {
        auto Reach = GetReachable();
        Targets = std::move(Reach);
    }
    else
    {
        if (Depth >= (int)DepthLayers.size()) { std::cout << "[GcTester] Invalid depth\n"; return 0; }
        Targets = DepthLayers[(size_t)Depth];
    }

    int Cut = 0;
    for (auto* p : Targets)
    {
        if (bUseVector)
        {
            // vector children
            auto& Vec = p->Children;
            for (size_t i = 0; i < Vec.size(); )
            {
                if (Roll(Rng) < Percent)
                {
                    Vec[i] = Vec.back();
                    Vec.pop_back();
                    ++Cut;
                }
                else ++i;
            }   
        }
        else
        {
            QTestObject** Slots[5] = { &p->Friend1, &p->Friend2, &p->Friend3, &p->Friend4, &p->Friend5 };
            for (auto& Slot : Slots)
            {
                if (*Slot && Roll(Rng) < Percent)
                {
                    *Slot = nullptr;
                    ++Cut;
                }
            }
        }
    }

    if (!bSilient)
    {
        std::cout << "[GcTester] BreakPercent depth=" << Depth << " percent=" << Percent << " cut=" << Cut << "\n";    
    }
    
    return Cut;
}

int QGcTester::BreakRandomEdges(int EdgeCount, int Seed)
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

    std::cout << "[GcTester] BreakRandomEdges cut=" << cut << "\n";
    return cut;
}

int QGcTester::DetachRoots(int Count, double Ratio)
{
    double Percent = std::clamp(Ratio * 100.f, 0.0, 100.0);
    
    int Removed = 0;
    if (!Roots.empty())
    {
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
    }
    BuildLayers(nullptr, true);
    std::cout << "[GcTester] DetachRoots removed=" << Removed << " remainingRoots=" << Roots.size() << "\n";
    return Removed;
}

// ---------------- Stats / measure / GC ----------------

void QGcTester::PrintDepthStats(int TargetDepth) const
{
    if (TargetDepth < 0 || TargetDepth >= (int)DepthLayers.size())
    {
        std::cout << "[GcTester] Invalid depth " << TargetDepth << "\n";
        return;
    }
    const auto& L = DepthLayers[(size_t)TargetDepth];
    size_t N = L.size();
    size_t minC = SIZE_MAX, maxC = 0, sumC = 0;

    for (auto* Node : L)
    {
        size_t c = Node ? GetChildCount(Node) : 0;
        minC = std::min(minC, c);
        maxC = std::max(maxC, c);
        sumC += c;
    }
    double AvgC = N ? (double)sumC / (double)N : 0.0;
    std::cout << "[GcTester] Depth " << TargetDepth
              << " nodes=" << N
              << " children(min/avg/max)=(" << (N ? minC : 0) << "/"
              << AvgC << "/" << maxC << ")\n";
}

void QGcTester::MeasureGc(int Repeats)
{
    if (Repeats <= 0)
    {
        std::cout << "[GcTester] repeats>0 required\n";
        return;
    }
    
    auto& GC = GarbageCollector::Get();
    double Minv = 1e100, Maxv = -1.0, Sum = 0.0;

    for (int i = 0; i < Repeats; ++i)
    {
        const double Ms = GC.Collect();
        Minv = std::min(Minv, Ms);
        Maxv = std::max(Maxv, Ms);
        Sum += Ms;
    }
    std::cout << "[GcTester] MeasureGc repeats=" << Repeats
              << " avg=" << (Sum / Repeats)
              << " min=" << Minv
              << " max=" << Maxv << " ms\n";
}

// ---------------- Churn ----------------

void QGcTester::Churn(int Steps, int AllocPerStep, double BreakPct, int GcEveryN, int Seed)
{
    if (Steps <= 0 || AllocPerStep < 0)
    {
        std::cout << "[GcTester] invalid params\n"; return;
    }
    
    std::mt19937 Rng(static_cast<uint32_t>(Seed));

    int CutCountBetweenGc = 0;
    
    for (int s = 1; s <= Steps; ++s)
    {
        QTestObject* Root = nullptr;
        // 1) allocate and attach to random reachable parents
        auto Reached = GetReachable();
        if (Reached.empty() && !Roots.empty())
        {
            // ensure at least root exists
            Root = static_cast<QTestObject*>(Roots[0]);
            Reached.push_back(Root);
        }
        for (int i = 0; i < AllocPerStep; ++i)
        {
            auto* Picked = PickRandom(Reached, Rng);
            auto* NewNode = MakeNode();
            if (Picked)
            {
                LinkChild(Picked, NewNode, &Rng);   
            }
        }

        BuildLayers(Root, true);
        
        // 2) random break by percent across all depths
        if (BreakPct > 0.0)
        {
            CutCountBetweenGc += BreakPercent(BreakPct, -1, Rng(), true);   
        }

        // 3) optional GC
        if (GcEveryN > 0 && (s % GcEveryN == 0))
        {
            std::cout << "[GcTester] created " << Steps * AllocPerStep << " objects"<< ", cut " << CutCountBetweenGc << " objects during " << GcEveryN << " steps" << "\n";
            CutCountBetweenGc = 0;
            GarbageCollector::Get().Collect();   
        }
    }
    
    std::cout << "[GcTester] Churn done: steps=" << Steps
              << " alloc/step=" << AllocPerStep
              << " breakPct=" << BreakPct
              << " gcEveryN=" << GcEveryN << "\n";
}

void QGcTester::RepeatRandomAndCollect(int NumSteps, int NumNodes, int NumBranches)
{
    GarbageCollector& GC = GarbageCollector::Get();

    SetAssignMode(0);
    
    for (int i = 0; i < NumSteps; ++i)
    {
        ClearAll(true);
        PatternRandom(NumNodes, NumBranches);
        GC.Collect();
    }

    SetAssignMode(1);
    
    for (int i = 0; i < NumSteps; ++i)
    {
        ClearAll(true);
        PatternRandom(NumNodes, NumBranches);
        GC.Collect();
    }

    SetAssignMode(2);

    for (int i = 0; i < NumSteps; ++i)
    {
        ClearAll(true);
        PatternRandom(NumNodes, NumBranches);
        GC.Collect();
    }
    
    ClearAll(true);
}
