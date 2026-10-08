#pragma once

#include "MeshModule.h"

struct MeshModuleFactory {
    MeshModule *(*create)();
    MeshModuleFactory *next;
};

MeshModuleFactory *&meshModuleFactories();

void createRegisteredModules();

#define MESHTASTIC_REGISTER_MODULE(Type)                                                                                         \
    static MeshModule *meshtasticCreate##Type()                                                                                  \
    {                                                                                                                            \
        return new Type();                                                                                                       \
    }                                                                                                                            \
    struct MeshtasticModuleRegistrar_##Type {                                                                                    \
        MeshModuleFactory factory;                                                                                               \
        MeshtasticModuleRegistrar_##Type() : factory{meshtasticCreate##Type, nullptr}                                            \
        {                                                                                                                        \
            factory.next = meshModuleFactories();                                                                                \
            meshModuleFactories() = &factory;                                                                                    \
        }                                                                                                                        \
    };                                                                                                                           \
    MeshtasticModuleRegistrar_##Type meshtasticModuleRegistrar_##Type
