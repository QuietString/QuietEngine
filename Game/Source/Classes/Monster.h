#pragma once
#include "Actor.h"

class QMonster : public QActor
{
public:
    QPROPERTY()
    int Health = 100;

    QFUNCTION()
    int GetHealth() const { return Health; }

    QFUNCTION()
    void SetHealth(int InHealth) { Health = InHealth; }
    
    QFUNCTION()
    bool IsDead() const { return Health <= 0; }

    QFUNCTION()
    void TakeDamage(int Damage) { Health -= Damage; }

    QPROPERTY()
    QActor* Target = nullptr;

    QFUNCTION()
    QActor* GetTarget() const { return Target; }
    
    QFUNCTION()
    void SetTarget(QActor* InTarget) { Target = InTarget; }
};
