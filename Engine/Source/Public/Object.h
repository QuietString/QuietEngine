#pragma once

#include "ObjectBase.h"

class QObject : public QObjectBase
{
public:
    virtual ~QObject() = default;

    // Example shared property (optional).
    // Not marked QPROPERTY on purpose; derived classes can expose their own.
    void SetDebugName(const std::string& name)
    {
        DebugName_ = name;
    }

    const std::string& GetDebugName() const
    {
        return DebugName_;
    }

private:
    std::string DebugName_;
};
