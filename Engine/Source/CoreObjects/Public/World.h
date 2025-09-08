#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

// Globally accessible singleton root object
class QWorld : public QObject
{
public:

    static QWorld* Get() { static QWorld W; return &W;}
    
    QPROPERTY()
    std::vector<QObject*> Objects;

    QPROPERTY()
    QObject* SingleObject;
};
