#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

class QWorld : public QObject
{
public:
    QWorld() = default;
    ~QWorld() override = default;

    QPROPERTY()
    std::vector<QObject*> Objects;
    
    static void SetWorldSingleton(QWorld* World);
};

QWorld* GetWorld();