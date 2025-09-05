#pragma once
#include "Object.h"
#include "qmeta_macros.h"

class Controller : public QObject
{
public:
    QPROPERTY()
    int ControllerID = 5;

    QFUNCTION()
    void SetControllerID(int ID) { ControllerID = ID;}
};
