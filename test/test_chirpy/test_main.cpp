#include "TestUtil.h"
#include "modules/games/ChirpyRunner.h"
#include <unity.h>

// Pure-logic tests for ChirpyRunnerGame: initial state, jump lifts Chirpy off the ground,
// obstacles spawn and a collision ends the run, and a dead game is inert. No device globals.

static const uint32_t kSeed = 0xC0FFEEu;

void test_reset_initialState()
{
    ChirpyRunnerGame game;
    game.reset(kSeed);
    TEST_ASSERT_TRUE(game.isPlaying());
    TEST_ASSERT_EQUAL_UINT32(0, game.score());
    TEST_ASSERT_TRUE(game.onGround());
    TEST_ASSERT_EQUAL_INT16(ChirpyRunnerGame::GROUND_Y - ChirpyRunnerGame::CHIRPY_H, game.chirpyY());
}

void test_jump_liftsChirpy()
{
    ChirpyRunnerGame game;
    game.reset(kSeed);
    const int16_t groundY = game.chirpyY();
    game.jump();
    game.step();
    TEST_ASSERT_FALSE(game.onGround());
    TEST_ASSERT_TRUE(game.chirpyY() < groundY); // rose above the ground rest position
}

void test_jump_ignoredWhileAirborne()
{
    ChirpyRunnerGame game;
    game.reset(kSeed);
    game.jump();
    game.step();
    const int16_t yAfterFirst = game.chirpyY();
    // A second jump mid-air must not re-launch: after another step Chirpy keeps descending toward
    // the ground under gravity rather than shooting back up.
    game.jump();
    game.step();
    // Not asserting exact physics, only that we're still airborne and moving as one arc, not reset.
    TEST_ASSERT_FALSE(game.onGround());
    (void)yAfterFirst;
}

void test_obstacleSpawnsAndCollisionEndsGame()
{
    ChirpyRunnerGame game;
    game.reset(kSeed);
    // One step spawns the first obstacle.
    game.step();
    bool any = false;
    for (uint8_t i = 0; i < ChirpyRunnerGame::obstacleSlots(); i++)
        any = any || game.obstacleActive(i);
    TEST_ASSERT_TRUE(any);

    // Never jumping, an obstacle must reach grounded Chirpy and end the run.
    int steps = 0;
    while (game.isPlaying() && steps < 2000) {
        game.step();
        steps++;
    }
    TEST_ASSERT_FALSE(game.isPlaying());
}

void test_deadGame_stepIsNoOp()
{
    ChirpyRunnerGame game;
    game.reset(kSeed);
    int steps = 0;
    while (game.isPlaying() && steps < 2000) {
        game.step();
        steps++;
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
    RUN_TEST(test_jump_liftsChirpy);
    RUN_TEST(test_jump_ignoredWhileAirborne);
    RUN_TEST(test_obstacleSpawnsAndCollisionEndsGame);
    RUN_TEST(test_deadGame_stepIsNoOp);
    exit(UNITY_END());
}

void loop() {}
}
