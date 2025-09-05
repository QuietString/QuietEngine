#include <iostream>
#include <Module.h>

#include "qmeta_runtime.h"
#include "Demo.h"
#include "GarbageCollector.h"
#include "Runtime.h"

int main(int argc, char** argv)
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    qmod::ModuleManager& M = qmod::ModuleManager::Get();
    M.StartupAll();
    
    QGC::GcManager::Get().SetAutoInterval(15.f);
    QGC::GcManager::Get().Initialize();

    std::cout << "Type 'help' for commands." << std::endl;
    qruntime::StartConsoleInput();

    qruntime::RunMainLoop(std::chrono::milliseconds(16), 5);
    
    //Demo::RunGCTest();
    qruntime::StopConsoleInput();
    
    M.ShutdownAll();
    return 0;
}
