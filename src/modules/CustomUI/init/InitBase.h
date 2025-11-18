#pragma once

#include <Arduino.h>

/**
 * Base interface for all CustomUI initializers
 * Provides a common pattern for modular component initialization
 * Initializers ONLY handle hardware initialization - no logic or updates
 */
class InitBase {
public:
    virtual ~InitBase() = default;
    
    /**
     * Initialize the component
     * @return true if initialization successful, false otherwise
     */
    virtual bool init() = 0;
    
    /**
     * Cleanup resources when shutting down
     */
    virtual void cleanup() = 0;
    
    /**
     * Get component name for logging
     * @return component name string
     */
    virtual const char* getName() const = 0;
    
    /**
     * Check if component is initialized and ready
     * @return true if ready, false otherwise
     */
    virtual bool isReady() const = 0;
};