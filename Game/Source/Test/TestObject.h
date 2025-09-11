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
    QTestObject* Friend1 = nullptr;

    QPROPERTY()
    QTestObject* Friend2 = nullptr;

    QPROPERTY()
    QTestObject* Friend3 = nullptr;

    QPROPERTY()
    QTestObject* Friend4 = nullptr;
    
    QPROPERTY()
    QTestObject* Friend5 = nullptr;
    
    QFUNCTION()
    void RemoveChildren() { Children.clear(); }
    
    QPROPERTY()
    std::vector<QTestObject*> Children;
};