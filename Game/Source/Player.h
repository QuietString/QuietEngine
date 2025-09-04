#pragma once
#include "qmeta_macros.h"

class Player {
public:
    QPROPERTY(EditAnywhere, Category="Stats", ClampMin=0, ClampMax=100)
    int Health = 100;

    QPROPERTY(EditAnywhere, Category="Stats")
    float WalkSpeed = 600.0f;

    QFUNCTION()
    int AddHealth(int Delta);

    QFUNCTION()
    void SetWalkSpeed(float Speed);
};