#include "ConsoleManager.h"

#include <iostream>

#include "GarbageCollector.h"
#include "Runtime.h"

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
    using namespace QGC;

    auto tokens = Tokenize(Line);
    if (tokens.empty())
    {
        return false;
    }

    auto& GC = GcManager::Get();
    const std::string& cmd = tokens[0];

    try
    {
        if (cmd == "help")
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
                "  funcs <Name>" << std::endl;
            return true;
        }
        else if (cmd == "tick" && tokens.size() >= 2)
        {
            double Dt = std::stod(tokens[1]);
            qruntime::Tick(Dt);
            return true;
        }
        else if (cmd == "gc")
        {
            GC.Collect();
            return true;
        }
        else if (cmd == "ls")
        {
            GC.ListObjects();
            return true;
        }
        else if (cmd == "props" && tokens.size() >= 2)
        {
            GC.ListPropertiesByDebugName(tokens[1]);
            return true;
        }
        else if (cmd == "funcs" && tokens.size() >= 2)
        {
            std::cout << "[func] not implemented for now.\n";
            GC.ListFunctionsByDebugName(tokens[1]);
            return true;
        }
        else if (cmd == "new" && tokens.size() >= 3)
        {
            std::cout << "[new] not implemented for now.\n";
            return true;
        }
        else if (cmd == "set" && tokens.size() >= 4)
        {
            std::cout << "[set] not implemented for now.\n";
            //GC.SetPropertyFromString(tokens[1], tokens[2], tokens[3]);
            return true;
        }
        else if (cmd == "call" && tokens.size() >= 3)
        {
            std::cout << "[call] is not implemented for now.\n";
            // std::vector<qmeta::Variant> args;
            // for (size_t i = 3; i < tokens.size(); ++i)
            // {
            //     const std::string& a = tokens[i];
            //     // naive parse: int/float/bool/string
            //     if (a == "true" || a == "false")
            //     {
            //         args.emplace_back(a == "true");
            //     }
            //     else if (a.find('.') != std::string::npos)
            //     {
            //         args.emplace_back(std::stod(a));
            //     }
            //     else
            //     {
            //         // try int
            //         try { args.emplace_back(std::stoll(a)); }
            //         catch (...) { args.emplace_back(a); }
            //     }
            // }
            // GC.Call(tokens[1], tokens[2], args);
            return true;
        }
        else if (cmd == "save" && tokens.size() >= 2)
        {
            std::cout << "[save] is not implemented for now.\n";

            //const std::string file = (tokens.size() >= 3 ? tokens[2] : "");
            //GC.Save(tokens[1], file);
            return true;
        }
        else if (cmd == "load" && tokens.size() >= 3)
        {
            std::cout << "[load] is not implemented for now.\n";
            //const std::string file = (tokens.size() >= 4 ? tokens[3] : "");
            //GC.Load(tokens[1], tokens[2], file);
            return true;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[CommandError] " << e.what() << std::endl;
        return true;
    }

    return false;
}
