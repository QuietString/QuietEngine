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

    QFUNCTION()
    void SpawnObject();
    
    QFUNCTION()
    void AddObject(const std::string& ObjName);

    QFUNCTION()
    void RemoveObject(const std::string& ObjName);
    
    static void SetWorldSingleton(QWorld* World);
};

QWorld* GetWorld();