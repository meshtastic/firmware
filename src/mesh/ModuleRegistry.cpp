#include "ModuleRegistry.h"

MeshModuleFactory *&meshModuleFactories()
{
    static MeshModuleFactory *head = nullptr;
    return head;
}

void createRegisteredModules()
{
    for (MeshModuleFactory *node = meshModuleFactories(); node; node = node->next) {
        MeshModule *module = node->create();
        if (module) {
            MeshModule::runSetup(module);
        }
    }
}
