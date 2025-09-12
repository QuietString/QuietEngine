#include "ConsoleManager.h"

#include <iostream>

#include "EngineUtils.h"
#include "GarbageCollector.h"
#include "Runtime.h"
#include "CoreObjects/Public/World.h"

// ---- helpers for type-aware argument parsing ----
static std::string Trim(std::string s) {
    auto is_space = [](unsigned char c){ return std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](char c){ return !is_space((unsigned char)c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [&](char c){ return !is_space((unsigned char)c); }).base(), s.end());
    return s;
}


static void StripPrefix(std::string& s, const char* pref) {
    if (s.rfind(pref, 0) == 0) s.erase(0, std::strlen(pref));
}

static std::string NormalizeType(std::string t) {
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

static bool IsPointerType(const std::string& normType) {
    return !normType.empty() && normType.back() == '*';
}

static bool IsBoolType(const std::string& t) {
    return t == "bool";
}

static bool IsStringType(const std::string& t) {
    return (t == "std::string" || t == "string");
}

static bool IsFloatType(const std::string& t) {
    return (t == "float" || t == "double");
}

static bool IsSignedIntType(const std::string& t) {
    return (t == "int" || t == "int32_t" || t == "int64_t" || t == "long" || t == "longlong");
}

static bool IsUnsignedIntType(const std::string& t) {
    return (t == "unsigned" || t == "unsignedint" || t == "uint32_t" || t == "uint64_t" || t == "unsignedlonglong" || t == "size_t");
}

// Parse one token to Variant using expected param type. Returns true on success.
static bool ParseTokenByType(const std::string& token, const std::string& expectedTypeRaw, GarbageCollector& GC, qmeta::Variant& outVar)
{
    using qmeta::Variant;

    const std::string T = NormalizeType(expectedTypeRaw);
    const std::string Tok = Trim(token);

    // Pointers (QObject* and subclasses)
    if (IsPointerType(T)) {
        if (Tok == "null" || Tok == "nullptr" || Tok == "0") {
            outVar = Variant((void*)nullptr);
            return true;
        }
        // try resolve object by debug name
        if (QObject* Obj = GC.FindByDebugName(Tok)) {
            outVar = Variant(static_cast<void*>(Obj));
            return true;
        }
        // as a last resort, allow hex address like 0x1234
        if (Tok.rfind("0x", 0) == 0) {
            void* p = reinterpret_cast<void*>(std::strtoull(Tok.c_str(), nullptr, 16));
            outVar = Variant(p);
            return true;
        }
        return false; // pointer expected but not resolvable
    }

    if (IsBoolType(T)) {
        if (Tok == "true" || Tok == "1")  { outVar = Variant(true);  return true; }
        if (Tok == "false"|| Tok == "0")  { outVar = Variant(false); return true; }
        return false;
    }

    if (IsStringType(T)) {
        // Allow quotes but not required
        if (!Tok.empty() && ((Tok.front()=='"' && Tok.back()=='"') || (Tok.front()=='\'' && Tok.back()=='\''))) {
            outVar = Variant(Tok.substr(1, Tok.size()-2));
        } else {
            outVar = Variant(Tok);
        }
        return true;
    }

    if (IsFloatType(T)) {
        try { outVar = Variant(std::stod(Tok)); return true; } catch (...) { return false; }
    }

    if (IsSignedIntType(T)) {
        try { outVar = Variant((long long)std::stoll(Tok)); return true; } catch (...) { return false; }
    }

    if (IsUnsignedIntType(T)) {
        try { outVar = Variant((unsigned long long)std::stoull(Tok)); return true; } catch (...) { return false; }
    }

    // Fallback: try int -> double -> bool -> string
    try { outVar = Variant((long long)std::stoll(Tok)); return true; } catch (...) {}
    try { outVar = Variant(std::stod(Tok)); return true; } catch (...) {}
    if (Tok == "true" || Tok == "false") { outVar = Variant(Tok == "true"); return true; }
    outVar = Variant(Tok);
    return true;
}

// Lenient parse when no meta signature is available: object-name -> QObject*, else old rules
static qmeta::Variant ParseTokenLenient(const std::string& token, GarbageCollector& GC)
{
    using qmeta::Variant;
    if (QObject* Obj = GC.FindByDebugName(token)) {
        return Variant(static_cast<void*>(Obj));
    }
    if (token == "true" || token == "false") {
        return Variant(token == "true");
    }
    try { return Variant((long long)std::stoll(token)); } catch (...) {}
    try { return Variant(std::stod(token)); } catch (...) {}
    return Variant(token);
}

std::vector<std::string> ConsoleManager::Tokenize(const std::string& s)
{
    std::vector<std::string> Out;
    std::istringstream StringStream(s);
    std::string Token;
    while (StringStream >> std::quoted(Token))
    {
        Out.push_back(Token);
    }
    return Out;
}

bool ConsoleManager::ExecuteCommand(const std::string& Line)
{
    auto Tokens = Tokenize(Line);
    if (Tokens.empty())
    {
        return false;
    }

    auto& GC = GarbageCollector::Get();
    const std::string& Cmd = Tokens[0];

    try
    {
        if (Cmd == "help")
        {
            std::cout <<
                "Commands:\n"
                "  new <Type> <Name>\n"
                "  link <OwnerName> <Property> <TargetName>\n"
                "  unlink <OwnerName> <Property>\n"
                "  set <Name> <Property> <Value>\n"
                "  call <Name> <Function> [args...]\n"
                "  save <Name> [FileName]\n"
                "  load <Type> <Name> [FileName]\n"
                "  gc\n"
                "  tick <seconds>\n"
                "  ls\n"
                "  props <Name>\n"
                "  funcs <Name>" << "\n";
            return true;
        }
        else if (Cmd == "tick" && Tokens.size() >= 2)
        {
            double Dt = std::stod(Tokens[1]);
            qruntime::Tick(Dt);
            return true;
        }
        else if (Cmd == "gc")
        {
            if (Tokens.size() == 1)
            {
                GC.Collect();
                return true;    
            }
            else if (Tokens.size() == 4)
            {
                if (Tokens[1] == "set" && Tokens[2] == "interval")
                {
                    double Interval = std::stod(Tokens[3]);
                    GC.SetAutoInterval(Interval);
                }
            }
        }
        else if (Cmd == "gctest")
        {
            auto* W = GetWorld();
            if (!W) { std::cout << "World not found.\n"; return true; }

            auto FindTester = [&]() -> QObject* {
                const qmeta::TypeInfo* Ti = nullptr;
                for (QObject* O : W->Objects)
                {
                    if (!O) continue;
                    Ti = GC.GetTypeInfo(O);
                    if (Ti && Ti->name == "QGcTester")
                    {
                        return O;
                    }
                }

                return nullptr;
            };

            QObject* Tester = FindTester();
            if (!Tester)
            {
                std::cout << "QGcTester instance not found. Make sure the Game module created it in BeginPlay().\n";
                return true;
            }

            if (Tokens.size() == 2 && Tokens[1] == "clear")
            {
                GC.Call(Tester, "ClearAll", {});
                return true;
            }

            if (Tokens.size() == 2)
            {
                if (Tokens[1] == "s")
                {
                    GC.Call(Tester, "SetUseVector", {false});
                    return true;
                }
                else if (Tokens[1] == "v")
                {
                    GC.Call(Tester, "SetUseVector", {true});
                    return true;
                }
            }

            if (Tokens.size() >= 2 && Tokens[1] == "build")
            {
                if (Tokens.size() < 5)
                {
                    std::cout << "Usage: gctest build <roots> <depth> <branch> [seed]\n";
                    return true;
                }
                int Roots = std::stoi(Tokens[2]);
                int Depth = std::stoi(Tokens[3]);
                int Branch = std::stoi(Tokens[4]);
                int Seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 1337;
                GC.Call(Tester, "Build", { qmeta::Variant(Roots), qmeta::Variant(Depth), qmeta::Variant(Branch), qmeta::Variant(Seed) });
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "pattern")
            {
                // gctest pattern <chain|grid|random|rings|diamond> ...
                if (Tokens.size() < 3)
                {
                    std::cout << "Usage: gctest pattern <chain|grid|random|rings|diamond> <args...>\n";
                    return true;
                }
                const std::string Mode = Tokens[2];

                if (Mode == "chain")
                {
                    if (Tokens.size() < 4) { std::cout << "gctest pattern chain <length> [seed]\n"; return true; }
                    int length = std::stoi(Tokens[3]);
                    int seed = (Tokens.size() >= 5) ? std::stoi(Tokens[4]) : 1;
                    GC.Call(Tester, "PatternChain", { qmeta::Variant(length), qmeta::Variant(seed) });
                    return true;
                }
                else if (Mode == "grid")
                {
                    if (Tokens.size() < 5) { std::cout << "gctest pattern grid <w> <h> [seed]\n"; return true; }
                    int w = std::stoi(Tokens[3]);
                    int h = std::stoi(Tokens[4]);
                    int seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 1;
                    GC.Call(Tester, "PatternGrid", { qmeta::Variant(w), qmeta::Variant(h), qmeta::Variant(seed) });
                    return true;
                }
                else if (Mode == "random")
                {
                    if (Tokens.size() < 5) { std::cout << "gctest pattern random <nodes> <branchCount> [seed]\n"; return true; }
                    int Nodes = std::stoi(Tokens[3]);
                    int BranchCount = std::stoi(Tokens[4]);
                    int Seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 1337;
                    GC.Call(Tester, "PatternRandom", { qmeta::Variant(Nodes), qmeta::Variant(BranchCount), qmeta::Variant(Seed) });
                    return true;
                }
                else if (Mode == "rings")
                {
                    if (Tokens.size() < 5) { std::cout << "gctest pattern rings <rings> <ringSize> [seed]\n"; return true; }
                    int rings = std::stoi(Tokens[3]);
                    int ringSize = std::stoi(Tokens[4]);
                    int seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 7;
                    GC.Call(Tester, "PatternRings", { qmeta::Variant(rings), qmeta::Variant(ringSize), qmeta::Variant(seed) });
                    return true;
                }
                else if (Mode == "diamond")
                {
                    if (Tokens.size() < 5) { std::cout << "gctest pattern diamond <layers> <breadth> [seed]\n"; return true; }
                    int layers = std::stoi(Tokens[3]);
                    int breadth = std::stoi(Tokens[4]);
                    int seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 3;
                    GC.Call(Tester, "PatternDiamond", { qmeta::Variant(layers), qmeta::Variant(breadth), qmeta::Variant(seed) });
                    return true;
                }
                else
                {
                    std::cout << "Unknown pattern.\n";
                    return true;
                }
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "breakd")
            {
                if (Tokens.size() < 4)
                {
                    std::cout << "Usage: gctest breakd <depth> <all|count> [seed]\n";
                    return true;
                }
                int depth = std::stoi(Tokens[2]);
                int count = (Tokens[3] == "all") ? -1 : std::stoi(Tokens[3]);
                int seed = (Tokens.size() >= 5) ? std::stoi(Tokens[4]) : 42;
                GC.Call(Tester, "BreakAtDepth", { qmeta::Variant(depth), qmeta::Variant(count), qmeta::Variant(seed) });
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "breakp")
            {
                // gctest breakp <percent 0..100> [depth|-1] [seed]
                if (Tokens.size() < 4)
                {
                    std::cout << "gctest breakp <percent> [depth|-1] [seed]\n";
                    return true;
                }
                double pct = std::stod(Tokens[2]);
                int depth = (Tokens.size() >= 4) ? std::stoi(Tokens[3]) : -1;
                int seed  = (Tokens.size() >= 5) ? std::stoi(Tokens[4]) : 24;
                GC.Call(Tester, "BreakPercent", { qmeta::Variant(pct), qmeta::Variant(depth), qmeta::Variant(seed), qmeta::Variant(false) });
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "breakedges")
            {
                // gctest breakedges <count> [seed]
                if (Tokens.size() < 3) { std::cout << "gctest breakedges <count> [seed]\n"; return true; }
                int count = std::stoi(Tokens[2]);
                int seed  = (Tokens.size() >= 4) ? std::stoi(Tokens[3]) : 99;
                GC.Call(Tester, "BreakRandomEdges", { qmeta::Variant(count), qmeta::Variant(seed) });
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "detachroots")
            {
                // gctest detachroots <count|0> [ratio]
                if (Tokens.size() < 3) { std::cout << "gctest detachroots <count> [ratio]\n"; return true; }
                int Count = std::stoi(Tokens[2]);
                double Ratio = (Tokens.size() >= 4) ? std::stod(Tokens[3]) : 0.0;
                GC.Call(Tester, "DetachRoots", { qmeta::Variant(Count), qmeta::Variant(Ratio) });
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "measure")
            {
                // gctest measure <repeats>
                if (Tokens.size() < 3) { std::cout << "gctest measure <repeats>\n"; return true; }
                int rep = std::stoi(Tokens[2]);
                GC.Call(Tester, "MeasureGc", { qmeta::Variant(rep) });
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "churn")
            {
                // gctest churn <steps> <allocPerStep> <breakPct> <gcEveryN> [seed]
                if (Tokens.size() < 6) { std::cout << "gctest churn <steps> <allocPerStep> <breakPct> <gcEveryN> [seed]\n"; return true; }
                int steps = std::stoi(Tokens[2]);
                int allocPerStep = std::stoi(Tokens[3]);
                double breakPct = std::stod(Tokens[4]);
                int gcEveryN = std::stoi(Tokens[5]);
                int seed = (Tokens.size() >= 7) ? std::stoi(Tokens[6]) : 2025;
                GC.Call(Tester, "Churn", { qmeta::Variant(steps), qmeta::Variant(allocPerStep),
                                            qmeta::Variant(breakPct), qmeta::Variant(gcEveryN),
                                            qmeta::Variant(seed) });
                return true;
            }

            std::cout << "Invalid cmd: " << Tokens[0] << " " << Tokens[1] << "\n";
            return true;
        }
        else if (Cmd == "ls")
        {
            GC.ListObjects();
            return true;
        }
        else if (Cmd == "props" && Tokens.size() >= 2)
        {
            GC.ListPropertiesByDebugName(Tokens[1]);
            return true;
        }
        else if (Cmd == "read")
        {
            if (Tokens.size() < 3)
            {
                std::cout << "Usage: read <Name> <Property>\n";
                return true;
            }

            const std::string& ObjName = Tokens[1];
            const std::string& PropName = Tokens[2];

            QObject* Obj = GC.FindByDebugName(ObjName);
            if (!Obj)
            {
                std::cout << "Not found: " << ObjName << "\n";
                return true;
            }

            const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Obj);
            if (!Ti)
            {
                std::cout << "No TypeInfo for: " << ObjName << "\n";
                return true;
            }

            // Locate property meta to know its type and offset
            const qmeta::MetaProperty* MetaProp = Ti->FindProperty(PropName);
            
            if (!MetaProp)
            {
                std::cout << "Property not found: " << PropName << "\n";
                return true;
            }

            std::string ValueStr = EngineUtils::FormatPropertyValue(Obj, *MetaProp);
            std::cout << ValueStr << "\n";
            return true;
        }
        else if (Cmd == "funcs" && Tokens.size() >= 2)
        {
            GC.ListFunctionsByDebugName(Tokens[1]);
            return true;
        }
        else if (Cmd == "new" && Tokens.size() >= 3)
        {
            std::cout << "[new] not implemented for now.\n";
            return true;
        }
        else if (Cmd == "unlink")
        {
            std::string UnlinkUsage = "Usage: unlink [single|all] <OwnerName> <Property>\n";
            if (Tokens.size() < 3 || Tokens.size() > 5)
            {
                std::cout << UnlinkUsage;
                return true;
            }
            
            if (Tokens[1] == "single" && Tokens.size() == 4)
            {
                bool bResult = GC.UnlinkByName(Tokens[2], Tokens[3]);
                if (!bResult)
                {
                    std::cout << "Failed to unlink " << Tokens[1] << "." << Tokens[2] << "\n";
                }
                
                return true;
            }
            else if (Tokens[1] == "all" && Tokens.size() == 3)
            {
                bool bResult = GC.UnlinkAllByName(Tokens[2]);
                if (!bResult)
                {
                    std::cout << "Failed to unlink " << Tokens[1] << "." << Tokens[2] << "\n";
                }

                return true;
            }
            else
            {
                std::cout << UnlinkUsage;
                return true;
            }
        }
        else if (Cmd == "set" && Tokens.size() >= 4)
        {
            bool bResult = GC.SetPropertyByName(Tokens[1], Tokens[2], Tokens[3]);
            if (bResult)
            {
                std::cout << "Set " << Tokens[1] << "." << Tokens[2] << " to " << Tokens[3] << "\n";
            }
            else
            {
                std::cout << "Failed to set " << Tokens[1] << "." << Tokens[2] << "\n";
            }

            return true;
        }
        else if (Cmd == "call" && Tokens.size() >= 3)
        {
            const std::string& ObjName  = Tokens[1];
            const std::string& FuncName = Tokens[2];

            QObject* Target = GC.FindByDebugName(ObjName);
            if (!Target) {
                std::cout << "[Call] Object not found: " << ObjName << std::endl;
                return true;
            }

            const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Target);

            // Try to locate function meta (name match)
            const qmeta::MetaFunction* MF = nullptr;
            if (Ti) {
                for (const auto& F : Ti->functions) {
                    if (F.name == FuncName) { MF = &F; break; }
                }
            }

            std::vector<qmeta::Variant> Args;
            Args.reserve(Tokens.size() - 3);

            bool typedOk = false;

            if (MF) {
                const size_t ParamCount = MF->params.size();
                const size_t Supplied   = (Tokens.size() > 3) ? (Tokens.size() - 3) : 0;

                if (Supplied < ParamCount) {
                    std::cout << "[Call] Not enough arguments. expected=" << ParamCount << " got=" << Supplied << std::endl;
                    return true;
                }

                typedOk = true;
                for (size_t i = 0; i < ParamCount; ++i) {
                    const auto& P = MF->params[i]; // must have .type
                    qmeta::Variant V;
                    if (!ParseTokenByType(Tokens[3 + i], P.type, GC, V)) {
                        typedOk = false;
                        break;
                    }
                    Args.emplace_back(std::move(V));
                }

                // If more tokens than parameters, push remaining as lenient (variadic-like or ignored by callee)
                for (size_t i = ParamCount; i < Supplied; ++i) {
                    Args.emplace_back(ParseTokenLenient(Tokens[3 + i], GC));
                }
            }

            if (!typedOk) {
                // Fallback: lenient parsing (object names -> QObject*, else numeric/bool/string)
                for (size_t i = 3; i < Tokens.size(); ++i) {
                    Args.emplace_back(ParseTokenLenient(Tokens[i], GC));
                }
            }

            // Make the call
            qmeta::Variant Result = GC.CallByName(ObjName, FuncName, Args);

            // Pretty-print return value
            std::string Formatted = EngineUtils::FormatPropertyValue(Result);
            std::cout << Formatted << std::endl;
            return true;
        }
        else if (Cmd == "save" && Tokens.size() >= 2)
        {
            std::cout << "[save] is not implemented for now.\n";

            //const std::string file = (Tokens.size() >= 3 ? Tokens[2] : "");
            //GC.Save(tokens[1], file);
            return true;
        }
        else if (Cmd == "load" && Tokens.size() >= 3)
        {
            std::cout << "[load] is not implemented for now.\n";
            //const std::string file = (tokens.size() >= 4 ? tokens[3] : "");
            //GC.Load(tokens[1], tokens[2], file);
            return true;
        }
        else
        {
            std::cout << "Unknown command: " << Cmd << "\n";
            return true;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[CommandError] " << e.what() << "\n";
        return true;
    }

    return false;
}
