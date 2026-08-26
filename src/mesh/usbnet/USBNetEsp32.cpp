#include "USBNet.h"

#if HAS_USB_NET && defined(ARCH_ESP32)

#include <soc/soc_caps.h>

// Hardware eligibility, enforced here so a variant that sets HAS_USB_NET=1
// without the prerequisites fails at compile time with an explanation instead
// of misbehaving on hardware (see variants/esp32/usbnet.ini for the adoption
// recipe).
#if !SOC_USB_OTG_SUPPORTED
#error                                                                                                                           \
    "HAS_USB_NET requires a native USB-OTG SoC (ESP32-S2/S3/P4). C3/C6/H2 only have USB-Serial-JTAG and cannot present a USB device."
#endif
#ifndef BOARD_HAS_PSRAM
#error                                                                                                                           \
    "HAS_USB_NET requires PSRAM (-D BOARD_HAS_PSRAM). Without PSRAM in the heap the first phone-API session's ~15KB contiguous allocation aborts the node with std::bad_alloc."
#endif

#include "USBNetPolicy.h"
#include "configuration.h"
#include "mesh/api/WiFiServerAPI.h"

#include <Arduino.h>
#include <dhcpserver/dhcpserver.h> // dhcps_lease_t for the ESP_NETIF_REQUESTED_IP_ADDRESS option
#include <esp_event.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <string.h>

#include "class/net/net_device.h" // tud_network_link_state / _default_link_state_cb
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_net.h"

// The app and the esp_tinyusb component must agree on the TinyUSB config. If the
// include path ever resolves tusb_config.h to a different copy - the Arduino
// core ships its own arduino_tinyusb tree, where CFG_TUD_NCM derives from a
// Kconfig symbol this build does not define - the structs and callbacks here
// would silently disagree with the component we link against. Fail the build.
#if !defined(CFG_TUD_NCM) || CFG_TUD_NCM != 1
#error                                                                                                                           \
    "CFG_TUD_NCM is not 1 in the app build: tusb_config.h resolved to the wrong TinyUSB copy (check the -I order in the variant ini)."
#endif

namespace
{

esp_netif_t *usbNetif = nullptr;
bool linkIsUp = false;

/// esp_netif expects a driver handle whose first member is an esp_netif_driver_base_t.
struct UsbNetDriver {
    esp_netif_driver_base_t base;
};

UsbNetDriver usbDriver = {};

esp_netif_ip_info_t makeIpInfo()
{
    esp_netif_ip_info_t info = {};
    const usbnet::Ipv4 &ip = usbnet::NODE_IP;
    const usbnet::Ipv4 &mask = usbnet::NETMASK;

    IP4_ADDR(&info.ip, ip.o[0], ip.o[1], ip.o[2], ip.o[3]);
    IP4_ADDR(&info.netmask, mask.o[0], mask.o[1], mask.o[2], mask.o[3]);

    // Deliberately 0.0.0.0. dhcpserver.c only emits DHCP option 3 (Router) when
    // the offer flag is set AND the netif gateway is non-zero, so leaving this
    // clear is what keeps the USB link from becoming the host's default route.
    IP4_ADDR(&info.gw, 0, 0, 0, 0);
    return info;
}

/**
 * lwIP wants to put a frame on the wire.
 *
 * Uses the synchronous send: it blocks until TinyUSB has taken the buffer, which
 * matches lwIP's expectation that the buffer is dead once transmit() returns.
 * The async variant would need the buffer to outlive this call. The wrapper
 * already gates on tud_network_can_xmit() internally, so there is no need to
 * check the link here.
 */
esp_err_t usbNetTransmit(void *h, void *buffer, size_t len)
{
    (void)h;

    // With CONFIG_LWIP_TCPIP_CORE_LOCKING=y this runs inline on whichever task
    // called into lwIP - often the Arduino loop task - holding the lwIP core
    // lock. tinyusb_net_send_sync() ultimately enqueues onto TinyUSB's 16-entry
    // event queue with an infinite wait, so a stalled or busy USB task would
    // block the caller indefinitely rather than time out. Refuse early when the
    // device cannot take the frame, and keep the wait short: dropping a frame is
    // free here because TCP retransmits, whereas blocking the loop task is not.
    if (!tud_mounted() || !tud_network_can_xmit((uint16_t)len))
        return ESP_ERR_INVALID_STATE;

    return tinyusb_net_send_sync(buffer, (uint16_t)len, nullptr, pdMS_TO_TICKS(20));
}

void usbNetFreeRxBuffer(void *h, void *buffer)
{
    (void)h;
    free(buffer);
}

/**
 * A frame arrived from the host.
 *
 * `buffer` belongs to TinyUSB and is only valid for the duration of this call -
 * esp_tinyusb's tud_network_recv_cb() calls tud_network_recv_renew() the moment
 * we return, which hands the buffer straight back to the USB stack. So copy
 * before handing off; esp_netif_receive() takes ownership of the copy and
 * releases it through driver_free_rx_buffer above.
 *
 * (Note this is why warthog's "tud_network_recv_cb must return false" rule does
 * not apply here: that callback belongs to esp_tinyusb, and it does the renew
 * itself rather than deferring it to us.)
 */
esp_err_t usbNetRxCb(void *buffer, uint16_t len, void *ctx)
{
    (void)ctx;
    if (!usbNetif)
        return ESP_OK; // not up yet, drop

    void *copy = malloc(len);
    if (!copy) {
        LOG_WARN("USBNet: dropped %u byte frame, out of memory", (unsigned)len);
        return ESP_ERR_NO_MEM;
    }

    memcpy(copy, buffer, len);
    return esp_netif_receive(usbNetif, copy, len, nullptr);
}

esp_err_t usbNetPostAttach(esp_netif_t *netif, void *args)
{
    UsbNetDriver *driver = static_cast<UsbNetDriver *>(args);
    driver->base.netif = netif;

    esp_netif_driver_ifconfig_t ifconfig = {
        .handle = driver,
        .transmit = usbNetTransmit,
        .transmit_wrap = nullptr,
        .driver_free_rx_buffer = usbNetFreeRxBuffer,
    };

    return esp_netif_set_driver_config(netif, &ifconfig);
}

} // namespace

/**
 * Start with the NCM link reported DOWN.
 *
 * TinyUSB calls this weak hook from netd_init() to decide the initial state.
 * iOS starts DHCP exactly once, when it sees the link come up, and never
 * retries - so the link must not be announced until the DHCP server and the
 * phone API are genuinely accepting. usbNetRaiseLink() does that afterwards.
 */
extern "C" bool tud_network_default_link_state_cb(void)
{
    return false;
}

bool usbNetBringUpNetwork()
{
    if (usbNetif)
        return true;

    // Both are idempotent; WiFi may already have run them.
    esp_netif_init();
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        LOG_ERROR("USBNet: event loop init failed (%d)", (int)err);
        return false;
    }

    esp_netif_ip_info_t ipInfo = makeIpInfo();

    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.if_key = "USB";
    base.if_desc = "usbnet";
    base.route_prio = 10; // below WiFi/Ethernet: this link never carries internet
    base.ip_info = &ipInfo;
    base.flags = (esp_netif_flags_t)((base.flags & ~ESP_NETIF_DHCP_CLIENT) | ESP_NETIF_DHCP_SERVER);

    esp_netif_config_t cfg = {
        .base = &base,
        .driver = nullptr,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    usbNetif = esp_netif_new(&cfg);
    if (!usbNetif) {
        LOG_ERROR("USBNet: esp_netif_new failed");
        return false;
    }

    usbDriver.base.post_attach = usbNetPostAttach;
    if (esp_netif_attach(usbNetif, &usbDriver) != ESP_OK) {
        LOG_ERROR("USBNet: esp_netif_attach failed");
        esp_netif_destroy(usbNetif);
        usbNetif = nullptr;
        return false;
    }

    // DHCP server. Stop first: esp_netif_set_ip_info() is rejected while the
    // server is running.
    esp_netif_dhcps_stop(usbNetif);
    if (esp_netif_set_ip_info(usbNetif, &ipInfo) != ESP_OK) {
        LOG_ERROR("USBNet: set_ip_info failed");
        esp_netif_destroy(usbNetif);
        usbNetif = nullptr;
        return false;
    }

    // Make USBNetPolicy.h authoritative for what dhcps hands out - pool range,
    // lease time and the router-offer flag all come from the policy header, so
    // the native unit tests and the device cannot drift apart. All three are
    // deliberately non-fatal: dhcps defaults still produce a working (if less
    // tidy) server, and aborting here would mean no USB device at all with no
    // console to explain why.
    // Must be set before action_start: dhcps reads its options when it is
    // started, not per-request.
    dhcps_lease_t pool = {};
    pool.enable = true;
    IP4_ADDR(&pool.start_ip, usbnet::POOL_FIRST.o[0], usbnet::POOL_FIRST.o[1], usbnet::POOL_FIRST.o[2], usbnet::POOL_FIRST.o[3]);
    IP4_ADDR(&pool.end_ip, usbnet::POOL_FIRST.o[0], usbnet::POOL_FIRST.o[1], usbnet::POOL_FIRST.o[2],
             usbnet::POOL_FIRST.o[3] + usbnet::POOL_SIZE - 1);
    if (esp_netif_dhcps_option(usbNetif, ESP_NETIF_OP_SET, ESP_NETIF_REQUESTED_IP_ADDRESS, &pool, sizeof(pool)) != ESP_OK)
        LOG_WARN("USBNet: could not set DHCP pool - dhcps defaults apply");

    uint32_t leaseMinutes = usbnet::LEASE_SECONDS / 60;
    if (esp_netif_dhcps_option(usbNetif, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME, &leaseMinutes,
                               sizeof(leaseMinutes)) != ESP_OK)
        LOG_WARN("USBNet: could not set DHCP lease time - dhcps default applies");

    // Belt and braces alongside the zero gateway: keep the router offer flag in
    // sync with the policy (false) so option 3 cannot be emitted even if the
    // gateway is ever set by accident. Option 6 (DNS) is suppressed at build
    // time - see CONFIG_LWIP_DHCPS_ADD_DNS in variants/esp32/usbnet.ini, which
    // would otherwise vend our own address as a DNS server that answers nothing.
    uint8_t offerRouter = usbnet::DHCP_OFFER_POLICY.vendRouter ? 1 : 0;
    if (esp_netif_dhcps_option(usbNetif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &offerRouter,
                               sizeof(offerRouter)) != ESP_OK)
        LOG_WARN("USBNet: could not clear the router-offer flag (gateway stays 0.0.0.0, so option 3 is still not emitted)");

    // Creating and attaching a netif is not enough - the underlying lwIP netif
    // stays down until esp_netif_start() runs, and a down netif silently answers
    // nothing, so the host's DHCP times out and it self-assigns a 169.254
    // address. esp_netif_action_start() is what triggers that (it consults
    // ESP_NETIF_FLAG_AUTOUP), and it also auto-starts the DHCP server for
    // ESP_NETIF_DHCP_SERVER netifs - hence no explicit dhcps_start here.
    // action_connected() then brings the interface up.
    esp_netif_action_start(usbNetif, nullptr, 0, nullptr);
    esp_netif_action_connected(usbNetif, nullptr, 0, nullptr);

    // action_start auto-starts dhcps for DHCP_SERVER netifs, but not on every
    // path - notably after an explicit dhcps_stop() like the one above. Start it
    // by hand if it did not come up.
    //
    // Deliberately not fatal: a node that enumerates but hands out no address is
    // debuggable, whereas aborting here means no USB device at all and no way to
    // see why, since the gadget owns the only console this board has.
    esp_netif_dhcp_status_t status;
    if (esp_netif_dhcps_get_status(usbNetif, &status) != ESP_OK || status != ESP_NETIF_DHCP_STARTED) {
        esp_err_t err = esp_netif_dhcps_start(usbNetif);
        if (err != ESP_OK)
            LOG_ERROR("USBNet: DHCP server not running and dhcps_start failed (%d) - host will self-assign", (int)err);
        else
            LOG_INFO("USBNet: DHCP server started explicitly");
    }

    LOG_INFO("USBNet: netif up at %u.%u.%u.%u, DHCP pool from %u.%u.%u.%u, no router/DNS vended", usbnet::NODE_IP.o[0],
             usbnet::NODE_IP.o[1], usbnet::NODE_IP.o[2], usbnet::NODE_IP.o[3], usbnet::POOL_FIRST.o[0], usbnet::POOL_FIRST.o[1],
             usbnet::POOL_FIRST.o[2], usbnet::POOL_FIRST.o[3]);
    return true;
}

bool usbNetStartDevice()
{
    uint8_t baseMac[6] = {};
    if (esp_read_mac(baseMac, ESP_MAC_ETH) != ESP_OK) {
        LOG_ERROR("USBNet: could not read base MAC");
        return false;
    }

    uint8_t mac[6];
    usbnet::deriveUsbMac(baseMac, mac);

    // Stock esp_tinyusb descriptors. With CONFIG_TINYUSB_NET_MODE_NCM the
    // component already supplies a valid CDC-NCM configuration descriptor and
    // builds the iMACAddress string (12 bare hex chars, as the class spec
    // requires) from the MAC below - so unlike warthog's ECM work there is no
    // hand-written descriptor to get wrong here.
    // TODO before iPad testing: the stock config descriptor declares bus-powered
    // at 100 mA. Our boards are self-powered, and Apple's accessory guidance
    // prefers self-powered <= 50 mA. 100 mA is inside the documented safe zone,
    // so this is a refinement rather than a blocker.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    // `Serial` is HWCDC on USB-Serial-JTAG, and we are about to take the USB pads
    // away from it. Every later log write then waits on a host that no longer
    // exists. The core bounds that wait rather than hanging outright, but at up
    // to ~20 retries x tx_timeout_ms it can still stall the Arduino loop task -
    // the task that runs the phone API's accept loop and the whole mesh stack.
    // Drop console output instead of blocking on it.
    Serial.setTxTimeoutMs(0);
#endif

    // Must use TINYUSB_DEFAULT_CONFIG(): a zero-initialized tinyusb_config_t
    // leaves the USB task stack size and priority at 0, and the driver rejects
    // it outright ("tinyusb_task_check_config: Task size can't be 0"). The macro
    // also picks a real core rather than tskNO_AFFINITY, which esp_tinyusb
    // likewise refuses.
    tinyusb_config_t tusbCfg = TINYUSB_DEFAULT_CONFIG();

    // Pin the USB task to core 0, away from the Arduino loop task.
    //
    // esp_tinyusb 2.x moved task configuration out of Kconfig and into this
    // struct, so CONFIG_TINYUSB_TASK_STACK_SIZE / CONFIG_TINYUSB_TASK_AFFINITY_CPU0
    // no longer exist and are silently ignored if set in custom_sdkconfig.
    // The defaults are xCoreID = 1 (tinyusb_default_config.h:74, because
    // CONFIG_FREERTOS_UNICORE is unset) at priority 5 - the same core as the
    // Arduino loop task, which runs at priority 1 with CONFIG_ARDUINO_RUNNING_CORE=1.
    // Under strict-priority FreeRTOS scheduling any sustained USB traffic then
    // starves the loop task, taking the API accept loop, PhoneAPI and the whole
    // mesh stack with it until the task watchdog reboots the node. Idle traffic
    // hides it, which is why the node looks healthy until a client connects.
    //
    // 4096 is also thin for the RX path (usbNetRxCb -> malloc -> memcpy ->
    // esp_netif_receive -> tcpip_input).
    tusbCfg.task = TINYUSB_TASK_CUSTOM(6144, 5, 0);

    if (tinyusb_driver_install(&tusbCfg) != ESP_OK) {
        LOG_ERROR("USBNet: tinyusb_driver_install failed");
        return false;
    }

    tinyusb_net_config_t netCfg = {};
    memcpy(netCfg.mac_addr, mac, sizeof(mac));
    netCfg.on_recv_callback = usbNetRxCb;

    if (tinyusb_net_init(&netCfg) != ESP_OK) {
        LOG_ERROR("USBNet: tinyusb_net_init failed");
        return false;
    }

    LOG_INFO("USBNet: CDC-NCM device started, MAC %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

void usbNetRaiseLink()
{
    if (linkIsUp)
        return;

    tud_network_link_state(0, true);
    linkIsUp = true;
    LOG_INFO("USBNet: NCM link up, host may now DHCP");
}

void usbNetDropLink()
{
    if (!linkIsUp)
        return;

    tud_network_link_state(0, false);
    linkIsUp = false;
}

bool usbNetIsMounted()
{
    return tud_mounted();
}

#endif // HAS_USB_NET && ARCH_ESP32
