#pragma once
#include "qmeta_macros.h"


class Player
{
public:
    QPROPERTY()
    int Health = 100;
    
    QPROPERTY()
    float WalkSpeed = 600.0f;
    
    QFUNCTION()
    int AddHealth(int Delta);
    
    QFUNCTION()
    void SetWalkSpeed(float Speed);
};