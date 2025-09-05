#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>

#include "Object.h"
#include "qmeta_runtime.h"
#include "Asset.h"
#include "Object.h"
#include "TypeName.h"

namespace QGC
{
    class GcManager
    {
    public:
        static GcManager& Get();

        template <class T, class... Args>
        T* NewObject(const std::string& Name, Args&&... args)
        {
            static_assert(std::is_base_of_v<QObject, T>, "T must derive QObject");
            const qmeta::TypeInfo* Ti = qmeta::GetRegistry().find(qtype::TypeName<T>());
            if (!Ti)
            {
                throw std::runtime_error(std::string("TypeInfo not found for ") + std::string(qtype::TypeName<T>()));
            }
            T* Obj = new T(std::forward<Args>(args)...);
            Obj->SetDebugName(Name);
            RegisterInternal(Obj, *Ti, Name);
            return Obj;
        }

        // Create by type name (for console)
        QObject* NewByTypeName(const std::string& TypeName, const std::string& Name);

        void Initialize(float InTickInterval);
        
        // Roots
        void AddRoot(QObject* Obj);
        void RemoveRoot(QObject* Obj);

        // GC steps
        void Tick(double DeltaSeconds);
        void Collect();
        void SetAutoInterval(double Seconds);

        // Debug utilities
        void ListObjects() const;
        void ListProperties(const std::string& Name) const;
        void ListFunctions(const std::string& Name) const;

        // High-level helpers used by console
        bool Link(const std::string& OwnerName, const std::string& Property, const std::string& TargetName);
        bool Unlink(const std::string& OwnerName, const std::string& Property);
        bool SetPropertyFromString(const std::string& Name, const std::string& Property, const std::string& Value);
        qmeta::Variant Call(const std::string& Name, const std::string& Function, const std::vector<qmeta::Variant>& Args);

        // Asset IO
        bool Save(const std::string& Name, const std::string& FileNameIfAny);
        bool Load(const std::string& TypeName, const std::string& Name, const std::string& FileNameIfAny);

        // Lookup
        QObject* FindByName(const std::string& Name) const;

        // Access stored TypeInfo for an object
        const qmeta::TypeInfo* GetTypeInfo(const QObject* Obj) const;
    
    private:
        struct Node
        {
            QObject* Ptr = nullptr;
            const qmeta::TypeInfo* Ti = nullptr;
            std::string Name;
            bool Marked = false;
        };

        void RegisterInternal(QObject* Obj, const qmeta::TypeInfo& Ti, const std::string& Name);
        void Mark(QObject* Root);
        void TraversePointers(QObject* Obj, const qmeta::TypeInfo& Ti, std::vector<QObject*>& OutChildren) const;
        static bool IsQObjectPointerType(const std::string& Type);

    private:
        std::unordered_map<QObject*, Node> Objects_;
        std::unordered_map<std::string, QObject*> ByName_;
        std::vector<QObject*> Roots;
        double Accumulated = 0.0;
        
        // Auto collect time interval in seconds. Disabled when less than or equal to zero.
        double Interval = 2.0;
    };
}
