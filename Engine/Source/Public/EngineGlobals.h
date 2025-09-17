#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "GarbageCollector.h"
#include "Object.h"
#include "qmeta_runtime.h"
#include "TypeName.h"

static inline std::atomic<uint64_t> NextGlobalId {0};

template <class T, class... Args>
T* NewObject(Args&&... args)
{
    static_assert(std::is_base_of_v<QObject, T>, "T must derive QObject");
    const qmeta::TypeInfo* Ti = qmeta::GetRegistry().find(qtype::TypeName<T>());
    if (!Ti)
    {
        throw std::runtime_error(std::string("TypeInfo not found for ") + std::string(qtype::TypeName<T>()));
    }
    
    T* Obj = new T(std::forward<Args>(args)...);
    
    const uint64_t Id = NextGlobalId.fetch_add(1, std::memory_order_relaxed) + 1; // start at 1
    Obj->SetObjectId(Id);
    
    // Auto debug-name: ClassName_ID
    std::string AutoName;
    AutoName.reserve(Ti->name.size() + 20);
    AutoName.append(Ti->name).push_back('_');
    AutoName.append(std::to_string(Id));
    Obj->SetDebugName(AutoName);

    GarbageCollector& GC = GarbageCollector::Get();
    GC.RegisterInternal(Obj, *Ti, AutoName, Id);
    return Obj;
}

// ----- Factory helpers for QHT -----
namespace qht_factories
{
    template<class T>
    QObject* DefaultFactoryThunk()
    {
        if constexpr (std::is_default_constructible_v<T> &&
                      !std::is_abstract_v<T> &&
                      std::is_base_of_v<QObject, T>)
        {
            return NewObject<T>();
        }
        else
        {
            return nullptr; // non-default-constructible or abstract
        }
    }

    template<class T>
    void RegisterIfCreatable(const char* Name)
    {
        if constexpr (std::is_default_constructible_v<T> &&
                      !std::is_abstract_v<T> &&
                      std::is_base_of_v<QObject, T>)
        {
            GarbageCollector::RegisterTypeFactory(Name, &DefaultFactoryThunk<T>);
        }
    }
}

inline QObject* NewObjectByName(const std::string& ClassName)
{
    return GarbageCollector::NewObjectByName(ClassName);
}