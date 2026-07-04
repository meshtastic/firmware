#include "CastleBoyApp.h"
#include "global.h"
#include "menu.h"

namespace {

enum AppMode : uint8_t { MODE_LAUNCHER, MODE_CASTLEBOY, MODE_SNAKE, MODE_BLOCKS, MODE_BREAKOUT };

bool started = false;
AppMode mode = MODE_LAUNCHER;
uint8_t launcherIndex = 0;
uint8_t xbm[128 * 64 / 8];
uint32_t appFrame = 0;
uint8_t bHoldFrames = 0;
bool bLongHandled = false;
bool bShortReleased = false;

// 5-column, 7-row uppercase/digit font. Entries are vertical columns.
const uint8_t tinyFont[][5] PROGMEM = {
  {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
  {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
  {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},
  {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
  {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
  {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
  {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
  {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
  {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
  {0x06,0x49,0x49,0x29,0x1e}
};

uint8_t glyphIndex(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '0' && c <= '9') return 26 + c - '0';
  return 255;
}

void drawChar(int16_t x, int16_t y, char c) {
  const uint8_t gi = glyphIndex(c);
  if (gi == 255) return;
  for (uint8_t col = 0; col < 5; ++col) {
    uint8_t bits = pgm_read_byte(&tinyFont[gi][col]);
    for (uint8_t row = 0; row < 7; ++row)
      if (bits & (1u << row)) ab.fillRect(x + col, y + row, 1, 1, WHITE);
  }
}

void drawText(int16_t x, int16_t y, const char *s) {
  while (*s) {
    drawChar(x, y, *s++);
    x += 6;
  }
}

void drawCentered(int16_t y, const char *s) {
  drawText((128 - (int16_t)strlen(s) * 6 + 1) / 2, y, s);
}

void drawScore(uint16_t score) {
  drawText(1, 1, "SCORE");
  Util::drawNumber(39, 1, score, ALIGN_LEFT);
}

void drawGameOver(uint16_t score) {
  drawCentered(20, "GAME OVER");
  drawCentered(32, "A RETRY");
  drawCentered(42, "B MENU");
  drawScore(score);
}

// ---- Snake -----------------------------------------------------------------
struct SnakePoint { uint8_t x, y; };
SnakePoint snake[96];
uint8_t snakeLength, snakeDirection, snakeNextDirection, foodX, foodY;
uint16_t snakeScore;
bool snakeGameOver;

bool snakeOccupies(uint8_t x, uint8_t y, uint8_t limit) {
  for (uint8_t i = 0; i < limit; ++i)
    if (snake[i].x == x && snake[i].y == y) return true;
  return false;
}

void placeFood() {
  do {
    foodX = random(0, 31);
    foodY = random(1, 16);
  } while (snakeOccupies(foodX, foodY, snakeLength));
}

void resetSnake() {
  snakeLength = 5;
  for (uint8_t i = 0; i < snakeLength; ++i) snake[i] = {(uint8_t)(15 - i), 8};
  snakeDirection = snakeNextDirection = 1;
  snakeScore = 0;
  snakeGameOver = false;
  placeFood();
}

void updateSnake() {
  if (ab.justPressed(UP_BUTTON) && snakeDirection != 2) snakeNextDirection = 0;
  if (ab.justPressed(RIGHT_BUTTON) && snakeDirection != 3) snakeNextDirection = 1;
  if (ab.justPressed(DOWN_BUTTON) && snakeDirection != 0) snakeNextDirection = 2;
  if (ab.justPressed(LEFT_BUTTON) && snakeDirection != 1) snakeNextDirection = 3;
  if (snakeGameOver) {
    if (ab.justPressed(A_BUTTON)) resetSnake();
    return;
  }
  if (appFrame % 7) return;
  snakeDirection = snakeNextDirection;
  SnakePoint head = snake[0];
  if (snakeDirection == 0) { if (!head.y) { snakeGameOver = true; return; } --head.y; }
  if (snakeDirection == 1) { if (++head.x >= 31) { snakeGameOver = true; return; } }
  if (snakeDirection == 2) { if (++head.y >= 16) { snakeGameOver = true; return; } }
  if (snakeDirection == 3) { if (!head.x) { snakeGameOver = true; return; } --head.x; }
  if (snakeOccupies(head.x, head.y, snakeLength - 1)) { snakeGameOver = true; return; }
  const bool ate = head.x == foodX && head.y == foodY;
  if (ate && snakeLength < 96) ++snakeLength;
  for (uint8_t i = snakeLength - 1; i; --i) snake[i] = snake[i - 1];
  snake[0] = head;
  if (ate) { snakeScore += 10; placeFood(); }
}

void drawSnake() {
  if (snakeGameOver) { drawGameOver(snakeScore); return; }
  drawScore(snakeScore);
  ab.fillRect(foodX * 4, foodY * 4, 3, 3, WHITE);
  for (uint8_t i = 0; i < snakeLength; ++i)
    ab.fillRect(snake[i].x * 4, snake[i].y * 4, 3, 3, WHITE);
}

// ---- Blocks (compact falling-block game) -----------------------------------
uint16_t blocksBoard[18];
uint8_t blockPiece, blockNextPiece, blockRotation;
int8_t blockX, blockY;
uint16_t blocksScore;
bool blocksGameOver;
uint32_t blockDropAt;

const uint16_t pieceMasks[7][4] PROGMEM = {
  {0x0f00,0x2222,0x00f0,0x4444},{0x0660,0x0660,0x0660,0x0660},
  {0x0e40,0x04c4,0x04e0,0x4640},{0x0e20,0x044c,0x08e0,0x6440},
  {0x0c60,0x04c8,0x0c60,0x04c8},{0x06c0,0x08c4,0x06c0,0x08c4},
  {0x0e80,0x0c44,0x02e0,0x4460}
};

bool blockCell(uint8_t piece, uint8_t rotation, uint8_t x, uint8_t y) {
  return pgm_read_word(&pieceMasks[piece][rotation]) & (1u << (15 - (y * 4 + x)));
}

bool blocksCollides(int8_t x, int8_t y, uint8_t rotation) {
  for (uint8_t py = 0; py < 4; ++py) for (uint8_t px = 0; px < 4; ++px) {
    if (!blockCell(blockPiece, rotation, px, py)) continue;
    int8_t bx = x + px, by = y + py;
    if (bx < 0 || bx >= 10 || by >= 18) return true;
    if (by >= 0 && (blocksBoard[by] & (1u << bx))) return true;
  }
  return false;
}

void spawnBlock() {
  blockPiece = blockNextPiece;
  blockNextPiece = random(0, 7);
  blockRotation = 0;
  blockX = 3;
  blockY = -1;
  if (blocksCollides(blockX, blockY, blockRotation)) blocksGameOver = true;
}

void resetBlocks() {
  memset(blocksBoard, 0, sizeof(blocksBoard));
  blocksScore = 0;
  blocksGameOver = false;
  blockDropAt = appFrame + 25;
  blockNextPiece = random(0, 7);
  spawnBlock();
}

void lockBlock() {
  for (uint8_t py = 0; py < 4; ++py) for (uint8_t px = 0; px < 4; ++px)
    if (blockCell(blockPiece, blockRotation, px, py) && blockY + py >= 0)
      blocksBoard[blockY + py] |= 1u << (blockX + px);
  for (int8_t y = 17; y >= 0; --y) {
    if ((blocksBoard[y] & 0x03ff) == 0x03ff) {
      for (int8_t yy = y; yy > 0; --yy) blocksBoard[yy] = blocksBoard[yy - 1];
      blocksBoard[0] = 0;
      blocksScore += 100;
      ++y;
    }
  }
  spawnBlock();
}

void updateBlocks() {
  if (blocksGameOver) { if (ab.justPressed(A_BUTTON)) resetBlocks(); return; }
  if (ab.justPressed(LEFT_BUTTON) && !blocksCollides(blockX - 1, blockY, blockRotation)) --blockX;
  if (ab.justPressed(RIGHT_BUTTON) && !blocksCollides(blockX + 1, blockY, blockRotation)) ++blockX;
  if (ab.justPressed(A_BUTTON)) {
    uint8_t r = (blockRotation + 1) & 3;
    if (!blocksCollides(blockX, blockY, r)) blockRotation = r;
  }
  if (ab.justPressed(UP_BUTTON)) {
    while (!blocksCollides(blockX, blockY + 1, blockRotation)) ++blockY;
    lockBlock();
    return;
  }
  if (appFrame >= blockDropAt || ab.pressed(DOWN_BUTTON)) {
    blockDropAt = appFrame + 25;
    if (!blocksCollides(blockX, blockY + 1, blockRotation)) ++blockY;
    else lockBlock();
  }
}

void drawBlocks() {
  if (blocksGameOver) { drawGameOver(blocksScore); return; }

  ab.fillRect(1, 15, 43, 29, WHITE);
  ab.fillRect(2, 16, 41, 27, BLACK);
  drawText(6, 19, "SCORE");
  Util::drawNumber(22, 31, blocksScore, ALIGN_CENTER);

  ab.fillRect(84, 1, 43, 31, WHITE);
  ab.fillRect(85, 2, 41, 29, BLACK);
  drawText(93, 5, "NEXT");
  for (uint8_t py = 0; py < 4; ++py) for (uint8_t px = 0; px < 4; ++px)
    if (blockCell(blockNextPiece, 0, px, py))
      ab.fillRect(97 + px * 4, 14 + py * 4, 3, 3, WHITE);

  const int8_t ox = 49, oy = 9;
  ab.fillRect(ox - 2, oy - 1, 1, 55, WHITE);
  ab.fillRect(ox + 30, oy - 1, 1, 55, WHITE);
  for (uint8_t y = 0; y < 18; ++y) for (uint8_t x = 0; x < 10; ++x)
    if (blocksBoard[y] & (1u << x)) ab.fillRect(ox + x * 3, oy + y * 3, 2, 2, WHITE);
  for (uint8_t py = 0; py < 4; ++py) for (uint8_t px = 0; px < 4; ++px)
    if (blockCell(blockPiece, blockRotation, px, py) && blockY + py >= 0)
      ab.fillRect(ox + (blockX + px) * 3, oy + (blockY + py) * 3, 2, 2, WHITE);
}

// ---- Breakout ---------------------------------------------------------------
int16_t paddleX, ballX, ballY, ballDX, ballDY;
uint32_t bricks;
uint16_t breakoutScore;
bool breakoutGameOver;

void resetBreakout() {
  paddleX = 52; ballX = 63; ballY = 45; ballDX = 1; ballDY = -1;
  bricks = 0x00ffffff;
  breakoutScore = 0;
  breakoutGameOver = false;
}

void updateBreakout() {
  if (breakoutGameOver) { if (ab.justPressed(A_BUTTON)) resetBreakout(); return; }
  if (ab.pressed(LEFT_BUTTON) && paddleX > 1) paddleX -= 2;
  if (ab.pressed(RIGHT_BUTTON) && paddleX < 103) paddleX += 2;
  if (appFrame & 1) return;
  ballX += ballDX; ballY += ballDY;
  if (ballX <= 1 || ballX >= 126) { ballDX = -ballDX; ballX += ballDX; }
  if (ballY <= 9) { ballDY = 1; ballY = 10; }
  if (ballY >= 57 && ballY <= 60 && ballX >= paddleX && ballX <= paddleX + 24) {
    ballDY = -1;
    ballDX = ballX < paddleX + 12 ? -1 : 1;
  }
  if (ballY > 63) { breakoutGameOver = true; return; }
  if (ballY >= 13 && ballY < 31) {
    uint8_t row = (ballY - 13) / 6, col = ballX / 16;
    if (col < 8) {
      uint8_t index = row * 8 + col;
      if (bricks & (1ul << index)) {
        bricks &= ~(1ul << index);
        ballDY = -ballDY;
        breakoutScore += 10;
        if (!bricks) resetBreakout();
      }
    }
  }
}

void drawBreakout() {
  if (breakoutGameOver) { drawGameOver(breakoutScore); return; }
  drawScore(breakoutScore);
  for (uint8_t row = 0; row < 3; ++row) for (uint8_t col = 0; col < 8; ++col)
    if (bricks & (1ul << (row * 8 + col))) ab.fillRect(col * 16 + 1, row * 6 + 13, 14, 4, WHITE);
  ab.fillRect(paddleX, 59, 24, 3, WHITE);
  ab.fillRect(ballX, ballY, 2, 2, WHITE);
}

void showLauncher() {
  mode = MODE_LAUNCHER;
  bHoldFrames = 0;
  bLongHandled = false;
  bShortReleased = false;
  noTone(PIN_BUZZER);
}

void updateLauncher() {
  if (ab.justPressed(UP_BUTTON) && launcherIndex) --launcherIndex;
  if (ab.justPressed(DOWN_BUTTON) && launcherIndex < 3) ++launcherIndex;
  if (!ab.justPressed(A_BUTTON)) return;
  if (launcherIndex == 0) { Menu::showTitle(); mode = MODE_CASTLEBOY; }
  if (launcherIndex == 1) { resetSnake(); mode = MODE_SNAKE; }
  if (launcherIndex == 2) { resetBlocks(); mode = MODE_BLOCKS; }
  if (launcherIndex == 3) { resetBreakout(); mode = MODE_BREAKOUT; }
}

void drawLauncher() {
  static const char *items[] = {"CASTLEBOY", "SNAKE", "BLOCKS", "BREAKOUT"};
  drawCentered(1, "MESH ARCADE");
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t y = 18 + i * 11;
    if (i == launcherIndex) {
      ab.fillRect(8, y - 2, 112, 10, WHITE);
      ab.fillRect(9, y - 1, 110, 8, BLACK);
    }
    drawText(18, y, items[i]);
  }
}

}

void CastleBoyApp::begin()
{
  ab.begin();
  ab.setFrameRate(FPS);
  mode = MODE_LAUNCHER;
  launcherIndex = 0;
  appFrame = 0;
  bHoldFrames = 0;
  bLongHandled = false;
  bShortReleased = false;
  started = true;
}

void CastleBoyApp::step(uint8_t buttons)
{
  if (!started) begin();
  ++appFrame;
  ab.setButtons(buttons);
  ab.clear();
  ab.pollButtons();

  if (mode == MODE_LAUNCHER) { updateLauncher(); drawLauncher(); return; }

  bShortReleased = false;
  if (mode != MODE_CASTLEBOY) {
    if (ab.pressed(B_BUTTON)) {
      if (bHoldFrames < 255) ++bHoldFrames;
      if (bHoldFrames >= 48 && !bLongHandled) {
        bLongHandled = true;
        showLauncher();
        drawLauncher();
        return;
      }
    } else {
      if (bHoldFrames > 0 && !bLongHandled)
        bShortReleased = true;
      bHoldFrames = 0;
      bLongHandled = false;
    }
  }

  if (mode == MODE_CASTLEBOY) {
    Menu::loop();
    if (flashCounter > 0) {
      ab.fillRect(0, 0, 128, 64, WHITE);
      --flashCounter;
    }
  } else if (mode == MODE_SNAKE) {
    updateSnake(); drawSnake();
  } else if (mode == MODE_BLOCKS) {
    if (bShortReleased) {
      uint8_t r = (blockRotation + 1) & 3;
      if (!blocksCollides(blockX, blockY, r)) blockRotation = r;
    }
    updateBlocks(); drawBlocks();
  } else if (mode == MODE_BREAKOUT) {
    updateBreakout(); drawBreakout();
  }
}

const uint8_t *CastleBoyApp::buffer() { return ab.getBuffer(); }

const uint8_t *CastleBoyApp::xbmBuffer()
{
  const uint8_t *native = ab.getBuffer();
  memset(xbm, 0, sizeof(xbm));
  for (uint8_t y = 0; y < 64; ++y) for (uint8_t x = 0; x < 128; ++x)
    if (native[x + (y >> 3) * 128] & (1u << (y & 7)))
      xbm[(uint16_t)y * 16 + (x >> 3)] |= (uint8_t)(1u << (x & 7));
  return xbm;
}
