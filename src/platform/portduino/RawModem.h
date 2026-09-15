#pragma once

#include "KissFraming.h"
#include "concurrency/OSThread.h"
#include <WiFi.h>
#include <cstddef>
#include <cstdint>
#include <vector>

/** PHY parameters requested by a raw modem client. */
struct RawModemPhy {
    uint32_t freqHz = 0;
    uint32_t bwHz = 0;
    uint8_t sf = 0;
    uint8_t cr = 0;        // 5-8, i.e. 4/5 - 4/8
    int8_t power = 0;      // dBm as requested; regional limit and PA gain still apply, see RadioInterface::limitPower()
    uint8_t syncWord = 0;  // RadioLib single-byte form
    uint16_t preamble = 0; // symbols
};

// Raw modem mode (Portduino, off unless General.RawModemPort or --raw-modem is set): the mesh stack leaves the radio and
// one TCP client drives it through the KISS modem protocol (KISS framing, SetHardware commands; GetVersion reports 2).
class RawModem : private concurrency::OSThread
{
  public:
    explicit RawModem(int port);

    /** Client-requested PHY settings, or nullptr until the client has set any (the mesh config applies until then). */
    const RawModemPhy *getPhy() const { return phySet ? &phy : nullptr; }

    /** From the radio: a frame was received intact. */
    void onReceive(const uint8_t *frame, size_t len, float snr, float rssi);

    /** From the radio: a frame was received but could not be read (e.g. CRC error). */
    void onReceiveError() { rxErrors++; }

    /** From the radio: the transmit started by RadioLibInterface::startSendRaw() has ended. */
    void onTxDone(bool transmitted);

  protected:
    int32_t runOnce() override;

  private:
    enum TxState { TX_IDLE, TX_WAIT_CLEAR, TX_SENDING };

    WiFiServer server;
    WiFiClient client;
    bool haveClient = false;

    kiss::Deframer deframer;
    std::vector<uint8_t> outBuf;

    RawModemPhy phy;              // what GetRadio / GetTxPower / GetPhyExtra report
    uint16_t preambleRequest = 0; // as sent with SetPreamble; 0 = default for the spreading factor
    bool phySet = false;          // SetRadio received, so phy is programmed into the radio

    TxState txState = TX_IDLE;
    uint8_t txBuf[255];
    size_t txLen = 0;
    uint32_t txSince = 0;
    uint32_t clientGen = 0; // bumped per accepted connection, so a TxDone reaches only the client that sent the frame
    uint32_t txGen = 0;

    uint32_t rxPackets = 0, txPackets = 0, rxErrors = 0;

    void acceptClient();
    void dropClient(const char *reason);
    void readClient();
    void flushClient();
    void processFrame();
    void handleHardware(uint8_t cmd, const uint8_t *data, size_t len);
    void runTx();
    void finishTx(bool transmitted);

    bool commitPhy(RawModemPhy next, uint16_t nextPreamble, bool radioSet);

    void writeFrame(uint8_t type, const uint8_t *data, size_t len, const uint8_t *data2 = nullptr, size_t len2 = 0);
    void writeHardware(uint8_t sub, const uint8_t *data = nullptr, size_t len = 0);
    void writeError(uint8_t code);
};

extern RawModem *rawModem;

/** Start raw modem mode on this TCP port; exits if the port is not usable. */
void initRawModem(int port);
