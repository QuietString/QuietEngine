#pragma once
#include "GarbageCollector.h"
#include "qmeta_runtime.h"

namespace ConsoleUtil
{
    // Build inheritance chain from most-derived to base (e.g., QPlayer -> QActor -> QObject).
    void BuildClassChain(const qmeta::TypeInfo* Ti, std::vector<const qmeta::TypeInfo*>& Out);

    std::string JoinClassChain(const std::vector<const qmeta::TypeInfo*>& Chain);

    // Parse int with error handling
    bool TryParseInt(const std::string& s, long long& Out);
    
    // ---- helpers for type-aware argument parsing ----
    std::string Trim(std::string s);
    
    void StripPrefix(std::string& s, const char* pref);

    std::string NormalizeType(std::string t);
    
    bool IsPointerType(const std::string& normType);

    bool IsBoolType(const std::string& t);

    bool IsStringType(const std::string& t);

    bool IsFloatType(const std::string& t);

    bool IsSignedIntType(const std::string& Type);

    bool IsUnsignedIntType(const std::string& Type);
    
    // Parse one token to Variant using expected param type. Returns true on success.
    bool ParseTokenByType(const std::string& Token, const std::string& ExpectedTypeRaw, GarbageCollector& GC, qmeta::Variant& OutVar);

    // Lenient parse when no meta signature is available: object-name -> QObject*, else old rules
    qmeta::Variant ParseTokenLenient(const std::string& token, GarbageCollector& GC);
}
