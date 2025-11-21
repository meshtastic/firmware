#include "SnakeGameScreen.h"
#include "configuration.h"

SnakeGameScreen::SnakeGameScreen() 
    : BaseScreen("Snake Game"),
      gameState(GameState::PLAYING),
      currentDirection(Direction::RIGHT),
      nextDirection(Direction::RIGHT),
      score(0),
      gameSpeed(INITIAL_SPEED),
      lastMoveTime(0),
      gameStartTime(0),
      lastSnakeTail(-1, -1),
      lastFoodPos(-1, -1),
      prevTailRendered(-1, -1),
      prevFoodRendered(-1, -1),
      lastScore(-1),
      fullRedrawNeeded(true),
      firstDraw(true) {
    
    // Set navigation hints for snake game
    std::vector<NavHint> hints;
    hints.push_back(NavHint('A', "Home"));
    hints.push_back(NavHint('*', "Pause"));
    hints.push_back(NavHint('#', "Reset"));
    setNavigationHints(hints);
    
    LOG_INFO("🐍 SnakeGameScreen created");
}

SnakeGameScreen::~SnakeGameScreen() {
    LOG_INFO("🐍 SnakeGameScreen destroyed");
}

void SnakeGameScreen::onEnter() {
    LOG_INFO("🐍 Entering Snake Game screen");
    
    // Initialize or reset game
    if (firstDraw) {
        initializeGame();
        firstDraw = false;
    } else {
        resetGame();
    }
    
    gameStartTime = millis();
    fullRedrawNeeded = true;
    forceRedraw();
}

void SnakeGameScreen::onExit() {
    LOG_INFO("🐍 Exiting Snake Game screen");
}

bool SnakeGameScreen::needsUpdate() const {
    bool shouldUpdate = gameState == GameState::PLAYING || BaseScreen::needsUpdate();
    
    // Debug: Track needsUpdate calls (reduce frequency)
    static unsigned long lastUpdateLog = 0;
    unsigned long now = millis();
    if (now - lastUpdateLog > 10000) { // Log every 10 seconds
        LOG_DEBUG("🐍 needsUpdate: %s (gameState: %d)", 
                shouldUpdate ? "YES" : "NO", (int)gameState);
        lastUpdateLog = now;
    }
    
    return shouldUpdate;
}

void SnakeGameScreen::onDraw(lgfx::LGFX_Device& tft) {
    // Debug: Track onDraw calls (reduce frequency)
    static unsigned long lastDrawLog = 0;
    unsigned long now = millis();
    if (now - lastDrawLog > 5000) { // Log every 5 seconds
        LOG_DEBUG("🐍 onDraw called, gameState: %d, fullRedraw: %s", 
                 (int)gameState, fullRedrawNeeded ? "YES" : "NO");
        lastDrawLog = now;
    }
    
    // Update game logic if playing
    if (gameState == GameState::PLAYING) {
        updateGame();
    }
    
    // Handle drawing efficiently
    if (fullRedrawNeeded) {
        // Full redraw (game start, game over, etc.)
        drawCompleteGame(tft);
        fullRedrawNeeded = false;
    } else if (gameState == GameState::PLAYING && snake.size() > 0) {
        // Dirty rectangle updates during normal gameplay
        
        // Only clear old tail if snake moved (not when growing)
        static Point prevTail(-1, -1);
        if (prevTail.x >= 0 && prevTail.y >= 0 && 
            !(prevTail == lastSnakeTail)) {
            clearCell(tft, prevTail);
        }
        prevTail = lastSnakeTail;
        
        // Draw new snake head
        drawSnakeHead(tft, snake.front());
        
        // If food position changed, clear old and draw new
        static Point prevFood(-1, -1);
        if (!(food == prevFood)) {
            if (prevFood.x >= 0 && prevFood.y >= 0) {
                clearCell(tft, prevFood);
            }
            drawFood(tft);
            prevFood = food;
        }
    }
    
    // Draw overlays for paused/game over states (these need full overlay)
    if (gameState == GameState::PAUSED) {
        drawPauseOverlay(tft);
    } else if (gameState == GameState::GAME_OVER) {
        drawGameOverOverlay(tft);
    }
}

bool SnakeGameScreen::handleKeyPress(char key) {
    switch (gameState) {
        case GameState::PLAYING:
            handleGameInput(key);
            break;
            
        case GameState::PAUSED:
            handlePauseInput(key);
            break;
            
        case GameState::GAME_OVER:
            handleGameOverInput(key);
            break;
    }
    
    // Handle global navigation
    if (key == 'A' || key == 'a') {
        return false; // Let global navigation handle return to home
    }
    
    return true; // Key handled by game
}

// ========== Game Logic Implementation ==========

void SnakeGameScreen::initializeGame() {
    LOG_INFO("🐍 Initializing Snake Game");
    
    // Initialize snake in center of field, length 3
    snake.clear();
    int centerX = GRID_WIDTH / 2;
    int centerY = GRID_HEIGHT / 2;
    
    snake.push_back(Point(centerX, centerY));     // Head
    snake.push_back(Point(centerX - 1, centerY)); // Body
    snake.push_back(Point(centerX - 2, centerY)); // Tail
    
    // Set initial direction
    currentDirection = Direction::RIGHT;
    nextDirection = Direction::RIGHT;
    
    // Initialize game state
    score = 0;
    gameSpeed = INITIAL_SPEED;
    gameState = GameState::PLAYING;
    lastMoveTime = millis();
    
    // Generate first food
    generateFood();
    
    // Track positions for dirty rectangle optimization
    lastSnakeTail = snake.back();
    lastFoodPos = food;
    
    LOG_INFO("🐍 Game initialized - Snake length: %d, Food at (%d,%d)", 
             snake.size(), food.x, food.y);
}

void SnakeGameScreen::resetGame() {
    LOG_INFO("🐍 Resetting Snake Game");
    initializeGame();
    fullRedrawNeeded = true;
}

void SnakeGameScreen::updateGame() {
    if (gameState != GameState::PLAYING) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Check if it's time to move
    if (currentTime - lastMoveTime >= gameSpeed) {
        // Apply buffered direction change
        currentDirection = nextDirection;
        
        moveSnake();
        lastMoveTime = currentTime;
    }
}

void SnakeGameScreen::moveSnake() {
    if (snake.empty()) {
        return;
    }
    
    // Calculate new head position
    Point newHead = snake.front();
    
    switch (currentDirection) {
        case Direction::UP:
            newHead.y--;
            break;
        case Direction::DOWN:
            newHead.y++;
            break;
        case Direction::LEFT:
            newHead.x--;
            break;
        case Direction::RIGHT:
            newHead.x++;
            break;
    }
    
    // Check for collisions
    if (!isValidPosition(newHead)) {
        gameState = GameState::GAME_OVER;
        LOG_INFO("🐍 Game Over! Wall collision at (%d,%d). Final Score: %d", newHead.x, newHead.y, score);
        fullRedrawNeeded = true;  // Only full redraw on game over
        return;
    }
    
    // Check self-collision with new head position
    for (const Point& segment : snake) {
        if (newHead == segment) {
            gameState = GameState::GAME_OVER;
            LOG_INFO("🐍 Game Over! Self-collision at (%d,%d). Final Score: %d", newHead.x, newHead.y, score);
            fullRedrawNeeded = true;  // Only full redraw on game over
            return;
        }
    }
    
    // Store tail position for dirty rectangle optimization BEFORE removing it
    lastSnakeTail = snake.back();
    
    // Check if food is eaten
    bool ateFood = (newHead == food);
    
    // Add new head
    snake.push_front(newHead);
    
    if (ateFood) {
        eatFood();
        // When food is eaten, snake grows (no tail removal)
        // We only need to draw the new head and new food
    } else {
        // Remove tail (normal movement)
        snake.pop_back();
        // We need to clear old tail and draw new head
    }
    
    // Set flag for dirty rectangle updates (not full redraw)
    fullRedrawNeeded = false;
    
    LOG_DEBUG("🐍 Snake moved to (%d,%d), direction: %d, ateFood: %s", 
             newHead.x, newHead.y, (int)currentDirection, ateFood ? "YES" : "NO");
}

void SnakeGameScreen::generateFood() {
    int attempts = 0;
    const int maxAttempts = 100;
    
    do {
        food.x = random(0, GRID_WIDTH);
        food.y = random(0, GRID_HEIGHT);
        attempts++;
    } while (!isValidPosition(food) && attempts < maxAttempts);
    
    // Ensure food doesn't spawn on snake
    for (const Point& segment : snake) {
        if (food == segment) {
            if (attempts < maxAttempts) {
                generateFood(); // Recursive call to try again
            }
            return;
        }
    }
    
    LOG_DEBUG("🐍 Food generated at (%d, %d) after %d attempts", food.x, food.y, attempts);
}

bool SnakeGameScreen::isValidPosition(const Point& pos) const {
    return pos.x >= 0 && pos.x < GRID_WIDTH && 
           pos.y >= 0 && pos.y < GRID_HEIGHT;
}

bool SnakeGameScreen::checkCollision() const {
    if (snake.empty()) {
        return false;
    }
    
    const Point& head = snake.front();
    
    // Check self-collision (skip head when checking)
    for (size_t i = 1; i < snake.size(); i++) {
        if (head == snake[i]) {
            LOG_INFO("🐍 Self-collision detected at (%d,%d)", head.x, head.y);
            return true;
        }
    }
    
    return false;
}

void SnakeGameScreen::eatFood() {
    score += 10;
    
    // Store old food position before generating new one
    lastFoodPos = food;
    
    generateFood();
    calculateSpeed();
    
    LOG_INFO("🐍 Food eaten! Score: %d, Snake length: %d, New food at (%d,%d)", 
             score, snake.size(), food.x, food.y);
}

void SnakeGameScreen::calculateSpeed() {
    // Increase speed based on snake length (more length = faster game)
    int speedReduction = (snake.size() - 3) * SPEED_INCREMENT; // Start from initial length of 3
    int newSpeed = INITIAL_SPEED - speedReduction;
    gameSpeed = max(newSpeed, MINIMUM_SPEED);
    
    LOG_DEBUG("🐍 Speed updated: %d ms (length: %d)", gameSpeed, snake.size());
}

// ========== Input Handling ==========

void SnakeGameScreen::handleGameInput(char key) {
    Direction newDirection = currentDirection;
    
    switch (key) {
        case '2': // Up
            newDirection = Direction::UP;
            LOG_INFO("🐍 Direction change requested: UP");
            break;
        case '8': // Down  
            newDirection = Direction::DOWN;
            LOG_INFO("🐍 Direction change requested: DOWN");
            break;
        case '4': // Left
            newDirection = Direction::LEFT;
            LOG_INFO("🐍 Direction change requested: LEFT");
            break;
        case '6': // Right
            newDirection = Direction::RIGHT;
            LOG_INFO("🐍 Direction change requested: RIGHT");
            break;
        case '*': // Pause
            gameState = GameState::PAUSED;
            LOG_INFO("🐍 Game Paused");
            return;
        case '#': // Reset
            resetGame();
            return;
        default:
            return; // Key not handled
    }
    
    // Validate direction change (can't reverse into self)
    if (isValidDirectionChange(newDirection)) {
        nextDirection = newDirection;
        LOG_INFO("🐍 Direction accepted: %d", (int)newDirection);
    } else {
        LOG_INFO("🐍 Direction change rejected (reverse direction)");
    }
}

void SnakeGameScreen::handlePauseInput(char key) {
    switch (key) {
        case '*': // Resume
            gameState = GameState::PLAYING;
            lastMoveTime = millis(); // Reset timing
            LOG_INFO("🐍 Game Resumed");
            break;
        case '#': // Reset
            resetGame();
            break;
        default:
            // Any other key resumes
            gameState = GameState::PLAYING;
            lastMoveTime = millis();
            break;
    }
}

void SnakeGameScreen::handleGameOverInput(char key) {
    switch (key) {
        case '#': // Restart
        case '*': // Restart  
            resetGame();
            break;
        default:
            // Any key restarts
            resetGame();
            break;
    }
}

bool SnakeGameScreen::isValidDirectionChange(Direction newDir) const {
    // Can't reverse direction (go directly opposite)
    switch (currentDirection) {
        case Direction::UP:
            return newDir != Direction::DOWN;
        case Direction::DOWN:
            return newDir != Direction::UP;
        case Direction::LEFT:
            return newDir != Direction::RIGHT;
        case Direction::RIGHT:
            return newDir != Direction::LEFT;
    }
    return true;
}

// ========== Rendering System ==========

void SnakeGameScreen::drawCompleteGame(lgfx::LGFX_Device& tft) {
    // Clear content area
    tft.fillRect(0, getContentY(), getContentWidth(), getContentHeight(), BACKGROUND_COLOR);
    
    // Draw game field border
    drawGameField(tft);
    
    // Draw snake
    drawSnake(tft);
    
    // Draw food
    drawFood(tft);
    
    LOG_DEBUG("🐍 Complete game drawn");
}

void SnakeGameScreen::drawGameField(lgfx::LGFX_Device& tft) {
    // Draw border around the actual game field with margins
    int fieldX = FIELD_OFFSET_X;
    int fieldY = getContentY() + FIELD_OFFSET_Y;
    int fieldWidth = GRID_WIDTH * CELL_SIZE;
    int fieldHeight = GRID_HEIGHT * CELL_SIZE;
    
    // Draw outer border
    tft.drawRect(fieldX - 1, fieldY - 1, fieldWidth + 2, fieldHeight + 2, WALL_COLOR);
    
    // Optional: Draw a subtle grid for debugging (can be removed later)
    #ifdef DEBUG_GRID
    tft.setTextColor(0x2104, BACKGROUND_COLOR); // Dark gray
    for (int x = 0; x <= GRID_WIDTH; x++) {
        int lineX = fieldX + x * CELL_SIZE;
        tft.drawFastVLine(lineX, fieldY, fieldHeight, 0x2104);
    }
    for (int y = 0; y <= GRID_HEIGHT; y++) {
        int lineY = fieldY + y * CELL_SIZE;
        tft.drawFastHLine(fieldX, lineY, fieldWidth, 0x2104);
    }
    #endif
    
    LOG_DEBUG("🐍 Game field: %dx%d grid, %dx%d pixels at (%d,%d)", 
             GRID_WIDTH, GRID_HEIGHT, fieldWidth, fieldHeight, fieldX, fieldY);
}

void SnakeGameScreen::drawSnake(lgfx::LGFX_Device& tft) {
    for (const Point& segment : snake) {
        drawCell(tft, segment, SNAKE_COLOR, SNAKE_BORDER_COLOR);
    }
}

void SnakeGameScreen::drawFood(lgfx::LGFX_Device& tft) {
    drawCell(tft, food, FOOD_COLOR, FOOD_BORDER_COLOR);
}

void SnakeGameScreen::drawScore(lgfx::LGFX_Device& tft) {
    // Draw score in top-right corner of content area
    tft.setTextColor(SCORE_COLOR, BACKGROUND_COLOR);
    tft.setTextSize(1);
    
    String scoreText = "Score: " + String(score);
    int textWidth = scoreText.length() * 6; // Approximate character width
    tft.setCursor(getContentWidth() - textWidth - 5, getContentY() + 5);
    tft.print(scoreText);
    
    // Draw snake length on same line, left side
    String lengthText = "Len: " + String(snake.size());
    tft.setCursor(5, getContentY() + 5);
    tft.print(lengthText);
}

void SnakeGameScreen::drawGameStatus(lgfx::LGFX_Device& tft) {
    // Draw game time
    unsigned long gameTime = millis() - gameStartTime;
    String timeText = "Time: " + formatTime(gameTime);
    
    tft.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
    tft.setTextSize(1);
    tft.setCursor(5, getContentY() + getContentHeight() - 15);
    tft.print(timeText);
}

void SnakeGameScreen::drawPauseOverlay(lgfx::LGFX_Device& tft) {
    // Semi-transparent overlay
    int overlayX = getContentWidth() / 4;
    int overlayY = getContentY() + getContentHeight() / 3;
    int overlayW = getContentWidth() / 2;
    int overlayH = 60;
    
    // Draw overlay background
    tft.fillRect(overlayX, overlayY, overlayW, overlayH, 0x2104); // Dark gray
    tft.drawRect(overlayX, overlayY, overlayW, overlayH, TEXT_COLOR);
    
    // Draw pause message
    tft.setTextColor(TEXT_COLOR, 0x2104);
    tft.setTextSize(2);
    tft.setCursor(overlayX + 20, overlayY + 10);
    tft.print("PAUSED");
    
    tft.setTextSize(1);
    tft.setCursor(overlayX + 10, overlayY + 35);
    tft.print("Press any key");
    tft.setCursor(overlayX + 10, overlayY + 45);
    tft.print("to continue");
}

void SnakeGameScreen::drawGameOverOverlay(lgfx::LGFX_Device& tft) {
    // Game over overlay
    int overlayX = getContentWidth() / 8;
    int overlayY = getContentY() + getContentHeight() / 4;
    int overlayW = getContentWidth() * 3 / 4;
    int overlayH = 100;
    
    // Draw overlay background
    tft.fillRect(overlayX, overlayY, overlayW, overlayH, 0x2104); // Dark gray
    tft.drawRect(overlayX, overlayY, overlayW, overlayH, 0xF800); // Red border
    
    // Draw game over message
    tft.setTextColor(0xF800, 0x2104); // Red text
    tft.setTextSize(2);
    int textX = overlayX + (overlayW - 12*8) / 2; // Center "GAME OVER"
    tft.setCursor(textX, overlayY + 10);
    tft.print("GAME OVER");
    
    // Draw final score
    tft.setTextColor(SCORE_COLOR, 0x2104);
    tft.setTextSize(1);
    String finalScore = "Score: " + String(score);
    tft.setCursor(overlayX + 10, overlayY + 35);
    tft.print(finalScore);
    
    String lengthInfo = "Length: " + String(snake.size());
    tft.setCursor(overlayX + 10, overlayY + 50);
    tft.print(lengthInfo);
    
    // Calculate and show game time
    unsigned long gameTime = millis() - gameStartTime;
    String timeInfo = "Time: " + formatTime(gameTime);
    tft.setCursor(overlayX + 10, overlayY + 65);
    tft.print(timeInfo);
    
    // Draw restart instruction
    tft.setTextColor(TEXT_COLOR, 0x2104);
    tft.setCursor(overlayX + 10, overlayY + 85);
    tft.print("Press any key to restart");
}

void SnakeGameScreen::drawInstructions(lgfx::LGFX_Device& tft) {
    // Draw control instructions at bottom
    tft.setTextColor(0x8410, BACKGROUND_COLOR); // Dim gray
    tft.setTextSize(1);
    
    int y = getContentY() + getContentHeight() - 25;
    tft.setCursor(5, y);
    tft.print("2468=Move *=Pause #=Reset A=Home");
}

// ========== Dirty Rectangle Optimization ==========

void SnakeGameScreen::clearSnakeTail(lgfx::LGFX_Device& tft, const Point& tailPos) {
    clearCell(tft, tailPos);
}

void SnakeGameScreen::drawSnakeHead(lgfx::LGFX_Device& tft, const Point& headPos) {
    drawCell(tft, headPos, SNAKE_COLOR, SNAKE_BORDER_COLOR);
}

void SnakeGameScreen::clearOldFood(lgfx::LGFX_Device& tft, const Point& oldFoodPos) {
    clearCell(tft, oldFoodPos);
}

void SnakeGameScreen::drawCell(lgfx::LGFX_Device& tft, const Point& pos, uint16_t fillColor, uint16_t borderColor) {
    // Bounds check to prevent drawing outside game field
    if (pos.x < 0 || pos.x >= GRID_WIDTH || pos.y < 0 || pos.y >= GRID_HEIGHT) {
        LOG_ERROR("🐍 Attempted to draw cell outside bounds: (%d,%d), grid is %dx%d", 
                 pos.x, pos.y, GRID_WIDTH, GRID_HEIGHT);
        return;
    }
    
    Point screenPos = gridToScreen(pos.x, pos.y);
    
    // Additional safety check for screen coordinates
    if (screenPos.y < getContentY() || screenPos.y + CELL_SIZE > getContentY() + getContentHeight()) {
        LOG_ERROR("🐍 Cell at (%d,%d) would render at Y %d, outside content area %d-%d", 
                 pos.x, pos.y, screenPos.y, getContentY(), getContentY() + getContentHeight());
        return;
    }
    
    // Draw cell with border
    tft.fillRect(screenPos.x, screenPos.y, CELL_SIZE, CELL_SIZE, fillColor);
    if (BORDER_SIZE > 0) {
        tft.drawRect(screenPos.x, screenPos.y, CELL_SIZE, CELL_SIZE, borderColor);
    }
}

void SnakeGameScreen::clearCell(lgfx::LGFX_Device& tft, const Point& pos) {
    // Bounds check to prevent clearing outside game field
    if (pos.x < 0 || pos.x >= GRID_WIDTH || pos.y < 0 || pos.y >= GRID_HEIGHT) {
        return; // Silently ignore invalid clear requests
    }
    
    Point screenPos = gridToScreen(pos.x, pos.y);
    
    // Additional safety check
    if (screenPos.y < getContentY() || screenPos.y + CELL_SIZE > getContentY() + getContentHeight()) {
        return; // Silently ignore out-of-bounds clear
    }
    
    tft.fillRect(screenPos.x, screenPos.y, CELL_SIZE, CELL_SIZE, BACKGROUND_COLOR);
}

// ========== Utility Functions ==========

Point SnakeGameScreen::screenToGrid(int screenX, int screenY) const {
    return Point((screenX - FIELD_OFFSET_X) / CELL_SIZE, 
                (screenY - getContentY() - FIELD_OFFSET_Y) / CELL_SIZE);
}

Point SnakeGameScreen::gridToScreen(int gridX, int gridY) const {
    int screenX = gridX * CELL_SIZE + FIELD_OFFSET_X;
    int screenY = gridY * CELL_SIZE + getContentY() + FIELD_OFFSET_Y;
    
    // Debug boundary check
    if (screenY < getContentY() || screenY + CELL_SIZE > getContentY() + getContentHeight()) {
        LOG_DEBUG("🐍 WARNING: Cell at grid (%d,%d) maps to screen Y %d, content area is %d-%d", 
                 gridX, gridY, screenY, getContentY(), getContentY() + getContentHeight());
    }
    
    return Point(screenX, screenY);
}

bool SnakeGameScreen::isInsideGameField(const Point& pos) const {
    return pos.x >= 0 && pos.x < GRID_WIDTH && pos.y >= 0 && pos.y < GRID_HEIGHT;
}

String SnakeGameScreen::formatTime(unsigned long milliseconds) const {
    unsigned long seconds = milliseconds / 1000;
    unsigned long minutes = seconds / 60;
    seconds = seconds % 60;
    
    return String(minutes) + ":" + (seconds < 10 ? "0" : "") + String(seconds);
}