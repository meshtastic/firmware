#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(USE_WS5500) && defined(ETH_SHARED_SPI)

#include <ETH.h>
#include <esp_eth_driver.h>

/**
 * W5500 driver for boards where the MAC shares its SPI bus with other peripherals.
 *
 * Arduino's ETHClass reaches SPI through SPIClass, whose mutex is invisible to LovyanGFX and to
 * anything else driving the peripheral registers directly, so a display sharing the bus corrupts
 * the MAC's frame-header reads. This installs esp_eth itself so the SPI callbacks can take
 * spiLock - the one mutex the radio, display, SD and sensors all already honour.
 *
 * NetworkInterface supplies localIP()/connected()/config() and the ARDUINO_EVENT_ETH_GOT_IP
 * plumbing; only the link-state events need forwarding by hand.
 */
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

    esp_eth_handle_t ethHandle = nullptr;
    esp_eth_netif_glue_handle_t glueHandle = nullptr;
};

extern SharedBusEthernet sharedBusEthernet;

// Route the firmware's ETH.* calls at this driver instead of Arduino's global, matching how
// USE_CH390D swaps in its own class.
#define ETH sharedBusEthernet

#endif
