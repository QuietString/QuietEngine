#pragma once
#include <string>

#include "Object.h"
#include "qmeta_runtime.h"

class EngineUtils
{
public:
    static std::string FormatPropertyValue(QObject* Owner, const qmeta::MetaProperty& P);

    static std::string FormatPropertyValue(const qmeta::Variant& V);
};
