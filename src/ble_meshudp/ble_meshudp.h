#ifndef BLE_MESHUDP_H
#define BLE_MESHUDP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  MESHUDP_ROLE_PERIPHERAL = 0,
  MESHUDP_ROLE_CENTRAL = 1,
  MESHUDP_ROLE_DUAL = 2,
} meshudp_role_t;

// callback when a datagram is received (delivered after any reassembly)
typedef void (*meshudp_rx_cb_t)(void *ctx, const uint8_t *buf, size_t len);

// main init - call once after SoftDevice is enabled
// - role: which role(s) this device will operate in
// - rx_cb: callback to receive datagrams
// - ctx: opaque pointer returned in callback

typedef struct {
  meshudp_role_t role;
  meshudp_rx_cb_t rx_cb;
  void *ctx;
  const char *adv_name; // optional device name advertised
} meshudp_config_t;

int meshudp_init(meshudp_config_t *cfg);
int meshudp_start_advertising(void);
int meshudp_stop_advertising(void);
int meshudp_start_central_scan(void);
int meshudp_stop_scan(void);

// send a datagram to the connected peer(s)
// - for peripheral role it will notify connected centrals (if subscribed)
// - for central role it will write to the peripheral's RX characteristic
int meshudp_send_datagram(const uint8_t *buf, size_t len);

// simple runtime helpers
bool meshudp_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_MESHUDP_H
