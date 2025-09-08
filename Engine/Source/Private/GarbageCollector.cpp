#include "GarbageCollector.h"
#include <iostream>
#include <sstream>
#include <algorithm>

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

    void GcManager::RegisterInternal(QObject* Obj, const TypeInfo& Ti, const std::string& Name)
    {
        Node N;
        N.Ptr = Obj;
        N.Ti = &Ti;
        N.Name = Name;
        Objects_.emplace(Obj, N);
        if (!Name.empty())
        {
            //ByName_[Name] = Obj;
            //std::cout << "[GC] Registered " << Name << " : " << Ti.name << std::endl;
        }
        else
        {
            std::cout << "[GC] Warning: unnamed object " << Obj << std::endl; 
        }
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
        auto* P = NewObject<QWorld>("Root");
        AddRoot(P);
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

    bool GcManager::IsQObjectPointerType(const std::string& Type)
    {
        // Very naive: treat any "X*" as candidate; we will filter by managed set membership.
        for (char c : Type)
        {
            if (c == '*') return true;
        }
        return false;
    }

    void GcManager::TraversePointers(QObject* Obj, const TypeInfo& Ti, std::vector<QObject*>& OutChildren) const
    {
        OutChildren.clear();
        unsigned char* Base = BytePtr(Obj);
        for (const MetaProperty& P : Ti.properties)
        {
            if (!IsQObjectPointerType(P.type)) continue;

            QObject** Slot = reinterpret_cast<QObject**>(Base + P.offset);
            if (Slot && *Slot)
            {
                // Only follow if the child is tracked by the GC
                auto It = Objects_.find(*Slot);
                if (It != Objects_.end())
                {
                    OutChildren.push_back(*Slot);
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

            auto it = Objects_.find(Cur);
            if (it == Objects_.end()) continue;
            Node& N = it->second;
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
        for (auto& Pair : Objects_)
        {
            Pair.second.Marked = false;
        }

        // 2) Mark from roots
        for (QObject* R : Roots)
        {
            Mark(R);
        }

        // 3) Build list of dead objects
        std::vector<QObject*> Dead;
        Dead.reserve(Objects_.size());
        for (auto& Pair : Objects_)
        {
            if (!Pair.second.Marked)
            {
                Dead.push_back(Pair.first);
            }
        }

        // 4) Null-out references to dead objects in survivors to avoid dangling pointers
        for (auto& Pair : Objects_)
        {
            if (!Pair.second.Marked) continue;
            QObject* Owner = Pair.first;
            unsigned char* Base = BytePtr(Owner);
            const TypeInfo& Ti = *Pair.second.Ti;
            for (const MetaProperty& P : Ti.properties)
            {
                if (!IsQObjectPointerType(P.type)) continue;
                QObject** Slot = reinterpret_cast<QObject**>(Base + P.offset);
                if (Slot && *Slot)
                {
                    if (Objects_.count(*Slot) && !Objects_.at(*Slot).Marked)
                    {
                        *Slot = nullptr;
                    }
                }
            }
        }

        // 5) Delete dead and remove from maps
        for (QObject* D : Dead)
        {
            auto It = Objects_.find(D);
            if (It != Objects_.end())
            {
                if (!It->second.Name.empty())
                {
                    auto itn = ByName_.find(It->second.Name);
                    if (itn != ByName_.end() && itn->second == D)
                    {
                        ByName_.erase(itn);
                    }
                }
                delete It->second.Ptr;
                Objects_.erase(It);
            }
        }

        std::cout << "[GC] Collected " << Dead.size() << " objects, alive=" << Objects_.size() << std::endl;
    }

    void GcManager::ListObjects() const
    {
        std::cout << "[Objects]\n";
        for (auto& kv : Objects_)
        {
            const Node& N = kv.second;
            std::cout << " - " << (N.Name.empty() ? "(unnamed)" : N.Name) << " : " << N.Ti->name << std::endl;
        }
    }

    void GcManager::ListProperties(const std::string& Name) const
    {
        QObject* Obj = FindByName(Name);
        if (!Obj) { std::cout << "Not found: " << Name << "\n"; return; }
        auto it = Objects_.find(Obj);
        const TypeInfo& Ti = *it->second.Ti;
        std::cout << "[Props] " << Name << " : " << Ti.name << "\n";
        for (auto& p : Ti.properties)
        {
            std::cout << " - " << p.type << " " << p.name << " (offset " << p.offset << ")" << std::endl;
        }
    }

    void GcManager::ListFunctions(const std::string& Name) const
    {
        QObject* Obj = FindByName(Name);
        if (!Obj) { std::cout << "Not found: " << Name << "\n"; return; }
        auto it = Objects_.find(Obj);
        const TypeInfo& Ti = *it->second.Ti;
        std::cout << "[Funcs] " << Name << " : " << Ti.name << "\n";
        for (auto& f : Ti.functions)
        {
            std::cout << " - " << f.return_type << " " << f.name << "(";
            for (size_t i = 0; i < f.params.size(); ++i)
            {
                if (i) std::cout << ", ";
                std::cout << f.params[i].type << " " << f.params[i].name;
            }
            std::cout << ")\n";
        }
    }

    QObject* GcManager::FindByName(const std::string& Name) const
    {
        auto it = ByName_.find(Name);
        return it == ByName_.end() ? nullptr : it->second;
    }

    const TypeInfo* GcManager::GetTypeInfo(const QObject* Obj) const
    {
        auto It = Objects_.find(const_cast<QObject*>(Obj));
        return It == Objects_.end() ? nullptr : It->second.Ti;
    }

    bool GcManager::Link(const std::string& OwnerName, const std::string& Property, const std::string& TargetName)
    {
        QObject* Owner = FindByName(OwnerName);
        QObject* Target = FindByName(TargetName);
        if (!Owner || !Target) return false;
        auto ito = Objects_.find(Owner);
        if (ito == Objects_.end()) return false;
        unsigned char* Base = BytePtr(Owner);
        for (auto& p : ito->second.Ti->properties)
        {
            if (p.name == Property && IsQObjectPointerType(p.type))
            {
                auto* slot = reinterpret_cast<QObject**>(Base + p.offset);
                *slot = Target;
                std::cout << "[Link] " << OwnerName << "." << Property << " -> " << TargetName << "\n";
                return true;
            }
        }
        return false;
    }

    bool GcManager::Unlink(const std::string& OwnerName, const std::string& Property)
    {
        QObject* Owner = FindByName(OwnerName);
        if (!Owner) return false;
        auto ito = Objects_.find(Owner);
        if (ito == Objects_.end()) return false;
        unsigned char* Base = BytePtr(Owner);
        for (auto& p : ito->second.Ti->properties)
        {
            if (p.name == Property && IsQObjectPointerType(p.type))
            {
                auto* slot = reinterpret_cast<QObject**>(Base + p.offset);
                *slot = nullptr;
                std::cout << "[Unlink] " << OwnerName << "." << Property << " -> null\n";
                return true;
            }
        }
        return false;
    }

    bool GcManager::SetPropertyFromString(const std::string& Name, const std::string& Property, const std::string& Value)
    {
        QObject* Obj = FindByName(Name);
        if (!Obj) return false;
        auto It = Objects_.find(Obj);
        if (It == Objects_.end()) return false;
        unsigned char* Base = BytePtr(Obj);

        for (auto& p : It->second.Ti->properties)
        {
            if (p.name != Property) continue;
            // very small set: int/float/bool/string
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

    qmeta::Variant GcManager::Call(const std::string& Name, const std::string& Function, const std::vector<qmeta::Variant>& Args)
    {
        QObject* Obj = FindByName(Name);
        if (!Obj) throw std::runtime_error("Object not found: " + Name);
        auto it = Objects_.find(Obj);
        if (it == Objects_.end()) throw std::runtime_error("Not GC-managed: " + Name);
        return qmeta::CallByName(Obj, *it->second.Ti, Function, Args);
    }

    bool GcManager::Save(const std::string& Name, const std::string& FileNameIfAny)
    {
        QObject* Obj = FindByName(Name);
        if (!Obj) return false;
        auto it = Objects_.find(Obj);
        if (it == Objects_.end()) return false;

        auto dir = qasset::DefaultAssetDirFor(*it->second.Ti);
        const std::string fn = FileNameIfAny.empty() ? (it->second.Ti->name + ".quasset") : FileNameIfAny;
        qasset::SaveOrThrow(Obj, *it->second.Ti, dir, fn);
        std::cout << "[Save] " << Name << " -> " << (dir / fn).string() << std::endl;
        return true;
    }

    bool GcManager::Load(const std::string& TypeName, const std::string& Name, const std::string& FileNameIfAny)
    {
        QObject* Obj = FindByName(Name);
        if (!Obj) return false;
        auto it = Objects_.find(Obj);
        if (it == Objects_.end()) return false;
        if (it->second.Ti->name != TypeName) return false;

        auto dir = qasset::DefaultAssetDirFor(*it->second.Ti);
        const std::string fn = FileNameIfAny.empty() ? (it->second.Ti->name + ".quasset") : FileNameIfAny;
        qasset::LoadOrThrow(Obj, *it->second.Ti, dir / fn);
        std::cout << "[Load] " << Name << " <- " << (dir / fn).string() << std::endl;
        return true;
    }
}
