// Tests for the XModem file-transfer adapter (src/xmodem.cpp).
//
// Group 1: XModemAdapter::isValidFilename - the path-traversal guard on the XModem file-transfer
// handler. The filename in a SOH/STX control frame is attacker-controlled and drives FSCom
// open/remove; on the Portduino daemon FSCom is the host filesystem, so a ".." component could
// escape the mountpoint. Absolute/subdirectory paths must still be accepted.
//
// Group 2 onward: the handlePacket() state machine itself - session start, per-packet seq + CRC
// validation, NAK/retransmit, CAN cleanup, EOT close, and the getForPhone()/resetForPhone()
// contract PhoneAPI uses to drain replies. PhoneAPI feeds handlePacket attacker-controllable
// ToRadio protobufs, and none of this had pinning coverage. These tests assert what the code does
// today; the two tests marked "documents current behaviour" pin known state-confusion edges so a
// deliberate fix has to update them consciously.
#include "TestUtil.h"
#include "xmodem.h"
#include <unity.h>

#ifdef FSCom

#include "SPILock.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

void test_xmodem_rejects_dotdot_traversal(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename(".."));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("../secret"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("/prefs/../../etc/passwd"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("a/../b"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir/.."));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("/.."));
}

void test_xmodem_rejects_backslash_traversal(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("..\\secret"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("..\\..\\Windows\\System32\\drivers\\etc\\hosts"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir\\..\\..\\x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir/..\\x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir\\.."));
}

void test_xmodem_rejects_drive_qualified(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("C:\\Windows\\System32\\x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("C:/Windows/System32/x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("c:relative.txt"));
}

void test_xmodem_rejects_empty(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename(""));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename(nullptr));
}

void test_xmodem_allows_legit_paths(void)
{
    // The file manager transfers absolute, subdirectoried paths from the manifest.
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("/prefs/config.proto"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("firmware.bin"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("dir/sub/file.txt"));
    // ".." only inside a name (not a whole component) is a valid filename, not traversal.
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("my..file"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("..."));
    // A colon that cannot form a drive qualifier is a legal filename on the POSIX daemon.
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("1:30pm.txt"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("dir/1:30pm.txt"));
}

// --- handlePacket state-machine fixture ---

// Exposes the protected CRC helpers so crafted packets carry the exact checksum the adapter
// computes, and so the transmit-side crc16 field can be cross-checked.
class XModemTestShim : public XModemAdapter
{
  public:
    using XModemAdapter::check;
    using XModemAdapter::crc16_ccitt;
};

static XModemTestShim *xm = nullptr;

static constexpr size_t kChunk = sizeof(meshtastic_XModem_buffer_t::bytes); // 128
static const char *kRxPath = "/xmodem_test_rx.bin";
static const char *kTxPath = "/xmodem_test_tx.bin";

// Control-only frame (EOT/ACK/NAK/CAN).
static meshtastic_XModem makeControl(meshtastic_XModem_Control control)
{
    meshtastic_XModem p = meshtastic_XModem_init_zero;
    p.control = control;
    return p;
}

// Session-start frame: seq 0, filename in the buffer (NUL included, as the phone sends it).
static meshtastic_XModem makeStart(meshtastic_XModem_Control control, const char *path)
{
    meshtastic_XModem p = meshtastic_XModem_init_zero;
    p.control = control;
    p.seq = 0;
    p.buffer.size = strlen(path) + 1;
    memcpy(p.buffer.bytes, path, p.buffer.size);
    return p;
}

// Data frame with a correct (or deliberately corrupted) CRC.
static meshtastic_XModem makeData(uint16_t seq, const uint8_t *data, size_t len, bool goodCrc = true)
{
    meshtastic_XModem p = meshtastic_XModem_init_zero;
    p.control = meshtastic_XModem_Control_SOH;
    p.seq = seq;
    p.buffer.size = len;
    memcpy(p.buffer.bytes, data, len);
    p.crc16 = xm->crc16_ccitt(p.buffer.bytes, (int)len);
    if (!goodCrc)
        p.crc16 ^= 0x1;
    return p;
}

static void fillPattern(uint8_t *buf, size_t len, uint8_t seed)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(seed + i * 7);
}

static void writeAll(const char *path, const uint8_t *data, size_t len)
{
    File f = FSCom.open(path, FILE_O_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(f, path);
    TEST_ASSERT_EQUAL_size_t(len, f.write(data, len));
    f.close();
}

static size_t readAll(const char *path, uint8_t *buf, size_t maxLen)
{
    File f = FSCom.open(path, FILE_O_READ);
    TEST_ASSERT_TRUE_MESSAGE(f, path);
    size_t n = f.read(buf, maxLen);
    f.close();
    return n;
}

// Starts a receive session into kRxPath and asserts the adapter accepted it.
static void startReceive(void)
{
    xm->handlePacket(makeStart(meshtastic_XModem_Control_SOH, kRxPath));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);
    TEST_ASSERT_TRUE(xm->isBusy());
}

// Writes a patterned file at kTxPath and starts a transmit session; returns the first outbound
// packet after asserting its shape.
static meshtastic_XModem startTransmit(const uint8_t *payload, size_t len)
{
    writeAll(kTxPath, payload, len);
    xm->handlePacket(makeStart(meshtastic_XModem_Control_STX, kTxPath));
    meshtastic_XModem out = xm->getForPhone();
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_SOH, out.control);
    TEST_ASSERT_EQUAL_UINT16(1, out.seq);
    TEST_ASSERT_TRUE(xm->isBusy());
    return out;
}

// --- CRC ---

void test_xmodem_crc16_known_answer(void)
{
    // CRC-16/XMODEM check value: crc("123456789") == 0x31C3, and the zero-length CRC is 0.
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x31C3, xm->crc16_ccitt(check, sizeof(check)));
    TEST_ASSERT_EQUAL_HEX16(0x0000, xm->crc16_ccitt(check, 0));
    TEST_ASSERT_TRUE(xm->check(check, sizeof(check), 0x31C3));
    TEST_ASSERT_FALSE(xm->check(check, sizeof(check), 0x31C2));
}

// --- Receive path ---

void test_xmodem_receive_happy_path(void)
{
    uint8_t payload[300];
    fillPattern(payload, sizeof(payload), 31);

    startReceive();

    size_t off = 0;
    uint16_t seq = 1;
    while (off < sizeof(payload)) {
        const size_t chunk = std::min(kChunk, sizeof(payload) - off);
        xm->handlePacket(makeData(seq, payload + off, chunk));
        TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);
        off += chunk;
        seq++;
    }

    xm->handlePacket(makeControl(meshtastic_XModem_Control_EOT));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());

    uint8_t readBack[400];
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), readAll(kRxPath, readBack, sizeof(readBack)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, readBack, sizeof(payload));
}

void test_xmodem_receive_truncates_a_stale_file(void)
{
    // FILE_O_WRITE on Adafruit_LittleFS is append, not truncate; xmodem.cpp removes the target
    // before opening. A shorter transfer over a longer stale file must leave no tail bytes.
    uint8_t stale[400];
    memset(stale, 'Z', sizeof(stale));
    writeAll(kRxPath, stale, sizeof(stale));

    uint8_t payload[10];
    fillPattern(payload, sizeof(payload), 3);

    startReceive();
    xm->handlePacket(makeData(1, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);
    xm->handlePacket(makeControl(meshtastic_XModem_Control_EOT));

    uint8_t readBack[400];
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), readAll(kRxPath, readBack, sizeof(readBack)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, readBack, sizeof(payload));
}

void test_xmodem_receive_rejects_wrong_seq(void)
{
    uint8_t p1[kChunk], p2[kChunk];
    fillPattern(p1, sizeof(p1), 11);
    fillPattern(p2, sizeof(p2), 97);

    startReceive();
    xm->handlePacket(makeData(1, p1, sizeof(p1)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);

    // Duplicate of an already-accepted packet: rejected (NAK), not rewritten.
    xm->handlePacket(makeData(1, p1, sizeof(p1)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NAK, xm->getForPhone().control);

    // Skip ahead: also rejected, and packetno must not have advanced past 2.
    xm->handlePacket(makeData(3, p2, sizeof(p2)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NAK, xm->getForPhone().control);

    // The expected seq still works after both rejections.
    xm->handlePacket(makeData(2, p2, sizeof(p2)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);

    xm->handlePacket(makeControl(meshtastic_XModem_Control_EOT));

    uint8_t readBack[3 * kChunk];
    TEST_ASSERT_EQUAL_size_t(2 * kChunk, readAll(kRxPath, readBack, sizeof(readBack)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p1, readBack, kChunk);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p2, readBack + kChunk, kChunk);
}

void test_xmodem_receive_rejects_bad_crc(void)
{
    uint8_t payload[64];
    fillPattern(payload, sizeof(payload), 55);

    startReceive();
    xm->handlePacket(makeData(1, payload, sizeof(payload), /*goodCrc=*/false));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NAK, xm->getForPhone().control);

    // The sender retries the same seq with a good CRC; only that copy lands in the file.
    xm->handlePacket(makeData(1, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);

    xm->handlePacket(makeControl(meshtastic_XModem_Control_EOT));

    uint8_t readBack[2 * kChunk];
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), readAll(kRxPath, readBack, sizeof(readBack)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, readBack, sizeof(payload));
}

void test_xmodem_receive_naks_traversal_filename(void)
{
    xm->handlePacket(makeStart(meshtastic_XModem_Control_SOH, "../evil"));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NAK, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());

    // isReceiving stayed false, so a follow-up data packet falls through with no reply at all.
    xm->resetForPhone();
    uint8_t junk[16];
    fillPattern(junk, sizeof(junk), 1);
    xm->handlePacket(makeData(1, junk, sizeof(junk)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NUL, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());
}

// NOTE: the receive-side open-failure NAK branch (xmodem.cpp "open(%s, WRITE) failed") is not
// testable on native: Portduino's VFSImpl::open() returns a truthy File whenever the mode permits
// creation, even when the underlying fopen fails, so the branch is unreachable here.

void test_xmodem_can_mid_receive_removes_the_file(void)
{
    uint8_t payload[kChunk];
    fillPattern(payload, sizeof(payload), 42);

    startReceive();
    xm->handlePacket(makeData(1, payload, sizeof(payload)));
    xm->handlePacket(makeData(2, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);

    xm->handlePacket(makeControl(meshtastic_XModem_Control_CAN));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());
    TEST_ASSERT_FALSE(FSCom.exists(kRxPath));
}

void test_xmodem_can_after_eot_removes_completed_file(void)
{
    // Documents current behaviour: the CAN handler acts on the stale filename from the previous
    // session even when no transfer is in flight, deleting a file that completed successfully.
    // A deliberate fix (ignoring CAN while idle) should update this test.
    uint8_t payload[8];
    fillPattern(payload, sizeof(payload), 5);

    startReceive();
    xm->handlePacket(makeData(1, payload, sizeof(payload)));
    xm->handlePacket(makeControl(meshtastic_XModem_Control_EOT));
    TEST_ASSERT_FALSE(xm->isBusy());
    TEST_ASSERT_TRUE(FSCom.exists(kRxPath));

    xm->handlePacket(makeControl(meshtastic_XModem_Control_CAN));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);
    TEST_ASSERT_FALSE(FSCom.exists(kRxPath));
}

// --- Transmit path ---

void test_xmodem_transmit_happy_path(void)
{
    uint8_t payload[300];
    fillPattern(payload, sizeof(payload), 7);

    meshtastic_XModem out = startTransmit(payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT16(kChunk, out.buffer.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, out.buffer.bytes, kChunk);
    TEST_ASSERT_EQUAL_HEX16(xm->crc16_ccitt(out.buffer.bytes, out.buffer.size), out.crc16);

    // ACK-drive the whole stream and reassemble it; the last (short) packet latches EOT, which
    // arrives on the following ACK.
    uint8_t reassembled[sizeof(payload) + kChunk];
    size_t got = 0;
    uint16_t expectSeq = 1;
    for (int guard = 0; guard < 10; guard++) {
        TEST_ASSERT_EQUAL(meshtastic_XModem_Control_SOH, out.control);
        TEST_ASSERT_EQUAL_UINT16(expectSeq, out.seq);
        TEST_ASSERT_EQUAL_HEX16(xm->crc16_ccitt(out.buffer.bytes, out.buffer.size), out.crc16);
        memcpy(reassembled + got, out.buffer.bytes, out.buffer.size);
        got += out.buffer.size;
        expectSeq++;

        xm->handlePacket(makeControl(meshtastic_XModem_Control_ACK));
        out = xm->getForPhone();
        if (out.control == meshtastic_XModem_Control_EOT)
            break;
    }

    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_EOT, out.control);
    TEST_ASSERT_FALSE(xm->isBusy());
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, reassembled, sizeof(payload));
}

void test_xmodem_transmit_nak_resends_same_packet(void)
{
    uint8_t payload[300];
    fillPattern(payload, sizeof(payload), 61);

    meshtastic_XModem first = startTransmit(payload, sizeof(payload));

    // NAK seeks back and re-reads the same block: identical seq, bytes and CRC.
    xm->handlePacket(makeControl(meshtastic_XModem_Control_NAK));
    meshtastic_XModem resent = xm->getForPhone();
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_SOH, resent.control);
    TEST_ASSERT_EQUAL_UINT16(first.seq, resent.seq);
    TEST_ASSERT_EQUAL_UINT16(first.buffer.size, resent.buffer.size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first.buffer.bytes, resent.buffer.bytes, first.buffer.size);
    TEST_ASSERT_EQUAL_HEX16(first.crc16, resent.crc16);

    // A subsequent ACK still advances to the next block.
    xm->handlePacket(makeControl(meshtastic_XModem_Control_ACK));
    meshtastic_XModem next = xm->getForPhone();
    TEST_ASSERT_EQUAL_UINT16(2, next.seq);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload + kChunk, next.buffer.bytes, kChunk);
}

void test_xmodem_transmit_retry_cap_cancels(void)
{
    uint8_t payload[300];
    fillPattern(payload, sizeof(payload), 23);

    startTransmit(payload, sizeof(payload));

    // retrans starts at MAXRETRANS on a fresh adapter; NAKs 1..MAXRETRANS-1 resend, the
    // MAXRETRANS'th decrements it to zero and aborts with CAN.
    for (int i = 1; i < MAXRETRANS; i++) {
        xm->handlePacket(makeControl(meshtastic_XModem_Control_NAK));
        TEST_ASSERT_EQUAL(meshtastic_XModem_Control_SOH, xm->getForPhone().control);
        TEST_ASSERT_EQUAL_UINT16(1, xm->getForPhone().seq);
    }
    xm->handlePacket(makeControl(meshtastic_XModem_Control_NAK));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_CAN, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());
}

void test_xmodem_transmit_naks_missing_file(void)
{
    xm->handlePacket(makeStart(meshtastic_XModem_Control_STX, "/xmodem_test_missing.bin"));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NAK, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());
}

void test_xmodem_soh_mid_transmit_cancels(void)
{
    uint8_t payload[300];
    fillPattern(payload, sizeof(payload), 89);

    startTransmit(payload, sizeof(payload));

    // A data frame arriving while we are the sender is protocol confusion: cancel the transfer.
    uint8_t junk[16];
    fillPattern(junk, sizeof(junk), 2);
    xm->handlePacket(makeData(5, junk, sizeof(junk)));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_CAN, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());
}

void test_xmodem_eot_mid_transmit_leaves_state_busy(void)
{
    // Documents current behaviour: the EOT handler only clears isReceiving, so an EOT received
    // while transmitting ACKs, closes the file, and leaves the adapter wedged busy. A deliberate
    // fix should update this test.
    uint8_t payload[300];
    fillPattern(payload, sizeof(payload), 13);

    startTransmit(payload, sizeof(payload));
    xm->handlePacket(makeControl(meshtastic_XModem_Control_EOT));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_ACK, xm->getForPhone().control);
    TEST_ASSERT_TRUE(xm->isBusy());
}

// --- Idle replies and the getForPhone/resetForPhone contract ---

void test_xmodem_ack_nak_while_idle_provoke_can(void)
{
    xm->handlePacket(makeControl(meshtastic_XModem_Control_ACK));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_CAN, xm->getForPhone().control);

    // getForPhone() is a read, not a drain: the reply stays until resetForPhone() clears it.
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_CAN, xm->getForPhone().control);
    xm->resetForPhone();
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NUL, xm->getForPhone().control);

    xm->handlePacket(makeControl(meshtastic_XModem_Control_NAK));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_CAN, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());
}

void test_xmodem_unknown_control_ignored(void)
{
    xm->handlePacket(makeControl(meshtastic_XModem_Control_CTRLZ));
    TEST_ASSERT_EQUAL(meshtastic_XModem_Control_NUL, xm->getForPhone().control);
    TEST_ASSERT_FALSE(xm->isBusy());
}

// --- Unity lifecycle ---

void setUp(void)
{
    FSCom.remove(kRxPath);
    FSCom.remove(kTxPath);
    xm = new XModemTestShim();
}

void tearDown(void)
{
    delete xm; // File member closes any handle still held
    xm = nullptr;
    FSCom.remove(kRxPath);
    FSCom.remove(kTxPath);
}

#else // !FSCom

void setUp(void) {}
void tearDown(void) {}

#endif // FSCom

void setup()
{
    initializeTestEnvironment();
#ifdef FSCom
    // handlePacket brackets every FSCom touch with spiLock; nothing in the test environment
    // creates it, so do it here (initSPI asserts it only runs once).
    if (!spiLock)
        initSPI();
#endif
    UNITY_BEGIN();
#ifdef FSCom
    printf("\n=== isValidFilename ===\n");
    RUN_TEST(test_xmodem_rejects_dotdot_traversal);
    RUN_TEST(test_xmodem_rejects_backslash_traversal);
    RUN_TEST(test_xmodem_rejects_drive_qualified);
    RUN_TEST(test_xmodem_rejects_empty);
    RUN_TEST(test_xmodem_allows_legit_paths);

    printf("\n=== CRC ===\n");
    RUN_TEST(test_xmodem_crc16_known_answer);

    printf("\n=== Receive path ===\n");
    RUN_TEST(test_xmodem_receive_happy_path);
    RUN_TEST(test_xmodem_receive_truncates_a_stale_file);
    RUN_TEST(test_xmodem_receive_rejects_wrong_seq);
    RUN_TEST(test_xmodem_receive_rejects_bad_crc);
    RUN_TEST(test_xmodem_receive_naks_traversal_filename);
    RUN_TEST(test_xmodem_can_mid_receive_removes_the_file);
    RUN_TEST(test_xmodem_can_after_eot_removes_completed_file);

    printf("\n=== Transmit path ===\n");
    RUN_TEST(test_xmodem_transmit_happy_path);
    RUN_TEST(test_xmodem_transmit_nak_resends_same_packet);
    RUN_TEST(test_xmodem_transmit_retry_cap_cancels);
    RUN_TEST(test_xmodem_transmit_naks_missing_file);
    RUN_TEST(test_xmodem_soh_mid_transmit_cancels);
    RUN_TEST(test_xmodem_eot_mid_transmit_leaves_state_busy);

    printf("\n=== Idle replies / phone contract ===\n");
    RUN_TEST(test_xmodem_ack_nak_while_idle_provoke_can);
    RUN_TEST(test_xmodem_unknown_control_ignored);
#endif
    exit(UNITY_END());
}

void loop() {}
