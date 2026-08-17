// InkHUD setup for the RockBase IoT NM-EPD-420.
//
// The B/W profile is intentionally conservative: one tile, a short applet list,
// and taller two-button menu rows for the 400x300 panel.

#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

#if !defined(NM_EPD_420_BW_INKHUD)
#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#endif
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"

#include "graphics/niche/Drivers/EInk/NMEPD420.h"
#include "graphics/niche/Inputs/TwoButton.h"

#include "Channels.h"
#include "MeshRadio.h"
#include "Power.h"
#include "airtime.h"
#include "buzz.h"
#include "main.h"
#include "mesh/NodeDB.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"

#if defined(NM_EPD_420_BW_INKHUD)
namespace NicheGraphics::InkHUD
{

class NMStatusApplet : public Applet
{
  public:
    void onRender(bool full) override
    {
        (void)full;
        drawHeader("Status");

        setFont(fontLarge);
        printAt(0, Applet::getHeaderHeight() + 6, owner.short_name[0] ? owner.short_name : "NM-EPD-420");

        setFont(fontSmall);
        int16_t y = Applet::getHeaderHeight() + 6 + fontLarge.lineHeight() + 8;

        char line[56];
        snprintf(line, sizeof(line), "Region: %s", myRegion ? myRegion->name : "Unset");
        printAt(0, y, line);
        y += fontSmall.lineHeight() + 4;

        const meshtastic_Channel &ch = channels.getByIndex(0);
        const char *channelName = channels.isDefaultChannel(0) ? "Public" : ch.settings.name;
        snprintf(line, sizeof(line), "Channel 0: %s", channelName && channelName[0] ? channelName : "Unnamed");
        printAt(0, y, line);
        y += fontSmall.lineHeight() + 4;

        snprintf(line, sizeof(line), "Nodes: %u", (unsigned)max((int)nodeDB->getNumMeshNodes() - 1, 0));
        printAt(0, y, line);
        y += fontSmall.lineHeight() + 4;

        snprintf(line, sizeof(line), "Air: %.1f%%  Ch: %.1f%%", airTime->utilizationTXPercent(),
                 airTime->channelUtilizationPercent());
        printAt(0, y, line);
        y += fontSmall.lineHeight() + 4;

        if (powerStatus->getHasBattery()) {
            snprintf(line, sizeof(line), "Battery: %u%%  %.2fV", powerStatus->getBatteryChargePercent(),
                     powerStatus->getBatteryVoltageMv() / 1000.0f);
        } else {
            snprintf(line, sizeof(line), "Battery: USB/External");
        }
        printAt(0, y, line);
    }
};

class NMEnvironmentApplet : public Applet
{
  public:
    void onRender(bool full) override
    {
        (void)full;
        drawHeader("Environment");

        meshtastic_EnvironmentMetrics env = meshtastic_EnvironmentMetrics_init_zero;
        const bool hasEnv = nodeDB->copyNodeEnvironment(nodeDB->getNodeNum(), env);
        const bool hasTemp = hasEnv && env.has_temperature;
        const bool hasHumidity = hasEnv && env.has_relative_humidity;

        if (!hasTemp && !hasHumidity) {
            setFont(fontMedium);
            printAt(X(0.5), Y(0.42), "Waiting for AHT20", CENTER, MIDDLE);
            setFont(fontSmall);
            printAt(X(0.5), Y(0.56), "First reading may take", CENTER, MIDDLE);
            printAt(X(0.5), Y(0.66), "a few refresh cycles", CENTER, MIDDLE);
            printAt(0, Y(1.0), "Sensor: pending", LEFT, BOTTOM);
            return;
        }

        setFont(fontLarge);
        char value[32];
        if (hasTemp) {
            snprintf(value, sizeof(value), "%.1f C", env.temperature);
        } else {
            snprintf(value, sizeof(value), "--.- C");
        }
        printAt(0, Applet::getHeaderHeight() + 8, value);

        setFont(fontMedium);
        if (hasHumidity) {
            snprintf(value, sizeof(value), "%.0f %% RH", env.relative_humidity);
        } else {
            snprintf(value, sizeof(value), "-- %% RH");
        }
        printAt(0, Applet::getHeaderHeight() + 14 + fontLarge.lineHeight(), value);

        setFont(fontSmall);
        printAt(0, Y(1.0) - (fontSmall.lineHeight() * 2), "Last valid sample");
        printAt(0, Y(1.0), "Sensor: AHT20", LEFT, BOTTOM);
    }
};

} // namespace NicheGraphics::InkHUD
#endif

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    Drivers::EInk *driver = new Drivers::NMEPD420;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);

#if defined(NM_EPD_420_BW_INKHUD)
    inkhud->setDisplayResilience(12, 1.2);
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_9PT_WIN1252;

    // NM-EPD-420-BW InkHUD profile
    auto &settings = inkhud->persistence->settings;
    settings.userTiles.maxCount = 1;
    settings.userTiles.count = 1;
    settings.rotation = 0;
    settings.optionalFeatures.batteryIcon = true;
    settings.optionalMenuItems.nextTile = false;
    settings.optionalMenuItems.backlight = false;
    settings.joystick.enabled = false;

    inkhud->addApplet("NM Status", new InkHUD::NMStatusApplet, true, false, 0);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0), true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet, true, true);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false);
    inkhud->addApplet("Environment", new InkHUD::NMEnvironmentApplet, true, false);
    // NM-EPD-420 legacy InkHUD profile
#else
    inkhud->setDisplayResilience(7, 1.5);
    InkHUD::Applet::fontLarge = FREESANS_12PT_WIN1252;
    InkHUD::Applet::fontMedium = FREESANS_9PT_WIN1252;
    InkHUD::Applet::fontSmall = FREESANS_6PT_WIN1252;

    inkhud->persistence->settings.userTiles.maxCount = 4;
    inkhud->persistence->settings.userTiles.count = 2;
    inkhud->persistence->settings.rotation = 0;
    inkhud->persistence->settings.optionalMenuItems.nextTile = true;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true, 0);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Positions", new InkHUD::PositionsApplet, true, false, 1);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 1);
#endif

    inkhud->begin();

    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance();

    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin());
    buttons->setHandlerShortPress(0, [inkhud]() { inkhud->shortpress(); });
    buttons->setHandlerLongPress(0, [inkhud]() { inkhud->longpress(); });

#if defined(NM_EPD_420_BW_INKHUD)
    buttons->setWiring(1, PIN_BUTTON2);
    buttons->setHandlerShortPress(1, [inkhud]() {
        InkHUD::SystemApplet *menu = inkhud->getSystemApplet("Menu");
        if (menu->isForeground()) {
            inkhud->touchNavUp();
        } else {
            inkhud->prevApplet();
        }
        playChirp();
    });
#else
    buttons->setWiring(1, PIN_BUTTON2);
    buttons->setHandlerShortPress(1, [inkhud]() {
        inkhud->nextTile();
        playChirp();
    });
#endif

    buttons->start();
}

#endif
