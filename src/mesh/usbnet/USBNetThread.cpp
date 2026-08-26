#include "USBNet.h"

#if HAS_USB_NET

#include "USBNetPolicy.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/api/WiFiServerAPI.h"
#include <Arduino.h>

#ifndef USB_NET_START_DELAY_MS
/**
 * How long to stay off the USB bus after boot.
 *
 * On a native-USB S3 the OTG controller and USB-Serial-JTAG share the same
 * D+/D- pads, so once the gadget starts, the USJ device disappears until the
 * next reset. Holding off for a few seconds leaves a window in which the board
 * still enumerates as USJ, which is the difference between "flash it normally"
 * and "hold BOOT every single time" during development.
 *
 * Recovery does not depend on this: entering ROM download mode with BOOT hands
 * the pads back regardless. It is a convenience, not the safety net.
 */
#define USB_NET_START_DELAY_MS 8000
#endif

namespace
{

class USBNetThread : public concurrency::OSThread
{
  public:
    USBNetThread() : concurrency::OSThread("USBNet") {}

    /// Stop the gadget and park the thread.
    void shutdown()
    {
        if (state == State::Running) {
            LOG_INFO("USBNet: shutting down");
            usbNetDropLink();
        }
        state = State::Stopped;
        enabled = false;
    }

  protected:
    int32_t runOnce() override;

  private:
    enum class State {
        WaitingForStartDelay, // holding the bus so USB-Serial-JTAG stays usable
        Starting,             // netif + DHCP + API server + TinyUSB bring-up
        WaitingForHost,       // enumerated? only then may the link go up
        Running,
        Stopped,
    };

    State state = State::WaitingForStartDelay;
};

int32_t USBNetThread::runOnce()
{
    switch (state) {
    case State::WaitingForStartDelay:
        if (millis() < USB_NET_START_DELAY_MS)
            return 250;

        LOG_INFO("USBNet: start delay elapsed, bringing up CDC-NCM gadget");
        state = State::Starting;
        return 0;

    case State::Starting:
        // The netif, DHCP server and phone API are already live - initUsbNet()
        // did them in setup(). All that is left is the USB device itself, and
        // then the link, which must not be announced until everything a host
        // might reach is answering: iOS runs DHCP exactly once when the NCM link
        // comes up and never retries.
        if (!usbNetStartDevice()) {
            LOG_ERROR("USBNet: USB device start failed, giving up");
            state = State::Stopped;
            return 0;
        }

        state = State::WaitingForHost;
        return 100;

    case State::WaitingForHost:
        // The link may only go up once the host has actually enumerated us,
        // otherwise the one DHCP attempt lands before anything can answer it.
        if (!usbNetIsMounted())
            return 100;

        usbNetRaiseLink();
        state = State::Running;
        return 1000;

    case State::Running:
        // A replug or an iPad sleep/wake drops the mount. Take the link back
        // down so the next mount re-raises it and the host re-runs DHCP.
        if (!usbNetIsMounted()) {
            LOG_INFO("USBNet: host went away, dropping link");
            usbNetDropLink();
            state = State::WaitingForHost;
            return 100;
        }
        return 1000;

    case State::Stopped:
    default:
        return disable();
    }
}

USBNetThread *usbNetThread = nullptr;

} // namespace

void initUsbNet()
{
    if (usbNetThread)
        return; // idempotent, mirroring initApiServer()

    // Bring the network up here in setup(), and leave only the USB device to the
    // thread. Both halves of that split are load-bearing:
    //
    //  * The netif must exist before initApiServer(). On a WiFi-less build
    //    nothing else initializes lwIP, and WiFiServer::begin() then faults on a
    //    null stack context ("Not using WIFI" immediately followed by a
    //    LoadProhibited panic at EXCVADDR 0x4c).
    //  * initApiServer() must not be called from inside a runOnce(). APIServerPort
    //    is itself an OSThread, and registering one while the scheduler is
    //    mid-iteration left the socket listening in lwIP but never accepted -
    //    clients connected and then hung with no reply.
    if (!usbNetBringUpNetwork()) {
        LOG_ERROR("USBNet: network bring-up failed, gadget disabled");
        return;
    }

    // Idempotent (see initApiServer() in mesh/api/WiFiServerAPI.cpp), so it is a
    // no-op if WiFi already started it. Called directly rather than through
    // onNetworkConnected(), which suppresses the API server on colour-TFT builds
    // - exactly the boards someone cables to an iPad.
    initApiServer();

    usbNetThread = new USBNetThread();

    // Thread headroom matters here and is easy to run out of without noticing.
    // ServerAPI is an OSThread created PER CONNECTION, and OSThread's constructor
    // does assert(controller->add(this)) - so once ThreadController's fixed
    // Thread*[MAX_THREADS] array is full, the next client to connect panics the
    // node instead of failing gracefully. This build adds two threads (USBNet and
    // ApiServer) on top of an already-busy variant, so log the margin.
    const int used = concurrency::mainController.size();
    LOG_INFO("USBNet: network + API server up, gadget starting in %ums (%d/%d threads used)", (unsigned)USB_NET_START_DELAY_MS,
             used, (int)MAX_THREADS);
    if (used >= MAX_THREADS - 1)
        LOG_ERROR("USBNet: only %d thread slot(s) left - an incoming TCP client will panic the node", MAX_THREADS - used);
}

void deInitUsbNet()
{
    if (usbNetThread)
        usbNetThread->shutdown();
}

#endif // HAS_USB_NET
