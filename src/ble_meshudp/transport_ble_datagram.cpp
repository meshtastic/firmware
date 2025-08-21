/*
 C++ shim (example) showing how you might hook the above C API into Meshtastic's transport interface.
*/

#include "ble_meshudp.h"
#include <cstring>
#include <cstdint>
#include <cstdio>

// forward declaration of Meshtastic receive handler (replace with actual function in Meshtastic)
extern "C" void meshtastic_receive_datagram_from_transport(const uint8_t *buf, size_t len);

static void my_meshudp_rx(void *ctx, const uint8_t *buf, size_t len) {
  // Called when a datagram arrives over BLE. Pass it into Meshtastic's receive path.
  if (buf == nullptr || len == 0) return;
  meshtastic_receive_datagram_from_transport(buf, len);
}

extern "C" void transport_ble_datagram_init(bool start_as_central) {
  meshudp_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.role = start_as_central ? MESHUDP_ROLE_DUAL : MESHUDP_ROLE_PERIPHERAL;
  cfg.rx_cb = my_meshudp_rx;
  cfg.ctx = nullptr;
  cfg.adv_name = "Meshtastic-BLE";

  int rc = meshudp_init(&cfg);
  if (rc != 0) {
    // log error
  }

  if (cfg.role == MESHUDP_ROLE_PERIPHERAL || cfg.role == MESHUDP_ROLE_DUAL) {
    meshudp_start_advertising();
  }
  if (cfg.role == MESHUDP_ROLE_CENTRAL || cfg.role == MESHUDP_ROLE_DUAL) {
    meshudp_start_central_scan();
  }
}

extern "C" int transport_ble_datagram_send(const uint8_t *buf, size_t len) {
  return meshudp_send_datagram(buf, len);
}

/*
Integration steps:

1) Copy these three files into your fork under a new directory, e.g. src/ble_meshudp/
2) Add the files to the PlatformIO build for the nrf52_promicro_diy environment if needed (platformio typically picks up src/ tree automatically).
3) Add a transport registration in Meshtastic's transport manager so the BLE transport can be selected.
   For a simple test you can call transport_ble_datagram_init(true) during boot in main() for the node that
   should be the central, and transport_ble_datagram_init(false) on the peripheral node.
4) Build and flash both nodes. Use nRF Connect app to verify advertising and connection. Use Meshtastic logs
   (serial) to confirm datagrams are forwarded into Meshtastic's message handling.
*/
