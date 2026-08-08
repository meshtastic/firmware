#pragma once

#include "configuration.h"

#if HAS_CELLULAR && defined(USE_EC618)

#include "mesh/cell/ATModem.h"

// EigenComm EC618 platform driver (Air780E/Air780EG/Air700E/Air600E): the SIMCom-originated
// bearer and socket dialect the platform clones, plus EC618's own AT* extensions.

// Set when the board wires the module's USIM_CD pin to a SIM detect circuit, so insertion
// and removal report as +CPIN URCs and a card inserted after boot is noticed without a reset.
#ifndef MODEM_SIM_HOTPLUG
#define MODEM_SIM_HOTPLUG 0
#endif

// AT+CSDT <level>: 0 falling edge (card present pulls low), 1 rising (default).
#ifndef MODEM_SIM_HOTPLUG_EDGE
#define MODEM_SIM_HOTPLUG_EDGE 1
#endif

class EC618Modem : public ATModem
{
  public:
    void resetModule(ATCallback cb) override;
    bool powerDown(ATCallback cb) override;
    bool querySimDetect(ATCallback cb) override;
    bool hasSimHotplug() const override { return MODEM_SIM_HOTPLUG; }
    bool queryBand(ATBandCallback cb) override;
    bool queryBatteryMv(ATBatteryCallback cb) override;

    void bringUpBearer(BearerStep step, ATCallback cb) override;

    void socketAttach(const ATSocketEvents &events) override;
    void socketRxMode(ATCallback cb) override;
    void socketOpen(const char *host, uint16_t port, ATCallback cb) override;
    void socketSend(const uint8_t *buf, size_t len, ATCallback cb) override;
    void socketRecv(size_t want, uint32_t timeoutMs, ATCallback cb) override;
    void socketClose(ATCallback cb) override;

    // Largest AT+CIPSEND payload accepted in one command.
    size_t socketSendMax() const override { return 1024; }

    // Largest AT+CIPRXGET=2 request; the modem caps this at one TCP segment.
    size_t socketRecvMax() const override { return 1460; }

    // CIPSTART is slow to settle on a cold connection.
    uint32_t socketConnectTimeoutMs() const override { return 75000; }

  protected:
    void sessionInit() override;
    void onReady() override;
    void queryVersion() override;

  private:
    void onCipRxGet(const String &line);
    void onUnexpectedReboot();

    String bootReason;
    ATSocketEvents socketEvents;
};

#endif
