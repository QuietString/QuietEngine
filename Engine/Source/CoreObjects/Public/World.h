#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

// Globally accessible singleton root object
class QWorld : public QObject
{
public:
    QWorld();
    ~QWorld() override = default;

    QPROPERTY()
    QObject* SingleObject;

    QPROPERTY()
    QObject* SingleObject2;

    QPROPERTY()
    std::vector<QObject*> Objects;
};