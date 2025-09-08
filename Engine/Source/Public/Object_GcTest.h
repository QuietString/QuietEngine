#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

class QObject_GcTest : public QObject
{
public:
    int DummyMember;
    
    QPROPERTY()
    QObject* ChildObject;
};
