#include "ConsoleManager.h"

#include <iostream>

#include "ConsoleUtil.h"
#include "EngineUtils.h"
#include "GarbageCollector.h"
#include "Runtime.h"
#include "CoreObjects/Public/World.h"

using namespace ConsoleUtil;

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
            else if (Tokens.size() == 2)
            {
                if (Tokens[1] == "t")
                {
                    GC.SetAllowTraverseParents(true);
                    std::cout << "GC parent traversal enabled.\n";
                }
                else if (Tokens[1] == "f")
                {
                    GC.SetAllowTraverseParents(false);
                    std::cout << "GC parent traversal disabled.\n";
                }
                else
                {
                    std::cout << "Usage: gc <t|f>\n";
                }
            }
            else if (Tokens.size() == 3 && Tokens[1] == "threads")
            {
                if (Tokens[2] == "auto")
                {
                    //GC.SetMaxGcThreads(0);
                    std::cout << "[gc] threads = auto\n";
                }
                else
                {
                    long long n = 0;
                    if (!TryParseInt(Tokens[2], n) || n < 0)
                    {
                        std::cout << "Usage: gc threads <n|auto>\n";
                        return true;
                    }
                    //GC.SetMaxGcThreads((int)n);
                    std::cout << "[gc] threads = " << n << "\n";
                }
                return true;
            }
            else if (Tokens.size() == 4)
            {
                if (Tokens[1] == "set" && Tokens[2] == "interval")
                {
                    double Interval = std::stod(Tokens[3]);
                    GC.SetAutoInterval(Interval);
                }
                else
                {
                    std::cout << "Usage: gc set interval <seconds>\n";
                }
            }
            else
            {
                std::cout << "Invalid gc command\n";
            }
        }
        else if (Cmd == "gctest")
        {
            if (Tokens.size() < 2)
            {
                std::cout << "Usage: gctest <subcmd> ...\n";
                return true;
            }
            
            auto* W = GetWorld();
            if (!W) { std::cout << "World not found.\n"; return true; }
            
            // auto FindTesters = [&]() -> std::vector<QObject*> {
            //     std::vector<QObject*> Testers;
            //     
            //     const qmeta::TypeInfo* Ti = nullptr;
            //     for (QObject* Obj : W->Objects)
            //     {
            //         if (!Obj) continue;
            //         Ti = GC.GetTypeInfo(Obj);
            //         if (Ti && Ti->name == "QGcTester")
            //         {
            //             Testers.emplace_back(Obj);
            //         }
            //     }
            //
            //     for (QObject* Obj : GC.GetRoots())
            //     {
            //         Ti = GC.GetTypeInfo(Obj);
            //         if (Ti && Ti->name == "QGcTester")
            //         {
            //             Testers.emplace_back(Obj);
            //         }
            //     }
            //
            //     return Testers;
            // };

            auto FindTestManager = [&]() -> QObject* {
                
                const qmeta::TypeInfo* Ti = nullptr;
                for (QObject* Obj : W->Objects)
                {
                    if (!Obj) continue;
                    Ti = GC.GetTypeInfo(Obj);
                    if (Ti && Ti->name == "QGcTestManager")
                    {
                        return Obj;
                    }
                }

                return nullptr;
            };
            
            //std::vector<QObject*> Testers = FindTesters();
            QObject* TestManager = FindTestManager();
            if (!TestManager)
            {
                std::cout << "QGcTestManager instance not found. Make sure the Game module created it in BeginPlay().\n";
                return true;
            }
            if (!TestManager)
            {
                std::cout << "QGcTestManager instance not found. Make sure the Game module created it in BeginPlay().\n";
                return true;
            }

            const std::string& Sub = Tokens[1];
            
            if (Sub == "repeat")
            {
                if (Tokens.size() < 5)
                {
                    std::cout << "Usage: gctest repeat <NumSteps> <NumNodes> <NumBranches>\n";
                    return true;
                }

                long long NumSteps = 0, NumNodes = 0, NumBranches = 0;
                if (!TryParseInt(Tokens[2], NumSteps) ||
                    !TryParseInt(Tokens[3], NumNodes) ||
                    !TryParseInt(Tokens[4], NumBranches))
                {
                    std::cout << "[gctest] invalid args. expected integers.\n";
                    return true;
                }
                
                if (!TestManager)
                {
                    std::cout << "[gctest] failed to get/create QGcTester\n";
                    return true;
                }

                // Call by reflection to avoid direct header dependency
                std::vector<qmeta::Variant> Args;
                Args.emplace_back(NumSteps);
                Args.emplace_back(NumNodes);
                Args.emplace_back(NumBranches);

                // for (auto& Tester : Testers)
                // {
                //     qmeta::Variant Ret = GC.CallByName(Tester->GetDebugName(), "RepeatRandomAndCollect", Args);    
                // }
                qmeta::Variant Ret = GC.CallByName(TestManager->GetDebugName(), "RepeatRandomAndCollect", Args);
                
                return true;
            }
            else if (Sub == "config")
            {
                // Usage: gctest config <AssignMode 0~2> <t|f|true|false|0|1>
                if (Tokens.size() < 4)
                {
                    std::cout << "Usage: gctest config <AssignMode 0~2> <t|f>\n";
                    return true;
                }

                long long ModeLL = 0;
                if (!TryParseInt(Tokens[2], ModeLL) || ModeLL < 0 || ModeLL > 2)
                {
                    std::cout << "[gctest] AssignMode must be 0, 1, or 2.\n";
                    return true;
                }
                const int AssignMode = static_cast<int>(ModeLL);

                std::string bTok = Tokens[3];
                for (auto& c : bTok) c = (char)std::tolower((unsigned char)c);
                bool bUseVector = false;
                if (bTok == "t" || bTok == "true" || bTok == "1") bUseVector = true;
                else if (bTok == "f" || bTok == "false" || bTok == "0") bUseVector = false;
                else
                {
                    std::cout << "[gctest] boolean must be t|f|true|false|0|1.\n";
                    return true;
                }

                if (!TestManager)
                {
                    std::cout << "[gctest] failed to get/create QGcTester\n";
                    return true;
                }

                // Call setters via reflection
                try
                {
                    std::vector<qmeta::Variant> Args1;
                    Args1.emplace_back((long long)AssignMode);
                    //for (auto& Tester : Testers)
                    {
                        GC.CallByName(TestManager->GetDebugName(), "SetAssignMode", Args1);
                    }
                    

                    std::vector<qmeta::Variant> Args2;
                    Args2.emplace_back(bUseVector);
                    //for (auto& Tester : Testers)
                    {
                        GC.CallByName(TestManager->GetDebugName(), "SetUseVector", Args2);
                    }
                }
                catch (const std::exception& e)
                {
                    std::cout << "[gctest] config error: " << e.what() << std::endl;
                    return true;
                }

                //for (auto& Tester : Testers)
                {
                    std::cout << "[gctest] config applied on "
                             << (TestManager->GetDebugName().empty() ? "(Unnamed)" : TestManager->GetDebugName())
                             << " : AssignMode=" << AssignMode
                             << ", bUseVector=" << (bUseVector ? "true" : "false") << std::endl;   
                }

                return true;
            }
                        
            if (Tokens.size() == 2 && Tokens[1] == "clear")
            {
                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "ClearAll", {false});    
                }
                
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

                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "Build", { qmeta::Variant(Roots), qmeta::Variant(Depth), qmeta::Variant(Branch), qmeta::Variant(Seed) });    
                }
                
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
                    //for (auto& Tester : Testers)
                    {
                        GC.Call(TestManager, "PatternChain", { qmeta::Variant(length), qmeta::Variant(seed) });
                    }
                    return true;
                }
                else if (Mode == "grid")
                {
                    if (Tokens.size() < 5)
                    {
                        std::cout << "gctest pattern grid <w> <h> [seed]\n";
                        return true;
                    }
                    int w = std::stoi(Tokens[3]);
                    int h = std::stoi(Tokens[4]);
                    int seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 1;
                    //for (auto& Tester : Testers)
                    {
                        GC.Call(TestManager, "PatternGrid", { qmeta::Variant(w), qmeta::Variant(h), qmeta::Variant(seed) });
                    }
                    return true;
                }
                else if (Mode == "random")
                {
                    if (Tokens.size() < 5) { std::cout << "gctest pattern random <nodes> <branchCount> [seed]\n"; return true; }
                    int Nodes = std::stoi(Tokens[3]);
                    int BranchCount = std::stoi(Tokens[4]);
                    int Seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 1337;
                    //for (auto& Tester : Testers)
                    {
                        GC.Call(TestManager, "PatternRandom", { qmeta::Variant(Nodes), qmeta::Variant(BranchCount), qmeta::Variant(Seed) });
                    }
                    return true;
                }
                else if (Mode == "rings")
                {
                    if (Tokens.size() < 5) { std::cout << "gctest pattern rings <rings> <ringSize> [seed]\n"; return true; }
                    int rings = std::stoi(Tokens[3]);
                    int ringSize = std::stoi(Tokens[4]);
                    int seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 7;
                    //for (auto& Tester : Testers)
                    {
                        GC.Call(TestManager, "PatternRings", { qmeta::Variant(rings), qmeta::Variant(ringSize), qmeta::Variant(seed) });
                    }
                    return true;
                }
                else if (Mode == "diamond")
                {
                    if (Tokens.size() < 5) { std::cout << "gctest pattern diamond <layers> <breadth> [seed]\n"; return true; }
                    int layers = std::stoi(Tokens[3]);
                    int breadth = std::stoi(Tokens[4]);
                    int seed = (Tokens.size() >= 6) ? std::stoi(Tokens[5]) : 3;
                    //for (auto& Tester : Testers)
                    {
                        GC.Call(TestManager, "PatternDiamond", { qmeta::Variant(layers), qmeta::Variant(breadth), qmeta::Variant(seed) });
                    }
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
                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "BreakAtDepth", { qmeta::Variant(depth), qmeta::Variant(count), qmeta::Variant(seed) });
                }
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
                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "BreakPercent", { qmeta::Variant(pct), qmeta::Variant(depth), qmeta::Variant(seed), qmeta::Variant(false) });
                }
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "breakedges")
            {
                // gctest breakedges <count> [seed]
                if (Tokens.size() < 3) { std::cout << "gctest breakedges <count> [seed]\n"; return true; }
                int count = std::stoi(Tokens[2]);
                int seed  = (Tokens.size() >= 4) ? std::stoi(Tokens[3]) : 99;
                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "BreakRandomEdges", { qmeta::Variant(count), qmeta::Variant(seed) });
                }
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "detachroots")
            {
                // gctest detachroots <count|0> [ratio]
                if (Tokens.size() < 3) { std::cout << "gctest detachroots <count> [ratio]\n"; return true; }
                int Count = std::stoi(Tokens[2]);
                double Ratio = (Tokens.size() >= 4) ? std::stod(Tokens[3]) : 0.0;
                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "DetachRoots", { qmeta::Variant(Count), qmeta::Variant(Ratio) });
                }
                return true;
            }
            else if (Tokens.size() >= 2 && Tokens[1] == "measure")
            {
                // gctest measure <repeats>
                if (Tokens.size() < 3) { std::cout << "gctest measure <repeats>\n"; return true; }
                int rep = std::stoi(Tokens[2]);
                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "MeasureGc", { qmeta::Variant(rep) });
  
                }
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
                //for (auto& Tester : Testers)
                {
                    GC.Call(TestManager, "Churn", { qmeta::Variant(steps), qmeta::Variant(allocPerStep),
                                        qmeta::Variant(breakPct), qmeta::Variant(gcEveryN),
                                        qmeta::Variant(seed) });
                }
                
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
        else if (Cmd == "info" && Tokens.size() >= 2)
        {
            const std::string& ObjName = Tokens[1];
            QObject* Obj = GC.FindByDebugName(ObjName);
            if (!Obj)
            {
                std::cout << "[Info] Not found: " << ObjName << std::endl;
                return true;
            }

            const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Obj);
            if (!Ti)
            {
                std::cout << "[Info] No TypeInfo for: " << ObjName << std::endl;
                return true;
            }

            // 1) Build class chain (most-derived -> base)
            std::vector<const qmeta::TypeInfo*> ClassChain;
            ClassChain.reserve(8);
            BuildClassChain(Ti, ClassChain);

            // 2) Header
            std::cout << "[Info]\n";
            const std::string& DispName = Obj->GetDebugName();
            std::cout << "Name: " << (DispName.empty() ? "(Unnamed)" : DispName) << "\n";
            std::cout << "Class: " << JoinClassChain(ClassChain) << "\n";

            // 3) Properties (per class in chain order)
            std::cout << "Properties:\n";
            for (const qmeta::TypeInfo* ClassType : ClassChain)
            {
                if (ClassType->properties.empty()) continue;
                std::cout << "  [" << ClassType->name << "]\n";
                for (const auto& P : ClassType->properties)
                {
                    // Pretty-print value using reflection-aware formatter
                    std::string Val = EngineUtils::FormatPropertyValue(Obj, P);
                    std::cout << "    - " << P.type << " " << P.name << " = " << Val << "\n";
                }
            }

            // 4) Functions (per class in chain order)
            std::cout << "Functions:\n";
            for (const qmeta::TypeInfo* CT : ClassChain)
            {
                if (CT->functions.empty()) continue;
                std::cout << "  [" << CT->name << "]\n";
                for (const auto& F : CT->functions)
                {
                    std::ostringstream sig;
                    sig << "    - " << F.return_type << " " << F.name << "(";
                    for (size_t i = 0; i < F.params.size(); ++i)
                    {
                        if (i) sig << ", ";
                        sig << F.params[i].type << " " << F.params[i].name;
                    }
                    sig << ")";
                    std::cout << sig.str() << "\n";
                }
            }

            return true;
        }
        else if (Cmd == "new")
        {
            if (Tokens.size() == 2)
            {
                const std::string& ClassName = Tokens[1];
                if (QObject* Obj = GarbageCollector::NewObjectByName(ClassName))
                {
                    std::cout << "New object created: " << ClassName << " " << Obj->GetDebugName() << "\n";   
                }
                
                return true;    
            }
            else
            {
                std::cout << "Usage: new <ClassName>\n";
            }
            
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
            if (!Target)
            {
                std::cout << "[Call] Object not found: " << ObjName << std::endl;
                return true;
            }

            const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Target);

            // Try to locate function meta (name match)
            const qmeta::MetaFunction* MetaFunc = nullptr;
            if (Ti)
            {
                for (const auto& F : Ti->functions)
                {
                    if (F.name == FuncName)
                    {
                        MetaFunc = &F; break;
                    }
                }
            }

            std::vector<qmeta::Variant> Args;
            Args.reserve(Tokens.size() - 3);

            bool TypedOk = false;

            if (MetaFunc)
            {
                const size_t ParamCount = MetaFunc->params.size();
                const size_t Supplied   = (Tokens.size() > 3) ? (Tokens.size() - 3) : 0;

                if (Supplied < ParamCount) {
                    std::cout << "[Call] Not enough arguments. expected=" << ParamCount << " got=" << Supplied << std::endl;
                    return true;
                }

                TypedOk = true;
                for (size_t i = 0; i < ParamCount; ++i)
                {
                    const auto& MetaParam = MetaFunc->params[i]; // must have .type
                    qmeta::Variant ArgsVariant;
                    if (!ParseTokenByType(Tokens[3 + i], MetaParam.type, GC, ArgsVariant))
                    {
                        TypedOk = false;
                        break;
                    }
                    Args.emplace_back(std::move(ArgsVariant));
                }

                // If more tokens than parameters, push remaining as lenient (variadic-like or ignored by callee)
                for (size_t i = ParamCount; i < Supplied; ++i)
                {
                    Args.emplace_back(ParseTokenLenient(Tokens[3 + i], GC));
                }
            }

            if (!TypedOk)
            {
                // Fallback: lenient parsing (object names -> QObject*, else numeric/bool/string)
                for (size_t i = 3; i < Tokens.size(); ++i)
                {
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
