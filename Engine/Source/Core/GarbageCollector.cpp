#include "GarbageCollector.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <deque> 
#include <atomic> 
#include "Asset.h"

using qmeta::TypeInfo;
using qmeta::MetaProperty;
using qmeta::MetaFunction;

namespace 
{
    GarbageCollector* GcSingleton;
}

void GarbageCollector::SetGcSingleton(GarbageCollector* Gc)
{
    GcSingleton = Gc;
}

GarbageCollector& GarbageCollector::Get()
{
    return *GcSingleton;
}

static inline std::string Strip(std::string s)
{
    auto is_space = [](unsigned char c){ return std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char c){ return !is_space(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char c){ return !is_space(c); }).base(), s.end());
    return s;
}

void GarbageCollector::RegisterInternal(QObject* Obj, const TypeInfo& Ti, const std::string& Name, uint64_t Id)
{
    auto [It, Inserted] = Objects.try_emplace(Obj, &Ti, Id);
    // cache layout once (stable while GC runs)
    It->second.Layout = &GetPtrLayout(Ti);

    NameToObjectMap.emplace(Name, Obj);
}

QObject* GarbageCollector::NewByTypeName(const std::string& TypeName, const std::string& Name)
{
    const TypeInfo* Ti = qmeta::GetRegistry().find(TypeName);
    if (!Ti)
    {
        throw std::runtime_error("Type not found: " + TypeName);
    }

    // Allocate by name is not generally possible in C++ without factories.
    // For demo purposes create a raw block and require user to have a default ctor via placement new.
    // Better: you can add a factory map later.
    throw std::runtime_error("NewByTypeName: factory not implemented for type " + TypeName);
}

void GarbageCollector::Initialize()
{
    
}

void GarbageCollector::SetMaxGcThreads(int InThreads)
{
}

void GarbageCollector::AddRoot(QObject* Obj)
{
    if (!Obj) return;
    if (std::find(Roots.begin(), Roots.end(), Obj) == Roots.end())
    {
        Roots.push_back(Obj);
    }
}

void GarbageCollector::RemoveRoot(QObject* Obj)
{
    Roots.erase(std::remove(Roots.begin(), Roots.end(), Obj), Roots.end());
}

void GarbageCollector::Tick(double DeltaSeconds)
{
    Accumulated += DeltaSeconds;
    if (Interval > 0.0 && Accumulated >= Interval)
    {
        Collect();
        Accumulated = 0.0;
    }
}

void GarbageCollector::SetAutoInterval(double Seconds)
{
    Interval = Seconds;
}

// Helpers
static inline bool EndsWithStar(const std::string& s)
{
    return !s.empty() && s.back() == '*';
}

bool GarbageCollector::IsPointerType(const std::string& Type)
{
    // raw pointer of any T*: exclude vectors
    return Type.find("std::vector") == std::string::npos && EndsWithStar(Type);
}

bool GarbageCollector::IsVectorOfPointer(const std::string& Type)
{
    // very permissive: any "std::vector< ... * >"
    if (Type.find("std::vector") == std::string::npos) return false;
    // naive check that there's a '*' before the closing '>'
    auto lt = Type.find('<');
    auto gt = Type.rfind('>');
    if (lt == std::string::npos || gt == std::string::npos || lt >= gt) return false;
    auto inner = Type.substr(lt + 1, gt - lt - 1);
    // strip spaces
    inner.erase(std::remove_if(inner.begin(), inner.end(), ::isspace), inner.end());
    return !inner.empty() && inner.back() == '*';
}

bool GarbageCollector::IsManaged(const QObject* Obj) const
{
    if (!Obj)
    {
        return false;
    }
    
    return Objects.contains(const_cast<QObject*>(Obj));
}

bool GarbageCollector::TryMark(QObject* Obj)
{
    auto It = Objects.find(Obj);
    if (It == Objects.end()) return false;

    auto& Epoch = It->second.MarkEpoch;
    uint32_t Expected = Epoch.load(std::memory_order_relaxed);
    while (Expected != CurrentEpoch)
    {
        if (Epoch.compare_exchange_weak(Expected, CurrentEpoch, std::memory_order_relaxed))
            // we set to CurrentEpoch: first visit
            return true; 
        // else: Expected reloaded, loop
    }

    // already marked this round
    return false;
}

void GarbageCollector::TraversePointers(QObject* Obj, const TypeInfo& Ti, std::vector<QObject*>& OutChildren) const
{
    OutChildren.clear();
    unsigned char* Base = BytePtr(Obj);

    auto Visitor = [&](const qmeta::MetaProperty& P)
    {
        if (IsPointerType(P))
        {
            // any raw pointer T*
            QObject* const* Slot = reinterpret_cast<QObject* const*>(Base + P.offset);
            if (Slot && *Slot)
            {
                auto It = Objects.find(*Slot);
                if (It != Objects.end())
                    OutChildren.push_back(*Slot);
            }
        }
        else if (IsVectorOfPointer(P))
        {
            // any std::vector<T*>, treat as vector<QObject*>
            auto* Vec = reinterpret_cast<const std::vector<QObject*>*>(Base + P.offset);
            for (QObject* Child : *Vec)
            {
                if (!Child) continue;
                auto It = Objects.find(Child);
                if (It != Objects.end())
                    OutChildren.push_back(Child);
            }
        }
    };
    
    // Traverse including parents
    Ti.ForEachPropertyWithOption(Visitor, bAllowTraverseParents);
}

const GarbageCollector::FPtrOffsetLayout& GarbageCollector::GetPtrLayout(const qmeta::TypeInfo& Ti)
{
    if (auto CacheIter = PtrCache.find(&Ti); CacheIter != PtrCache.end())
    {
        return CacheIter->second;
    }

    FPtrOffsetLayout L;
    auto Acc = [&](const TypeInfo* T){
        if (!T)
        {
            return;
        }
        
        for (const auto& P : T->properties)
        {
            if ((P.GcFlags & qmeta::PF_RawQObjectPtr) != 0)
            {
                L.RawOffsets.push_back(P.offset);
            } 
            else if ((P.GcFlags & qmeta::PF_VectorOfQObjectPtr))
            {
                L.VecOffsets.push_back(P.offset);
            }
        }
    };

    // Cache at all once including base(parent class)
    for (auto* Cur = &Ti; Cur; Cur = Cur->base)
    {
        Acc(Cur);
    }

    return PtrCache.emplace(&Ti, std::move(L)).first->second;
}

void GarbageCollector::Mark()
{
    std::vector<QObject*> Stack;
    Stack.reserve(Roots.size());
    for (auto* Root : Roots)
    {
        if (!Root) continue;
        if (TryMark(Root))
            Stack.push_back(Root);
    }

    while (!Stack.empty())
    {
        QObject* Cur = Stack.back();
        Stack.pop_back();

        auto It = Objects.find(Cur);
        if (It == Objects.end()) continue;

        const Node& N = It->second;
        unsigned char* Base = BytePtr(Cur);
        const FPtrOffsetLayout* L = N.Layout;

        for (size_t Off : L->RawOffsets)
        {
            QObject* const* Slot = reinterpret_cast<QObject* const*>(Base + Off);
            if (!Slot || !*Slot) continue;

            auto Ct = Objects.find(*Slot);
            if (Ct == Objects.end()) continue;

            if (TryMark(*Slot))
                Stack.push_back(*Slot);
        }
        for (size_t Off : L->VecOffsets)
        {
            const auto* Vec = reinterpret_cast<const std::vector<QObject*>*>(Base + Off);
            for (QObject* C : *Vec)
            {
                if (!C) continue;
                auto Ct = Objects.find(C);
                if (Ct == Objects.end()) continue;

                if (TryMark(C))
                    Stack.push_back(C);
            }
        }
    }
}

void GarbageCollector::MarkParallel(int NumThreads)
{
    // Single thread can be faster for small graphs
    if (Objects.size() < 20000 || NumThreads <= 1)
    {
        Mark();
        return;
    }

    // 1) Configure initial frontier from roots
    struct WorkItem { QObject* Obj; Node* Nd; };
    std::vector<WorkItem> Frontier;
    Frontier.reserve(Roots.size());

    for (QObject* Root : Roots)
    {
        if (!Root) continue;
        auto It = Objects.find(Root);
        if (It == Objects.end()) continue;
        if (TryMark(Root))
            Frontier.push_back({ Root, &It->second });
    }
    if (Frontier.empty())
        return;

    // 2) 라운드별로 프런티어를 처리한다 (BFS 스타일)
    constexpr size_t WorkChunk = 64;

    for (;;)
    {
        // 스레드별 next 프런티어 버퍼
        std::vector<std::vector<WorkItem>> NextPerThread((size_t)NumThreads);

        std::atomic<size_t> Cursor{0};

        auto Worker = [&](int Tid)
        {
            std::vector<WorkItem>& Out = NextPerThread[(size_t)Tid];

            while (true)
            {
                size_t Begin = Cursor.fetch_add(WorkChunk, std::memory_order_relaxed);
                if (Begin >= Frontier.size()) break;
                size_t End = std::min(Frontier.size(), Begin + WorkChunk);

                for (size_t i = Begin; i < End; ++i)
                {
                    WorkItem WI = Frontier[i];
                    unsigned char* Base = BytePtr(WI.Obj);
                    const FPtrOffsetLayout* L = WI.Nd->Layout;

                    // raw QObject*
                    for (size_t Off : L->RawOffsets)
                    {
                        QObject* const* Slot = reinterpret_cast<QObject* const*>(Base + Off);
                        if (!Slot || !*Slot) continue;

                        auto Ct = Objects.find(*Slot);
                        if (Ct == Objects.end()) continue;

                        if (TryMark(*Slot))
                            Out.push_back({ *Slot, &Ct->second });
                    }
                    // vector<QObject*>
                    for (size_t Off : L->VecOffsets)
                    {
                        const auto* Vec = reinterpret_cast<const std::vector<QObject*>*>(Base + Off);
                        for (QObject* C : *Vec)
                        {
                            if (!C) continue;
                            auto Ct = Objects.find(C);
                            if (Ct == Objects.end()) continue;

                            if (TryMark(C))
                                Out.push_back({ C, &Ct->second });
                        }
                    }
                }
            }
        };

        // 스레드 실행
        std::vector<std::thread> Threads;
        Threads.reserve((size_t)NumThreads);
        for (int t = 0; t < NumThreads; ++t)
            Threads.emplace_back(Worker, t);
        for (auto& th : Threads) th.join();

        // 3) 다음 프런티어로 머지
        size_t TotalNext = 0;
        for (auto& V : NextPerThread) TotalNext += V.size();
        if (TotalNext == 0)
            break;

        std::vector<WorkItem> Next;
        Next.reserve(TotalNext);
        for (auto& V : NextPerThread)
        {
            Next.insert(Next.end(),
                        std::make_move_iterator(V.begin()),
                        std::make_move_iterator(V.end()));
        }
        Frontier.swap(Next);
    }
}

double GarbageCollector::Collect(bool bSilent)
{
    using Clock = std::chrono::high_resolution_clock;
    auto ms = [](const Clock::time_point& a, const Clock::time_point& b)
    {
        return std::chrono::duration<double, std::milli>(a - b).count();
    };

    const auto TTotal0 = Clock::now();

    // 1) Clear marks (epoch bump)
    const auto TClear0 = Clock::now();
    CurrentEpoch++;
    if (CurrentEpoch == 0)
    {
        // wrap-around guard: reset all marks to 0
        for (auto& [Obj, Node] : Objects)
        {
            Node.MarkEpoch.store(0, std::memory_order_relaxed);
        }
        CurrentEpoch = 1;
    }
    const auto TClear1 = Clock::now();

    // 2) Mark (parallel)
    const auto TMark0 = Clock::now();
    int Threads = MaxGcThreads <= 0
        ? std::max(1u, std::thread::hardware_concurrency() - 1u)
        : MaxGcThreads;
    if (Threads <= 1) Mark();
    else              MarkParallel(Threads);
    const auto TMark1 = Clock::now();

    // 3) Build dead list
    const auto TBuild0 = Clock::now();
    std::vector<QObject*> Dead;
    Dead.reserve(Objects.size());
    for (auto& [Obj, Node] : Objects)
    {
        if (Node.MarkEpoch.load(std::memory_order_relaxed) != CurrentEpoch)
            Dead.push_back(Obj);
    }
    const auto TBuild1 = Clock::now();
    const double MsBuild = ms(TBuild1, TBuild0);

    // 4) Fixup references from survivors to dead
    const auto TFix0 = Clock::now();
    std::unordered_set<QObject*> DeadSet;
    DeadSet.reserve(Dead.size() * 2 + 1);
    for (auto* d : Dead) DeadSet.insert(d);

    for (auto& [Obj, Node] : Objects)
    {
        if (Node.MarkEpoch.load(std::memory_order_relaxed) != CurrentEpoch)
            continue;

        unsigned char* Base = BytePtr(Obj);
        const FPtrOffsetLayout& Layout = GetPtrLayout(*Node.Ti);

        for (size_t Offset : Layout.RawOffsets)
        {
            QObject** Slot = reinterpret_cast<QObject**>(Base + Offset);
            if (Slot && *Slot && DeadSet.contains(*Slot))
                *Slot = nullptr;
        }
        for (size_t Offset : Layout.VecOffsets)
        {
            auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + Offset);
            std::erase_if(*Vec, [&](QObject* p){ return p && DeadSet.count(p); });
        }
    }
    const auto TFix1 = Clock::now();
    const double MsFixup = ms(TFix1, TFix0);

    // 5) Sweep (delete & erase)
    const auto TSweep0 = Clock::now();
    for (QObject* D : Dead)
    {
        auto It = Objects.find(D);
        if (It != Objects.end())
        {
            QObject* Obj = It->first;
            Objects.erase(It); // remove from list first
            delete Obj;        // then destroy memory
        }
    }
    const auto TSweep1 = Clock::now();

    const auto TTotal1 = Clock::now();

    const double MsClear = ms(TClear1, TClear0);
    const double MsMark  = ms(TMark1, TMark0);
    const double MsSweep = ms(TSweep1, TSweep0);
    const double MsTotal = ms(TTotal1, TTotal0);

    if (!bSilent)
    {
        std::cout << "[GC] Collected " << Dead.size()
                  << " objects, alive=" << Objects.size()
                  << ". Total " << MsTotal << " ms.\n";
        
        std::cout << "ms (clear=" << MsClear
                  << ", mark="    << MsMark
                  << ", buildDead="<< MsBuild
                  << ", fixup="   << MsFixup
                  << ", sweep="   << MsSweep
                  << ", threads=" << Threads
                  << ")\n";
    }

    return MsTotal;
}

void GarbageCollector::ListObjects() const
{
    // Group by reflected type name
    struct Group { std::vector<const QObject*> Objs; };
    std::unordered_map<std::string, Group> Groups;
    Groups.reserve(Objects.size());

    for (const auto& kv : Objects)
    {
        const QObject* Obj = kv.first;
        const Node& Node   = kv.second;
        const std::string& TypeName = Node.Ti ? Node.Ti->name : std::string("<UnknownType>");
        Groups[TypeName].Objs.push_back(Obj);
    }

    // Order: by descending count, then by type name (stable, readable)
    std::vector<std::pair<std::string, Group>> ordered;
    ordered.reserve(Groups.size());
    for (auto& kv : Groups) ordered.emplace_back(kv.first, std::move(kv.second));
    std::sort(ordered.begin(), ordered.end(),
        [](const auto& A, const auto& B)
        {
            if (A.second.Objs.size() != B.second.Objs.size())
                return A.second.Objs.size() > B.second.Objs.size();
            return A.first < B.first;
        });

    const size_t Total = Objects.size();
    const size_t TypeCount = ordered.size();

    std::cout << "[Objects] total=" << Total << ", types=" << TypeCount << std::endl;

    // Print up to this many names per type
    constexpr size_t MaxSamples = 3;

    for (const auto& [typeName, group] : ordered)
    {
        // Deterministic sample order: by internal Id if available, else by pointer
        std::vector<const QObject*> Samples = group.Objs;
        std::sort(Samples.begin(), Samples.end(),
            [this](const QObject* A, const QObject* B)
            {
                auto IterA = Objects.find(const_cast<QObject*>(A));
                auto IterB = Objects.find(const_cast<QObject*>(B));
                if (IterA != Objects.end() && IterB != Objects.end())
                    return IterA->second.Id < IterB->second.Id;
                return A < B;
            });

        // Build sample name list
        std::ostringstream Names;
        Names << "[";
        size_t printed = 0;
        for (const QObject* Obj : Samples)
        {
            if (printed == MaxSamples) break;
            const std::string& Nm = Obj->GetDebugName();
            if (printed) Names << ", ";
            Names << (Nm.empty() ? "(Unnamed)" : Nm);
            ++printed;
        }
        
        if (group.Objs.size() > MaxSamples)
        {
            Names << ", ...";
        }
        
        Names << "]";

        std::cout << " - " << typeName << " (count=" << group.Objs.size() << ") " << Names.str() << std::endl;
    }
}


void GarbageCollector::ListPropertiesByDebugName(const std::string& Name) const
{
    QObject* Obj = FindByDebugName(Name);
    if (!Obj)
    {
        std::cout << "Object [" << Name <<"] is not found." << "\n"; return;
    }
    
    auto It = Objects.find(Obj);
    const TypeInfo& Ti = *It->second.Ti;
    
    std::cout << "[Properties] " << Name << " : " << Ti.name << "\n";
    Ti.ForEachProperty([&](const MetaProperty& p){
        std::cout << " - " << p.type << " " << p.name << " (offset " << p.offset << ")\n";
    });
}

void GarbageCollector::ListFunctionsByDebugName(const std::string& Name) const
{
    QObject* Obj = FindByDebugName(Name);
    if (!Obj)
    {
        std::cout << "Object [" << Name <<"] is not found." << "\n"; return;
    }
    
    auto It = Objects.find(Obj);
    const TypeInfo& Ti = *It->second.Ti;

    std::cout << "[Functions] " << Name << " : " << Ti.name << "\n";
    Ti.ForEachFunction([&](const MetaFunction& Func){
        std::cout << " - " << Func.return_type << " " << Func.name << "(";
        for (size_t i = 0; i < Func.params.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << Func.params[i].type << " " << Func.params[i].name;
        }
        std::cout << ")\n";
    });
}

void GarbageCollector::SetAllowTraverseParents(bool bEnable)
{
    bAllowTraverseParents = bEnable;
}

bool GarbageCollector::GetAllowTraverseParents() const
{
    return bAllowTraverseParents;
}

QObject* GarbageCollector::FindByDebugName(const std::string& DebugName) const
{
    auto It = NameToObjectMap.find(DebugName);
    return (It == NameToObjectMap.end()) ? nullptr : It->second; 
}

const TypeInfo* GarbageCollector::GetTypeInfo(const QObject* Obj) const
{
    auto It = Objects.find(const_cast<QObject*>(Obj));
    return It == Objects.end() ? nullptr : It->second.Ti;
}

bool GarbageCollector::Unlink(QObject* Object, const std::string& Property)
{
    if (!Object)
    {
        std::cout << "[Unlink] Object is null\n";
        return false;
    }
    
    auto ObjectIter = Objects.find(Object);
    unsigned char* Base = BytePtr(Object);
    
    for (auto& MetaProp : ObjectIter->second.Ti->properties)
    {
        if (MetaProp.name != Property) continue;
        
        // Handle raw QObject*
        if (IsPointerType(MetaProp))
        {
            auto* Slot = reinterpret_cast<QObject**>(Base + MetaProp.offset);
            *Slot = nullptr;
            std::cout << "[Unlink] Name=" << Object->GetDebugName() << "." << Property << " -> null" << "\n";
            return true;
        }

        // Handle std::vector<QObject*>
        if (IsVectorOfPointer(MetaProp))
        {
            auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + MetaProp.offset);
            // Remove all references held by the vector
            for (QObject*& E : *Vec) { E = nullptr; }
            Vec->clear();
            std::cout << "[Unlink] Name=" << Object->GetDebugName() << "." << Property << " -> cleared vector" << "\n";
            return true;
        }
    }
    
    return false;
}

bool GarbageCollector::UnlinkByName(const std::string& Name, const std::string& Property)
{ 
    QObject* Obj = FindByDebugName(Name);

    if (!Obj)
    {
        std::cout << "[Unlink] Object not found by Name: " << Name << "\n";
        return false;
    }
    
    return Unlink(Obj, Property);
}

bool GarbageCollector::UnlinkAllByName(const std::string& Name)
{
    QObject* Obj = FindByDebugName(Name);
    
    if (!Obj) return false;

    auto ObjectIter = Objects.find(Obj);
    unsigned char* Base = BytePtr(Obj);
    for (auto& MetaProp : ObjectIter->second.Ti->properties)
    {
        Unlink(Obj, MetaProp.name);
    }
    
    return true;
}

bool GarbageCollector::SetProperty(QObject* Obj, const std::string& Property, const std::string& Value)
{
    auto It = Objects.find(Obj);
    if (It == Objects.end()) return false;
    unsigned char* Base = BytePtr(Obj);

    for (auto& p : It->second.Ti->properties)
    {
        if (p.name != Property) continue;

        if (p.type == "int" || p.type == "int32_t")
        {
            *reinterpret_cast<int*>(Base + p.offset) = std::stoi(Value);
            return true;
        }
        else if (p.type == "float")
        {
            *reinterpret_cast<float*>(Base + p.offset) = std::stof(Value);
            return true;
        }
        else if (p.type == "double")
        {
            *reinterpret_cast<double*>(Base + p.offset) = std::stod(Value);
            return true;
        }
        else if (p.type == "bool")
        {
            *reinterpret_cast<bool*>(Base + p.offset) = (Value == "true" || Value == "1");
            return true;
        }
        else if (p.type == "std::string" || p.type == "string")
        {
            *reinterpret_cast<std::string*>(Base + p.offset) = Value;
            return true;
        }
    }
    return false;
}

bool GarbageCollector::SetPropertyByName(const std::string& Name, const std::string& Property, const std::string& Value)
{
    QObject* Obj = FindByDebugName(Name);
    if (!Obj) return false;

    return SetProperty(Obj, Property, Value);
}

qmeta::Variant GarbageCollector::Call(QObject* Obj, const std::string& FuncName, const std::vector<qmeta::Variant>& Args)
{
    if (!Obj) throw std::runtime_error("Object not found");
    auto It = Objects.find(Obj);
    if (It == Objects.end()) throw std::runtime_error("Not GC-managed");
    return qmeta::CallByName(Obj, *It->second.Ti, FuncName, Args);
}

qmeta::Variant GarbageCollector::CallByName(const std::string& Name, const std::string& Function, const std::vector<qmeta::Variant>& Args)
{
    QObject* Obj = FindByDebugName(Name);
    if (!Obj) throw std::runtime_error("Object not found by Name");

    return Call(Obj, Function, Args);
}