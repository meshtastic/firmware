#include "TestUtil.h"
#include "modules/games/Breakout.h"
#include <unity.h>

// Pure-logic tests for BreakoutGame: initial serve/brick state, paddle clamping, the ball waiting
// on the paddle until launched, brick-clearing on a straight-up serve, and the ball staying within
// the board. No device globals or display stack.

static const uint32_t kSeed = 0xC0FFEEu;

// The ball docks on the paddle after every serve (including after losing a life), so a test that
// wants continuous play has to fire it whenever it is waiting.
static void stepLaunched(BreakoutGame &game)
{
    if (game.isBallDocked())
        game.launchBall();
    game.step();
}

void test_reset_initialState()
{
    BreakoutGame game;
    game.reset(kSeed);
    TEST_ASSERT_TRUE(game.isPlaying());
    TEST_ASSERT_EQUAL_UINT8(BreakoutGame::START_LIVES, game.lives());
    TEST_ASSERT_EQUAL_UINT8(1, game.level());
    TEST_ASSERT_EQUAL_UINT32(0, game.score());
    // Every brick present at the start.
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(BreakoutGame::BRICK_ROWS) * BreakoutGame::BRICK_COLS, game.bricksRemaining());
    // Paddle centred, ball above it and inside the board.
    TEST_ASSERT_EQUAL_INT16((BreakoutGame::BOARD_W - BreakoutGame::PADDLE_W) / 2, game.paddleX());
    TEST_ASSERT_TRUE(game.ballX() >= 0 && game.ballX() < BreakoutGame::BOARD_W);
    TEST_ASSERT_TRUE(game.ballY() >= 0 && game.ballY() < BreakoutGame::BOARD_H);
    // The ball waits on the paddle until the player serves it.
    TEST_ASSERT_TRUE(game.isBallDocked());
}

void test_ballWaitsOnPaddleUntilLaunched()
{
    BreakoutGame game;
    game.reset(kSeed);
    const int16_t restY = game.ballY();

    // Stepping without serving must not move the ball vertically, lose a life, or end the run.
    for (int i = 0; i < 50; i++)
        game.step();
    TEST_ASSERT_TRUE(game.isBallDocked());
    TEST_ASSERT_EQUAL_INT16(restY, game.ballY());
    TEST_ASSERT_EQUAL_UINT8(BreakoutGame::START_LIVES, game.lives());
    TEST_ASSERT_TRUE(game.isPlaying());

    // A docked ball tracks the paddle, so it can still be aimed before serving.
    const int16_t beforeX = game.ballX();
    for (int i = 0; i < 5; i++)
        game.moveLeft();
    game.step();
    TEST_ASSERT_TRUE(game.ballX() < beforeX);

    // Once launched it is in play and starts climbing toward the bricks.
    game.launchBall();
    TEST_ASSERT_FALSE(game.isBallDocked());
    for (int i = 0; i < 4; i++)
        game.step();
    TEST_ASSERT_TRUE(game.ballY() < restY);
}

void test_paddle_clampsToEdges()
{
    BreakoutGame game;
    game.reset(kSeed);
    for (int i = 0; i < 100; i++)
        game.moveLeft();
    TEST_ASSERT_EQUAL_INT16(0, game.paddleX());
    for (int i = 0; i < 100; i++)
        game.moveRight();
    TEST_ASSERT_EQUAL_INT16(BreakoutGame::BOARD_W - BreakoutGame::PADDLE_W, game.paddleX());
}

void test_serve_clearsABrickAndScores()
{
    BreakoutGame game;
    game.reset(kSeed);
    // Once served, the ball travels upward from just above the paddle straight into the brick field;
    // within a few dozen steps it must clear at least one brick and score.
    game.launchBall();
    for (int i = 0;
         i < 60 && game.bricksRemaining() == static_cast<uint16_t>(BreakoutGame::BRICK_ROWS) * BreakoutGame::BRICK_COLS; i++)
        game.step();
    TEST_ASSERT_TRUE(game.bricksRemaining() < static_cast<uint16_t>(BreakoutGame::BRICK_ROWS) * BreakoutGame::BRICK_COLS);
    TEST_ASSERT_TRUE(game.score() > 0);
}

void test_ball_staysInBounds()
{
    BreakoutGame game;
    game.reset(kSeed);
    // Drive the paddle to follow the ball so the game keeps going, and check the ball never leaves
    // the board horizontally across a long run.
    for (int i = 0; i < 500 && game.isPlaying(); i++) {
        if (game.ballX() < game.paddleX())
            game.moveLeft();
        else
            game.moveRight();
        stepLaunched(game);
        TEST_ASSERT_TRUE(game.ballX() >= 0 && game.ballX() < BreakoutGame::BOARD_W);
        TEST_ASSERT_TRUE(game.ballY() >= 0);
    }
}

void test_deadGame_stepIsNoOp()
{
    BreakoutGame game;
    game.reset(kSeed);
    // Serve each ball, then steer the paddle AWAY from it so every ball is missed and all lives
    // drain. (The ball re-docks after each loss, so it has to be re-served. Note it now serves from
    // the paddle's centre, so simply parking the paddle would let it rally instead of dying.)
    for (int i = 0; i < 20000 && game.isPlaying(); i++) {
        if (game.isBallDocked())
            game.launchBall();
        else if (game.ballX() < game.paddleX())
            game.moveRight();
        else
            game.moveLeft();
        game.step();
    }
    TEST_ASSERT_FALSE(game.isPlaying());
    const uint32_t scoreBefore = game.score();
    TEST_ASSERT_FALSE(game.step()); // stays dead, no further change
    TEST_ASSERT_EQUAL_UINT32(scoreBefore, game.score());
}

void setUp(void) {}

void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_reset_initialState);
    RUN_TEST(test_ballWaitsOnPaddleUntilLaunched);
    RUN_TEST(test_paddle_clampsToEdges);
    RUN_TEST(test_serve_clearsABrickAndScores);
    RUN_TEST(test_ball_staysInBounds);
    RUN_TEST(test_deadGame_stepIsNoOp);
    exit(UNITY_END());
}

void loop() {}
}
