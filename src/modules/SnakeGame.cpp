#include "SnakeGame.h"

void SnakeGame::reset(uint32_t seed)
{
    memset(occ, 0, sizeof(occ));
    len = 0;
    points = 0;
    alive = true;
    won = false;
    rng = seed ? seed : 0xA5A5A5A5u; // xorshift32 must never be seeded with 0

    // Spawn a START_LEN snake horizontally in the middle of the board, heading right, with the
    // head at the centre and the body trailing to its left.
    const uint8_t cx = GRID_W / 2;
    const uint8_t cy = GRID_H / 2;
    dir = DIR_RIGHT;
    pendingDir = DIR_RIGHT;
    tailIdx = 0;
    for (uint8_t i = 0; i < START_LEN; i++) {
        const uint8_t x = static_cast<uint8_t>(cx - (START_LEN - 1) + i);
        body[i] = {x, cy};
        setOcc(cellIndex(x, cy));
        len++;
    }
    headIdx = static_cast<uint16_t>(START_LEN - 1);

    placeFood();
}

bool SnakeGame::isReverse(Direction a, Direction b)
{
    return (a == DIR_UP && b == DIR_DOWN) || (a == DIR_DOWN && b == DIR_UP) || (a == DIR_LEFT && b == DIR_RIGHT) ||
           (a == DIR_RIGHT && b == DIR_LEFT);
}

bool SnakeGame::setDirection(Direction d)
{
    if (isReverse(dir, d))
        return false;
    pendingDir = d;
    return true;
}

uint32_t SnakeGame::nextRandom()
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

bool SnakeGame::placeFood()
{
    const uint16_t free = static_cast<uint16_t>(CELL_COUNT - len);
    if (free == 0)
        return false; // board full -> caller treats as a win

    // Pick the k-th free cell (k uniform in [0, free)) via a single linear scan. Deterministic,
    // always valid, and cheap for a 384-cell board -- no rejection sampling / near-full special case.
    uint16_t k = static_cast<uint16_t>(nextRandom() % free);
    for (uint16_t idx = 0; idx < CELL_COUNT; idx++) {
        if (!getOcc(idx)) {
            if (k == 0) {
                foodCell = {static_cast<uint8_t>(idx % GRID_W), static_cast<uint8_t>(idx / GRID_W)};
                return true;
            }
            k--;
        }
    }
    return false; // unreachable while free > 0
}

bool SnakeGame::step()
{
    if (!alive)
        return false;

    dir = pendingDir; // commit the latched heading

    const Cell h = body[headIdx];
    int16_t nx = h.x;
    int16_t ny = h.y;
    switch (dir) {
    case DIR_UP:
        ny--;
        break;
    case DIR_DOWN:
        ny++;
        break;
    case DIR_LEFT:
        nx--;
        break;
    case DIR_RIGHT:
        nx++;
        break;
    }

    // Wall collision (signed math catches the 0-- underflow too).
    if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
        alive = false;
        return false;
    }

    const uint16_t nidx = cellIndex(static_cast<uint8_t>(nx), static_cast<uint8_t>(ny));
    const bool eating = (nx == foodCell.x && ny == foodCell.y);

    // When not eating the tail vacates this tick, so free it first: moving into the cell the
    // tail is leaving is legal (classic snake), moving into any other body cell is fatal.
    if (!eating) {
        clearOcc(cellIndex(body[tailIdx].x, body[tailIdx].y));
        tailIdx = static_cast<uint16_t>((tailIdx + 1) % CAP);
        len--;
    }

    if (getOcc(nidx)) {
        alive = false;
        return false;
    }

    // Advance the head into the new cell.
    headIdx = static_cast<uint16_t>((headIdx + 1) % CAP);
    body[headIdx] = {static_cast<uint8_t>(nx), static_cast<uint8_t>(ny)};
    setOcc(nidx);
    len++;

    if (eating) {
        points++;
        if (!placeFood()) {
            won = true;
            alive = false; // board completely filled -- the player won
        }
    }

    return alive;
}
