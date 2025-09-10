#pragma once
#include "Object.h"
#include "qmeta_macros.h"

class QActor : public QObject
{
public:
    QPROPERTY()
    std::string Temp1;

    QPROPERTY()
    std::string Temp2;
};
