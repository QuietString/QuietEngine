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
    
private:
    uint64_t ObjectId = 0;
    std::string DebugName;
};
