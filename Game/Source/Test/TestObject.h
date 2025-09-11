#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

class QTestObject : public QObject
{
public:
    QPROPERTY()
    int Integer = 0;

    QFUNCTION()
    void SetInteger(int InValue) { Integer = InValue; }
    
    QFUNCTION()
    void RemoveFriend(int Idx);
    
    QPROPERTY()
    QTestObject* Friend1;

    QPROPERTY()
    QTestObject* Friend2;

    QPROPERTY()
    QTestObject* Friend3;

    QFUNCTION()
    void RemoveChildren() { Children.clear(); }
    
    QPROPERTY()
    std::vector<QTestObject*> Children;
};