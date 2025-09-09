#include "ConsoleManager.h"

#include <iostream>

#include "GarbageCollector.h"
#include "Runtime.h"

// --- helper: stringify a property value by its reflected type ---
static std::string FormatPropertyValue(QObject* Owner, const qmeta::TypeInfo& Ti, const qmeta::MetaProperty& P)
{
    using namespace QGC;
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

    GcManager& Gc = GcManager::Get();

    // QObject* (and any derived) -> print DebugName (or address fallback)
    if (Gc.IsPointerType(T))
    {
        // Read raw pointer value and try to treat it as QObject* if GC-managed
        void* Raw = *reinterpret_cast<void**>(Addr);
        if (!Raw) return "null";

        QObject* AsObj = reinterpret_cast<QObject*>(Raw);
        if (Gc.IsManaged(AsObj))
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
        if (Gc.IsVectorOfPointer(T))
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
                if (Gc.IsManaged(E))
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
    using namespace QGC;

    auto Tokens = Tokenize(Line);
    if (Tokens.empty())
    {
        return false;
    }

    auto& GC = GcManager::Get();
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
                "  funcs <Name>" << std::endl;
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
            GC.Collect();
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
                return false;
            }

            const std::string& Name = Tokens[1];
            const std::string& Prop = Tokens[2];

            QObject* Obj = GC.FindByDebugName(Name);
            if (!Obj)
            {
                std::cout << "Not found: " << Name << std::endl;
                return false;
            }

            const qmeta::TypeInfo* Ti = GC.GetTypeInfo(Obj);
            if (!Ti)
            {
                std::cout << "No TypeInfo for: " << Name << std::endl;
                return false;
            }

            // Locate property meta to know its type and offset
            const qmeta::MetaProperty* MP = nullptr;
            for (auto& P : Ti->properties)
            {
                if (P.name == Prop) { MP = &P; break; }
            }
            if (!MP)
            {
                std::cout << "Property not found: " << Prop << std::endl;
                return false;
            }

            std::string ValueStr = FormatPropertyValue(Obj, *Ti, *MP);
            std::cout << ValueStr << std::endl;
            return true;
        }
        else if (Cmd == "funcs" && Tokens.size() >= 2)
        {
            std::cout << "[func] not implemented for now.\n";
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
            if (Tokens.size() < 3 || Tokens.size() > 5)
            {
                std::cout << "Usage: unlink <OwnerName> <Property> [single|all]\n";
                return false;
            }
            
            if (Tokens[1] == "single" && Tokens.size() == 4)
            {
                bool bResult = GC.UnlinkByName(Tokens[2], Tokens[3]);
                if (!bResult)
                {
                    std::cout << "Failed to unlink " << Tokens[1] << "." << Tokens[2] << std::endl;
                }
                
                return bResult;
            }
            else if (Tokens[1] == "all" && Tokens.size() == 3)
            {
                bool bResult = GC.UnlinkAllByName(Tokens[2]);
                if (!bResult)
                {
                    std::cout << "Failed to unlink " << Tokens[1] << "." << Tokens[2] << std::endl;
                }

                return bResult;
            }
            else
            {
                std::cout << "Usage: unlink <OwnerName> <Property> [single|all]" << std::endl;
                return false;
            }
        }
        else if (Cmd == "set" && Tokens.size() >= 4)
        {
            bool bResult = GC.SetPropertyByName(Tokens[1], Tokens[2], Tokens[3]);
            if (bResult)
            {
                std::cout << "Set " << Tokens[1] << "." << Tokens[2] << " to " << Tokens[3] << std::endl;
            }
            else
            {
                std::cout << "Failed to set " << Tokens[1] << "." << Tokens[2] << std::endl;
            }

            return bResult;
        }
        else if (Cmd == "call" && Tokens.size() >= 3)
        {
            std::vector<qmeta::Variant> Args;
            for (size_t i = 3; i < Tokens.size(); ++i)
            {
                const std::string& a = Tokens[i];
                // naive parse: int/float/bool/string
                if (a == "true" || a == "false")
                {
                    Args.emplace_back(a == "true");
                }
                else if (a.find('.') != std::string::npos)
                {
                    Args.emplace_back(std::stod(a));
                }
                else
                {
                    // try int
                    try { Args.emplace_back(std::stoll(a)); }
                    catch (...) { Args.emplace_back(a); }
                }
            }
            
            GC.CallByName(Tokens[1], Tokens[2], Args);
            
            return true;
        }
        else if (Cmd == "save" && Tokens.size() >= 2)
        {
            std::cout << "[save] is not implemented for now.\n";

            //const std::string file = (tokens.size() >= 3 ? tokens[2] : "");
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
            std::cout << "Unknown command: " << Cmd << std::endl;
            return false;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[CommandError] " << e.what() << std::endl;
        return true;
    }

    return false;
}
