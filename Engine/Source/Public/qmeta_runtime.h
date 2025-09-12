#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <typeindex>
#include <format>
#include <iostream>
#include <ostream>
#include <type_traits>
#include <stdexcept>

namespace qmeta {

// -------- Variant --------
// Simple type-erased value for RPC arguments and return values.
struct Variant {
    enum class EBaseType { Empty, Int, UInt, Float, Double, Bool, String, Pointer };

    EBaseType BaseType = EBaseType::Empty;
    union {
        int64_t     i64;
        uint64_t    u64;
        double      f64;
        void*       ptr;
    } data{};
    std::string str;

    Variant() = default;
    Variant(int v)              { BaseType = EBaseType::Int;   data.i64 = v; }
    Variant(int64_t v)          { BaseType = EBaseType::Int;   data.i64 = v; }
    Variant(unsigned v)         { BaseType = EBaseType::UInt;  data.u64 = v; }
    Variant(uint64_t v)         { BaseType = EBaseType::UInt;  data.u64 = v; }
    Variant(float v)            { BaseType = EBaseType::Double; data.f64 = static_cast<double>(v); }
    Variant(double v)           { BaseType = EBaseType::Double; data.f64 = v; }
    Variant(bool v)             { BaseType = EBaseType::Bool;  data.u64 = v ? 1u : 0u; }
    Variant(const char* s)      { BaseType = EBaseType::String; str = s; }
    Variant(std::string s)      { BaseType = EBaseType::String; str = std::move(s); }
    Variant(void* p)            { BaseType = EBaseType::Pointer; data.ptr = p; }

    template<class T>
    T as() const {
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T,int32_t>) {
            if (BaseType == EBaseType::Int) return static_cast<int>(data.i64);
            if (BaseType == EBaseType::UInt) return static_cast<int>(data.u64);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            if (BaseType == EBaseType::Int) return data.i64;
            if (BaseType == EBaseType::UInt) return static_cast<int64_t>(data.u64);
        } else if constexpr (std::is_same_v<T, unsigned> || std::is_same_v<T,uint32_t>) {
            if (BaseType == EBaseType::UInt) return static_cast<unsigned>(data.u64);
            if (BaseType == EBaseType::Int)  return static_cast<unsigned>(data.i64);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            if (BaseType == EBaseType::UInt) return data.u64;
            if (BaseType == EBaseType::Int)  return static_cast<uint64_t>(data.i64);
        } else if constexpr (std::is_same_v<T, float>) {
            if (BaseType == EBaseType::Double) return static_cast<float>(data.f64);
            if (BaseType == EBaseType::Int)    return static_cast<float>(data.i64);
            if (BaseType == EBaseType::UInt)   return static_cast<float>(data.u64);
        } else if constexpr (std::is_same_v<T, double>) {
            if (BaseType == EBaseType::Double) return data.f64;
            if (BaseType == EBaseType::Int)    return static_cast<double>(data.i64);
            if (BaseType == EBaseType::UInt)   return static_cast<double>(data.u64);
        } else if constexpr (std::is_same_v<T, bool>) {
            if (BaseType == EBaseType::Bool) return data.u64 != 0;
            if (BaseType == EBaseType::Int)  return data.i64 != 0;
            if (BaseType == EBaseType::UInt) return data.u64 != 0;
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (BaseType == EBaseType::String) return str;
        } else if constexpr (std::is_pointer_v<T>) {
            if (BaseType == EBaseType::Pointer) return static_cast<T>(data.ptr);
        }
        throw std::runtime_error("qmeta::Variant: bad cast");
    }
};

// -------- Metadata maps --------
using MetaMap = std::unordered_map<std::string, std::string>;

struct MetaProperty {
    std::string name;
    std::string type;
    std::size_t offset = 0;
    MetaMap     meta;
};

struct MetaParam {
    std::string name;   // optional if you want names
    std::string type;   // textual type
};

// Invoker signature: returns Variant (or empty Variant for void), takes object and an array of Variants
using InvokeFn = Variant(*)(void* obj, const Variant* args, std::size_t argc);

struct MetaFunction {
    std::string name;
    std::string return_type;
    std::vector<MetaParam> params;
    InvokeFn    invoker = nullptr;
    MetaMap     meta;
};

struct TypeInfo {
    std::string name;
    std::size_t size = 0;
    std::vector<MetaProperty> properties;
    std::vector<MetaFunction> functions;
    MetaMap meta;

    // unresolved base type name (set by QHT)
    std::string base_name;             

    // resolved base after Registry::link_bases()
    const TypeInfo* base = nullptr;

    template <class F>
    void ForEachProperty(F&& Func) const
    {
        if (base) base->ForEachProperty(Func);
        for (auto& p : properties) Func(p);
    }
    
    template <class F>
    void ForEachFunction(F&& Func) const
    {
        if (base) base->ForEachFunction(Func);
        for (auto& f : functions) Func(f);
    }
    
    const MetaProperty* FindProperty(std::string_view n) const
    {
        if (base)
        {
            if (auto* r = base->FindProperty(n))
            {
                return r;
            }
        }
        
        for (auto& p : properties)
        {
            if (p.name == n)
            {
                return &p;  
            }
        }
        
        return nullptr;
    }
    
    const MetaFunction* FindFunction(std::string_view n) const
    {
        if (base)
        {
            if (auto* r = base->FindFunction(n))
            {
                return r;
            }
        }
        
        for (auto& f : functions)
        {
            if (f.name == n)
            {
                return &f;   
            }    
        }
        
        return nullptr;
    }
};

class Registry
{
public:
    const TypeInfo* find(std::string_view type_name) const
    {
        auto it = Types.find(std::string(type_name));
        return it == Types.end() ? nullptr : &it->second;
    }

    TypeInfo& add_type(std::string name, std::size_t size)
    {
        auto [it, inserted] = Types.try_emplace(std::move(name));
        TypeInfo& t = it->second;
        t.name = it->first;
        t.size = size;
        return t;
    }

    void link_bases()
    {
        for (auto& [_, t] : Types)
        {
            if (!t.base_name.empty())
            {
                if (auto Iter = Types.find(t.base_name); Iter != Types.end())
                {
                    t.base = &Iter->second;    
                }
            }
        }
    }
    
    const std::unordered_map<std::string, TypeInfo>& all() const
    {
        return Types;
    }

private:
    std::unordered_map<std::string, TypeInfo> Types;
};

inline Registry& GetRegistry()
{
    static Registry g;
    return g;
}

// Utility: get property address by name
inline void* GetPropertyPtr(void* Obj, const TypeInfo& Ti, std::string_view PropName)
{
    const TypeInfo* ParentInfo = Ti.base;
    
    if (ParentInfo)
    {
        for (auto& p : ParentInfo->properties)
        {
            if (p.name == PropName)
            {
                return static_cast<void*>(static_cast<unsigned char*>(Obj) + p.offset);
            }
        }
    }

    for (auto& p : Ti.properties)
    {
        if (p.name == PropName)
        {
            return static_cast<void*>(static_cast<unsigned char*>(Obj) + p.offset);
        }
    }
    
    return nullptr;
}

// Utility: call a function by name
inline Variant CallByName(void* Obj, const TypeInfo& Ti, const std::string_view func, const std::vector<Variant>& Args)
{
    const TypeInfo* ParentInfo = Ti.base;
    if (ParentInfo)
    {
        for (auto& Func : ParentInfo->functions)
        {
            if (Func.name == func)
            {
                if (!Func.invoker) throw std::runtime_error("qmeta: null invoker");
                return Func.invoker(Obj, Args.data(), Args.size());
            }
        }
    }
    
    for (auto& Func : Ti.functions)
    {
        if (Func.name == func)
        {
            if (!Func.invoker) throw std::runtime_error("qmeta: null invoker");
            return Func.invoker(Obj, Args.data(), Args.size());
        }
    }
    
    std::string Msg = std::format("{}.{} not found", Ti.name, func);
    
    throw std::runtime_error(Msg);
}

}