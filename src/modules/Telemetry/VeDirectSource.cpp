#include "VeDirectSource.h"

#if HAS_VEDIRECT_TELEMETRY

#include "Throttle.h"
#include "UptimeClock.h"
#include <cstdlib>
#include <cstring>

VeDirectSource veDirectSource;

// Text frames are "\r\n<label>\t<value>"; a block ends with a Checksum field that brings the byte sum of the
// whole block to 0 mod 256. An interleaved HEX frame (':' ... '\n') voids the block in progress.
VeDirectSource::FeedResult VeDirectSource::feed(uint8_t c)
{
    if (parseState == ParseState::HEX_FRAME) {
        if (c == '\n')
            parseState = ParseState::WAIT_LINE;
        return FeedResult::PENDING;
    }
    // The checksum byte can take any value, ':' included, so it must not be read as a HEX frame start
    if (c == ':' && parseState != ParseState::CHECKSUM) {
        parseState = ParseState::HEX_FRAME;
        blockSum = 0;
        staged = {};
        cleanBlockStart = false;
        return FeedResult::PENDING;
    }

    blockSum += c;
    switch (parseState) {
    case ParseState::WAIT_LINE:
        if (c == '\n') {
            labelLen = 0;
            parseState = ParseState::LABEL;
        }
        break;
    case ParseState::LABEL:
        if (c == '\t') {
            label[labelLen] = '\0';
            if (strcmp(label, "Checksum") == 0) {
                parseState = ParseState::CHECKSUM;
            } else {
                valueLen = 0;
                parseState = ParseState::VALUE;
            }
        } else if (c == '\r' || labelLen >= sizeof(label) - 1) {
            parseState = ParseState::WAIT_LINE; // not a VE.Direct field line, resync on the next newline
        } else {
            label[labelLen++] = c;
        }
        break;
    case ParseState::VALUE:
        if (c == '\r') {
            value[valueLen] = '\0';
            stageField();
            parseState = ParseState::WAIT_LINE;
        } else if (valueLen < sizeof(value) - 1) {
            value[valueLen++] = c;
        }
        break;
    case ParseState::CHECKSUM: {
        FeedResult result = FeedResult::PENDING;
        if (blockSum == 0) {
            commitBlock();
            result = FeedResult::BLOCK_VALID;
        } else if (cleanBlockStart) {
            result = FeedResult::BLOCK_CORRUPT;
        }
        blockSum = 0;
        staged = {};
        cleanBlockStart = true;
        parseState = ParseState::WAIT_LINE;
        return result;
    }
    case ParseState::HEX_FRAME:
        break; // handled before the switch
    }
    return FeedResult::PENDING;
}

void VeDirectSource::stageField()
{
    long v = strtol(value, nullptr, 10);
    if (strcmp(label, "V") == 0) { // battery voltage, mV
        staged.battVoltage = v / 1000.0f;
        staged.hasBattVoltage = true;
    } else if (strcmp(label, "I") == 0) { // battery current, mA - the unit PowerMetrics carries
        staged.battCurrent = v;
        staged.hasBattCurrent = true;
    } else if (strcmp(label, "VPV") == 0) { // panel voltage, mV
        staged.panelVoltage = v / 1000.0f;
        staged.hasPanelVoltage = true;
    } else if (strcmp(label, "PPV") == 0) { // panel power, W
        staged.panelPower = v;
        staged.hasPanelPower = true;
    } else if (strcmp(label, "IL") == 0) { // load current, mA
        staged.loadCurrent = v;
        staged.hasLoadCurrent = true;
    }
}

// Merge rather than replace: BMV-series devices split their fields across two consecutive blocks
void VeDirectSource::commitBlock()
{
    isFresh(); // drops readings from before an outage, so they cannot merge into this block
    if (staged.hasBattVoltage) {
        published.battVoltage = staged.battVoltage;
        published.hasBattVoltage = true;
    }
    if (staged.hasBattCurrent) {
        published.battCurrent = staged.battCurrent;
        published.hasBattCurrent = true;
    }
    if (staged.hasPanelVoltage) {
        published.panelVoltage = staged.panelVoltage;
        published.hasPanelVoltage = true;
    }
    if (staged.hasPanelPower) {
        published.panelPower = staged.panelPower;
        published.hasPanelPower = true;
    }
    if (staged.hasLoadCurrent) {
        published.loadCurrent = staged.loadCurrent;
        published.hasLoadCurrent = true;
    }
    lastValidBlockMsec = Time::stampMillis();
}

bool VeDirectSource::isFresh()
{
    if (lastValidBlockMsec == 0)
        return false;
    if (Throttle::isWithinTimespanMs(lastValidBlockMsec, STALE_AFTER_MS))
        return true;
    lastValidBlockMsec = 0;
    published = {};
    return false;
}

bool VeDirectSource::getMetrics(meshtastic_Telemetry *m)
{
    if (!isFresh())
        return false;

    meshtastic_PowerMetrics &pm = m->variant.power_metrics;
    if (published.hasBattVoltage) {
        pm.ch1_voltage = published.battVoltage;
        pm.has_ch1_voltage = true;
    }
    if (published.hasBattCurrent) {
        pm.ch1_current = published.battCurrent;
        pm.has_ch1_current = true;
    }
    if (published.hasPanelVoltage) {
        pm.ch2_voltage = published.panelVoltage;
        pm.has_ch2_voltage = true;
    }
    if (published.hasPanelPower) {
        // MPPTs report panel power, not current; derive mA so ch2 reads volts/milliamps like the others
        pm.ch2_current = (published.hasPanelVoltage && published.panelVoltage > 1.0f)
                             ? published.panelPower / published.panelVoltage * 1000.0f
                             : 0;
        pm.has_ch2_current = true;
    }
    if (published.hasLoadCurrent) {
        if (published.hasBattVoltage) {
            pm.ch3_voltage = published.battVoltage; // the load output runs at battery voltage
            pm.has_ch3_voltage = true;
        }
        pm.ch3_current = published.loadCurrent;
        pm.has_ch3_current = true;
    }
    return published.hasBattVoltage || published.hasBattCurrent || published.hasPanelVoltage || published.hasPanelPower ||
           published.hasLoadCurrent;
}

#endif
