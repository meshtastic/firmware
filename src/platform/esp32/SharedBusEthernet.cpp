#include "SharedBusEthernet.h"

#if defined(ARCH_ESP32) && defined(USE_WS5500) && defined(ETH_SHARED_SPI)

#include "SPILock.h"
#include "concurrency/LockGuard.h"
#include <SPI.h>
#include <esp_mac.h>

#ifndef ETH_SHARED_SPI_MHZ
#define ETH_SHARED_SPI_MHZ 20
#endif

SharedBusEthernet sharedBusEthernet;

// Called from esp_eth's RX task. Lock order is spiLock -> SPI transaction, matching
// LockingArduinoHal, so the radio path cannot deadlock against this.
static void *ethSpiInit(const void *ctx)
{
    return (void *)ctx;
}

static esp_err_t ethSpiDeinit(void *)
{
    return ESP_OK;
}

static void ethSpiSelect(uint32_t cmd, uint32_t addr)
{
    ETH_SHARED_SPI.beginTransaction(SPISettings(ETH_SHARED_SPI_MHZ * 1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(ETH_CS_PIN, LOW);
    ETH_SHARED_SPI.write16(cmd);
    ETH_SHARED_SPI.write(addr);
}

static void ethSpiRelease()
{
    digitalWrite(ETH_CS_PIN, HIGH);
    ETH_SHARED_SPI.endTransaction();
}

static esp_err_t ethSpiRead(void *, uint32_t cmd, uint32_t addr, void *data, uint32_t len)
{
    concurrency::LockGuard g(spiLock);
    ethSpiSelect(cmd, addr);
    ETH_SHARED_SPI.transferBytes(nullptr, (uint8_t *)data, len);
    ethSpiRelease();
    return ESP_OK;
}

static esp_err_t ethSpiWrite(void *, uint32_t cmd, uint32_t addr, const void *data, uint32_t len)
{
    concurrency::LockGuard g(spiLock);
    ethSpiSelect(cmd, addr);
    ETH_SHARED_SPI.writeBytes((const uint8_t *)data, len);
    ethSpiRelease();
    return ESP_OK;
}

void SharedBusEthernet::onEthEvent(void *arg, esp_event_base_t, int32_t id, void *)
{
    SharedBusEthernet *self = (SharedBusEthernet *)arg;
    arduino_event_t event;
    event.event_id = ARDUINO_EVENT_MAX;

    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        event.event_id = ARDUINO_EVENT_ETH_CONNECTED;
        event.event_info.eth_connected = self->handle();
        self->setStatusBits(ESP_NETIF_CONNECTED_BIT);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        event.event_id = ARDUINO_EVENT_ETH_DISCONNECTED;
        self->clearStatusBits(ESP_NETIF_CONNECTED_BIT | ESP_NETIF_HAS_IP_BIT | ESP_NETIF_HAS_LOCAL_IP6_BIT |
                              ESP_NETIF_HAS_GLOBAL_IP6_BIT);
        break;
    case ETHERNET_EVENT_START:
        event.event_id = ARDUINO_EVENT_ETH_START;
        self->setStatusBits(ESP_NETIF_STARTED_BIT);
        break;
    case ETHERNET_EVENT_STOP:
        event.event_id = ARDUINO_EVENT_ETH_STOP;
        self->clearStatusBits(ESP_NETIF_STARTED_BIT | ESP_NETIF_CONNECTED_BIT | ESP_NETIF_HAS_IP_BIT |
                              ESP_NETIF_HAS_LOCAL_IP6_BIT | ESP_NETIF_HAS_GLOBAL_IP6_BIT | ESP_NETIF_HAS_STATIC_IP_BIT);
        break;
    default:
        return;
    }

    Network.postEvent(&event);
}

bool SharedBusEthernet::begin()
{
    if (ethHandle)
        return true;

    Network.begin();

    pinMode(ETH_CS_PIN, OUTPUT);
    digitalWrite(ETH_CS_PIN, HIGH);

    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;
    devcfg.clock_speed_hz = ETH_SHARED_SPI_MHZ * 1000 * 1000;
    devcfg.spics_io_num = ETH_CS_PIN;
    devcfg.queue_size = 20;

    // spi_host_id and devcfg go unused once custom_spi_driver is set, but the config macro wants them.
    eth_w5500_config_t w5500Config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &devcfg);
    w5500Config.int_gpio_num = ETH_INT_PIN;
    w5500Config.custom_spi_driver.config = this;
    w5500Config.custom_spi_driver.init = ethSpiInit;
    w5500Config.custom_spi_driver.deinit = ethSpiDeinit;
    w5500Config.custom_spi_driver.read = ethSpiRead;
    w5500Config.custom_spi_driver.write = ethSpiWrite;

    eth_mac_config_t macConfig = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phyConfig = ETH_PHY_DEFAULT_CONFIG();
    phyConfig.phy_addr = 1;
    phyConfig.reset_gpio_num = ETH_RST_PIN;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500Config, &macConfig);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phyConfig);
    if (!mac || !phy) {
        LOG_ERROR("W5500 MAC/PHY alloc failed");
        return false;
    }

    esp_eth_config_t ethConfig = ETH_DEFAULT_CONFIG(mac, phy);
    if (esp_eth_driver_install(&ethConfig, &ethHandle) != ESP_OK || !ethHandle) {
        LOG_ERROR("W5500 driver install failed");
        return false;
    }

    uint8_t macAddress[6];
    if (esp_read_mac(macAddress, ESP_MAC_ETH) == ESP_OK)
        esp_eth_ioctl(ethHandle, ETH_CMD_S_MAC_ADDR, macAddress);

    esp_netif_inherent_config_t netifBase = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t netifConfig = ESP_NETIF_DEFAULT_ETH();
    netifConfig.base = &netifBase;
    _esp_netif = esp_netif_new(&netifConfig);
    if (!_esp_netif) {
        LOG_ERROR("W5500 netif alloc failed");
        return false;
    }
    if (!initNetif(ESP_NETIF_ID_ETH)) {
        LOG_ERROR("W5500 netif init failed");
        return false;
    }

    glueHandle = esp_eth_new_netif_glue(ethHandle);
    if (!glueHandle || esp_netif_attach(_esp_netif, glueHandle) != ESP_OK) {
        LOG_ERROR("W5500 netif attach failed");
        return false;
    }

    // Registered before start so the START event is not missed.
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, onEthEvent, this);

    if (esp_eth_start(ethHandle) != ESP_OK) {
        LOG_ERROR("W5500 start failed");
        return false;
    }

    LOG_INFO("W5500 on shared SPI bus, %u MHz", (unsigned)ETH_SHARED_SPI_MHZ);
    return true;
}

uint16_t SharedBusEthernet::linkSpeed() const
{
    eth_speed_t speed = ETH_SPEED_100M;
    if (ethHandle)
        esp_eth_ioctl(ethHandle, ETH_CMD_G_SPEED, &speed);
    return speed == ETH_SPEED_10M ? 10 : 100;
}

bool SharedBusEthernet::fullDuplex() const
{
    eth_duplex_t duplex = ETH_DUPLEX_FULL;
    if (ethHandle)
        esp_eth_ioctl(ethHandle, ETH_CMD_G_DUPLEX_MODE, &duplex);
    return duplex == ETH_DUPLEX_FULL;
}

size_t SharedBusEthernet::printDriverInfo(Print &out) const
{
    return out.print(",W5500");
}

#endif
