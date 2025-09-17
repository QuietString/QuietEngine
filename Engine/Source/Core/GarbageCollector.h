#pragma once
#include "Object.h"
#include "qmeta_runtime.h"

class GarbageCollector
{
public:
    static GarbageCollector& Get();

    static void SetGcSingleton(GarbageCollector* Gc);

    void Initialize();

    // Object Factory
public:
    using FactoryFunc = QObject*(*)();

    static void RegisterTypeFactory(const std::string& TypeName, FactoryFunc Fn);
    static QObject* NewObjectByName(const std::string& TypeName);

public:
    // === GC threading control ===
    // 0: auto (uses std::thread::hardware_concurrency)
    // 1: single-thread (falls back to Mark())
    // N>=2: use N threads for MarkParallel()
    void SetMaxGcThreads(int InThreads);
    int  GetMaxGcThreads() const;
    
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

    void SetAllowTraverseParents(bool bEnable);
    bool GetAllowTraverseParents() const;
    
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
    // --- GC fast paths ---
    struct FPtrOffsetLayout
    {
        std::vector<std::size_t> RawOffsets; // T*: QObject*
        std::vector<std::size_t> VecOffsets; // std::vector<T*>
    };
    
    struct Node
    {
        const qmeta::TypeInfo* Ti = nullptr;
        uint64_t Id = 0;

        // Cached layout pointer (stable heap address)
        const FPtrOffsetLayout* Layout = nullptr;
        
        // atomic for parallel mark
        std::atomic<uint32_t> MarkEpoch { 0 }; 

        // Back pointer to the actual object to avoid map lookup on expansion
        QObject* Self = nullptr;

        Node() = default;
        Node(QObject* InSelf, const qmeta::TypeInfo* InTi, uint64_t InId)
            : Ti(InTi), Id(InId), MarkEpoch{0}, Self(InSelf)
        {}

        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
        Node(Node&&) noexcept = default;
        Node& operator=(Node&&) noexcept = default;
    };

    // Layout cache: stable addresses via unique_ptr so node Layout pointers never invalidate on rehash
    mutable std::unordered_map<const qmeta::TypeInfo*, std::unique_ptr<FPtrOffsetLayout>> PtrCache;
    
    //mutable std::unordered_map<const qmeta::TypeInfo*, FPtrOffsetLayout> PtrCache;
    // protects PtrCache build-once
    mutable std::mutex PtrCacheMutex;
    uint32_t CurrentEpoch = 1;

    // Threading knob (see SetMaxGcThreads)
    int MaxGcThreads = 0;

    const FPtrOffsetLayout* GetPtrLayout(const qmeta::TypeInfo& Ti);
    //const FPtrOffsetLayout& GetPtrLayout(const qmeta::TypeInfo& Ti);
    
    // Marks all objects from a root to kill by BFS 
    void Mark();

    // Return num threads
    int MarkParallel();
    
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
    static bool IsPointerType(const qmeta::MetaProperty& MetaProp) { return (MetaProp.GcFlags & qmeta::PF_RawQObjectPtr) != 0; }
    static bool IsPointerType(const std::string& Type);

    static bool IsVectorOfPointer(const std::string& Type);
    static bool IsVectorOfPointer(const qmeta::MetaProperty& MetaProp) { return (MetaProp.GcFlags & qmeta::PF_VectorOfQObjectPtr) != 0; }

    static unsigned char* BytePtr(void* p) { return static_cast<unsigned char*>(p); }

    // Whether a pointer is tracked by GC (safe check for console formatting etc.)
    bool IsManaged(const QObject* Obj) const;

private:
    // Debug usage only
    bool bAllowTraverseParents = true;
};
