#pragma once

#include "BaseScreen.h"
#include <LovyanGFX.hpp>
#include <deque>
#include <Arduino.h>

/**
 * Nokia-style Snake Game Screen
 * Features:
 * - Classic snake gameplay with food collection
 * - Dirty rectangle optimization for smooth movement
 * - Score tracking and speed progression
 * - Game over with restart option
 * - Pause/resume functionality
 */

// Game state enumeration
enum class GameState {
    PLAYING,
    PAUSED, 
    GAME_OVER
};

// Direction enumeration
enum class Direction {
    UP,
    DOWN,
    LEFT,
    RIGHT
};

// Point structure for positions
struct Point {
    int x, y;
    Point(int x = 0, int y = 0) : x(x), y(y) {}
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

class SnakeGameScreen : public BaseScreen {
public:
    SnakeGameScreen();
    virtual ~SnakeGameScreen();
    
    // Screen lifecycle
    virtual void onEnter() override;
    virtual void onExit() override;
    virtual void onDraw(lgfx::LGFX_Device& tft) override;
    virtual bool needsUpdate() const override;  // Override for continuous game updates
    
    // Input handling
    virtual bool handleKeyPress(char key) override;

private:
    // === Game State ===
    GameState gameState;
    Direction currentDirection;
    Direction nextDirection;  // Buffer next direction to prevent rapid direction changes
    std::deque<Point> snake;  // Snake segments (head at front)
    Point food;
    int score;
    int gameSpeed;           // Milliseconds between moves
    unsigned long lastMoveTime;
    unsigned long gameStartTime;
    
    // === Game Field Configuration ===
    static const int CELL_SIZE = 10;       // 10x10 pixels per cell
    static const int FIELD_MARGIN = 5;     // 5px margin from content edges
    static const int FIELD_OFFSET_X = FIELD_MARGIN;  // Start with margin from left
    static const int FIELD_OFFSET_Y = FIELD_MARGIN;  // Start with margin from top
    static const int USABLE_WIDTH = 320 - (2 * FIELD_MARGIN);   // 310px usable
    static const int USABLE_HEIGHT = 180 - (2 * FIELD_MARGIN);  // 170px usable (content is 180px)
    static const int GRID_WIDTH = USABLE_WIDTH / CELL_SIZE;      // 31 cells wide
    static const int GRID_HEIGHT = USABLE_HEIGHT / CELL_SIZE;    // 17 cells high
    static const int BORDER_SIZE = 1;       // 1px border around cells
    
    // === Visual Theme ===
    static const uint16_t SNAKE_COLOR = 0x07E0;      // Bright green
    static const uint16_t SNAKE_BORDER_COLOR = 0x03E0; // Darker green
    static const uint16_t FOOD_COLOR = 0xF800;        // Red
    static const uint16_t FOOD_BORDER_COLOR = 0x7800; // Dark red
    static const uint16_t BACKGROUND_COLOR = 0x0000;  // Black
    static const uint16_t WALL_COLOR = 0x4208;        // Gray
    static const uint16_t TEXT_COLOR = 0xFFFF;        // White
    static const uint16_t SCORE_COLOR = 0xFFE0;       // Yellow
    
    // === Game Speed Configuration ===
    static const int INITIAL_SPEED = 400;      // 400ms between moves initially (slower start)
    static const int MINIMUM_SPEED = 100;      // Fastest possible speed
    static const int SPEED_INCREMENT = 15;     // Speed increase per snake segment
    
    // === Core Game Logic ===
    void initializeGame();
    void resetGame();
    void updateGame();
    void moveSnake();
    void generateFood();
    bool isValidPosition(const Point& pos) const;
    bool checkCollision() const;
    void eatFood();
    void calculateSpeed();
    
    // === Input Processing ===
    void handleGameInput(char key);
    void handlePauseInput(char key);
    void handleGameOverInput(char key);
    bool isValidDirectionChange(Direction newDir) const;
    
    // === Rendering System ===
    void drawCompleteGame(lgfx::LGFX_Device& tft);
    void drawGameField(lgfx::LGFX_Device& tft);
    void drawSnake(lgfx::LGFX_Device& tft);
    void drawFood(lgfx::LGFX_Device& tft);
    void drawScore(lgfx::LGFX_Device& tft);
    void drawGameStatus(lgfx::LGFX_Device& tft);
    void drawPauseOverlay(lgfx::LGFX_Device& tft);
    void drawGameOverOverlay(lgfx::LGFX_Device& tft);
    void drawInstructions(lgfx::LGFX_Device& tft);
    
    // === Dirty Rectangle Optimization ===
    void clearSnakeTail(lgfx::LGFX_Device& tft, const Point& tailPos);
    void drawSnakeHead(lgfx::LGFX_Device& tft, const Point& headPos);
    void clearOldFood(lgfx::LGFX_Device& tft, const Point& oldFoodPos);
    void drawCell(lgfx::LGFX_Device& tft, const Point& pos, uint16_t fillColor, uint16_t borderColor);
    void clearCell(lgfx::LGFX_Device& tft, const Point& pos);
    
    // === Utility Functions ===
    Point screenToGrid(int screenX, int screenY) const;
    Point gridToScreen(int gridX, int gridY) const;
    bool isInsideGameField(const Point& pos) const;
    String formatTime(unsigned long milliseconds) const;
    
    // === State Tracking for Optimization ===
    Point lastSnakeTail;     // Track last tail position for dirty rectangle
    Point lastFoodPos;       // Track last food position
    Point prevTailRendered;  // Track what was actually rendered as tail
    Point prevFoodRendered;  // Track what was actually rendered as food
    int lastScore;           // Track score changes
    bool fullRedrawNeeded;   // Flag for complete redraw
    bool firstDraw;          // Flag for initial draw
};