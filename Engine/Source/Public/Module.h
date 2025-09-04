#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>

namespace qmod {

struct IModule {
    virtual ~IModule() = default;
    virtual const char* GetName() const = 0;
    virtual void StartupModule() {}
    virtual void ShutdownModule() {}
};

class ModuleManager {
public:
    static ModuleManager& Get() {
        static ModuleManager M;
        return M;
    }

    using Factory = std::function<std::unique_ptr<IModule>()>;

    void RegisterFactory(const char* name, Factory f, bool isPrimary = false) {
        factories_[name] = std::move(f);
        if (isPrimary) primary_ = name;
    }

    IModule* EnsureLoaded(const char* name) {
        auto it = loaded_.find(name);
        if (it != loaded_.end()) return it->second.get();
        auto fit = factories_.find(name);
        if (fit == factories_.end()) return nullptr;
        auto mod = fit->second();
        IModule* raw = mod.get();
        loaded_[name] = std::move(mod);
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