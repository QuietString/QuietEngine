#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>

#include "Object.h"
#include "qmeta_runtime.h"
#include "Asset.h"
#include "TypeName.h"

namespace QGC
{
    class GcManager
    {
    public:
        static GcManager& Get();

        // template <class T, class... Args>
        // T* NewObject(Args&&... args)
        // {
        //     static_assert(std::is_base_of_v<QObject, T>, "T must derive QObject");
        //     const qmeta::TypeInfo* Ti = qmeta::GetRegistry().find(qtype::TypeName<T>());
        //     if (!Ti)
        //     {
        //         throw std::runtime_error(std::string("TypeInfo not found for ") + std::string(qtype::TypeName<T>()));
        //     }
        //     
        //     T* Obj = new T(std::forward<Args>(args)...);
        //     
        //     const uint64_t id = NextGlobalId.fetch_add(1, std::memory_order_relaxed) + 1; // start at 1
        //     Obj->SetObjectId(id);
        //     
        //     // Auto debug-name: ClassName_ID
        //     std::string AutoName;
        //     AutoName.reserve(Ti->name.size() + 20);
        //     AutoName.append(Ti->name).push_back('_');
        //     AutoName.append(std::to_string(id));
        //     Obj->SetDebugName(AutoName);
        //     
        //     RegisterInternal(Obj, *Ti, AutoName, id);
        //     return Obj;
        // }

        // Create by type name (for console)
        QObject* NewByTypeName(const std::string& TypeName, const std::string& Name);

        void Initialize();
        
        // Roots
        QObject* GetRoot() const { return Roots.empty() ? nullptr : Roots[0]; }
        void AddRoot(QObject* Obj);
        void RemoveRoot(QObject* Obj);

        // GC steps
        void Tick(double DeltaSeconds);
        void Collect();
        void SetAutoInterval(double Seconds);

        // Debug utilities
        void ListObjects() const;
        void ListPropertiesByDebugName(const std::string& Name) const;
        void ListFunctionsByDebugName(const std::string& Name) const;
        
        bool Link(uint64_t OwnerId, const std::string& Property, uint64_t TargetId);
        bool Unlink(uint64_t OwnerId, const std::string& Property);
        bool UnlinkAllProperties(uint64_t OwnerId);
        bool UnlinkAllByName(const std::string& Name);
        bool UnlinkByName(const std::string& Name, const std::string& Property);
        bool SetProperty(QObject* Obj, const std::string& Property, const std::string& Value);
        bool SetPropertyById(uint64_t Id, const std::string& Property, const std::string& Value);
        bool SetPropertyByName(const std::string& Name, const std::string& Property, const std::string& Value);
        qmeta::Variant CallById(uint64_t Id, const std::string& Function, const std::vector<qmeta::Variant>& Args);
        qmeta::Variant CallByName(const std::string& Name, const std::string& Function, const std::vector<qmeta::Variant>& Args);

        // Asset IO
        bool Save(uint64_t Id, const std::string& FileNameIfAny);
        bool Load(uint64_t Id, const std::string& FileNameIfAny);
        
        // Lookup
        QObject* FindById(uint64_t Id) const;
        QObject* FindByDebugName(const std::string& DebugName) const;
        
        // Access stored TypeInfo for an object
        const qmeta::TypeInfo* GetTypeInfo(const QObject* Obj) const;

    public:
        void RegisterInternal(QObject* Obj, const qmeta::TypeInfo& Ti, const std::string& Name, uint64_t Id);

    private:
        struct Node
        {
            QObject* Ptr = nullptr;
            const qmeta::TypeInfo* Ti = nullptr;
            uint64_t Id = 0;
            bool Marked = false;
        };


        // Marks all objects from a root to kill by BFS 
        void Mark(QObject* Root);
        
        void TraversePointers(QObject* Obj, const qmeta::TypeInfo& Ti, std::vector<QObject*>& OutChildren) const;
        static bool IsPointerProperty(const std::string& TypeStr);
        static bool IsQObjectPointerType(const std::string& Type);
        
    private:
        std::unordered_map<QObject*, Node> Objects;
        std::unordered_map<uint64_t, QObject*> ById;
        std::unordered_map<std::string, QObject*> NameToObjectMap;
        
        std::vector<QObject*> Roots;
        double Accumulated = 0.0;
        
        // Auto collect time interval in seconds. Disabled when less than or equal to zero.
        double Interval = 2.0;

    public:
        static bool IsRawQObjectPtr(const std::string& type);
        static bool IsVectorOfQObjectPtr(const std::string& type);
        static unsigned char* BytePtr(void* p) { return static_cast<unsigned char*>(p); }
    };
}
