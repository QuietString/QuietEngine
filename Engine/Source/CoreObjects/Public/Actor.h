#pragma once
#include "Object.h"
#include "qmeta_macros.h"

class QActor : public QObject
{
public:
    QPROPERTY()
    int ActorInteger;

    QPROPERTY()
    QActor* Owner = nullptr;
    
    QFUNCTION()
    void SetActorInteger(int InValue) { ActorInteger = InValue; }
};
