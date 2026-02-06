#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./ObdApplet.h"

#include "NodeDB.h"
#if defined(ARCH_ESP32) && defined(USERPREFS_OBDII_ENABLED)
#include "modules/ObdiiTelemetryModule.h"
#endif

using namespace NicheGraphics;

void InkHUD::ObdApplet::onRender(bool full)
{
    (void)full;

    drawHeader("OBD");
    int16_t y = getHeaderHeight() + 2;

    setFont(fontSmall);

    // Time
    std::string timeStr = getTimeString();
    printAt(X(0.02f), y, "Time:", LEFT, TOP);
    printAt(X(0.35f), y, timeStr, LEFT, TOP);
    y += fontSmall.getLineHeight() + 2;

    // GPS
    std::string gpsLine = "GPS: no fix";
    if (nodeDB->hasLocalPositionSinceBoot() && localPosition.latitude_i != 0 && localPosition.longitude_i != 0) {
        char buf[48];
        float lat = localPosition.latitude_i * 1e-7f;
        float lon = localPosition.longitude_i * 1e-7f;
        snprintf(buf, sizeof(buf), "GPS: %.5f, %.5f", lat, lon);
        gpsLine = buf;
    }
    printAt(X(0.02f), y, gpsLine, LEFT, TOP);
    y += fontSmall.getLineHeight() + 2;

    // OBD metrics
    int rpm = -1;
    int mv = -1;
    uint32_t lastMs = 0;
    const char *state = "n/a";
#if defined(ARCH_ESP32) && defined(USERPREFS_OBDII_ENABLED)
    if (obdiiTelemetryModule) {
        rpm = obdiiTelemetryModule->getLatestRpm();
        mv = obdiiTelemetryModule->getLatestVoltageMv();
        lastMs = obdiiTelemetryModule->getLastUpdateMs();
        state = obdiiTelemetryModule->getStateLabel();
    }
#endif

    char rpmBuf[32];
    if (rpm >= 0)
        snprintf(rpmBuf, sizeof(rpmBuf), "RPM: %d", rpm);
    else
        snprintf(rpmBuf, sizeof(rpmBuf), "RPM: --");
    printAt(X(0.02f), y, rpmBuf, LEFT, TOP);
    y += fontSmall.getLineHeight() + 2;

    char voltBuf[32];
    if (mv >= 0)
        snprintf(voltBuf, sizeof(voltBuf), "V: %.2f", (float)mv / 1000.0f);
    else
        snprintf(voltBuf, sizeof(voltBuf), "V: --");
    printAt(X(0.02f), y, voltBuf, LEFT, TOP);
    y += fontSmall.getLineHeight() + 2;

    char statusBuf[48];
    if (lastMs > 0) {
        uint32_t ageSec = (millis() - lastMs) / 1000;
        snprintf(statusBuf, sizeof(statusBuf), "OBD: %s (%lus)", state, (unsigned long)ageSec);
    } else {
        snprintf(statusBuf, sizeof(statusBuf), "OBD: %s", state);
    }
    printAt(X(0.02f), y, statusBuf, LEFT, TOP);
}

int32_t InkHUD::ObdApplet::runOnce()
{
    if (isActive()) {
        requestUpdate(Drivers::EInk::UpdateTypes::RESPONSIVE);
    }
    return 2000;
}

#endif
