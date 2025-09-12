#pragma once
#include <vector>

#include "Actor.h"
#include "Object.h"
#include "qmeta_macros.h"


class QPlayer : public QActor
{
public:
    QPROPERTY()
    float WalkSpeed = 600.0f;

    QPROPERTY()
    std::string Name = "NoName";
    
    QPROPERTY()
    QPlayer* Friend = nullptr;

    QPROPERTY()
    std::vector<QPlayer*> Friends;

    QFUNCTION()
    void SetWalkSpeed(float Speed);
};