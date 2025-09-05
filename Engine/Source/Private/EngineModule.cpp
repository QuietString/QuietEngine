#include <Module.h>
#include <qmeta_runtime.h>

// Forward from the bridge file
void RegisterEngineReflections(qmeta::Registry&);

class FEngineModule final : public qmod::IModule {
public:
    virtual const char* GetName() const override { return "Engine"; }
    virtual void StartupModule() override {
        qmeta::Registry& R = qmeta::GetRegistry();
        RegisterEngineReflections(R);
        // TODO: initialize engine singletons, asset registry, etc.
    }
    virtual void ShutdownModule() override {
        // TODO: shutdown systems
    }
};

Q_IMPLEMENT_MODULE(FEngineModule, "Engine")

// Forces this translation unit to be linked so the static auto-registrar runs.
extern "C" void Q_Mod_Engine_Anchor()
{
}