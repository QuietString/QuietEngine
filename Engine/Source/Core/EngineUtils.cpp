#include "EngineUtils.h"

#include <sstream>

#include "GarbageCollector.h"

// --- helper: stringify a property value by its reflected type ---
std::string EngineUtils::FormatPropertyValue(QObject* Owner, const qmeta::MetaProperty& P)
{
    using namespace qmeta;

    std::ostringstream OutputStream;

    GarbageCollector& GC = GarbageCollector::Get();
    const TypeInfo& Ti = *GC.GetTypeInfo(Owner);
    
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
