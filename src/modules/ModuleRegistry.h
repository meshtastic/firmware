// src/modules/ModuleRegistry.h

#ifndef MODULE_REGISTRY_H
#define MODULE_REGISTRY_H

#include <vector> // Required for std::vector

// Define the function pointer type for module initialization
typedef void (*ModuleInitFunc)(void);

// The central list to hold pointers to the initialization functions.
// This is defined externally in the CPP file.
extern std::vector<ModuleInitFunc> g_module_init_functions;

// Function that all modules will call to register themselves
void register_module_initializer(ModuleInitFunc func);

// Function called by the core firmware setup to initialize all modules
void init_dynamic_modules();

/**
 * @brief Macro used by module authors to self-register a new Meshtastic Module.
 * This creates a lambda that instantiates the module and automatically applies the constructor attribute.
 * @param ModuleClassName The name of the module's C++ class (e.g., MySensorModule).
 */
#define MESHTASTIC_REGISTER_MODULE(ModuleClassName)                                                                              \
    static void __attribute__((constructor)) register_##ModuleClassName()                                                        \
    {                                                                                                                            \
        register_module_initializer([]() { new ModuleClassName(); });                                                            \
    }

#endif // MODULE_REGISTRY_H