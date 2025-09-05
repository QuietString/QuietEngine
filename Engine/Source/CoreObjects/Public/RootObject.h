#pragma once
#include <vector>

#include "Object.h"
#include "qmeta_macros.h"

class QRootObject : public QObject
{
public:

    QPROPERTY()
    std::vector<QObject*> Objects;
};
