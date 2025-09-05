#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>

namespace qmod {

struct IModule
{
    virtual ~IModule() = default;
    virtual const char* GetName() const = 0;
    virtual void StartupModule() {}
    virtual void ShutdownModule() {}
};

// A singleton class
class ModuleManager {
public:
    static ModuleManager& Get() {
        static ModuleManager M;
        return M;
    }

    using Factory = std::function<std::unique_ptr<IModule>()>;

    void RegisterFactory(const char* Name, Factory f, bool bIsPrimary = false)
    {
        factories_[Name] = std::move(f);
        if (bIsPrimary) primary_ = Name;
    }

    IModule* EnsureLoaded(const char* Name) {
        auto It = loaded_.find(Name);
        if (It != loaded_.end()) return It->second.get();
        auto fit = factories_.find(Name);
        if (fit == factories_.end()) return nullptr;
        auto mod = fit->second();
        IModule* raw = mod.get();
        loaded_[Name] = std::move(mod);
        raw->StartupModule();
        return raw;
    }

    void StartupAll() {
        for (auto& kv : factories_) EnsureLoaded(kv.first.c_str());
    }

    void ShutdownAll() {
        std::vector<std::string> order;
        order.reserve(loaded_.size());
        for (auto& kv : loaded_) order.push_back(kv.first);
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            loaded_[*it]->ShutdownModule();
        }
        loaded_.clear();
    }

    const char* PrimaryModule() const { return primary_.empty() ? nullptr : primary_.c_str(); }

private:
    std::unordered_map<std::string, Factory> factories_;
    std::unordered_map<std::string, std::unique_ptr<IModule>> loaded_;
    std::string primary_;
};

}

// Macros to declare/implement modules without DLLs.
#define Q_DECLARE_MODULE(ModuleClass) \
    class ModuleClass : public qmod::IModule { \
    public: \
        virtual const char* GetName() const override { return #ModuleClass; } \
        virtual void StartupModule() override; \
        virtual void ShutdownModule() override; \
    };

#define Q_IMPLEMENT_MODULE(ModuleClass, ModuleNameLiteral) \
    namespace { \
        struct ModuleClass##_AutoReg { \
            ModuleClass##_AutoReg() { \
                qmod::ModuleManager::Get().RegisterFactory(ModuleNameLiteral, [](){ return std::make_unique<ModuleClass>(); }, false); \
            } \
        } g_##ModuleClass##_AutoReg; \
    }

#define Q_IMPLEMENT_PRIMARY_GAME_MODULE(ModuleClass, ModuleNameLiteral) \
    namespace { \
        struct ModuleClass##_PrimaryAutoReg { \
            ModuleClass##_PrimaryAutoReg() { \
                qmod::ModuleManager::Get().RegisterFactory(ModuleNameLiteral, [](){ return std::make_unique<ModuleClass>(); }, true); \
            } \
        } g_##ModuleClass##_PrimaryAutoReg; \
    }

// Force-link a module by touching its anchor function.
#define Q_FORCE_LINK_MODULE(ModuleTag)                               \
extern "C" void Q_Mod_##ModuleTag##_Anchor();                    \
static int S_Force_Link_##ModuleTag = (Q_Mod_##ModuleTag##_Anchor(), 0)
