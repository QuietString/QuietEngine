#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

class QObject_GcTest : public QObject
{
public:
    QPROPERTY()
    QObject* ChildObject = nullptr;

    QPROPERTY()
    std::vector<QObject*> Children;
};
