#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_WS5500) && defined(ETH_SHARED_SPI)

#include <ETH.h>
#include <esp_eth_driver.h>

// W5500 driver for boards whose MAC shares its SPI bus. Installs esp_eth directly so the SPI
// callbacks can take spiLock, which Arduino's ETHClass cannot.
class SharedBusEthernet : public NetworkInterface
{
  public:
    bool begin();
    uint16_t linkSpeed() const;
    bool fullDuplex() const;
    esp_eth_handle_t handle() const { return ethHandle; }

  protected:
    size_t printDriverInfo(Print &out) const override;

  private:
    static void onEthEvent(void *arg, esp_event_base_t base, int32_t id, void *data);
    void teardown();

    esp_eth_handle_t ethHandle = nullptr;
    esp_eth_netif_glue_handle_t glueHandle = nullptr;
    esp_eth_mac_t *ethMac = nullptr;
    esp_eth_phy_t *ethPhy = nullptr;
    bool eventRegistered = false;
};

extern SharedBusEthernet sharedBusEthernet;

// Route the firmware's ETH.* calls at this driver instead of Arduino's global, matching how
// USE_CH390D swaps in its own class.
#define ETH sharedBusEthernet

#endif
