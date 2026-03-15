#pragma once

#include <cstdint>

namespace vigil {

/*
 * PriorityQueue — Manages LoRa TX ordering across 3 Meshtastic channels.
 *
 * Architecture decision #1 (CEO): CH2 > CH1 > CH3.
 *
 *   ┌──────────┐  ┌──────────┐  ┌──────────┐
 *   │ CH2: User│  │CH1: Detect│  │CH3: Coord │
 *   │ (alerts) │  │ (vectors) │  │ (cal/hb)  │
 *   │ HIGHEST  │  │  MEDIUM   │  │  LOWEST   │
 *   └────┬─────┘  └────┬─────┘  └────┬──────┘
 *        │              │              │
 *        ▼              ▼              ▼
 *   ┌──────────────────────────────────────┐
 *   │  TX scheduler: always drain CH2     │
 *   │  first, then CH1, then CH3          │
 *   └──────────────────────────────────────┘
 *
 * When CH1 queue is full during active detection,
 * oldest CH1 messages are dropped (not CH2).
 */

enum class Channel : uint8_t {
    DETECTOR = 0, // CH1: node-to-node DoA vectors
    USER = 1,     // CH2: final drone alerts to soldiers
    COORD = 2,    // CH3: calibration scheduling + heartbeats
};

class PriorityQueue {
  public:
    // TODO Phase C: Queue implementation with per-channel buffers
    // TODO Phase C: TX scheduling that respects CH2 > CH1 > CH3
};

} // namespace vigil
