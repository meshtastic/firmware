#pragma once

#include "InitBase.h"
#include <LovyanGFX.hpp>

// Forward declaration
class LGFX;

/**
 * Display initializer using LovyanGFX for optimal ESP32-S3 + ST7789 performance
 * Only handles initialization - CustomUIModule handles all drawing logic
 * Features:
 * - 80MHz SPI with automatic DMA
 * - PSRAM support
 * - High-performance rendering (40-60 FPS)
 * - Memory efficient (~150-220KB free)
 */
class InitDisplay : public InitBase {
public:
    InitDisplay();
    virtual ~InitDisplay();
    
    // InitBase interface
    virtual bool init() override;
    void update() { /* No update needed - initialization only */ }
    virtual void cleanup() override;
    virtual const char* getName() const override { return "Display"; }
    virtual bool isReady() const override { return (tft != nullptr); }
    
    // Display access for CustomUIModule
    lgfx::LGFX_Device* getDisplay() { return tft; }

private:
    lgfx::LGFX_Device* tft;
    bool initialized;
};