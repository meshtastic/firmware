#include "TestUtil.h"
#include "modules/games/Breakout.h"
#include <unity.h>

// Pure-logic tests for BreakoutGame: initial serve/brick state, paddle clamping, brick-clearing on
// a straight-up serve, and the ball staying within the board. No device globals or display stack.

static const uint32_t kSeed = 0xC0FFEEu;

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
    // The ball serves upward from just above the paddle straight into the brick field; within a
    // few dozen steps it must clear at least one brick and score.
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
        game.step();
        TEST_ASSERT_TRUE(game.ballX() >= 0 && game.ballX() < BreakoutGame::BOARD_W);
        TEST_ASSERT_TRUE(game.ballY() >= 0);
    }
}

void test_deadGame_stepIsNoOp()
{
    BreakoutGame game;
    game.reset(kSeed);
    // Park the paddle in a corner and never move it; the ball is eventually lost every life.
    game.moveLeft();
    for (int i = 0; i < 20000 && game.isPlaying(); i++) {
        for (int j = 0; j < 40; j++) // hold the paddle pinned left
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
    RUN_TEST(test_paddle_clampsToEdges);
    RUN_TEST(test_serve_clearsABrickAndScores);
    RUN_TEST(test_ball_staysInBounds);
    RUN_TEST(test_deadGame_stepIsNoOp);
    exit(UNITY_END());
}

void loop() {}
}
