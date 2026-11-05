#include "configuration.h"

#if defined(HAS_SE050) && defined(HAS_CUSTOM_CRYPTO_ENGINE)

#include "SE050.h"
#include "mesh/CryptoEngine.h"
#include "meshUtils.h"
#include <Curve25519.h>
#include <stdint.h>

// Runs Meshtastic's PKI key agreement inside the SE050 instead of in software.
//
// This is the mirrored stage: the node keeps its private key in config exactly as
// before, and a copy lives in the secure element so the chip can do the agreement.
// Nothing else in the firmware can tell the difference, which is the point - it
// makes the hardware path testable on a live node without changing the semantics
// of an identity that thirty-odd call sites still assume is exportable.
//
// Measured cost is ~64ms per agreement against ~27ms for the bundled Curve25519.

// Not freertosinc.h: it keys off ARDUINO_ARCH_RP2040, which this variant never
// defines, so on RP2350 it hands out the placeholder definitions instead of the
// real ones. __FREERTOS is what rp2350_base actually sets.
#if defined(ARCH_RP2040) && defined(__FREERTOS)
#include <FreeRTOS.h>
#include <task.h>

// How close the key agreement comes to running out of stack.
//
// Meshtastic builds this core with -D__FREERTOS=1, so setup() and loop() - and
// therefore the whole packet path - run in the framework's CORE0 task, created
// as xTaskCreate(__core0, "CORE0", 1024, ...) in freertos-main.cpp. That depth
// is in words: 4 KB, allocated from the FreeRTOS heap, which is why the frames
// here sit in main SRAM and nowhere near the linker's SCRATCH_Y stack.
//
// 4 KB is the whole budget for a call chain that reaches encryptCurve25519, and
// the driver's buffers used to take 2.5 KB of it. FreeRTOS is built with
// configCHECK_FOR_STACK_OVERFLOW 2 and arduino-pico's hook calls
// panic("Stack overflow"), so the failure mode was not corruption: the overflow
// was detected and the board was reset on purpose. That is the reboot.
//
// uxTaskGetStackHighWaterMark reports the minimum free the task has ever had,
// in words, so this is the real margin rather than an inference from addresses.
static void reportStackDepth()
{
    size_t freeBytes = (size_t)uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
    static size_t low = SIZE_MAX;
    if (freeBytes < low) {
        low = freeBytes;
        LOG_INFO("SE050: key agreement leaves %u bytes of the 4 KB CORE0 task stack", (unsigned)freeBytes);
    }
}
#else
static void reportStackDepth() {}
#endif

class SE050CryptoEngine : public CryptoEngine
{
  public:
    // Every PKI packet lands here: encryptCurve25519 and decryptCurve25519 both
    // route through it, so this single override captures the whole ECDH path.
    virtual bool setDHPublicKey(uint8_t *pubKey) override
    {
        if (!mirrorReady())
            return CryptoEngine::setDHPublicKey(pubKey);

        uint8_t peer[32];
        memcpy(peer, pubKey, 32);
        uint8_t agreed[32];
        bool ok = se050->identityEcdh(peer, agreed);
        reportStackDepth(); // after the agreement: the deep part is behind us and counted
        if (!ok) {
            LOG_WARN("SE050: key agreement failed, falling back to software");
            return CryptoEngine::setDHPublicKey(pubKey);
        }

        // Curve25519::dh2 rejects weak points, and going through the chip skips
        // that check. A small-subgroup public key collapses the shared secret to
        // zero, so refuse it rather than encrypt against a known value.
        bool allZero = true;
        for (int i = 0; i < 32; i++)
            allZero &= (agreed[i] == 0);
        if (allZero) {
            LOG_WARN("SE050: key agreement produced a zero secret, rejecting peer key");
            return false;
        }

        memcpy(shared_key, agreed, 32);
        return true;
    }

  private:
    // The mirror can only be set up once NodeDB has handed us the private key, which
    // happens after the chip is probed, so it is done on first use rather than at
    // construction. One failure is enough to stop retrying: if the import did not
    // work it will not start working, and retrying would cost 64ms per packet.
    bool mirrorReady()
    {
        if (mirrored)
            return true;
        if (attempted || !se050)
            return false;
        attempted = true;

        if (memfll(private_key, 0, sizeof(private_key))) {
            LOG_DEBUG("SE050: no private key yet, staying on software crypto");
            attempted = false; // NodeDB may still fill it in
            return false;
        }

        uint8_t pub[32];
#ifdef SE050_REPLACE_MIRROR
        // One-shot: discards a mirror left behind by a previous node identity.
        // Meant to be flashed once and taken back out, not carried in a build.
        const bool replaceStale = true;
#else
        const bool replaceStale = false;
#endif
        if (!se050->identityImport(private_key, pub, replaceStale)) {
            LOG_WARN("SE050: could not mirror the node key, staying on software crypto");
            return false;
        }
        if (memcmp(pub, public_key, 32) != 0)
            LOG_WARN("SE050: mirrored key does not match the advertised public key");

        LOG_INFO("SE050: PKI key agreement now runs in hardware");
        mirrored = true;
        return true;
    }

    bool mirrored = false;
    bool attempted = false;

  public:
    void selfTest()
    {
        if (!se050) {
            LOG_INFO("SE050: no secure element, PKI stays in software");
            return;
        }
        if (memfll(private_key, 0, sizeof(private_key))) {
            LOG_WARN("SE050: self-test skipped, node has no private key yet");
            return;
        }

        // Check the chip is actually in the path before comparing anything.
        //
        // Without this the test was worthless in the one case that matters: if
        // the mirror is unavailable, setDHPublicKey falls back to software, and
        // the test then compares software against software and reports "OK".
        // It did exactly that on a node whose mirror held a stale key - green
        // light, software crypto, nobody the wiser. A test that cannot fail is
        // not a test.
        if (!mirrorReady()) {
            LOG_ERROR("SE050: self-test FAILED - key agreement is not running on the chip, PKI stays in software");
            return;
        }

        // A throwaway peer, exchanged once through the chip and once in software.
        uint8_t peerPrivate[32], peerPublic[32];
        Curve25519::dh1(peerPublic, peerPrivate);

        uint8_t peerForHw[32];
        memcpy(peerForHw, peerPublic, 32);
        if (!setDHPublicKey(peerForHw)) {
            LOG_ERROR("SE050: self-test failed, key agreement returned an error");
            return;
        }
        uint8_t fromChip[32];
        memcpy(fromChip, shared_key, 32);

        // The software side, done the way the base class would: our public half
        // against the peer's private half yields the same secret.
        uint8_t expected[32];
        Curve25519::eval(expected, private_key, 0);
        if (!Curve25519::dh2(expected, peerPrivate)) {
            LOG_ERROR("SE050: self-test failed on the software side");
            return;
        }

        if (memcmp(fromChip, expected, 32) == 0)
            LOG_INFO("SE050: self-test OK - PKI agreement through the chip matches software");
        else
            LOG_ERROR("SE050: self-test MISMATCH - chip and software disagree on the shared secret");
    }
};

static SE050CryptoEngine *se050Crypto = new SE050CryptoEngine();
CryptoEngine *crypto = se050Crypto;

void se050CryptoSelfTest()
{
    se050Crypto->selfTest();
}

#endif
