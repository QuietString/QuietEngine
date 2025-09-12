#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

class QTestObject_Parent : public QObject
{
public:
    QPROPERTY()
    std::vector<QObject*> Children_Parent;
};
