#pragma once

/**
 * Minimal RadioLibInterface test double for airtime-ordering tests.
 *
 * applyCodingRate() records the last CR "written to hardware" so
 * getPacketTime() can read it back and report which CR was in effect.
 * This lets tests verify that completeSending() calls getPacketTime()
 * BEFORE restoring the base CR rather than after.
 *
 * Protected members are exposed through thin wrappers so test functions
 * can drive the object without subclassing it themselves.
 */

#include "RadioLibInterface.h"
#include "error.h"

class TestRadioLibInterface : public RadioLibInterface
{
  public:
    /** CR last passed to applyCodingRate() — represents the hardware state. */
    uint8_t lastAppliedCr = 0;

    /** CR that was in effect when getPacketTime() was most recently called. */
    uint8_t crSeenByGetPacketTime = 0;

    /**
     * Construct without real hardware.
     * Null HAL and RADIOLIB_NC pins are safe as long as no SPI / IRQ
     * operations are triggered — completeSending() performs neither.
     */
    TestRadioLibInterface() : RadioLibInterface(nullptr, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, nullptr) {}

    // -----------------------------------------------------------------------
    // Required pure-virtual overrides
    // -----------------------------------------------------------------------

    /** Record the CR so getPacketTime() can observe it. */
    int16_t applyCodingRate(uint8_t codingRate) override
    {
        lastAppliedCr = codingRate;
        return RADIOLIB_ERR_NONE;
    }

    /**
     * Encode the current hardware CR as the return value so tests can assert
     * on how long an airtime was computed.  Also snapshots lastAppliedCr into
     * crSeenByGetPacketTime to verify ordering relative to the CR restore.
     */
    uint32_t getPacketTime(uint32_t /*pl*/, bool /*received*/) override
    {
        crSeenByGetPacketTime = lastAppliedCr;
        return static_cast<uint32_t>(lastAppliedCr) * 1000U;
    }

    bool isChannelActive() override { return false; }
    bool isActivelyReceiving() override { return false; }
    void addReceiveMetadata(meshtastic_MeshPacket *) override {}
    void disableInterrupt() override {}
    void enableInterrupt(void (*)()) override {}
    ErrorCode send(meshtastic_MeshPacket *) override { return ERRNO_OK; }

    // -----------------------------------------------------------------------
    // Protected-member wrappers for use by test functions
    // -----------------------------------------------------------------------

    /** Set the base (configured) coding rate as applyModemConfig() would. */
    void setBaseCr(uint8_t c) { cr = c; }

    /** Inject a packet as if startSend() had assigned it to sendingPacket. */
    void setActivePacket(meshtastic_MeshPacket *p) { sendingPacket = p; }

    /** Invoke completeSending() (protected) from a test function. */
    void triggerCompleteSending() { completeSending(); }

    /**
     * Simulate the hardware state after applyTemporaryCodingRateOverride() ran
     * (the override CR is active on the radio but not yet restored).
     * Accessible because RadioLibInterface declares TestRadioLibInterface a friend.
     */
    void setActiveTxCodingRateOverrideForTest(std::optional<uint8_t> v) { activeTxCodingRateOverride = v; }
};
