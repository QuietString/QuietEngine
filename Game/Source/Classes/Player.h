#pragma once
#include "Object.h"
#include "qmeta_macros.h"


class QPlayer : public QObject
{
public:
    QPROPERTY()
    int Health = 100;
    
    QPROPERTY()
    float WalkSpeed = 600.0f;
    
    QPROPERTY()
    QPlayer* Friend = nullptr;
    
    QFUNCTION()
    int AddHealth(int Delta);
    
    QFUNCTION()
    void SetWalkSpeed(float Speed);
};