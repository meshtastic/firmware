// utility/bonding.h — stub for nRF54L15/Zephyr
// NodeDB.cpp includes this when ARCH_NRF52 is defined.
// Bluetooth is excluded; this stub satisfies the include chain.
#pragma once

// BLE role constants (from Bluefruit SDK)
#define BLE_GAP_ROLE_PERIPH   0x01
#define BLE_GAP_ROLE_CENTRAL  0x02

// Stub for bond_print_list()
static inline void bond_print_list(uint8_t) {}
