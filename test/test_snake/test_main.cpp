#include "TestUtil.h"
#include "modules/games/SnakeGame.h"
#include <unity.h>

// Pure-logic tests for SnakeGame: ring-buffer advance, reversal rejection, wall/self collision,
// growth on eat, and food-placement validity. No device globals or display stack required.

static const uint32_t kSeed = 0xC0FFEEu;

// Count how many board cells the snake currently occupies (cross-check for len()).
static uint16_t countOccupied(const SnakeGame &game)
{
    uint16_t n = 0;
    for (uint8_t y = 0; y < SnakeGame::GRID_H; y++)
        for (uint8_t x = 0; x < SnakeGame::GRID_W; x++)
            if (game.occupied(x, y))
                n++;
    return n;
}

static void test_reset_initialState()
{
    SnakeGame game;
    game.reset(kSeed);

    TEST_ASSERT_TRUE(game.isPlaying());
    TEST_ASSERT_FALSE(game.isWon());
    TEST_ASSERT_EQUAL_UINT16(SnakeGame::START_LEN, game.length());
    TEST_ASSERT_EQUAL_UINT32(0u, game.score());
    TEST_ASSERT_EQUAL_INT(SnakeGame::DIR_RIGHT, game.direction());

    // Head spawns at board centre; the whole test file relies on this anchor.
    SnakeGame::Cell head = game.head();
    TEST_ASSERT_EQUAL_UINT8(SnakeGame::GRID_W / 2, head.x);
    TEST_ASSERT_EQUAL_UINT8(SnakeGame::GRID_H / 2, head.y);

    // Exactly START_LEN cells occupied, and the head is one of them.
    TEST_ASSERT_EQUAL_UINT16(SnakeGame::START_LEN, countOccupied(game));
    TEST_ASSERT_TRUE(game.occupied(head.x, head.y));
}

static void test_food_isValidAndOffBody()
{
    SnakeGame game;
    game.reset(kSeed);
    SnakeGame::Cell food = game.food();
    TEST_ASSERT_TRUE(food.x < SnakeGame::GRID_W);
    TEST_ASSERT_TRUE(food.y < SnakeGame::GRID_H);
    TEST_ASSERT_FALSE(game.occupied(food.x, food.y)); // food never spawns on the snake
}

static void test_setDirection_rejectsReversal()
{
    SnakeGame game;
    game.reset(kSeed); // heading right

    TEST_ASSERT_FALSE(game.setDirection(SnakeGame::DIR_LEFT)); // 180 reversal -> rejected
    TEST_ASSERT_TRUE(game.setDirection(SnakeGame::DIR_UP));    // perpendicular -> ok
    TEST_ASSERT_TRUE(game.setDirection(SnakeGame::DIR_RIGHT)); // same as committed dir -> ok (no-op)

    // A double-input within one tick can't chain into a reversal: after latching UP, LEFT is
    // still checked against the committed RIGHT and rejected, so the neck stays safe.
    game.setDirection(SnakeGame::DIR_UP);
    TEST_ASSERT_FALSE(game.setDirection(SnakeGame::DIR_LEFT));
}

static void test_step_movesAndTailFollows()
{
    SnakeGame game;
    game.reset(kSeed);
    SnakeGame::Cell head = game.head();
    game.placeFoodAt(0, 0); // corner, off the snake -> guaranteed non-eating step

    TEST_ASSERT_TRUE(game.step());
    SnakeGame::Cell newHead = game.head();
    TEST_ASSERT_EQUAL_UINT8(head.x + 1, newHead.x); // moved one cell right
    TEST_ASSERT_EQUAL_UINT8(head.y, newHead.y);
    TEST_ASSERT_EQUAL_UINT16(SnakeGame::START_LEN, game.length()); // length unchanged when not eating
    TEST_ASSERT_EQUAL_UINT16(SnakeGame::START_LEN, countOccupied(game));
    TEST_ASSERT_EQUAL_UINT32(0u, game.score());
}

static void test_eat_growsAndScores()
{
    SnakeGame game;
    game.reset(kSeed);
    SnakeGame::Cell head = game.head();
    game.placeFoodAt(head.x + 1, head.y); // food directly ahead

    TEST_ASSERT_TRUE(game.step());
    TEST_ASSERT_EQUAL_UINT16(SnakeGame::START_LEN + 1, game.length()); // grew by one
    TEST_ASSERT_EQUAL_UINT32(1u, game.score());
    TEST_ASSERT_EQUAL_UINT16(SnakeGame::START_LEN + 1, countOccupied(game));

    // A fresh food was placed and is not on the snake.
    SnakeGame::Cell food = game.food();
    TEST_ASSERT_FALSE(game.occupied(food.x, food.y));
}

static void test_wallCollision_endsGame()
{
    SnakeGame game;
    game.reset(kSeed);
    game.placeFoodAt(0, 0);
    game.setDirection(SnakeGame::DIR_UP); // head is at mid-height; drive straight up into the wall

    bool alive = true;
    int guard = 0;
    while (alive && guard++ < SnakeGame::GRID_H + 4) {
        game.placeFoodAt(0, 0); // keep food out of the way each tick
        alive = game.step();
    }
    TEST_ASSERT_FALSE(alive);
    TEST_ASSERT_FALSE(game.isPlaying());
}

static void test_selfCollision_endsGame()
{
    SnakeGame game;
    game.reset(kSeed);
    TEST_ASSERT_EQUAL_UINT8(16, game.head().x); // anchor the deterministic path below
    TEST_ASSERT_EQUAL_UINT8(6, game.head().y);

    // Grow to length 5 along a straight horizontal line (cells (14..18, 6)).
    game.placeFoodAt(17, 6);
    TEST_ASSERT_TRUE(game.step());
    game.placeFoodAt(18, 6);
    TEST_ASSERT_TRUE(game.step());
    TEST_ASSERT_EQUAL_UINT16(5, game.length());

    // Curl back on itself: DOWN, LEFT, then UP re-enters an occupied body cell.
    game.setDirection(SnakeGame::DIR_DOWN);
    game.placeFoodAt(0, 0);
    TEST_ASSERT_TRUE(game.step());
    game.setDirection(SnakeGame::DIR_LEFT);
    game.placeFoodAt(0, 0);
    TEST_ASSERT_TRUE(game.step());
    game.setDirection(SnakeGame::DIR_UP);
    game.placeFoodAt(0, 0);
    TEST_ASSERT_FALSE(game.step()); // bites its own body
    TEST_ASSERT_FALSE(game.isPlaying());
}

static void test_deadGame_stepIsNoOp()
{
    SnakeGame game;
    game.reset(kSeed);
    game.setDirection(SnakeGame::DIR_UP);
    for (int i = 0; i < SnakeGame::GRID_H + 4; i++) {
        game.placeFoodAt(0, 0);
        game.step();
    }
    TEST_ASSERT_FALSE(game.isPlaying());
    uint32_t scoreBefore = game.score();
    TEST_ASSERT_FALSE(game.step()); // stays dead, no state change
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
    RUN_TEST(test_food_isValidAndOffBody);
    RUN_TEST(test_setDirection_rejectsReversal);
    RUN_TEST(test_step_movesAndTailFollows);
    RUN_TEST(test_eat_growsAndScores);
    RUN_TEST(test_wallCollision_endsGame);
    RUN_TEST(test_selfCollision_endsGame);
    RUN_TEST(test_deadGame_stepIsNoOp);
    exit(UNITY_END());
}

void loop() {}
}
