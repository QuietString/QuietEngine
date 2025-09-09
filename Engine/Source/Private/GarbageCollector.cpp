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

    static inline unsigned char* BytePtr(void* p)
    {
        return static_cast<unsigned char*>(p);
    }

    static inline const unsigned char* BytePtr(const void* p)
    {
        return static_cast<const unsigned char*>(p);
    }

    // Helpers
    static inline bool EndsWithStar(const std::string& s)
    {
        return !s.empty() && s.back() == '*';
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

    void GcManager::TraversePointers(QObject* Obj, const TypeInfo& Ti, std::vector<QObject*>& OutChildren) const
    {
        OutChildren.clear();
        unsigned char* Base = BytePtr(Obj);
        for (const MetaProperty& P : Ti.properties)
        {
            if (IsRawQObjectPtr(P.type))
            {
                // Raw QObject*
                QObject** Slot = reinterpret_cast<QObject**>(Base + P.offset);
                if (Slot && *Slot)
                {
                    auto It = Objects.find(*Slot);
                    if (It != Objects.end())
                        OutChildren.push_back(*Slot);
                }
            }
            else if (IsVectorOfQObjectPtr(P.type))
            {
                // std::vector<QObject*>
                auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + P.offset);
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

    bool GcManager::IsPointerProperty(const std::string& TypeStr)
    {
        return TypeStr.find('*') != std::string::npos;
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

    void GcManager::Collect()
    {
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

        std::cout << "[GC] Collected " << Dead.size() << " objects, alive=" << Objects.size() << std::endl;
    }

    void GcManager::ListObjects() const
    {
        std::cout << "[Objects]\n";
        for (auto& kv : Objects)
        {
            const Node& N = kv.second;
            const std::string& Nm = kv.first->GetDebugName();
            std::cout << " - Name=" << (Nm.empty() ? "(Unnamed)" : Nm) << std::endl;
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
            std::cout << " - " << p.type << " " << p.name << " (offset " << p.offset << ")" << std::endl;
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
            if (p.name == Property && IsPointerProperty(p.type))
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
            if (IsRawQObjectPtr(MetaProp.type))
            {
                auto* Slot = reinterpret_cast<QObject**>(Base + MetaProp.offset);
                *Slot = nullptr;
                std::cout << "[Unlink] Name=" << Object->GetDebugName() << "." << Property << " -> null" << std::endl;
                return true;
            }

            // Handle std::vector<QObject*>
            if (IsVectorOfQObjectPtr(MetaProp.type))
            {
                auto* Vec = reinterpret_cast<std::vector<QObject*>*>(Base + MetaProp.offset);
                // Remove all references held by the vector
                for (QObject*& E : *Vec) { E = nullptr; }
                Vec->clear();
                std::cout << "[Unlink] Name=" << Object->GetDebugName() << "." << Property << " -> cleared vector" << std::endl;
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
            std::cout << "[Unlink] Object not found by Id: " << Id << std::endl;
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
