#include "gnssRail.h"

#include "configuration.h"

#ifdef SEEED_WIO_TRACKER_L2

#include "DebugConfiguration.h"
#include "GpioLogic.h"
#include "SPILock.h"
#include "concurrency/Lock.h"
#include "concurrency/OSThread.h"
#include "gps/GPS.h"

#include PCA95X5_INC

extern PCA95X5_CLS io;

namespace
{
constexpr uint32_t L2_GNSS_RETRY_MS = 5000;
constexpr uint32_t L2_GNSS_RETRY_SLOW_MS = 60000;
constexpr uint8_t L2_GNSS_FAST_RETRIES = 3;

concurrency::Lock railStateLock;
bool desiredOn = false;
bool retryPending = false;
uint8_t failedRetries = 0;

bool writeDesiredLocked()
{
    // Caller holds railStateLock. spiLock serialises this PCA9535 port read-modify-write
    // against the other expander users; this board builds with CONFIG_DISABLE_HAL_LOCKS.
    concurrency::LockGuard guard(spiLock);
    return io.digitalWrite(EXPANDS_GNSS_PWR_EN, desiredOn ? HIGH : LOW);
}

class L2GnssPowerRetry final : public concurrency::OSThread
{
  public:
    L2GnssPowerRetry() : OSThread("L2GnssRail", L2_GNSS_RETRY_MS) { disable(); }

    // Caller must hold railStateLock; set() is invoked from the main task.
    void arm()
    {
        setIntervalFromNow(L2_GNSS_RETRY_MS);
        enabled = true;
        concurrency::mainDelay.interrupt();
    }

  protected:
    int32_t runOnce() override
    {
        bool recovered = false;
        bool slowedDown = false;
        {
            concurrency::LockGuard guard(&railStateLock);
            if (!retryPending) {
                disable();
                return RUN_SAME;
            }

            if (writeDesiredLocked()) {
                retryPending = false;
                failedRetries = 0;
                recovered = true;
                disable();
            } else {
                if (failedRetries < L2_GNSS_FAST_RETRIES)
                    ++failedRetries;
                slowedDown = failedRetries == L2_GNSS_FAST_RETRIES;
                setIntervalFromNow(slowedDown ? L2_GNSS_RETRY_SLOW_MS : L2_GNSS_RETRY_MS);
            }
        }

        if (recovered)
            LOG_INFO("GNSS power write recovered");
        else if (slowedDown)
            LOG_WARN("GNSS power write still failing; retrying every %u s", (unsigned)(L2_GNSS_RETRY_SLOW_MS / 1000));
        return RUN_SAME;
    }
};

L2GnssPowerRetry *railRetry = nullptr;

class L2GnssPowerPin final : public GpioPin
{
  public:
    void set(bool on) override
    {
        bool ok;
        bool wasPending;
        {
            concurrency::LockGuard guard(&railStateLock);
            desiredOn = on;
            wasPending = retryPending;
            ok = writeDesiredLocked();
            retryPending = !ok;

            if (ok) {
                failedRetries = 0;
                if (wasPending && railRetry)
                    railRetry->disable();
            } else if (!wasPending && railRetry) {
                failedRetries = 0;
                railRetry->arm();
            }
        }

        if (!ok && !wasPending)
            LOG_ERROR("Failed to set GNSS power %s; retry scheduled", on ? "on" : "off");
        else if (ok && wasPending)
            LOG_INFO("GNSS power write recovered");
    }
};

L2GnssPowerPin gnssPowerPin;
} // namespace

void l2GnssRailInit()
{
    if (!railRetry)
        railRetry = new L2GnssPowerRetry();

#if !MESHTASTIC_EXCLUDE_GPS
    if (gps) {
        auto originalEnablePin = gps->enablePin;
        auto enablePin = new GpioVirtPin();
        // Preserve the configured GPIO chain and add the board rail as a second output.
        new GpioUnaryTransformer(enablePin, new GpioSplitter(originalEnablePin, &gnssPowerPin));
        const auto originalState = originalEnablePin->get();
        if (originalState != GpioVirtPin::PinState::Unset)
            enablePin->set(originalState == GpioVirtPin::PinState::On);
        gps->enablePin = enablePin;
        return;
    }
#endif
    gnssPowerPin.set(false);
}

#endif
