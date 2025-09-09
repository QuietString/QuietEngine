#include "GarbageCollector.h"
#include <iostream>
#include <sstream>
#include <algorithm>

#include "Object_GcTest.h"
#include "CoreObjects/Public/World.h"

using qmeta::Registry;
using qmeta::TypeInfo;
using qmeta::MetaProperty;
using qmeta::MetaFunction;

namespace QGC
{
    GcManager& GcManager::Get()
    {
        static GcManager G;
        return G;
    }

    void GcManager::RegisterInternal(QObject* Obj, const TypeInfo& Ti, const std::string& Name, uint64_t Id)
    {
        Node N;
        N.Ptr = Obj;
        N.Ti = &Ti;
        N.Id = Id;
        Objects.emplace(Obj, N);

        ById[Id] = Obj;
        NameToObjectMap[Name] = Obj;
    }

    QObject* GcManager::NewByTypeName(const std::string& TypeName, const std::string& Name)
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

    void GcManager::Initialize()
    {
        
    }

    void GcManager::AddRoot(QObject* Obj)
    {
        if (!Obj) return;
        if (std::find(Roots.begin(), Roots.end(), Obj) == Roots.end())
        {
            Roots.push_back(Obj);
        }
    }

    void GcManager::RemoveRoot(QObject* Obj)
    {
        Roots.erase(std::remove(Roots.begin(), Roots.end(), Obj), Roots.end());
    }
    
    void GcManager::Tick(double DeltaSeconds)
    {
        Accumulated += DeltaSeconds;
        if (Interval > 0.0 && Accumulated >= Interval)
        {
            Collect();
            Accumulated = 0.0;
        }
    }

    void GcManager::SetAutoInterval(double Seconds)
    {
        Interval = Seconds;
    }
    
    // Helpers
    static inline bool EndsWithStar(const std::string& s)
    {
        return !s.empty() && s.back() == '*';
    }

    bool GcManager::IsPointerType(const std::string& Type)
    {
        // raw pointer of any T*: exclude vectors
        return Type.find("std::vector") == std::string::npos && EndsWithStar(Type);
    }

    bool GcManager::IsVectorOfPointer(const std::string& Type)
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

    bool GcManager::IsRawQObjectPtr(const std::string& type)
    {
        // e.g., "QObject*"
        return type.find("QObject") != std::string::npos
            && type.find("std::vector") == std::string::npos
            && EndsWithStar(type);
    }

    bool GcManager::IsVectorOfQObjectPtr(const std::string& type)
    {
        // e.g., "std::vector<QObject*>"
        return type.find("std::vector") != std::string::npos
            && type.find("QObject*") != std::string::npos;
    }

    bool GcManager::IsManaged(const QObject* Obj) const
    {
        if (!Obj) return false;
        return Objects.find(const_cast<QObject*>(Obj)) != Objects.end();
    }

    void GcManager::TraversePointers(QObject* Obj, const TypeInfo& Ti, std::vector<QObject*>& OutChildren) const
    {
        OutChildren.clear();
        unsigned char* Base = BytePtr(Obj);
        for (const MetaProperty& P : Ti.properties)
        {
            if (IsPointerType(P.type))
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
            else if (IsVectorOfPointer(P.type))
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
        }
    }

    void GcManager::Mark(QObject* Root)
    {
        std::vector<QObject*> Stack;
        Stack.push_back(Root);

        while (!Stack.empty())
        {
            QObject* Cur = Stack.back();
            Stack.pop_back();

            auto It = Objects.find(Cur);
            if (It == Objects.end()) continue;
            Node& N = It->second;
            if (N.Marked) continue;

            N.Marked = true;

            std::vector<QObject*> Children;
            TraversePointers(N.Ptr, *N.Ti, Children);
            for (QObject* C : Children)
            {
                Stack.push_back(C);
            }
        }
    }

    double GcManager::Collect()
    {
        const auto T0 = std::chrono::high_resolution_clock::now();
        
        // 1) Clear marks
        for (auto& Pair : Objects)
        {
            Pair.second.Marked = false;
        }

        // 2) Mark from roots
        for (QObject* R : Roots)
        {
            Mark(R);
        }

        // 3) Build a list of dead objects
        std::vector<QObject*> Dead;
        Dead.reserve(Objects.size());
        
        for (auto& Pair : Objects)
        {
            if (!Pair.second.Marked)
            {
                Dead.push_back(Pair.first);
            }
        }

        // 4) Null-out references to dead objects in survivors to avoid dangling pointers
        for (auto& Pair : Objects)
        {
            if (!Pair.second.Marked) continue;
            
            QObject* Owner = Pair.first;
            unsigned char* Base = BytePtr(Owner);
            const TypeInfo& Ti = *Pair.second.Ti;
            
            for (const MetaProperty& P : Ti.properties)
            {
                if (IsRawQObjectPtr(P.type))
                {
                    QObject** Slot = reinterpret_cast<QObject**>(Base + P.offset);
                    if (Slot && *Slot)
                    {
                        auto It = Objects.find(*Slot);
                        if (It != Objects.end() && !It->second.Marked)
                        {
                            *Slot = nullptr;
                        }
                    }
                }
                else if (IsVectorOfQObjectPtr(P.type))
                {
                    auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + P.offset);
                    for (QObject*& Child : *Vec)
                    {
                        if (!Child) continue;
                        auto It = Objects.find(Child);
                        if (It != Objects.end() && !It->second.Marked)
                        {
                            Child = nullptr; // or erase later if you prefer   
                        }
                    }
                    // Optional: compact the vector
                    // Vec->erase(std::remove(Vec->begin(), Vec->end(), nullptr), Vec->end());
                }
            }
        }

        // 5) Delete dead and remove from maps
        for (QObject* D : Dead)
        {
            auto It = Objects.find(D);
            if (It != Objects.end())
            {
                const uint64_t ID = It->second.Id;
                auto IterId = ById.find(ID);
                if (IterId != ById.end() && IterId->second == D)
                {
                    ById.erase(IterId);
                }
   
                delete It->second.Ptr;
                Objects.erase(It);
            }
        }

        const auto T1 = std::chrono::high_resolution_clock::now();
        const double Ms = std::chrono::duration<double, std::milli>(T1 - T0).count();
        
        std::cout << "[GC] Collected " << Dead.size() << " objects, alive=" << Objects.size() << ". Took " << Ms << " ms." "\n";
        
        TrimAfterCollect();
        
        return Ms;
    }

    void GcManager::TrimAfterCollect()
    {
        // 1) shrink unordered_map buckets
        Objects.rehash(0);
        Objects.reserve(Objects.size()); // keep load-factor sane without immediate growth

        // 2) trim roots (keep only actual size)
        Roots.shrink_to_fit();

        // 3) Any other persistent temporary caches should be shrunk here if you add them later.
        // e.g., Mark/Gray queues kept as members (none at the moment; per-collect locals are destroyed).
    }

    void GcManager::ListObjects() const
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


    void GcManager::ListPropertiesByDebugName(const std::string& Name) const
    {
        QObject* Obj = FindByDebugName(Name);
        if (!Obj)
        {
            std::cout << "Not found: " << Name << "\n"; return;
        }
        
        auto It = Objects.find(Obj);
        const TypeInfo& Ti = *It->second.Ti;
        
        std::cout << "[Properties] " << Name << " : " << Ti.name << "\n";
        for (auto& p : Ti.properties)
        {
            std::cout << " - " << p.type << " " << p.name << " (offset " << p.offset << ")" << "\n";
        }
    }

    void GcManager::ListFunctionsByDebugName(const std::string& Name) const
    {
        QObject* Obj = FindByDebugName(Name);
        if (!Obj) { std::cout << "Not found: " << Name << "\n"; return; }
        auto It = Objects.find(Obj);
        const TypeInfo& Ti = *It->second.Ti;
        std::cout << "[Functions] " << Name << " : " << Ti.name << "\n";
        for (auto& MemberFunc : Ti.functions)
        {
            std::cout << " - " << MemberFunc.return_type << " " << MemberFunc.name << "(";
            for (size_t i = 0; i < MemberFunc.params.size(); ++i)
            {
                if (i) std::cout << ", ";
                std::cout << MemberFunc.params[i].type << " " << MemberFunc.params[i].name;
            }
            std::cout << ")\n";
        }
    }

    QObject* GcManager::FindById(uint64_t Id) const
    {
        auto It = ById.find(Id);
        return (It == ById.end()) ? nullptr : It->second;  
    }

    QObject* GcManager::FindByDebugName(const std::string& DebugName) const
    {
        auto It = NameToObjectMap.find(DebugName);
        return (It == NameToObjectMap.end()) ? nullptr : It->second; 
    }

    const TypeInfo* GcManager::GetTypeInfo(const QObject* Obj) const
    {
        auto It = Objects.find(const_cast<QObject*>(Obj));
        return It == Objects.end() ? nullptr : It->second.Ti;
    }

    bool GcManager::Link(uint64_t OwnerId, const std::string& Property, uint64_t TargetId)
    {
        QObject* Owner = FindById(OwnerId);
        QObject* Target = FindById(TargetId);
        if (!Owner || !Target) return false;

        auto ito = Objects.find(Owner);
        unsigned char* Base = BytePtr(Owner);
        for (auto& p : ito->second.Ti->properties)
        {
            if (p.name == Property && IsRawQObjectPtr(p.type))
            {
                auto* Slot = reinterpret_cast<QObject**>(Base + p.offset);
                *Slot = Target;
                std::cout << "[Link] id=" << OwnerId << "." << Property << " -> id=" << TargetId << "\n";
                return true;
            }
        }
        return false;
    }

    bool GcManager::Unlink(QObject* Object, const std::string& Property)
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
            if (IsPointerType(MetaProp.type))
            {
                auto* Slot = reinterpret_cast<QObject**>(Base + MetaProp.offset);
                *Slot = nullptr;
                std::cout << "[Unlink] Name=" << Object->GetDebugName() << "." << Property << " -> null" << "\n";
                return true;
            }

            // Handle std::vector<QObject*>
            if (IsVectorOfPointer(MetaProp.type))
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

    bool GcManager::UnlinkById(uint64_t Id, const std::string& Property)
    {
        QObject* Obj = FindById(Id);
        if (!Obj)
        {
            std::cout << "[Unlink] Object not found by Id: " << Id << "\n";
            return false;
        }

        return Unlink(Obj, Property);
    }

    bool GcManager::UnlinkByName(const std::string& Name, const std::string& Property)
    { 
        QObject* Obj = FindByDebugName(Name);

        if (!Obj)
        {
            std::cout << "[Unlink] Object not found by Name: " << Name << "\n";
            return false;
        }
        
        return Unlink(Obj, Property);
    }
    
    bool GcManager::UnlinkAllById(uint64_t OwnerId)
    {
        QObject* Owner = FindById(OwnerId);
        if (!Owner) return false;

        auto ObjectIter = Objects.find(Owner);
        unsigned char* Base = BytePtr(Owner);
        for (auto& MetaProp : ObjectIter->second.Ti->properties)
        {
            Unlink(Owner, MetaProp.name);
        }
        
        return true;
    }

    bool GcManager::UnlinkAllByName(const std::string& Name)
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

    bool GcManager::SetProperty(QObject* Obj, const std::string& Property, const std::string& Value)
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

    bool GcManager::SetPropertyById(uint64_t Id, const std::string& Property, const std::string& Value)
    {
        QObject* Obj = FindById(Id);
        if (!Obj) return false;

        return SetProperty(Obj, Property, Value);
    }

    bool GcManager::SetPropertyByName(const std::string& Name, const std::string& Property, const std::string& Value)
    {
        QObject* Obj = FindByDebugName(Name);
        if (!Obj) return false;

        return SetProperty(Obj, Property, Value);
    }

    qmeta::Variant GcManager::Call(QObject* Obj, const std::string& FuncName, const std::vector<qmeta::Variant>& Args)
    {
        if (!Obj) throw std::runtime_error("Object not found");
        auto It = Objects.find(Obj);
        if (It == Objects.end()) throw std::runtime_error("Not GC-managed");
        return qmeta::CallByName(Obj, *It->second.Ti, FuncName, Args);
    }

    qmeta::Variant GcManager::CallById(uint64_t Id, const std::string& Function, const std::vector<qmeta::Variant>& Args)
    {
        QObject* Obj = FindById(Id);
        if (!Obj) throw std::runtime_error("Object not found by Id");

        return Call(Obj, Function, Args);
    }

    qmeta::Variant GcManager::CallByName(const std::string& Name, const std::string& Function, const std::vector<qmeta::Variant>& Args)
    {
        QObject* Obj = FindByDebugName(Name);
        if (!Obj) throw std::runtime_error("Object not found by Name");

        return Call(Obj, Function, Args);
    }

    bool GcManager::Save(uint64_t Id, const std::string& FileNameIfAny)
    {
        QObject* Obj = FindById(Id);
        if (!Obj) return false;
        auto It = Objects.find(Obj);
        if (It == Objects.end()) return false;

        auto Dir = qasset::DefaultAssetDirFor(*It->second.Ti);
        const std::string Fn = FileNameIfAny.empty() ? (It->second.Ti->name + ".qasset") : FileNameIfAny;
        qasset::SaveOrThrow(Obj, *It->second.Ti, Dir, Fn);
        std::cout << "[Save] Id=" << Id << " -> " << (Dir / Fn).string() << "\n";
        return true;
    }

    bool GcManager::Load(uint64_t Id, const std::string& FileNameIfAny)
    {
        QObject* Obj = FindById(Id);
        if (!Obj) return false;
        auto It = Objects.find(Obj);
        if (It == Objects.end()) return false;

        auto Dir = qasset::DefaultAssetDirFor(*It->second.Ti);
        const std::string Fn = FileNameIfAny.empty() ? (It->second.Ti->name + ".qasset") : FileNameIfAny;
        qasset::LoadOrThrow(Obj, *It->second.Ti, Dir / Fn);
        std::cout << "[Load] Id=" << Id << " <- " << (Dir / Fn).string() << "\n";
        return true;
    }
}
