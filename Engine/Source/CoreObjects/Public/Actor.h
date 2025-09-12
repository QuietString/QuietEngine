#pragma once
#include "Object.h"
#include "qmeta_macros.h"

class QActor : public QObject
{
public:
    QPROPERTY()
    int ActorInteger;

    QFUNCTION()
    void SetActorInteger(int InValue) { ActorInteger = InValue; }
    
    QPROPERTY()
    QObject* Owner = nullptr;

    QFUNCTION()
    QObject* GetOwner() { return Owner; }
    
    QFUNCTION()
    void SetOwner(QObject* InOwner) { Owner = InOwner; }
};