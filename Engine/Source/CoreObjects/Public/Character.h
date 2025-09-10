#pragma once
#include "Actor.h"
#include "qmeta_macros.h"

class QCharacter : public QActor
{
public:
    QPROPERTY()
    int Health = 100;

    QPROPERTY()
    float TestValue = 3.0f;
};
