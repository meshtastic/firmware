#include "InitialSplashScreen.h"
#include <Arduino.h>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

InitialSplashScreen::InitialSplashScreen() {
    LOG_INFO("🎬 InitialSplashScreen: Constructor");
    animationComplete = false;
    titleShown = false;
    lastDrawnProgress = -1;
}

InitialSplashScreen::~InitialSplashScreen() {
    LOG_INFO("🎬 InitialSplashScreen: Destructor");
}

void InitialSplashScreen::playAnimation(lgfx::LGFX_Device* tft) {
    if (!tft) return;
    
    LOG_INFO("🎬 InitialSplashScreen: Starting loading animation");
    
    // Clear to black
    tft->fillScreen(COLOR_BLACK);
    
    // Show title immediately
    drawHackerCentralTitle(tft);
    titleShown = true;
    
    // Setup initial progress bar
    setupProgressBar(tft);
    
    LOG_INFO("🎬 InitialSplashScreen: Title and progress bar initialized");
}

void InitialSplashScreen::drawHackerCentralTitle(lgfx::LGFX_Device* tft) {
    LOG_INFO("🎬 Drawing HACKER CENTRAL title");
    
    int centerX = 160;
    int centerY = 60;
    
    // Draw "HACKER CENTRAL" in green
    const char* title = "HACKER CENTRAL";
    
    tft->setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft->setTextSize(2); // Larger text
    
    // Center the title
    int titleX = centerX - (strlen(title) * 12) / 2;
    
    tft->setCursor(titleX, centerY);
    tft->print(title);
    
    // Add "LOADING..." text below
    const char* loadingText = "LOADING...";
    tft->setTextSize(1);
    int loadingX = centerX - (strlen(loadingText) * 6) / 2;
    tft->setCursor(loadingX, centerY + 30);
    tft->setTextColor(COLOR_DIM_GREEN, COLOR_BLACK);
    tft->print(loadingText);
    
    LOG_INFO("🎬 Title and loading text displayed");
}



void InitialSplashScreen::setupProgressBar(lgfx::LGFX_Device* tft) {
    LOG_INFO("🎬 Setting up progress bar");
    
    // Reset progress tracking
    lastDrawnProgress = -1;
    
    // Draw progress bar frame in dim green
    tft->drawRect(PROGRESS_BAR_X - 1, PROGRESS_BAR_Y - 1, 
                  PROGRESS_BAR_WIDTH + 2, PROGRESS_BAR_HEIGHT + 2, COLOR_DIM_GREEN);
    
    // Clear the inside to black
    tft->fillRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, 
                  PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_BLACK);
}

bool InitialSplashScreen::updateLoadingProgress(lgfx::LGFX_Device* tft, int progress) {
    if (!tft || animationComplete) {
        return animationComplete;
    }
    
    // Ensure progress is within bounds
    progress = max(0, min(100, progress));
    
    // Draw the smooth progress bar
    drawSmoothProgressBar(tft, progress);
    
    // Check if animation is complete
    if (progress >= 100) {
        LOG_INFO("🎬 Loading animation complete!");
        
        // Show completion effect
        drawCompletionEffect(tft);
        animationComplete = true;
        return true;
    }
    
    return false;
}

void InitialSplashScreen::drawSmoothProgressBar(lgfx::LGFX_Device* tft, int progress) {
    // Only redraw if progress has actually changed
    if (progress == lastDrawnProgress) {
        return;
    }
    
    int currentFillWidth = (PROGRESS_BAR_WIDTH * progress) / 100;
    int lastFillWidth = lastDrawnProgress < 0 ? 0 : (PROGRESS_BAR_WIDTH * lastDrawnProgress) / 100;
    
    // Only draw the new progress area (incremental drawing)
    if (currentFillWidth > lastFillWidth) {
        // Draw new green progress section
        tft->fillRect(PROGRESS_BAR_X + lastFillWidth, PROGRESS_BAR_Y, 
                      currentFillWidth - lastFillWidth, PROGRESS_BAR_HEIGHT, COLOR_GREEN);
    }
    
    // Remove old leading edge (if any) by overwriting with green
    if (lastDrawnProgress >= 0 && lastFillWidth < PROGRESS_BAR_WIDTH && lastFillWidth > 0) {
        tft->drawFastVLine(PROGRESS_BAR_X + lastFillWidth, PROGRESS_BAR_Y, 
                          PROGRESS_BAR_HEIGHT, COLOR_GREEN);
    }
    
    // Add bright leading edge at new position
    if (currentFillWidth < PROGRESS_BAR_WIDTH && currentFillWidth > 0) {
        tft->drawFastVLine(PROGRESS_BAR_X + currentFillWidth, PROGRESS_BAR_Y, 
                          PROGRESS_BAR_HEIGHT, COLOR_BRIGHT_GREEN);
    }
    
    lastDrawnProgress = progress;
}

void InitialSplashScreen::drawCompletionEffect(lgfx::LGFX_Device* tft) {
    LOG_INFO("🎬 Drawing completion effect");
    
    // Flash the entire progress bar bright green
    tft->fillRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, 
                  PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_BRIGHT_GREEN);
    delay(150);
    
    // Return to normal green
    tft->fillRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, 
                  PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_GREEN);
    delay(100);
    
    // Flash again
    tft->fillRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, 
                  PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_BRIGHT_GREEN);
    delay(150);
    
    // Final green state
    tft->fillRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, 
                  PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT, COLOR_GREEN);
    
    // Show "READY" message
    int centerX = 160;
    const char* readyText = "READY!";
    int textX = centerX - (strlen(readyText) * 6) / 2;
    
    tft->setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft->setCursor(textX, PROGRESS_BAR_Y + 25);
    tft->print(readyText);
    
    delay(500); // Show "READY" for half a second
}



void InitialSplashScreen::animatedProgressBar(lgfx::LGFX_Device* tft, int x, int y, int width, int height, int progress, uint16_t color) {
    int fillWidth = (width * progress) / 100;
    
    // Clear the bar area first
    tft->fillRect(x, y, width, height, COLOR_BLACK);
    
    // Fill progress
    if (fillWidth > 0) {
        tft->fillRect(x, y, fillWidth, height, color);
    }
    
    // Add a bright pixel at the progress edge for movement effect
    if (fillWidth > 0 && fillWidth < width) {
        tft->drawFastVLine(x + fillWidth, y, height, COLOR_YELLOW);
    }
}