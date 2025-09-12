#pragma once
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <random>
#include <unordered_set>

#include "Object.h"
#include "EngineGlobals.h"
#include "TypeName.h"

// Factory for creating arbitrary QObject-derived test objects.
// Users register types either via template Register<T>() or by (name, creator) pairs.
// QGcTester holds an instance of this factory.
class TestObjectFactory
{
public:
    using Creator = std::function<QObject*()>;

    void Clear()
    {
        NameToCreator.clear();
        Pool.clear();
        NextIndex = 0;
    }

    // Register by C++ type (infers type name)
    template <class T>
    void Register()
    {
        const std::string Name(qtype::TypeName<T>());
        NameToCreator[Name] = []() -> QObject*
        {
            return NewObject<T>();
        };
        // If pool was previously empty, add this type by default so it can create something.
        if (Pool.empty())
        {
            Pool.push_back(Name);
        }
    }

    // Register by "TypeName" with custom creator (optional)
    void Register(const std::string& TypeName, Creator Fn)
    {
        NameToCreator[TypeName] = std::move(Fn);
        if (Pool.empty()) Pool.push_back(TypeName);
    }

    // Configure the creation pool (vector or set -> vector로 넘겨줘도 됨)
    void SetTypePool(const std::vector<std::string>& InTypes)
    {
        Pool.clear();
        for (const std::string& t : InTypes)
        {
            if (NameToCreator.find(t) != NameToCreator.end())
                Pool.push_back(t);
        }
        NextIndex = 0;
    }

    // Convenience for set<string>
    void SetTypePool(const std::unordered_set<std::string>& InTypes)
    {
        Pool.assign(InTypes.begin(), InTypes.end());
        // Filter to registered only
        Pool.erase(std::remove_if(Pool.begin(), Pool.end(),
            [&](const std::string& n){ return NameToCreator.find(n) == NameToCreator.end(); }),
            Pool.end());
        NextIndex = 0;
    }

    bool HasType(const std::string& TypeName) const
    {
        return NameToCreator.find(TypeName) != NameToCreator.end();
    }

    size_t GetRegisteredCount() const { return NameToCreator.size(); }
    size_t GetPoolCount() const { return Pool.size(); }

    // Round-robin
    QObject* CreateRoundRobin()
    {
        if (Pool.empty()) return nullptr;
        if (NameToCreator.empty()) return nullptr;

        const std::string& Pick = Pool[NextIndex % Pool.size()];
        NextIndex++;
        auto It = NameToCreator.find(Pick);
        if (It == NameToCreator.end()) return nullptr;
        return It->second();
    }

    // Random
    QObject* CreateRandom(std::mt19937& Rng)
    {
        if (Pool.empty()) return nullptr;
        if (NameToCreator.empty()) return nullptr;

        std::uniform_int_distribution<size_t> Dist(0, Pool.size() - 1);
        const std::string& Pick = Pool[Dist(Rng)];
        auto It = NameToCreator.find(Pick);
        if (It == NameToCreator.end()) return nullptr;
        return It->second();
    }

    // Expose pool for debugging
    const std::vector<std::string>& GetPool() const { return Pool; }

private:
    std::unordered_map<std::string, Creator> NameToCreator;
    std::vector<std::string> Pool;
    size_t NextIndex = 0;
};
