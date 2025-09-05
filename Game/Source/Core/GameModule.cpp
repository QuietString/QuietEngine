#include <Module.h>
#include <qmeta_runtime.h>

Q_FORCE_LINK_MODULE(Engine);

void RegisterGameReflections(qmeta::Registry&);

class FGameModule final : public qmod::IModule {
public:
    virtual const char* GetName() const override { return "Game"; }
    virtual void StartupModule() override {
        qmeta::Registry& R = qmeta::GetRegistry();
        RegisterGameReflections(R);
        // TODO: register game systems, content types, etc.
    }
    virtual void ShutdownModule() override {}
};

Q_IMPLEMENT_PRIMARY_GAME_MODULE(FGameModule, "Game")