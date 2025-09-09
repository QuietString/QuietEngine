#pragma once
#include <string>

// Minimal base for reflection/GC-ready objects.
class QObjectBase
{
public:
    QObjectBase() = default;
    virtual ~QObjectBase() = default;

    uint64_t GetObjectId() const { return ObjectId; }
    void SetObjectId(const uint64_t Id) { ObjectId = Id; }

    const std::string& GetDebugName() const { return DebugName; }
    void SetDebugName(const std::string& name) { DebugName = name; }

    // --- Custom allocation for all QObject-derived types ---
    // Unaligned
    static void* operator new(std::size_t sz);
    static void  operator delete(void* p) noexcept;
    static void  operator delete(void* p, std::size_t sz) noexcept; // sized delete

    // Over-aligned
    static void* operator new(std::size_t sz, std::align_val_t al);
    static void  operator delete(void* p, std::align_val_t al) noexcept;
    static void  operator delete(void* p, std::size_t sz, std::align_val_t al) noexcept;

private:
    uint64_t ObjectId = 0;
    std::string DebugName;
};
