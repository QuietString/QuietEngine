#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

class QObject_GcTest : public QObject
{
public:
    QPROPERTY()
    int Integer = 0;

    QFUNCTION()
    void SetInteger(int InValue) { Integer = InValue; }

    QFUNCTION()
    void RemoveChildren() { Children.clear(); }
    
    QPROPERTY()
    std::vector<QObject_GcTest*> Children;
};
