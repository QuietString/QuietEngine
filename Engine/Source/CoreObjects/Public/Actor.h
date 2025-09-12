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
    QActor* Owner = nullptr;

    QFUNCTION()
    QActor* GetOwner() { return Owner; }
    
    QFUNCTION()
    void SetOwner(QActor* InOwner) { Owner = InOwner; }
};