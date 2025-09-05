#include "Asset.h"

#include <fstream>
#include <cstring>
#include <stdexcept>

namespace {

// Normalize a C++ type token string (very simple normalizer).
std::string norm(std::string s) {
    // remove const, &, * spaces
    auto trim = [](std::string& x){
        while (!x.empty() && (x.front()==' ' || x.front()=='\t')) x.erase(x.begin());
        while (!x.empty() && (x.back() ==' ' || x.back() =='\t')) x.pop_back();
    };
    trim(s);
    // collapse spaces
    std::string t; t.reserve(s.size());
    bool sp = false;
    for (char c : s) {
        if (c=='*' || c=='&' || c=='\t') c = ' ';
        if (c==' ') { if (!sp) { t.push_back(' '); sp = true; } }
        else { t.push_back(c); sp = false; }
    }
    // drop leading "const "
    if (t.rfind("const ", 0) == 0) t.erase(0, 6);
    // drop trailing " const"
    if (t.size() > 6 && t.compare(t.size()-6, 6, " const")==0) t.erase(t.size()-6);
    // remove spaces entirely for simple matching
    std::string u; u.reserve(t.size());
    for (char c : t) if (c!=' ') u.push_back(c);
    return u;
}

// Very small type code mapping for primitives we support now.
enum class TCode : uint8_t { Unknown=0, Int32, UInt32, Int64, UInt64, Float, Double, Bool, String, FVector };

TCode TypeCodeFrom(const std::string& typeName) {
    std::string t = norm(typeName);
    if (t=="int" || t=="int32_t" || t=="int32") return TCode::Int32;
    if (t=="unsignedint" || t=="uint32_t" || t=="uint32") return TCode::UInt32;
    if (t=="int64_t" || t=="longlong") return TCode::Int64;
    if (t=="uint64_t" || t=="unsignedlonglong") return TCode::UInt64;
    if (t=="float") return TCode::Float;
    if (t=="double") return TCode::Double;
    if (t=="bool") return TCode::Bool;
    if (t=="std::string" || t=="string") return TCode::String;
    if (t=="FVector") return TCode::FVector;
    return TCode::Unknown;
}

// I/O helpers
template<class T>
void writePod(std::ofstream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template<class T>
void readPod(std::ifstream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
}
void writeStr(std::ofstream& os, const std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    writePod(os, n);
    if (n) os.write(s.data(), n);
}
std::string readStr(std::ifstream& is) {
    uint32_t n = 0; readPod(is, n);
    std::string s; s.resize(n);
    if (n) is.read(&s[0], n);
    return s;
}

struct Header {
    uint32_t magic = qasset::kMagic;
    uint16_t version = qasset::kVersion;
    uint16_t reserved = 0;
};

}

namespace qasset {

std::filesystem::path DefaultAssetDirFor(const qmeta::TypeInfo& Ti)
{
    auto It = Ti.meta.find("Module");
    const char* Module = (It==Ti.meta.end() ? "Game" : It->second.c_str());
    std::filesystem::path Base(Module); // "Game" or "Engine"
    Base /= "Contents";
    return Base;
}

// Writes: Header, TypeName, PropertyCount, then per-property blocks (name, tcode, payload),
// then FunctionCount and function metadata (name, return, paramCount, each param(name,type)).
bool Save(const void* Obj, const qmeta::TypeInfo& Ti, const std::filesystem::path& OutPath, const std::string& FileNameIfDir)
{
    std::filesystem::path Path = OutPath;
    if (Path.has_extension()) {
        // use as full file path
    } else {
        std::filesystem::create_directories(Path);
        std::string FileName = FileNameIfDir.empty() ? (Ti.name + ".qasset") : FileNameIfDir;
        Path /= FileName;
    }

    std::ofstream os(Path, std::ios::binary);
    if (!os) return false;

    Header h{};
    writePod(os, h);

    writeStr(os, Ti.name);

    // properties
    uint32_t PropertyCount = static_cast<uint32_t>(Ti.properties.size());
    writePod(os, PropertyCount);

    const unsigned char* Base = static_cast<const unsigned char*>(Obj);
    for (auto& Property : Ti.properties) {
        writeStr(os, Property.name);
        // type code for robust load
        TCode tc = TypeCodeFrom(Property.type);
        uint8_t tcu = static_cast<uint8_t>(tc);
        os.write(reinterpret_cast<const char*>(&tcu), sizeof(uint8_t));

        const void* Addr = Base + Property.offset;

        switch (tc) {
        case TCode::Int32:   writePod<int32_t>(os, *reinterpret_cast<const int32_t*>(Addr)); break;
        case TCode::UInt32:  writePod<uint32_t>(os, *reinterpret_cast<const uint32_t*>(Addr)); break;
        case TCode::Int64:   writePod<int64_t>(os, *reinterpret_cast<const int64_t*>(Addr)); break;
        case TCode::UInt64:  writePod<uint64_t>(os, *reinterpret_cast<const uint64_t*>(Addr)); break;
        case TCode::Float:   writePod<float>(os, *reinterpret_cast<const float*>(Addr)); break;
        case TCode::Double:  writePod<double>(os, *reinterpret_cast<const double*>(Addr)); break;
        case TCode::Bool:    { uint8_t b = *reinterpret_cast<const bool*>(Addr) ? 1u : 0u; writePod<uint8_t>(os, b); } break;
        case TCode::String:  writeStr(os, *reinterpret_cast<const std::string*>(Addr)); break;
        case TCode::FVector: {
            // Requires struct FVector { float X,Y,Z; } in project
            struct FVector { float X,Y,Z; };
            const FVector& v = *reinterpret_cast<const FVector*>(Addr);
            writePod<float>(os, v.X); writePod<float>(os, v.Y); writePod<float>(os, v.Z);
            break;
        }
        default:
            // Unknown: write zero-length payload to survive; could be extended with raw bytes + size if desired.
            uint32_t zero = 0; writePod(os, zero);
            break;
        }
    }

    // functions (metadata only)
    uint32_t FuncCount = static_cast<uint32_t>(Ti.functions.size());
    writePod(os, FuncCount);
    for (auto& f : Ti.functions) {
        writeStr(os, f.name);
        writeStr(os, f.return_type);
        uint32_t argc = static_cast<uint32_t>(f.params.size());
        writePod(os, argc);
        for (auto& a : f.params) {
            writeStr(os, a.name);
            writeStr(os, a.type);
        }
    }
    return true;
}

bool Load(void* Obj, const qmeta::TypeInfo& ti,
          const std::filesystem::path& InFile)
{
    std::ifstream is(InFile, std::ios::binary);
    if (!is) return false;

    Header h{}; readPod(is, h);
    if (h.magic != kMagic || h.version != kVersion) return false;

    std::string typeName = readStr(is);
    if (typeName != ti.name) {
        // You can choose to fail, or proceed for compatible aliases.
        return false;
    }

    uint32_t pcount = 0; readPod(is, pcount);
    unsigned char* base = static_cast<unsigned char*>(Obj);

    for (uint32_t i=0; i<pcount; ++i) {
        std::string pname = readStr(is);
        uint8_t tcu = 0; readPod(is, tcu);
        TCode tc = static_cast<TCode>(tcu);

        // Find property by name
        const qmeta::MetaProperty* mp = nullptr;
        for (auto& p : ti.properties) if (p.name == pname) { mp = &p; break; }

        // If property not found, skip payload
        auto skip_unknown_payload = [&](TCode code){
            switch (code) {
                case TCode::Int32: { int32_t tmp; readPod(is, tmp); break; }
                case TCode::UInt32:{ uint32_t tmp; readPod(is, tmp); break; }
                case TCode::Int64: { int64_t tmp; readPod(is, tmp); break; }
                case TCode::UInt64:{ uint64_t tmp; readPod(is, tmp); break; }
                case TCode::Float: { float tmp; readPod(is, tmp); break; }
                case TCode::Double:{ double tmp; readPod(is, tmp); break; }
                case TCode::Bool:  { uint8_t tmp; readPod(is, tmp); break; }
                case TCode::String:{ (void)readStr(is); break; }
                case TCode::FVector:{ float x,y,z; readPod(is,x); readPod(is,y); readPod(is,z); break; }
                default: { uint32_t zero; readPod(is, zero); break; }
            }
        };

        if (!mp) { skip_unknown_payload(tc); continue; }

        void* addr = base + mp->offset;

        switch (tc) {
        case TCode::Int32:   { int32_t v; readPod(is, v); *reinterpret_cast<int32_t*>(addr) = v; } break;
        case TCode::UInt32:  { uint32_t v; readPod(is, v); *reinterpret_cast<uint32_t*>(addr) = v; } break;
        case TCode::Int64:   { int64_t v; readPod(is, v); *reinterpret_cast<int64_t*>(addr) = v; } break;
        case TCode::UInt64:  { uint64_t v; readPod(is, v); *reinterpret_cast<uint64_t*>(addr) = v; } break;
        case TCode::Float:   { float v; readPod(is, v); *reinterpret_cast<float*>(addr) = v; } break;
        case TCode::Double:  { double v; readPod(is, v); *reinterpret_cast<double*>(addr) = v; } break;
        case TCode::Bool:    { uint8_t b; readPod(is, b); *reinterpret_cast<bool*>(addr) = (b!=0); } break;
        case TCode::String:  { std::string s = readStr(is); *reinterpret_cast<std::string*>(addr) = std::move(s); } break;
        case TCode::FVector: {
            struct FVector { float X,Y,Z; };
            FVector& v = *reinterpret_cast<FVector*>(addr);
            readPod(is, v.X); readPod(is, v.Y); readPod(is, v.Z);
            break;
        }
        default:
            // Unknown payload format: read the placeholder
            { uint32_t zero; readPod(is, zero); }
            break;
        }
    }

    // functions (metadata only) - skip/consume
    uint32_t fcount = 0; readPod(is, fcount);
    for (uint32_t i=0; i<fcount; ++i) {
        (void)readStr(is); // name
        (void)readStr(is); // return
        uint32_t argc = 0; readPod(is, argc);
        for (uint32_t a=0; a<argc; ++a) { (void)readStr(is); (void)readStr(is); }
    }

    return true;
}

void SaveOrThrow(const void* Obj, const qmeta::TypeInfo& Ti, const std::filesystem::path& OutPath, const std::string& FileNameIfDir)
{
    if (!Save(Obj, Ti, OutPath, FileNameIfDir))
        throw std::runtime_error("qasset: Save failed");
}

void LoadOrThrow(void* Obj, const qmeta::TypeInfo& Ti, const std::filesystem::path& InFile)
{
    if (!Load(Obj, Ti, InFile))
        throw std::runtime_error("qasset: Load failed");
}

}
