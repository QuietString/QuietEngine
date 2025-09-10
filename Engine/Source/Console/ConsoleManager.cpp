#include "ConsoleManager.h"

#include <iostream>

#include "GarbageCollector.h"
#include "Runtime.h"
#include "CoreObjects/Public/World.h"

// --- helper: stringify a property value by its reflected type ---
static std::string FormatPropertyValue(QObject* Owner, const qmeta::TypeInfo& Ti, const qmeta::MetaProperty& P)
{
    using namespace qmeta;

    std::ostringstream OutputStream;

    // Locate property memory by reflection offset
    void* Addr = GetPropertyPtr(static_cast<void*>(Owner), Ti, P.name);
    if (!Addr) return "<invalid address>";

    const std::string& T = P.type;

    auto equals_any = [&](const std::initializer_list<const char*> names) -> bool {
        for (const char* n : names) { if (T == n) return true; }
        return false;
    };

    // Primitive integers / floats / bool / string
    if (equals_any({"int","int32_t"}))                      { OutputStream << *reinterpret_cast<int*>(Addr); return OutputStream.str(); }
    if (equals_any({"int64_t","long long"}))                { OutputStream << *reinterpret_cast<int64_t*>(Addr); return OutputStream.str(); }
    if (equals_any({"unsigned","unsigned int","uint32_t"})) { OutputStream << *reinterpret_cast<unsigned*>(Addr); return OutputStream.str(); }
    if (equals_any({"uint64_t","unsigned long long"}))      { OutputStream << *reinterpret_cast<uint64_t*>(Addr); return OutputStream.str(); }
    if (equals_any({"float"}))                              { OutputStream << *reinterpret_cast<float*>(Addr); return OutputStream.str(); }
    if (equals_any({"double"}))                             { OutputStream << *reinterpret_cast<double*>(Addr); return OutputStream.str(); }
    if (equals_any({"bool"}))                               { OutputStream << (*reinterpret_cast<bool*>(Addr) ? "true" : "false"); return OutputStream.str(); }
    if (equals_any({"std::string","string"}))               { OutputStream << '"' << *reinterpret_cast<std::string*>(Addr) << '"'; return OutputStream.str(); }

    GarbageCollector& GC = GarbageCollector::Get();

    // QObject* (and any derived) -> print DebugName (or address fallback)
    if (GC.IsPointerType(T))
    {
        // Read raw pointer value and try to treat it as QObject* if GC-managed
        void* Raw = *reinterpret_cast<void**>(Addr);
        if (!Raw) return "null";

        QObject* AsObj = reinterpret_cast<QObject*>(Raw);
        if (GC.IsManaged(AsObj))
        {
            const std::string& Nm = AsObj->GetDebugName();
            return Nm.empty() ? "(Unnamed)" : Nm;
        }
        else
        {
            std::ostringstream oss;
            oss << Raw;
            return oss.str();
        }
    }
    
    // std::vector<...>
    if (T.find("std::vector") != std::string::npos)
    {
        // any std::vector<T*> (T possibly derived from QObject)
        if (GC.IsVectorOfPointer(T))
        {
            auto* Vec = reinterpret_cast<const std::vector<QObject*>*>(Addr);
            const size_t Count = Vec->size();
            const size_t MaxPreview = 8;
            OutputStream << "size=" << Count << " [" << Ti.name << "*] [";
            size_t Limit = std::min(Count, MaxPreview);
            for (size_t i = 0; i < Limit; ++i)
            {
                if (i) OutputStream << ", ";
                QObject* E = (*Vec)[i];
                if (!E) { OutputStream << "null"; continue; }
                if (GC.IsManaged(E))
                {
                    const std::string& Nm = E->GetDebugName();
                    OutputStream << (Nm.empty() ? "(Unnamed)" : Nm);
                }
                else
                {
                    OutputStream << static_cast<void*>(E);
                }
            }
            if (Count > Limit) OutputStream << ", ...";
            OutputStream << "]";
            return OutputStream.str();
        }
        
        // Common primitive vectors preview
        auto preview_prim = [&](auto* VecPtr, const char* tag) -> std::string {
            const size_t Count = VecPtr->size();
            const size_t MaxPreview = 8;
            OutputStream << "size=" << Count << " [" << tag << "] [";
            size_t Limit = std::min(Count, MaxPreview);
            for (size_t i = 0; i < Limit; ++i)
            {
                if (i) OutputStream << ", ";
                OutputStream << (*VecPtr)[i];
            }
            if (Count > Limit) OutputStream << ", ...";
            OutputStream << "]";
            return OutputStream.str();
        };

        if (T.find("int>") != std::string::npos)               return preview_prim(reinterpret_cast<std::vector<int>*>(Addr), "int");
        if (T.find("int32_t>") != std::string::npos)           return preview_prim(reinterpret_cast<std::vector<int32_t>*>(Addr), "int32_t");
        if (T.find("int64_t>") != std::string::npos)           return preview_prim(reinterpret_cast<std::vector<int64_t>*>(Addr), "int64_t");
        if (T.find("unsigned>") != std::string::npos || T.find("uint32_t>") != std::string::npos)
                                                               return preview_prim(reinterpret_cast<std::vector<unsigned>*>(Addr), "unsigned");
        if (T.find("uint64_t>") != std::string::npos)          return preview_prim(reinterpret_cast<std::vector<uint64_t>*>(Addr), "uint64_t");
        if (T.find("float>") != std::string::npos)             return preview_prim(reinterpret_cast<std::vector<float>*>(Addr), "float");
        if (T.find("double>") != std::string::npos)            return preview_prim(reinterpret_cast<std::vector<double>*>(Addr), "double");

        if (T.find("bool>") != std::string::npos)
        {
            auto* Vec = reinterpret_cast<std::vector<bool>*>(Addr);
            const size_t Count = Vec->size();
            const size_t MaxPreview = 8;
            OutputStream << "size=" << Count << " [bool] [";
            size_t Limit = std::min(Count, MaxPreview);
            for (size_t i = 0; i < Limit; ++i)
            {
                if (i) OutputStream << ", ";
                OutputStream << ((*Vec)[i] ? "true" : "false");
            }
            if (Count > Limit) OutputStream << ", ...";
            OutputStream << "]";
            return OutputStream.str();
        }

        if (T.find("std::string>") != std::string::npos || T.find("string>") != std::string::npos)
        {
            auto* Vec = reinterpret_cast<std::vector<std::string>*>(Addr);
            const size_t Count = Vec->size();
            const size_t MaxPreview = 8;
            OutputStream << "size=" << Count << " [string] [";
            size_t Limit = std::min(Count, MaxPreview);
            for (size_t i = 0; i < Limit; ++i)
            {
                if (i) OutputStream << ", ";
                OutputStream << '"' << (*Vec)[i] << '"';
            }
            if (Count > Limit) OutputStream << ", ...";
            OutputStream << "]";
            return OutputStream.str();
        }

        // Fallback
        OutputStream << "<vector preview not supported>";
        return OutputStream.str();
    }

    // Unknown type fallback
    OutputStream << "<unhandled type: " << T << ">";
    return OutputStream.str();
}

///////

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
                        return O;
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
                    if (Tokens.size() < 5) { std::cout << "gctest pattern random <nodes> <avgOut> [seed]\n"; return true; }
                    int nodes = std::stoi(Tokens[3]);
                    int avgOut = std::stoi(Tokens[4]);
                    int seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 1337;
                    GC.Call(Tester, "PatternRandom", { qmeta::Variant(nodes), qmeta::Variant(avgOut), qmeta::Variant(seed) });
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
                if (Tokens.size() < 3) { std::cout << "gctest breakp <percent> [depth|-1] [seed]\n"; return true; }
                double pct = std::stod(Tokens[2]);
                int depth = (Tokens.size() >= 4) ? std::stoi(Tokens[3]) : -1;
                int seed  = (Tokens.size() >= 5) ? std::stoi(Tokens[4]) : 24;
                GC.Call(Tester, "BreakPercent", { qmeta::Variant(pct), qmeta::Variant(depth), qmeta::Variant(seed) });
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

            const std::string& Name = Tokens[1];
            const std::string& Prop = Tokens[2];

            QObject* Obj = GC.FindByDebugName(Name);
            if (!Obj)
            {
                std::cout << "Not found: " << Name << "\n";
                return true;
            }

            const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Obj);
            if (!Ti)
            {
                std::cout << "No TypeInfo for: " << Name << "\n";
                return true;
            }

            // Locate property meta to know its type and offset
            const qmeta::MetaProperty* MP = nullptr;
            for (auto& P : Ti->properties)
            {
                if (P.name == Prop) { MP = &P; break; }
            }
            if (!MP)
            {
                std::cout << "Property not found: " << Prop << "\n";
                return true;
            }

            std::string ValueStr = FormatPropertyValue(Obj, *Ti, *MP);
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
            std::vector<qmeta::Variant> Args;
            for (size_t i = 3; i < Tokens.size(); ++i)
            {
                const std::string& CurToken = Tokens[i];
                // naive parse: int/float/bool/string
                if (CurToken == "true" || CurToken == "false")
                {
                    Args.emplace_back(CurToken == "true");
                }
                else if (CurToken.find('.') != std::string::npos)
                {
                    Args.emplace_back(std::stod(CurToken));
                }
                else
                {
                    // try int
                    try { Args.emplace_back(std::stoll(CurToken)); }
                    catch (...) { Args.emplace_back(CurToken); }
                }
            }
            
            GC.CallByName(Tokens[1], Tokens[2], Args);
            
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
