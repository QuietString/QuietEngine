#pragma once
#include <string>

// Minimal base for reflection/GC-ready objects.
class QObject
{
public:
    virtual ~QObject() = default;
    
    void SetDebugName(const std::string& Name)
    {
        DebugName_ = Name;
    }

    const std::string& GetDebugName() const
    {
        return DebugName_;
    }

private:
    std::string DebugName_;
};
