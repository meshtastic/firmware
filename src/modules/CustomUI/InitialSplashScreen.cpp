#include "InitialSplashScreen.h"
#include <Arduino.h>

// Logging macro
#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

InitialSplashScreen::InitialSplashScreen() {
    LOG_INFO("🎬 InitialSplashScreen: Constructor");
}

InitialSplashScreen::~InitialSplashScreen() {
    LOG_INFO("🎬 InitialSplashScreen: Destructor");
}

void InitialSplashScreen::playAnimation(lgfx::LGFX_Device* tft) {
    if (!tft) return;
    
    LOG_INFO("🎬 InitialSplashScreen: Starting animation sequence");
    
    // Clear to black
    tft->fillScreen(COLOR_BLACK);
    tft->setTextSize(1);
    
    // Phase 1: HACKER CENTRAL Title (0.3s)
    drawHackerCentralTitle(tft);
    
    // Phase 2: Start continuous loading animation
    startContinuousLoading(tft);
    
    LOG_INFO("🎬 InitialSplashScreen: Animation sequence started (continuous)");
}

void InitialSplashScreen::drawHackerCentralTitle(lgfx::LGFX_Device* tft) {
    LOG_INFO("🎬 Drawing HACKER CENTRAL title");
    
    int centerX = 160;
    int centerY = 80;
    
    // Draw "HACKER CENTRAL" in bold-style (multiple prints for bold effect)
    const char* title = "HACKER CENTRAL";
    int titleX = centerX - (strlen(title) * 6) / 2;
    
    tft->setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft->setTextSize(2); // Larger text for bold effect
    
    // Center the title with larger text
    titleX = centerX - (strlen(title) * 12) / 2;
    
    tft->setCursor(titleX, centerY - 8);
    tft->print(title);
    
    // Reset text size
    tft->setTextSize(1);
    
    delay(300);
}

void InitialSplashScreen::drawLoadingAnimation(lgfx::LGFX_Device* tft) {
    LOG_INFO("🎬 Drawing loading animation");
    
    int loadingY = 180;
    int centerX = 160;
    
    // "INITIALIZING" text
    const char* loadingText = "INITIALIZING";
    int textX = centerX - (strlen(loadingText) * 6) / 2;
    typewriterText(tft, loadingText, textX, loadingY, COLOR_YELLOW, 20);
    
    // Progress bar animation
    int barY = loadingY + 25;
    int barWidth = 200;
    int barHeight = 8;
    int barX = centerX - barWidth / 2;
    
    // Draw empty progress bar frame
    tft->drawRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2, COLOR_DIM_GREEN);
    tft->fillRect(barX, barY, barWidth, barHeight, COLOR_BLACK);
    
    // Animate progress bar
    for (int progress = 0; progress <= 100; progress += 5) {
        animatedProgressBar(tft, barX, barY, barWidth, barHeight, progress, COLOR_GREEN);
        delay(10);
    }
    
    // Quick completion flash
    tft->fillRect(barX, barY, barWidth, barHeight, COLOR_YELLOW);
    delay(200);
}

void InitialSplashScreen::startContinuousLoading(lgfx::LGFX_Device* tft) {
    LOG_INFO("🎬 Setting up loading screen");
    
    int centerX = 160;
    int loadingY = 120;
    
    // "INITIALIZING" text in green
    const char* loadingText = "INITIALIZING";
    int textX = centerX - (strlen(loadingText) * 6) / 2;
    tft->setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft->setCursor(textX, loadingY);
    tft->print(loadingText);
    
    // Progress bar setup
    int barY = loadingY + 25;
    int barWidth = 200;
    int barHeight = 8;
    int barX = centerX - barWidth / 2;
    
    // Draw progress bar frame in green
    tft->drawRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2, COLOR_GREEN);
    tft->fillRect(barX, barY, barWidth, barHeight, COLOR_BLACK);
    
    LOG_INFO("🎬 Static loading screen ready");
}

void InitialSplashScreen::drawReadyMessage(lgfx::LGFX_Device* tft) {
    LOG_INFO("🎬 Drawing ready message");
    
    int readyY = 210;
    int centerX = 160;
    
    // Clear loading area
    tft->fillRect(0, 180, 320, 60, COLOR_BLACK);
    
    // "SYSTEM READY" message
    const char* readyText = "SYSTEM READY";
    int textX = centerX - (strlen(readyText) * 6) / 2;
    
    // Fade in effect
    uint16_t colors[] = {0x2104, 0x4208, 0x630C, COLOR_GREEN}; // Gradual green fade
    
    for (int fade = 0; fade < 4; fade++) {
        tft->setTextColor(colors[fade], COLOR_BLACK);
        tft->setCursor(textX, readyY);
        tft->print(readyText);
        delay(150);
    }
    
    // Pulse effect
    for (int pulse = 0; pulse < 3; pulse++) {
        tft->setTextColor(COLOR_YELLOW, COLOR_BLACK);
        tft->setCursor(textX, readyY);
        tft->print(readyText);
        delay(150);
        
        tft->setTextColor(COLOR_GREEN, COLOR_BLACK);
        tft->setCursor(textX, readyY);
        tft->print(readyText);
        delay(150);
    }
    
    delay(300);
}

void InitialSplashScreen::typewriterText(lgfx::LGFX_Device* tft, const char* text, int x, int y, uint16_t color, int delayMs) {
    tft->setTextColor(color, COLOR_BLACK);
    tft->setCursor(x, y);
    
    int len = strlen(text);
    for (int i = 0; i < len; i++) {
        tft->print(text[i]);
        if (delayMs > 0) {
            delay(delayMs);
        }
    }
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