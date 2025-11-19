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
    
    /**
     * Update the loading animation progress
     * @param tft LovyanGFX display instance
     * @param progress Progress percentage (0-100)
     * @return true if animation is complete
     */
    bool updateLoadingProgress(lgfx::LGFX_Device* tft, int progress);
    
    /**
     * Check if loading animation is complete
     * @return true if animation finished
     */
    bool isAnimationComplete() const { return animationComplete; }
    
private:
    /**
     * Draw HACKER CENTRAL title
     */
    void drawHackerCentralTitle(lgfx::LGFX_Device* tft);
    
    /**
     * Setup initial progress bar frame
     */
    void setupProgressBar(lgfx::LGFX_Device* tft);
    
    /**
     * Draw completion effect when loading finishes
     */
    void drawCompletionEffect(lgfx::LGFX_Device* tft);
    
    /**
     * Animated progress bar
     */
    void animatedProgressBar(lgfx::LGFX_Device* tft, int x, int y, int width, int height, int progress, uint16_t color);
    
    /**
     * Draw smooth loading progress bar
     */
    void drawSmoothProgressBar(lgfx::LGFX_Device* tft, int progress);
    
    // Animation state
    bool animationComplete = false;
    bool titleShown = false;
    int lastDrawnProgress = -1;  // Track last drawn progress to avoid flicker
    
    // Progress bar dimensions
    static const int PROGRESS_BAR_X = 60;
    static const int PROGRESS_BAR_Y = 140;
    static const int PROGRESS_BAR_WIDTH = 200;
    static const int PROGRESS_BAR_HEIGHT = 12;
    
    // Colors for power-efficient display
    static const uint16_t COLOR_BLACK = 0x0000;
    static const uint16_t COLOR_GREEN = 0x07E0;
    static const uint16_t COLOR_YELLOW = 0xFFE0;
    static const uint16_t COLOR_DIM_GREEN = 0x4208;
    static const uint16_t COLOR_DARK_RED = 0x7800;
    static const uint16_t COLOR_BRIGHT_GREEN = 0x07FF;
};