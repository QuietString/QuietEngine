#pragma once
#include "Object.h"
#include "qmeta_runtime.h"

class GarbageCollector
{
public:
    static GarbageCollector& Get();

    static void SetGcSingleton(GarbageCollector* Gc);
    
    // Create by type name (for console)
    QObject* NewByTypeName(const std::string& TypeName, const std::string& Name);

    void Initialize();
    
    // Roots
    QObject* GetRoot() const { return Roots.empty() ? nullptr : Roots[0]; }
    void AddRoot(QObject* Obj);
    void RemoveRoot(QObject* Obj);
    
    // GC steps
    void Tick(double DeltaSeconds);

    // Return execution time(ms).
    double Collect(bool bSilent = false);

    void SetAutoInterval(double Seconds);

    // Debug utilities
    void ListObjects() const;
    void ListPropertiesByDebugName(const std::string& Name) const;
    void ListFunctionsByDebugName(const std::string& Name) const;
    
    bool Unlink(QObject* Object, const std::string& Property);
    //bool UnlinkById(uint64_t Id, const std::string& Property);
    bool UnlinkByName(const std::string& Name, const std::string& Property);
    //bool UnlinkAllById(uint64_t OwnerId);
    bool UnlinkAllByName(const std::string& Name);
    bool SetProperty(QObject* Obj, const std::string& Property, const std::string& Value);
    //bool SetPropertyById(uint64_t Id, const std::string& Property, const std::string& Value);
    bool SetPropertyByName(const std::string& Name, const std::string& Property, const std::string& Value);

    qmeta::Variant Call(QObject* Obj, const std::string& FuncName, const std::vector<qmeta::Variant>& Args);
    //qmeta::Variant CallById(uint64_t Id, const std::string& Function, const std::vector<qmeta::Variant>& Args);
    qmeta::Variant CallByName(const std::string& Name, const std::string& Function, const std::vector<qmeta::Variant>& Args);

    // Asset IO
    
    //bool Save(uint64_t Id, const std::string& FileNameIfAny);
    //bool Load(uint64_t Id, const std::string& FileNameIfAny);
    
    // Lookup
    //QObject* FindById(uint64_t Id) const;
    QObject* FindByDebugName(const std::string& DebugName) const;
    
    // Access stored TypeInfo for an object
    const qmeta::TypeInfo* GetTypeInfo(const QObject* Obj) const;

public:
    void RegisterInternal(QObject* Obj, const qmeta::TypeInfo& Ti, const std::string& Name, uint64_t Id);

private:
    
    enum class SlotKind : uint8_t { RawQObjectPtr, VectorOfQObjectPtr };

    struct FieldSlot {
        uint32_t offset = 0;
        SlotKind kind;
    };

    using Plan = std::vector<FieldSlot>;
    
    mutable std::unordered_map<const qmeta::TypeInfo*, Plan> PlanCache;

    uint32_t CurrentEpoch = 1;

    mutable std::vector<QObject*> ScratchStack;
    mutable std::vector<QObject*> ScratchChildren;
    
    struct Node
    {
        const qmeta::TypeInfo* Ti = nullptr;
        uint64_t Id = 0;
        bool Marked = false;
        uint32_t MarkEpoch = 0;
    };

private:
    const Plan& GetPlan(const qmeta::TypeInfo& Ti) const;
    
    // Marks all objects from a root to kill by BFS 
    void Mark(QObject* Root);
    
    void TraversePointers(QObject* Obj, const qmeta::TypeInfo& Ti, std::vector<QObject*>& OutChildren) const;
    
private:
    std::unordered_map<QObject*, Node> Objects;
    //std::unordered_map<uint64_t, QObject*> ById;
    std::unordered_map<std::string, QObject*> NameToObjectMap;
    
    std::vector<QObject*> Roots;
    double Accumulated = 0.0;
    
    // Auto collect time interval in seconds. Disabled when less than or equal to zero.
    double Interval = 2.0;

public:
    static bool IsPointerType(const std::string& Type);
    static bool IsVectorOfPointer(const std::string& Type);
    
    static bool IsRawQObjectPtr(const std::string& type);
    static bool IsVectorOfQObjectPtr(const std::string& type);
    static unsigned char* BytePtr(void* p) { return static_cast<unsigned char*>(p); }

    // Whether a pointer is tracked by GC (safe check for console formatting etc.)
    bool IsManaged(const QObject* Obj) const;
};
