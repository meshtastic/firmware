#include "configuration.h"

#include "NodeDB.h"
#include "PortduinoGlue.h"
#include "RadioLibInterface.h"
#include "RawModem.h"
#include "Router.h"
#include "Throttle.h"
#include "UptimeClock.h"
#include "main.h"
#include <PortduinoSocketCompat.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

RawModem *rawModem;

namespace
{
// KISS type bytes (port 0). TxDelay, Persistence, SlotTime, TxTail and FullDuplex are accepted and ignored.
constexpr uint8_t KISS_DATA = 0x00;
constexpr uint8_t KISS_SETHARDWARE = 0x06;
constexpr uint8_t KISS_RETURN = 0xFF;

// SetHardware sub-commands; a reply to a query carries the command with the top bit set.
constexpr uint8_t HW_SET_RADIO = 0x09;
constexpr uint8_t HW_SET_TX_POWER = 0x0A;
constexpr uint8_t HW_GET_RADIO = 0x0B;
constexpr uint8_t HW_GET_TX_POWER = 0x0C;
constexpr uint8_t HW_IS_CHANNEL_BUSY = 0x0E;
constexpr uint8_t HW_GET_AIRTIME = 0x0F;
constexpr uint8_t HW_GET_NOISE_FLOOR = 0x10;
constexpr uint8_t HW_GET_VERSION = 0x11;
constexpr uint8_t HW_GET_STATS = 0x12;
constexpr uint8_t HW_GET_DEVICE_NAME = 0x16;
constexpr uint8_t HW_PING = 0x17;
constexpr uint8_t HW_SET_SYNC_WORD = 0x1B;
constexpr uint8_t HW_SET_PREAMBLE = 0x1C;
constexpr uint8_t HW_GET_PHY_EXTRA = 0x1D;

constexpr uint8_t HW_RESP_OK = 0xF0;
constexpr uint8_t HW_RESP_ERROR = 0xF1;
constexpr uint8_t HW_RESP_TX_DONE = 0xF8;
constexpr uint8_t HW_RESP_RX_META = 0xF9;

constexpr uint8_t HW_ERR_INVALID_LENGTH = 0x01;
constexpr uint8_t HW_ERR_INVALID_PARAM = 0x02;
constexpr uint8_t HW_ERR_UNKNOWN_CMD = 0x05;
constexpr uint8_t HW_ERR_TX_BUSY = 0x07;

constexpr uint8_t KISS_MODEM_VERSION = 2; // 2 = SetSyncWord / SetPreamble / GetPhyExtra
constexpr uint16_t MIN_PREAMBLE_SYMBOLS = 6;
constexpr uint8_t MESHTASTIC_SYNC_WORD = 0x2b;
constexpr uint16_t MESHTASTIC_PREAMBLE = 16;
constexpr size_t MAX_OUT_BUF = 16 * 1024; // a client this far behind is not reading; drop it

inline uint8_t hwResp(uint8_t cmd)
{
    return cmd | 0x80;
}

void putLE32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

uint32_t getLE32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

int8_t clampInt8(long v)
{
    return v < -128 ? -128 : (v > 127 ? 127 : v);
}
} // namespace

void initRawModem(int port)
{
    if (port <= 1023 || port >= 65536 || port == TCPPort || port == portduino_config.webserverport) {
        LOG_ERROR("Raw modem port %d must be 1024-65535 and differ from the API port %d and Webserver.Port", port, TCPPort);
        exit(EXIT_FAILURE); // continuing would put a mesh node on air where a raw modem was asked for
    }
    rawModem = new RawModem(port);
    if (!RadioLibInterface::instance)
        LOG_WARN("Raw modem: no RadioLib radio (SimRadio?), so transmit requests will report failure");
}

RawModem::RawModem(int port) : concurrency::OSThread("RawModem"), server(port)
{
    phy.power = config.lora.tx_power;
    phy.syncWord = MESHTASTIC_SYNC_WORD;
    phy.preamble = MESHTASTIC_PREAMBLE;
    server.begin();
    LOG_INFO("Raw modem mode: mesh stack detached from the radio, KISS modem on TCP port %d", port);
}

int32_t RawModem::runOnce()
{
    acceptClient();
    if (haveClient) {
        readClient();
        flushClient();
    }
    runTx();
    return (haveClient || txState != TX_IDLE) ? 5 : 100;
}

// ------------------------------------------------------------------------------------------------ TCP client

void RawModem::acceptClient()
{
    WiFiClient incoming = server.available();
    if (!incoming)
        return;
    if (haveClient) {
        LOG_INFO("Raw modem: new client replaces the current one");
        client.stop();
    } else {
        LOG_INFO("Raw modem: client connected");
    }
    client = incoming;
    haveClient = true;
    clientGen++;
    deframer.reset();
    outBuf.clear();
}

void RawModem::dropClient(const char *reason)
{
    LOG_INFO("Raw modem: client %s", reason);
    client.stop();
    haveClient = false;
    outBuf.clear();
}

void RawModem::readClient()
{
    uint8_t buf[256];
    for (int i = 0; haveClient && i < 32; i++) {
        int n = client.read(buf, sizeof(buf));
        if (n == 0) {
            dropClient("disconnected");
            return;
        }
        if (n < 0) {
            if (!portduino_socket_would_block(portduino_socket_errno()))
                dropClient("read failed");
            return;
        }
        for (int j = 0; j < n; j++)
            if (deframer.feed(buf[j]))
                processFrame();
    }
}

void RawModem::flushClient()
{
    while (haveClient && !outBuf.empty()) {
        int n = (int)client.write(outBuf.data(), outBuf.size());
        if (n > 0) {
            outBuf.erase(outBuf.begin(), outBuf.begin() + n);
        } else if (n < 0 && portduino_socket_would_block(portduino_socket_errno())) {
            break;
        } else {
            dropClient("write failed");
            return;
        }
    }
    if (outBuf.size() > MAX_OUT_BUF)
        dropClient("not reading, dropped");
}

void RawModem::writeFrame(uint8_t type, const uint8_t *data, size_t len, const uint8_t *data2, size_t len2)
{
    if (haveClient)
        kiss::encode(outBuf, type, data, len, data2, len2);
}

void RawModem::writeHardware(uint8_t sub, const uint8_t *data, size_t len)
{
    writeFrame(KISS_SETHARDWARE, &sub, 1, data, len);
}

void RawModem::writeError(uint8_t code)
{
    writeHardware(HW_RESP_ERROR, &code, 1);
}

// ---------------------------------------------------------------------------------------------------- protocol

void RawModem::processFrame()
{
    uint8_t type = deframer.buf[0];
    const uint8_t *data = deframer.buf + 1;
    size_t len = deframer.len - 1;

    switch (type) {
    case KISS_DATA:
        if (txState != TX_IDLE) {
            writeError(HW_ERR_TX_BUSY);
        } else if (len == 0 || len > sizeof(txBuf)) {
            writeError(HW_ERR_INVALID_LENGTH);
        } else {
            memcpy(txBuf, data, len);
            txLen = len;
            txGen = clientGen;
            txState = TX_WAIT_CLEAR;
            txSince = Time::getMillis();
            runTx();
        }
        break;
    case KISS_SETHARDWARE:
        if (len >= 1)
            handleHardware(data[0], data + 1, len - 1);
        break;
    case KISS_RETURN:
    default:
        break; // other ports, and the CSMA knobs: there is no p-persistence, TX just waits out a packet being received
    }
}

void RawModem::handleHardware(uint8_t cmd, const uint8_t *data, size_t len)
{
    RadioLibInterface *radio = RadioLibInterface::instance;
    RawModemPhy next = phy;
    uint16_t nextPreamble = preambleRequest;
    uint8_t buf[12];

    switch (cmd) {
    case HW_SET_RADIO:
    case HW_SET_TX_POWER:
    case HW_SET_SYNC_WORD:
    case HW_SET_PREAMBLE:
        if (len < (cmd == HW_SET_RADIO ? 10 : cmd == HW_SET_PREAMBLE ? 2 : 1)) {
            writeError(HW_ERR_INVALID_LENGTH);
            return;
        }
        if (cmd == HW_SET_RADIO) {
            next.freqHz = getLE32(data);
            next.bwHz = getLE32(data + 4);
            next.sf = data[8];
            next.cr = data[9];
            if (next.freqHz == 0 || next.bwHz == 0 || next.sf < 5 || next.sf > 12 || next.cr < 5 || next.cr > 8) {
                writeError(HW_ERR_INVALID_PARAM);
                return;
            }
        } else if (cmd == HW_SET_TX_POWER) {
            next.power = (int8_t)data[0];
        } else if (cmd == HW_SET_SYNC_WORD) {
            next.syncWord = data[0];
        } else {
            nextPreamble = data[0] | (data[1] << 8);
            if (nextPreamble != 0 && nextPreamble < MIN_PREAMBLE_SYMBOLS) {
                writeError(HW_ERR_INVALID_PARAM);
                return;
            }
        }
        if (txState == TX_SENDING) {
            writeError(HW_ERR_TX_BUSY); // reconfiguring puts the radio in standby, which would cut the frame short
            return;
        }
        if (commitPhy(next, nextPreamble, cmd == HW_SET_RADIO))
            writeHardware(HW_RESP_OK);
        else
            writeError(HW_ERR_INVALID_PARAM);
        return;

    case HW_GET_RADIO:
        putLE32(buf, phy.freqHz);
        putLE32(buf + 4, phy.bwHz);
        buf[8] = phy.sf;
        buf[9] = phy.cr;
        writeHardware(hwResp(cmd), buf, 10);
        return;
    case HW_GET_TX_POWER:
        buf[0] = (uint8_t)phy.power;
        writeHardware(hwResp(cmd), buf, 1);
        return;
    case HW_GET_PHY_EXTRA:
        buf[0] = phy.syncWord;
        buf[1] = phy.preamble & 0xff;
        buf[2] = phy.preamble >> 8;
        writeHardware(hwResp(cmd), buf, 3);
        return;
    case HW_IS_CHANNEL_BUSY:
        buf[0] = (radio && radio->isActivelyReceiving()) ? 1 : 0;
        writeHardware(hwResp(cmd), buf, 1);
        return;
    case HW_GET_AIRTIME:
        if (len < 1) {
            writeError(HW_ERR_INVALID_LENGTH);
            return;
        }
        putLE32(buf, radio ? radio->getPacketTime((uint32_t)data[0]) : 0);
        writeHardware(hwResp(cmd), buf, 4);
        return;
    case HW_GET_NOISE_FLOOR: {
        int16_t nf = radio ? radio->getNoiseFloor() : -120;
        buf[0] = (uint16_t)nf & 0xff;
        buf[1] = (uint16_t)nf >> 8;
        writeHardware(hwResp(cmd), buf, 2);
        return;
    }
    case HW_GET_VERSION:
        buf[0] = KISS_MODEM_VERSION;
        buf[1] = 0;
        writeHardware(hwResp(cmd), buf, 2);
        return;
    case HW_GET_STATS:
        putLE32(buf, rxPackets);
        putLE32(buf + 4, txPackets);
        putLE32(buf + 8, rxErrors);
        writeHardware(hwResp(cmd), buf, 12);
        return;
    case HW_GET_DEVICE_NAME: {
        std::string name = "meshtasticd " + portduino_config.loraModules[portduino_config.lora_module];
        writeHardware(hwResp(cmd), (const uint8_t *)name.data(), name.size());
        return;
    }
    case HW_PING:
        writeHardware(hwResp(cmd));
        return;
    default:
        writeError(HW_ERR_UNKNOWN_CMD);
        return;
    }
}

bool RawModem::commitPhy(RawModemPhy next, uint16_t nextPreamble, bool radioSet)
{
    // Preamble 0 selects the protocol default, which clients verify against GetPhyExtra
    next.preamble = nextPreamble ? nextPreamble : (next.sf <= 8 ? 32 : 16);

    RawModemPhy prevPhy = phy;
    uint16_t prevPreamble = preambleRequest;
    bool prevSet = phySet;
    phy = next;
    preambleRequest = nextPreamble;
    phySet = phySet || radioSet;

    // Until SetRadio has supplied a frequency the settings are only stored; they take effect with it
    RadioInterface *radio = router ? router->getRadioIface() : nullptr;
    if (!phySet || !radio || radio->reconfigure())
        return true;

    LOG_WARN("Raw modem: radio rejected the new settings, restoring the previous ones");
    phy = prevPhy;
    preambleRequest = prevPreamble;
    phySet = prevSet;
    radio->reconfigure();
    return false;
}

// ---------------------------------------------------------------------------------------------------- radio

void RawModem::onReceive(const uint8_t *frame, size_t len, float snr, float rssi)
{
    rxPackets++;
    if (!haveClient)
        return;
    writeFrame(KISS_DATA, frame, len);
    uint8_t meta[2] = {(uint8_t)clampInt8(lround(snr * 4)), (uint8_t)clampInt8(lround(rssi))};
    writeHardware(HW_RESP_RX_META, meta, sizeof(meta));
    flushClient();
}

void RawModem::runTx()
{
    RadioLibInterface *radio = RadioLibInterface::instance;
    if (txState == TX_WAIT_CLEAR) {
        if (!radio) {
            finishTx(false);
            return;
        }
        // Hold off while a packet is being received, but not forever
        if ((radio->isSending() || radio->isActivelyReceiving()) &&
            Throttle::isWithinTimespanMs(txSince, radio->getPacketTime((uint32_t)MAX_LORA_PAYLOAD_LEN) * 3 / 2))
            return;
        if (radio->startSendRaw(txBuf, txLen)) {
            txState = TX_SENDING;
            txSince = Time::getMillis();
        } else {
            finishTx(false);
        }
    } else if (txState == TX_SENDING) {
        // Normally ended by onTxDone(); this covers a lost TX-done interrupt or the radio going away mid-send
        uint32_t limit = radio ? radio->getPacketTime((uint32_t)txLen) * 3 / 2 + 1000 : 0;
        if (!Throttle::isWithinTimespanMs(txSince, limit)) {
            LOG_WARN("Raw modem: no TX done from the radio, reporting failure");
            finishTx(false);
        }
    }
}

void RawModem::onTxDone(bool transmitted)
{
    if (txState == TX_SENDING)
        finishTx(transmitted);
}

void RawModem::finishTx(bool transmitted)
{
    txState = TX_IDLE;
    if (transmitted)
        txPackets++;
    if (txGen != clientGen)
        return; // the client that sent the frame is gone; its result is not the new client's
    uint8_t result = transmitted ? 0x01 : 0x00;
    writeHardware(HW_RESP_TX_DONE, &result, 1);
    flushClient();
}
