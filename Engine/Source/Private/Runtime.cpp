#include "Runtime.h"
#include "GarbageCollector.h"
#include <sstream>
#include <vector>
#include <iostream>
#include <cctype>
#include <iomanip>

#include "qmeta_runtime.h"

namespace
{
    // Simple tokenizer: split by spaces, keep quoted strings.
    std::vector<std::string> Tokenize(const std::string& s)
    {
        std::vector<std::string> out;
        std::istringstream is(s);
        std::string tok;
        while (is >> std::quoted(tok))
        {
            out.push_back(tok);
        }
        return out;
    }
}

void qruntime::Tick(double DeltaSeconds)
{
    QGC::GcManager::Get().Tick(DeltaSeconds);
}

void qruntime::SetGcInterval(double Seconds)
{
    QGC::GcManager::Get().SetAutoInterval(Seconds);
}

bool qruntime::ExecuteCommand(const std::string& Line)
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
                "  funcs <Name>\n";
            return true;
        }
        else if (cmd == "tick" && tokens.size() >= 2)
        {
            double dt = std::stod(tokens[1]);
            qruntime::Tick(dt);
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
            GC.ListProperties(tokens[1]);
            return true;
        }
        else if (cmd == "funcs" && tokens.size() >= 2)
        {
            GC.ListFunctions(tokens[1]);
            return true;
        }
        else if (cmd == "new" && tokens.size() >= 3)
        {
            GC.NewByTypeName(tokens[1], tokens[2]);
            return true;
        }
        else if (cmd == "link" && tokens.size() >= 4)
        {
            GC.Link(tokens[1], tokens[2], tokens[3]);
            return true;
        }
        else if (cmd == "unlink" && tokens.size() >= 3)
        {
            GC.Unlink(tokens[1], tokens[2]);
            return true;
        }
        else if (cmd == "set" && tokens.size() >= 4)
        {
            GC.SetPropertyFromString(tokens[1], tokens[2], tokens[3]);
            return true;
        }
        else if (cmd == "call" && tokens.size() >= 3)
        {
            std::vector<qmeta::Variant> args;
            for (size_t i = 3; i < tokens.size(); ++i)
            {
                const std::string& a = tokens[i];
                // naive parse: int/float/bool/string
                if (a == "true" || a == "false")
                {
                    args.emplace_back(a == "true");
                }
                else if (a.find('.') != std::string::npos)
                {
                    args.emplace_back(std::stod(a));
                }
                else
                {
                    // try int
                    try { args.emplace_back(std::stoll(a)); }
                    catch (...) { args.emplace_back(a); }
                }
            }
            GC.Call(tokens[1], tokens[2], args);
            return true;
        }
        else if (cmd == "save" && tokens.size() >= 2)
        {
            const std::string file = (tokens.size() >= 3 ? tokens[2] : "");
            GC.Save(tokens[1], file);
            return true;
        }
        else if (cmd == "load" && tokens.size() >= 3)
        {
            const std::string file = (tokens.size() >= 4 ? tokens[3] : "");
            GC.Load(tokens[1], tokens[2], file);
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
