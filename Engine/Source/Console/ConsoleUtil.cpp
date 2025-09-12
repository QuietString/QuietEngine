#include "ConsoleUtil.h"

#include <sstream>


void ConsoleUtil::BuildClassChain(const qmeta::TypeInfo* Ti, std::vector<const qmeta::TypeInfo*>& Out)
{
    // NOTE: If your TypeInfo uses a different field name for base type,
    // change ".Super" below accordingly (e.g., .Base, .Parent).
    for (auto* cur = Ti; cur; /*step below*/)
    {
        Out.push_back(cur);
        cur = cur->base;
    }
}

std::string ConsoleUtil::JoinClassChain(const std::vector<const qmeta::TypeInfo*>& Chain)
{
    std::ostringstream os;
    for (size_t i = 0; i < Chain.size(); ++i)
    {
        if (i) os << " : ";
        os << Chain[i]->name;
    }
    return os.str();
}

bool ConsoleUtil::TryParseInt(const std::string& s, long long& Out)
{
    try
    {
        Out = std::stoll(s);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string ConsoleUtil::Trim(std::string s)
{
    auto is_space = [](unsigned char c){ return std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](char c){ return !is_space((unsigned char)c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](char c){ return !is_space((unsigned char)c); }).base(), s.end());
    return s;
}

void ConsoleUtil::StripPrefix(std::string& s, const char* pref)
{
    if (s.rfind(pref, 0) == 0) s.erase(0, std::strlen(pref));
}

std::string ConsoleUtil::NormalizeType(std::string t)
{
    t = Trim(t);
    // strip common qualifiers and MSVC tags
    StripPrefix(t, "const ");
    StripPrefix(t, "class ");
    StripPrefix(t, "struct ");
    // drop trailing refs
    if (!t.empty() && t.back() == '&') t.pop_back();
    // collapse spaces around '*'
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    return t; // e.g. "QPlayer*", "QObject*", "int", "std::string", "double"
}

bool ConsoleUtil::IsPointerType(const std::string& normType)
{
    return !normType.empty() && normType.back() == '*';
}

bool ConsoleUtil::IsBoolType(const std::string& t)
{
    return t == "bool";
}

bool ConsoleUtil::IsStringType(const std::string& t)
{
    return (t == "std::string" || t == "string");
}

bool ConsoleUtil::IsFloatType(const std::string& t)
{
    return (t == "float" || t == "double");
}

bool ConsoleUtil::IsSignedIntType(const std::string& Type)
{
    return (Type == "int" || Type == "int32_t" || Type == "int64_t" || Type == "long" || Type == "longlong");
}

bool ConsoleUtil::IsUnsignedIntType(const std::string& Type)
{
    return (Type == "unsigned" || Type == "unsignedint" || Type == "uint32_t" || Type == "uint64_t" || Type == "unsignedlonglong" || Type == "size_t");
}

bool ConsoleUtil::ParseTokenByType(const std::string& Token, const std::string& ExpectedTypeRaw, GarbageCollector& GC, qmeta::Variant& OutVar)
{
    using qmeta::Variant;

    const std::string NormedType = NormalizeType(ExpectedTypeRaw);
    const std::string Tok = Trim(Token);

    // Pointers (QObject* and subclasses)
    if (IsPointerType(NormedType))
    {
        if (Tok == "null" || Tok == "nullptr" || Tok == "0")
        {
            OutVar = Variant((void*)nullptr);
            return true;
        }
        // try resolve object by debug name
        if (QObject* Obj = GC.FindByDebugName(Tok))
        {
            OutVar = Variant(static_cast<void*>(Obj));
            return true;
        }
        // as a last resort, allow hex address like 0x1234
        if (Tok.rfind("0x", 0) == 0)
        {
            void* p = reinterpret_cast<void*>(std::strtoull(Tok.c_str(), nullptr, 16));
            OutVar = Variant(p);
            return true;
        }
        return false; // pointer expected but not resolvable
    }

    if (IsBoolType(NormedType))
    {
        if (Tok == "true" || Tok == "1")  { OutVar = Variant(true);  return true; }
        if (Tok == "false"|| Tok == "0")  { OutVar = Variant(false); return true; }
        return false;
    }

    if (IsStringType(NormedType))
    {
        // Allow quotes but not required
        if (!Tok.empty() && ((Tok.front()=='"' && Tok.back()=='"') || (Tok.front()=='\'' && Tok.back()=='\'')))
        {
            OutVar = Variant(Tok.substr(1, Tok.size()-2));
        }
        else
        {
            OutVar = Variant(Tok);
        }
        return true;
    }

    if (IsFloatType(NormedType))
    {
        try
        {
            OutVar = Variant(std::stod(Tok));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    if (IsSignedIntType(NormedType))
    {
        try
        {
            OutVar = Variant((long long)std::stoll(Tok));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    if (IsUnsignedIntType(NormedType))
    {
        try { OutVar = Variant((unsigned long long)std::stoull(Tok)); return true; } catch (...) { return false; }
    }

    // Fallback: try int -> double -> bool -> string
    try { OutVar = Variant((long long)std::stoll(Tok)); return true; } catch (...) {}
    try { OutVar = Variant(std::stod(Tok)); return true; } catch (...) {}
    if (Tok == "true" || Tok == "false") { OutVar = Variant(Tok == "true"); return true; }
    OutVar = Variant(Tok);
    return true;
}

qmeta::Variant ConsoleUtil::ParseTokenLenient(const std::string& token, GarbageCollector& GC)
{
    using qmeta::Variant;
    if (QObject* Obj = GC.FindByDebugName(token))
    {
        return Variant(static_cast<void*>(Obj));
    }
    if (token == "true" || token == "false")
    {
        return Variant(token == "true");
    }
    try { return Variant((long long)std::stoll(token)); } catch (...) {}
    try { return Variant(std::stod(token)); } catch (...) {}
    return Variant(token);
}
