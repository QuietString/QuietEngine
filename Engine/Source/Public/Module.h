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

    // Optional tick hook that game module can implement
    struct ITickableModule
    {
        virtual ~ITickableModule() = default;
        virtual void Tick(double DeltaSeconds) {}
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
        Factories_[Name] = std::move(f);
        if (bIsPrimary) Primary_ = Name;
    }

    IModule* EnsureLoaded(const char* Name) {
        auto It = Loaded_.find(Name);
        if (It != Loaded_.end()) return It->second.get();
        auto fit = Factories_.find(Name);
        if (fit == Factories_.end()) return nullptr;
        auto mod = fit->second();
        IModule* raw = mod.get();
        Loaded_[Name] = std::move(mod);
        raw->StartupModule();
        return raw;
    }

    void StartupAll()
    {
        for (auto& Pair : Factories_)
        {
            EnsureLoaded(Pair.first.c_str());
        }
    }

    void ShutdownAll()
    {
        std::vector<std::string> Order;
        Order.reserve(Loaded_.size());
        for (auto& Pair : Loaded_)
        {
            Order.push_back(Pair.first);
        }
        for (auto It = Order.rbegin(); It != Order.rend(); ++It)
        {
            Loaded_[*It]->ShutdownModule();
        }
        Loaded_.clear();
    }

    const char* PrimaryModule() const { return Primary_.empty() ? nullptr : Primary_.c_str(); }

private:
    std::unordered_map<std::string, Factory> Factories_;
    std::unordered_map<std::string, std::unique_ptr<IModule>> Loaded_;
    std::string Primary_;
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
