#include "gnssRail.h"

#include "configuration.h"

#ifdef SEEED_WIO_TRACKER_L2

#include "DebugConfiguration.h"
#include "GpioLogic.h"
#include "SPILock.h"
#include "gps/GPS.h"

#include PCA95X5_INC

extern PCA95X5_CLS io;

namespace
{
class L2GnssPowerPin final : public GpioPin
{
  public:
    void set(bool on) override
    {
        bool ok;
        {
            concurrency::LockGuard guard(spiLock);
            ok = io.digitalWrite(EXPANDS_GNSS_PWR_EN, on ? HIGH : LOW);
        }
        if (!ok)
            LOG_ERROR("Failed to set GNSS power %s", on ? "on" : "off");
    }
};

L2GnssPowerPin gnssPowerPin;
} // namespace

void l2GnssRailInit()
{
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
