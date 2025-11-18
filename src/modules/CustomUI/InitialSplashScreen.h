#pragma once

#include <LovyanGFX.hpp>

/**
 * Initial Splash Screen Animation for CustomUI Module
 * Shows animated intro sequence when display first initializes
 */
class InitialSplashScreen {
public:
    InitialSplashScreen();
    ~InitialSplashScreen();
    
    /**
     * Play the complete splash screen animation
     * @param tft LovyanGFX display instance
     */
    void playAnimation(lgfx::LGFX_Device* tft);
    
private:
    /**
     * Draw HACKER CENTRAL title
     */
    void drawHackerCentralTitle(lgfx::LGFX_Device* tft);
    
    /**
     * Start continuous loading animation that runs until destroyed
     */
    void startContinuousLoading(lgfx::LGFX_Device* tft);
    
    /**
     * Draw loading progress bar animation
     */
    void drawLoadingAnimation(lgfx::LGFX_Device* tft);
    
    /**
     * Draw final "READY" message with fade effect
     */
    void drawReadyMessage(lgfx::LGFX_Device* tft);
    
    /**
     * Typewriter effect for text
     */
    void typewriterText(lgfx::LGFX_Device* tft, const char* text, int x, int y, uint16_t color, int delayMs = 50);
    
    /**
     * Animated progress bar
     */
    void animatedProgressBar(lgfx::LGFX_Device* tft, int x, int y, int width, int height, int progress, uint16_t color);
    
    // Colors for power-efficient display
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_GREEN = 0x07E0;
    static const uint16_t COLOR_YELLOW = 0xFFE0;
    static const uint16_t COLOR_DIM_GREEN = 0x4208;
    static const uint16_t COLOR_DARK_RED = 0x7800;
};