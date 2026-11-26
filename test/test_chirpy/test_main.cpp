// ChirpyRunnerGame jump, gravity and obstacle collision.
//
// Why: pure game logic with no hardware dependency, and the only coverage of the runner
// state machine.
// Regression guarded: a second jump re-launching mid-air, and obstacle collision not ending
// the run.

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
    // A second jump mid-air must not re-launch. Step an identical control game that never presses
    // jump again and compare every step until it lands: a single step can hide the re-launch, as
    // chirpyY() rounds sub-pixels away, but the arcs must part before the control touches down.
    ChirpyRunnerGame game;
    ChirpyRunnerGame control;
    game.reset(kSeed);
    control.reset(kSeed);
    game.jump();
    control.jump();
    game.step();
    control.step();
    TEST_ASSERT_FALSE(game.onGround());

    game.jump();
    for (int steps = 0; steps < 200 && !control.onGround(); steps++) {
        game.step();
        control.step();
        TEST_ASSERT_EQUAL_INT16(control.chirpyY(), game.chirpyY());
        TEST_ASSERT_EQUAL(control.onGround(), game.onGround());
    }
    TEST_ASSERT_TRUE_MESSAGE(control.onGround(), "the control arc never landed");
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
