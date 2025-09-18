#pragma once

#include "ObjectBase.h"

class QObject : public QObjectBase
{
public:
    ~QObject() override = default;

    bool bGcIgnoredSelfAndBelow = false;
};
