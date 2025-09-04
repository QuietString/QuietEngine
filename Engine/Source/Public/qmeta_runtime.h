#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <typeindex>
#include <type_traits>
#include <stdexcept>

namespace qmeta {

// -------- Variant --------
// Simple type-erased value for RPC arguments and return values.
struct Variant {
    enum class Kind { Empty, Int, UInt, Float, Double, Bool, String, Pointer };

    Kind kind = Kind::Empty;
    union {
        int64_t     i64;
        uint64_t    u64;
        double      f64;
        void*       ptr;
    } data{};
    std::string str;

    Variant() = default;
    Variant(int v)              { kind = Kind::Int;   data.i64 = v; }
    Variant(int64_t v)          { kind = Kind::Int;   data.i64 = v; }
    Variant(unsigned v)         { kind = Kind::UInt;  data.u64 = v; }
    Variant(uint64_t v)         { kind = Kind::UInt;  data.u64 = v; }
    Variant(float v)            { kind = Kind::Double; data.f64 = static_cast<double>(v); }
    Variant(double v)           { kind = Kind::Double; data.f64 = v; }
    Variant(bool v)             { kind = Kind::Bool;  data.u64 = v ? 1u : 0u; }
    Variant(const char* s)      { kind = Kind::String; str = s; }
    Variant(std::string s)      { kind = Kind::String; str = std::move(s); }
    Variant(void* p)            { kind = Kind::Pointer; data.ptr = p; }

    template<class T>
    T as() const {
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T,int32_t>) {
            if (kind == Kind::Int) return static_cast<int>(data.i64);
            if (kind == Kind::UInt) return static_cast<int>(data.u64);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            if (kind == Kind::Int) return data.i64;
            if (kind == Kind::UInt) return static_cast<int64_t>(data.u64);
        } else if constexpr (std::is_same_v<T, unsigned> || std::is_same_v<T,uint32_t>) {
            if (kind == Kind::UInt) return static_cast<unsigned>(data.u64);
            if (kind == Kind::Int)  return static_cast<unsigned>(data.i64);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            if (kind == Kind::UInt) return data.u64;
            if (kind == Kind::Int)  return static_cast<uint64_t>(data.i64);
        } else if constexpr (std::is_same_v<T, float>) {
            if (kind == Kind::Double) return static_cast<float>(data.f64);
            if (kind == Kind::Int)    return static_cast<float>(data.i64);
            if (kind == Kind::UInt)   return static_cast<float>(data.u64);
        } else if constexpr (std::is_same_v<T, double>) {
            if (kind == Kind::Double) return data.f64;
            if (kind == Kind::Int)    return static_cast<double>(data.i64);
            if (kind == Kind::UInt)   return static_cast<double>(data.u64);
        } else if constexpr (std::is_same_v<T, bool>) {
            if (kind == Kind::Bool) return data.u64 != 0;
            if (kind == Kind::Int)  return data.i64 != 0;
            if (kind == Kind::UInt) return data.u64 != 0;
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (kind == Kind::String) return str;
        } else if constexpr (std::is_pointer_v<T>) {
            if (kind == Kind::Pointer) return static_cast<T>(data.ptr);
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
};

class Registry {
public:
    const TypeInfo* find(std::string_view type_name) const {
        auto it = types_.find(std::string(type_name));
        return it == types_.end() ? nullptr : &it->second;
    }

    TypeInfo& add_type(std::string name, std::size_t size) {
        auto [it, inserted] = types_.try_emplace(std::move(name));
        TypeInfo& t = it->second;
        t.name = it->first;
        t.size = size;
        return t;
    }

    const std::unordered_map<std::string, TypeInfo>& all() const { return types_; }

private:
    std::unordered_map<std::string, TypeInfo> types_;
};

inline Registry& GetRegistry() {
    static Registry g;
    return g;
}

// Utility: get property address by name
inline void* GetPropertyPtr(void* obj, const TypeInfo& ti, std::string_view prop_name) {
    for (auto& p : ti.properties) {
        if (p.name == prop_name) {
            return static_cast<void*>(static_cast<unsigned char*>(obj) + p.offset);
        }
    }
    return nullptr;
}

// Utility: call function by name
inline Variant CallByName(void* obj, const TypeInfo& ti, std::string_view func, const std::vector<Variant>& args) {
    for (auto& f : ti.functions) {
        if (f.name == func) {
            if (!f.invoker) throw std::runtime_error("qmeta: null invoker");
            return f.invoker(obj, args.data(), args.size());
        }
    }
    throw std::runtime_error("qmeta: function not found");
}

// Will be emitted by the generator:
void RegisterAllGeneratedTypes();

} // namespace qmeta