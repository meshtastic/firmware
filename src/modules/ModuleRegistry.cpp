// src/modules/ModuleRegistry.cpp

#include "ModuleRegistry.h"
#include "DebugConfiguration.h"

// Initialize the global vector in static storage. 
// This vector will be populated by the constructor-attributed functions.
std::vector<ModuleInitFunc> g_module_init_functions;

/**
 * @brief Called by a module's constructor-attributed function to add 
 * its setup routine to the central list.
 */
void register_module_initializer(ModuleInitFunc func) {
    // This push_back happens during C++ static initialization, before main().
    g_module_init_functions.push_back(func); 
}

/**
 * @brief Initializes all modules that have self-registered.
 * Called once by the core Meshtastic firmware setup routine.
 */
void init_all_plugin_modules() {
    LOG_INFO("Initializing self-registering plugin modules via vector...\n");

    // Loop through the collected pointers and execute the setup functions
    for (ModuleInitFunc func : g_module_init_functions) {
        func(); // Executes the module's initialization code (e.g., new MyModule())
    }
}