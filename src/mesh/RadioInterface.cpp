#include "RadioInterface.h"
#include "Channels.h"
#include "DisplayFormatters.h"
#include "LLCC68Interface.h"
#include "LR1110Interface.h"
#include "LR1120Interface.h"
#include "LR1121Interface.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RF95Interface.h"
#include "Router.h"
#include "SX1262Interface.h"
#include "SX1268Interface.h"
#include "SX1280Interface.h"
#include "configuration.h"
#include "detect/LoRaRadioType.h"
#include "main.h"
#include "sleep.h"
#include <assert.h>
#include <pb_decode.h>
#include <pb_encode.h>

#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#include "platform/portduino/SimRadio.h"
#include "platform/portduino/USBHal.h"
#endif

#ifdef ARCH_STM32WL
#include "STM32WLE5JCInterface.h"
#endif

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_STD[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,     meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,   meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO};

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_EU_868[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,    meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,  meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,   meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE}; // no TURBO modes in EU868

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_LITE[] = {meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST,
                                                           meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW};

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_NARROW[] = {meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST,
                                                             meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW};

// // Same as Narrow presets, but separate so that extra ham settings can be added later.
// meshtastic_Config_LoRaConfig_ModemPreset PRESETS_HAM[] = {meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST,
//                                                           meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW};

meshtastic_Config_LoRaConfig_ModemPreset PRESETS_UNDEF[] = {meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST};

// Calculate 2^n without calling pow()
uint32_t pow_of_2(uint32_t n)
{
    return 1 << n;
}

#define RDEF(name, freq_start, freq_end, duty_cycle, spacing, padding, power_limit, text_throttle, position_throttle,            \
             telemetry_throttle, audio_permitted, frequency_switching, wide_lora, licensed_only, override_slot, default_preset,  \
             available_presets)                                                                                                  \
    {                                                                                                                            \
        meshtastic_Config_LoRaConfig_RegionCode_##name, freq_start, freq_end, duty_cycle, spacing, padding, power_limit,         \
            text_throttle, position_throttle, telemetry_throttle, audio_permitted, frequency_switching, wide_lora,               \
            licensed_only, override_slot, meshtastic_Config_LoRaConfig_ModemPreset_##default_preset, available_presets, #name    \
    }

const RegionInfo regions[] = {
    /*
        https://link.springer.com/content/pdf/bbm%3A978-1-4842-4357-2%2F1.pdf
        https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/
    */
    RDEF(US, 902.0f, 928.0f, 100, 0, 0, 30, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        EN300220 ETSI V3.2.1 [Table B.1, Item H, p. 21]

        https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.02.01_60/en_30022002v030201p.pdf
        FIXME: https://github.com/meshtastic/firmware/issues/3371
     */
    RDEF(EU_433, 433.0f, 434.0f, 10, 0, 0, 10, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
       https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/
       https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/
       https://www.legislation.gov.uk/uksi/1999/930/schedule/6/part/III/made/data.xht?view=snippet&wrap=true

       audio_permitted = false per regulation

       Special Note:
       The link above describes LoRaWAN's band plan, stating a power limit of 16 dBm. This is their own suggested specification,
       we do not need to follow it. The European Union regulations clearly state that the power limit for this frequency range is
       500 mW, or 27 dBm. It also states that we can use interference avoidance and spectrum access techniques (such as LBT +
       AFA) to avoid a duty cycle. (Please refer to line P page 22 of this document.)
       https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.01.01_60/en_30022002v030101p.pdf
     */
    RDEF(EU_868, 869.4f, 869.65f, 10, 0, 0, 27, false, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_EU_868),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(CN, 470.0f, 510.0f, 100, 0, 0, 19, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
        https://www.arib.or.jp/english/html/overview/doc/5-STD-T108v1_5-E1.pdf
        https://qiita.com/ammo0613/items/d952154f1195b64dc29f
     */
    RDEF(JP, 920.5f, 923.5f, 100, 0, 0, 13, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        https://www.iot.org.au/wp/wp-content/uploads/2016/12/IoTSpectrumFactSheet.pdf
        https://iotalliance.org.nz/wp-content/uploads/sites/4/2019/05/IoT-Spectrum-in-NZ-Briefing-Paper.pdf
        Also used in Brazil.
     */
    RDEF(ANZ, 915.0f, 928.0f, 100, 0, 0, 30, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        433.05 - 434.79 MHz, 25mW EIRP max, No duty cycle restrictions
        AU Low Interference Potential https://www.acma.gov.au/licences/low-interference-potential-devices-lipd-class-licence
        NZ General User Radio Licence for Short Range Devices https://gazette.govt.nz/notice/id/2022-go3100
     */
    RDEF(ANZ_433, 433.05f, 434.79f, 100, 0, 0, 14, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        https://digital.gov.ru/uploaded/files/prilozhenie-12-k-reshenyu-gkrch-18-46-03-1.pdf

        Note:
            - We do LBT, so 100% is allowed.
     */
    RDEF(RU, 868.7f, 869.2f, 100, 0, 0, 20, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        https://www.law.go.kr/LSW/admRulLsInfoP.do?admRulId=53943&efYd=0
        https://resources.lora-alliance.org/technical-specifications/rp002-1-0-4-regional-parameters
     */
    RDEF(KR, 920.0f, 923.0f, 100, 0, 0, 23, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Taiwan, 920-925Mhz, limited to 0.5W indoor or coastal, 1.0W outdoor.
        5.8.1 in the Low-power Radio-frequency Devices Technical Regulations
        https://www.ncc.gov.tw/english/files/23070/102_5190_230703_1_doc_C.PDF
        https://gazette.nat.gov.tw/egFront/e_detail.do?metaid=147283
     */
    RDEF(TW, 920.0f, 925.0f, 100, 0, 0, 27, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
     */
    RDEF(IN, 865.0f, 867.0f, 100, 0, 30, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),
    /*
         https://rrf.rsm.govt.nz/smart-web/smart/page/-smart/domain/licence/LicenceSummary.wdk?id=219752
         https://iotalliance.org.nz/wp-content/uploads/sites/4/2019/05/IoT-Spectrum-in-NZ-Briefing-Paper.pdf
      */
    RDEF(NZ_865, 864.0f, 868.0f, 100, 0, 36, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
       https://lora-alliance.org/wp-content/uploads/2020/11/lorawan_regional_parameters_v1.0.3reva_0.pdf
    */
    RDEF(TH, 920.0f, 925.0f, 100, 0, 0, 16, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        433,05-434,7 Mhz 10 mW
        https://nkrzi.gov.ua/images/upload/256/5810/PDF_UUZ_19_01_2016.pdf
    */
    RDEF(UA_433, 433.0f, 434.7f, 10, 0, 0, 10, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        868,0-868,6 Mhz 25 mW
        https://nkrzi.gov.ua/images/upload/256/5810/PDF_UUZ_19_01_2016.pdf
    */
    RDEF(UA_868, 868.0f, 868.6f, 1, 0, 0, 14, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Malaysia
        433 - 435 MHz at 100mW, no restrictions.
        https://www.mcmc.gov.my/skmmgovmy/media/General/pdf/Short-Range-Devices-Specification.pdf
    */
    RDEF(MY_433, 433.0f, 435.0f, 100, 0, 0, 20, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Malaysia
        919 - 923 Mhz at 500mW, no restrictions.
        923 - 924 MHz at 500mW with 1% duty cycle OR frequency hopping.
        Frequency hopping is used for 919 - 923 MHz.
        https://www.mcmc.gov.my/skmmgovmy/media/General/pdf/Short-Range-Devices-Specification.pdf
    */
    RDEF(MY_919, 919.0f, 924.0f, 100, 0, 0, 27, true, true, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Singapore
        SG_923 Band 30d: 917 - 925 MHz at 100mW, no restrictions.
        https://www.imda.gov.sg/-/media/imda/files/regulation-licensing-and-consultations/ict-standards/telecommunication-standards/radio-comms/imdatssrd.pdf
    */
    RDEF(SG_923, 917.0f, 925.0f, 100, 0, 0, 20, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Philippines
                433 - 434.7 MHz <10 mW erp, NTC approved device required
                868 - 869.4 MHz <25 mW erp, NTC approved device required
                915 - 918 MHz <250 mW EIRP, no external antenna allowed
                https://github.com/meshtastic/firmware/issues/4948#issuecomment-2394926135
    */

    RDEF(PH_433, 433.0f, 434.7f, 100, 0, 0, 10, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),
    RDEF(PH_868, 868.0f, 869.4f, 100, 0, 0, 14, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),
    RDEF(PH_915, 915.0f, 918.0f, 100, 0, 0, 24, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Kazakhstan
                433.075 - 434.775 MHz <10 mW EIRP, Low Powered Devices (LPD)
                863 - 868 MHz <25 mW EIRP, 500kHz channels allowed, must not be used at airfields
                                https://github.com/meshtastic/firmware/issues/7204
    */
    RDEF(KZ_433, 433.075f, 434.775f, 100, 0, 0, 10, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),
    RDEF(KZ_863, 863.0f, 868.0f, 100, 0, 0, 30, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Nepal
        865 MHz to 868 MHz frequency band for IoT (Internet of Things), M2M (Machine-to-Machine), and smart metering use,
       specifically in non-cellular mode. https://www.nta.gov.np/uploads/contents/Radio-Frequency-Policy-2080-English.pdf
    */
    RDEF(NP_865, 865.0f, 868.0f, 100, 0, 0, 30, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        Brazil
        902 - 907.5 MHz , 1W power limit, no duty cycle restrictions
        https://github.com/meshtastic/firmware/issues/3741
    */
    RDEF(BR_902, 902.0f, 907.5f, 100, 0, 0, 30, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        EU 866MHz band (Band no. 46b of 2006/771/EC and subsequent amendments) for Non-specific short-range devices (SRD)
        Gives 4 channels at 865.7/866.3/866.9/867.5 MHz, 400 kHz gap plus 37.5 kHz padding between channels, 27 dBm,
        duty cycle 2.5% (mobile) or 10% (fixed) https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:02006D0771(01)-20250123
    */
    RDEF(EU_866, 865.6f, 867.6f, 2.5, 0.4, 0.0375f, 27, false, false, false, false, 0, 99, 99, 0, LITE_FAST, PRESETS_LITE),

    /*
        EU 868MHz band: 3 channels at 869.410/869.4625/869.577 MHz
        Channel centres at 869.442/869.525/869.608 MHz,
        10.4 kHz padding on channels, 27 dBm, duty cycle 10%
    */
    RDEF(NARROW_868, 869.4f, 869.65f, 10, 0, 0.0104f, 27, false, false, false, false, 0, 0, 0, 1, NARROW_FAST, PRESETS_NARROW),

    /*
       2.4 GHZ WLAN Band equivalent. Only for SX128x chips.
    */
    RDEF(LORA_24, 2400.0f, 2483.5f, 100, 0, 0, 10, true, false, true, false, 0, 0, 0, 0, LONG_FAST, PRESETS_STD),

    /*
        This needs to be last. Same as US.
    */
    RDEF(UNSET, 902.0f, 928.0f, 100, 0, 0, 30, true, false, false, false, 0, 0, 0, 0, LONG_FAST, PRESETS_UNDEF)

};

const RegionInfo *myRegion;
bool RadioInterface::uses_default_frequency_slot = true; // this is modified in init region

static uint8_t bytes[MAX_LORA_PAYLOAD_LEN + 1];

// Global LoRa radio type
LoRaRadioType radioType = NO_RADIO;

extern RadioInterface *rIf;
extern RadioLibHal *RadioLibHAL;
#if defined(HW_SPI1_DEVICE) && defined(ARCH_ESP32)
extern SPIClass SPI1;
#endif

bool initLoRa()
{
    if (rIf != nullptr) {
        delete rIf;
        rIf = nullptr;
    }

#if ARCH_PORTDUINO
    SPISettings spiSettings(portduino_config.spiSpeed, MSBFIRST, SPI_MODE0);
#else
    SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);
#endif

#ifdef ARCH_PORTDUINO
    // as one can't use a function pointer to the class constructor:
    auto loraModuleInterface = [](LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                  RADIOLIB_PIN_TYPE busy) {
        switch (portduino_config.lora_module) {
        case use_rf95:
            return (RadioInterface *)new RF95Interface(hal, cs, irq, rst, busy);
        case use_sx1262:
            return (RadioInterface *)new SX1262Interface(hal, cs, irq, rst, busy);
        case use_sx1268:
            return (RadioInterface *)new SX1268Interface(hal, cs, irq, rst, busy);
        case use_sx1280:
            return (RadioInterface *)new SX1280Interface(hal, cs, irq, rst, busy);
        case use_lr1110:
            return (RadioInterface *)new LR1110Interface(hal, cs, irq, rst, busy);
        case use_lr1120:
            return (RadioInterface *)new LR1120Interface(hal, cs, irq, rst, busy);
        case use_lr1121:
            return (RadioInterface *)new LR1121Interface(hal, cs, irq, rst, busy);
        case use_llcc68:
            return (RadioInterface *)new LLCC68Interface(hal, cs, irq, rst, busy);
        case use_simradio:
            return (RadioInterface *)new SimRadio;
        default:
            assert(0); // shouldn't happen
            return (RadioInterface *)nullptr;
        }
    };

    LOG_DEBUG("Activate %s radio on SPI port %s", portduino_config.loraModules[portduino_config.lora_module].c_str(),
              portduino_config.lora_spi_dev.c_str());
    if (portduino_config.lora_spi_dev == "ch341") {
        RadioLibHAL = ch341Hal;
    } else {
        if (RadioLibHAL != nullptr) {
            delete RadioLibHAL;
            RadioLibHAL = nullptr;
        }
        RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
    }
    rIf =
        loraModuleInterface((LockingArduinoHal *)RadioLibHAL, portduino_config.lora_cs_pin.pin, portduino_config.lora_irq_pin.pin,
                            portduino_config.lora_reset_pin.pin, portduino_config.lora_busy_pin.pin);

    if (!rIf->init()) {
        LOG_WARN("No %s radio", portduino_config.loraModules[portduino_config.lora_module].c_str());
        delete rIf;
        rIf = NULL;
        exit(EXIT_FAILURE);
    } else {
        LOG_INFO("%s init success", portduino_config.loraModules[portduino_config.lora_module].c_str());
    }

#elif defined(HW_SPI1_DEVICE)
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI1, spiSettings);
#else // HW_SPI1_DEVICE
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
#endif

// radio init MUST BE AFTER service.init, so we have our radio config settings (from nodedb init)
#if defined(USE_STM32WLx)
    if (!rIf) {
        rIf = new STM32WLE5JCInterface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No STM32WL radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("STM32WL init success");
            radioType = STM32WLx_RADIO;
        }
    }
#endif

#if defined(RF95_IRQ) && RADIOLIB_EXCLUDE_SX127X != 1
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new RF95Interface(RadioLibHAL, LORA_CS, RF95_IRQ, RF95_RESET, RF95_DIO1);
        if (!rIf->init()) {
            LOG_WARN("No RF95 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("RF95 init success");
            radioType = RF95_RADIO;
        }
    }
#endif

#if defined(USE_SX1262) && !defined(ARCH_PORTDUINO) && !defined(TCXO_OPTIONAL) && RADIOLIB_EXCLUDE_SX126X != 1
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        auto *sxIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
#ifdef SX126X_DIO3_TCXO_VOLTAGE
        sxIf->setTCXOVoltage(SX126X_DIO3_TCXO_VOLTAGE);
#endif
        if (!sxIf->init()) {
            LOG_WARN("No SX1262 radio");
            delete sxIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 init success");
            rIf = sxIf;
            radioType = SX1262_RADIO;
        }
    }
#endif

#if defined(USE_SX1262) && !defined(ARCH_PORTDUINO) && defined(TCXO_OPTIONAL)
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        // try using the specified TCXO voltage
        auto *sxIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        sxIf->setTCXOVoltage(SX126X_DIO3_TCXO_VOLTAGE);
        if (!sxIf->init()) {
            LOG_WARN("No SX1262 radio with TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            delete sxIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 init success, TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            rIf = sxIf;
            radioType = SX1262_RADIO;
        }
    }

    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        // If specified TCXO voltage fails, attempt to use DIO3 as a reference instead
        rIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No SX1262 radio with XTAL, Vref 0.0V");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1262 init success, XTAL, Vref 0.0V");
            radioType = SX1262_RADIO;
        }
    }
#endif

#if defined(USE_SX1268)
#if defined(SX126X_DIO3_TCXO_VOLTAGE) && defined(TCXO_OPTIONAL)
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        // try using the specified TCXO voltage
        auto *sxIf = new SX1268Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        sxIf->setTCXOVoltage(SX126X_DIO3_TCXO_VOLTAGE);
        if (!sxIf->init()) {
            LOG_WARN("No SX1268 radio with TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            delete sxIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1268 init success, TCXO, Vref %fV", SX126X_DIO3_TCXO_VOLTAGE);
            rIf = sxIf;
            radioType = SX1268_RADIO;
        }
    }
#endif
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new SX1268Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No SX1268 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1268 init success");
            radioType = SX1268_RADIO;
        }
    }
#endif

#if defined(USE_LLCC68)
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new LLCC68Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No LLCC68 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LLCC68 init success");
            radioType = LLCC68_RADIO;
        }
    }
#endif

#if defined(USE_LR1110) && RADIOLIB_EXCLUDE_LR11X0 != 1
    if ((!rIf) && (config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_LORA_24)) {
        rIf = new LR1110Interface(RadioLibHAL, LR1110_SPI_NSS_PIN, LR1110_IRQ_PIN, LR1110_NRESET_PIN, LR1110_BUSY_PIN);
        if (!rIf->init()) {
            LOG_WARN("No LR1110 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LR1110 init success");
            radioType = LR1110_RADIO;
        }
    }
#endif

#if defined(USE_LR1120) && RADIOLIB_EXCLUDE_LR11X0 != 1
    if (!rIf) {
        rIf = new LR1120Interface(RadioLibHAL, LR1120_SPI_NSS_PIN, LR1120_IRQ_PIN, LR1120_NRESET_PIN, LR1120_BUSY_PIN);
        if (!rIf->init()) {
            LOG_WARN("No LR1120 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LR1120 init success");
            radioType = LR1120_RADIO;
        }
    }
#endif

#if defined(USE_LR1121) && RADIOLIB_EXCLUDE_LR11X0 != 1
    if (!rIf) {
        rIf = new LR1121Interface(RadioLibHAL, LR1121_SPI_NSS_PIN, LR1121_IRQ_PIN, LR1121_NRESET_PIN, LR1121_BUSY_PIN);
        if (!rIf->init()) {
            LOG_WARN("No LR1121 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("LR1121 init success");
            radioType = LR1121_RADIO;
        }
    }
#endif

#if defined(USE_SX1280) && RADIOLIB_EXCLUDE_SX128X != 1
    if (!rIf) {
        rIf = new SX1280Interface(RadioLibHAL, SX128X_CS, SX128X_DIO1, SX128X_RESET, SX128X_BUSY);
        if (!rIf->init()) {
            LOG_WARN("No SX1280 radio");
            delete rIf;
            rIf = NULL;
        } else {
            LOG_INFO("SX1280 init success");
            radioType = SX1280_RADIO;
        }
    }
#endif

    // check if the radio chip matches the selected region
    if ((config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) && rIf && (!rIf->wideLora())) {
        LOG_WARN("LoRa chip does not support 2.4GHz. Revert to unset");
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        nodeDB->saveToDisk(SEGMENT_CONFIG);

        if (rIf && !rIf->reconfigure()) {
            LOG_WARN("Reconfigure failed, rebooting");
            if (screen) {
                screen->showSimpleBanner("Rebooting...");
            }
            rebootAtMsec = millis() + 5000;
        }
    }
    return rIf != nullptr;
}

void initRegion()
{
    const RegionInfo *r = regions;
#ifdef REGULATORY_LORA_REGIONCODE
    for (; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET && r->code != REGULATORY_LORA_REGIONCODE; r++)
        ;
    LOG_INFO("Wanted region %d, regulatory override to %s", config.lora.region, r->name);
#else
    for (; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET && r->code != config.lora.region; r++)
        ;
    LOG_INFO("Wanted region %d, using %s", config.lora.region, r->name);
#endif
    myRegion = r;
}

const RegionInfo *getRegion(meshtastic_Config_LoRaConfig_RegionCode code)
{
    const RegionInfo *r = regions;
    for (; r->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET && r->code != code; r++)
        ;
    return r;
}

/**
 * Get duty cycle for current region. EU_866: 10% for routers, 2.5% for mobile.
 */
float getEffectiveDutyCycle()
{
    if (myRegion->code == meshtastic_Config_LoRaConfig_RegionCode_EU_866) {
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
            config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE) {
            return 10.0f;
        } else {
            return 2.5f;
        }
    }
    // For all other regions, return the standard duty cycle
    return myRegion->dutyCycle;
}

/**
 * ## LoRaWAN for North America

LoRaWAN defines 64, 125 kHz channels from 902.3 to 914.9 MHz increments.

The maximum output power for North America is +30 dBM.

The band is from 902 to 928 MHz. It mentions channel number and its respective channel frequency. All the 13 channels are
separated by 2.16 MHz with respect to the adjacent channels. Channel zero starts at 903.08 MHz center frequency.
*/

uint32_t RadioInterface::getPacketTime(const meshtastic_MeshPacket *p, bool received)
{
    uint32_t pl = 0;
    if (p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        pl = p->encrypted.size + sizeof(PacketHeader);
    } else {
        size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_Data_msg, &p->decoded);
        pl = numbytes + sizeof(PacketHeader);
    }
    return getPacketTime(pl, received);
}

/** The delay to use for retransmitting dropped packets */
uint32_t RadioInterface::getRetransmissionMsec(const meshtastic_MeshPacket *p)
{
    size_t numbytes = p->which_payload_variant == meshtastic_MeshPacket_decoded_tag
                          ? pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_Data_msg, &p->decoded)
                          : p->encrypted.size + MESHTASTIC_HEADER_LENGTH;
    uint32_t packetAirtime = getPacketTime(numbytes + sizeof(PacketHeader));
    // Make sure enough time has elapsed for this packet to be sent and an ACK is received.
    // LOG_DEBUG("Waiting for flooding message with airtime %d and slotTime is %d", packetAirtime, slotTimeMsec);
    float channelUtil = airTime->channelUtilizationPercent();
    uint8_t CWsize = map(channelUtil, 0, 100, CWmin, CWmax);
    // Assuming we pick max. of CWsize and there will be a client with SNR at half the range
    return 2 * packetAirtime + (pow_of_2(CWsize) + 2 * CWmax + pow_of_2(int((CWmax + CWmin) / 2))) * slotTimeMsec +
           PROCESSING_TIME_MSEC;
}

/** The delay to use when we want to send something */
uint32_t RadioInterface::getTxDelayMsec()
{
    /** We wait a random multiple of 'slotTimes' (see definition in header file) in order to avoid collisions.
    The pool to take a random multiple from is the contention window (CW), which size depends on the
    current channel utilization. */
    float channelUtil = airTime->channelUtilizationPercent();
    uint8_t CWsize = map(channelUtil, 0, 100, CWmin, CWmax);
    // LOG_DEBUG("Current channel utilization is %f so setting CWsize to %d", channelUtil, CWsize);
    return random(0, pow_of_2(CWsize)) * slotTimeMsec;
}

/** The CW size to use when calculating SNR_based delays */
uint8_t RadioInterface::getCWsize(float snr)
{
    // The minimum value for a LoRa SNR
    const int32_t SNR_MIN = -20;

    // The maximum value for a LoRa SNR
    const int32_t SNR_MAX = 10;

    return map(snr, SNR_MIN, SNR_MAX, CWmin, CWmax);
}

/** The worst-case SNR_based packet delay */
uint32_t RadioInterface::getTxDelayMsecWeightedWorst(float snr)
{
    uint8_t CWsize = getCWsize(snr);
    // offset the maximum delay for routers: (2 * CWmax * slotTimeMsec)
    return (2 * CWmax * slotTimeMsec) + pow_of_2(CWsize) * slotTimeMsec;
}

/** Returns true if we should rebroadcast early like a ROUTER */
bool RadioInterface::shouldRebroadcastEarlyLikeRouter(meshtastic_MeshPacket *p)
{
    // If we are a ROUTER, we always rebroadcast early
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER) {
        return true;
    }

    return false;
}

/** The delay to use when we want to flood a message */
uint32_t RadioInterface::getTxDelayMsecWeighted(meshtastic_MeshPacket *p)
{
    //  high SNR = large CW size (Long Delay)
    //  low SNR = small CW size (Short Delay)
    float snr = p->rx_snr;
    uint32_t delay = 0;
    uint8_t CWsize = getCWsize(snr);
    // LOG_DEBUG("rx_snr of %f so setting CWsize to:%d", snr, CWsize);
    if (shouldRebroadcastEarlyLikeRouter(p)) {
        delay = random(0, 2 * CWsize) * slotTimeMsec;
        LOG_DEBUG("rx_snr found in packet. Router: setting tx delay:%d", delay);
    } else {
        // offset the maximum delay for routers: (2 * CWmax * slotTimeMsec)
        delay = (2 * CWmax * slotTimeMsec) + random(0, pow_of_2(CWsize)) * slotTimeMsec;
        LOG_DEBUG("rx_snr found in packet. Setting tx delay:%d", delay);
    }

    return delay;
}

void printPacket(const char *prefix, const meshtastic_MeshPacket *p)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    std::string out =
        DEBUG_PORT.mt_sprintf("%s (id=0x%08x fr=0x%08x to=0x%08x, transport = %u, WantAck=%d, HopLim=%d Ch=0x%x", prefix, p->id,
                              p->from, p->to, p->transport_mechanism, p->want_ack, p->hop_limit, p->channel);
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        auto &s = p->decoded;

        out += DEBUG_PORT.mt_sprintf(" Portnum=%d", s.portnum);

        if (s.want_response)
            out += DEBUG_PORT.mt_sprintf(" WANTRESP");

        if (p->pki_encrypted)
            out += DEBUG_PORT.mt_sprintf(" PKI");

        if (s.source != 0)
            out += DEBUG_PORT.mt_sprintf(" source=%08x", s.source);

        if (s.dest != 0)
            out += DEBUG_PORT.mt_sprintf(" dest=%08x", s.dest);

        if (s.request_id)
            out += DEBUG_PORT.mt_sprintf(" requestId=%0x", s.request_id);

        /* now inside Data and therefore kinda opaque
        if (s.which_ackVariant == SubPacket_success_id_tag)
            out += DEBUG_PORT.mt_sprintf(" successId=%08x", s.ackVariant.success_id);
        else if (s.which_ackVariant == SubPacket_fail_id_tag)
            out += DEBUG_PORT.mt_sprintf(" failId=%08x", s.ackVariant.fail_id); */
    } else {
        out += " encrypted";
        out += DEBUG_PORT.mt_sprintf(" len=%d", p->encrypted.size + sizeof(PacketHeader));
    }

    if (p->rx_time != 0)
        out += DEBUG_PORT.mt_sprintf(" rxtime=%u", p->rx_time);
    if (p->rx_snr != 0.0)
        out += DEBUG_PORT.mt_sprintf(" rxSNR=%g", p->rx_snr);
    if (p->rx_rssi != 0)
        out += DEBUG_PORT.mt_sprintf(" rxRSSI=%i", p->rx_rssi);
    if (p->via_mqtt != 0)
        out += DEBUG_PORT.mt_sprintf(" via MQTT");
    if (p->hop_start != 0)
        out += DEBUG_PORT.mt_sprintf(" hopStart=%d", p->hop_start);
    if (p->next_hop != 0)
        out += DEBUG_PORT.mt_sprintf(" nextHop=0x%x", p->next_hop);
    if (p->relay_node != 0)
        out += DEBUG_PORT.mt_sprintf(" relay=0x%x", p->relay_node);
    if (p->priority != 0)
        out += DEBUG_PORT.mt_sprintf(" priority=%d", p->priority);

    out += ")";
    LOG_DEBUG("%s", out.c_str());
#endif
}

RadioInterface::RadioInterface()
{
    assert(sizeof(PacketHeader) == MESHTASTIC_HEADER_LENGTH); // make sure the compiler did what we expected
}

bool RadioInterface::reconfigure()
{
    applyModemConfig();
    return true;
}

bool RadioInterface::init()
{
    LOG_INFO("Start meshradio init");

    configChangedObserver.observe(&service->configChanged);
    preflightSleepObserver.observe(&preflightSleep);
    notifyDeepSleepObserver.observe(&notifyDeepSleep);

    // we now expect interfaces to operate in promiscuous mode
    // radioIf.setThisAddress(nodeDB->getNodeNum()); // Note: we must do this here, because the nodenum isn't inited at
    // constructor time.

    applyModemConfig();

    return true;
}

int RadioInterface::notifyDeepSleepCb(void *unused)
{
    sleep();
    return 0;
}

/** hash a string into an integer
 *
 * djb2 by Dan Bernstein.
 * http://www.cse.yorku.ca/~oz/hash.html
 */
uint32_t hash(const char *str)
{
    uint32_t hash = 5381;
    int c;

    while ((c = *str++) != 0)
        hash = ((hash << 5) + hash) + (unsigned char)c; /* hash * 33 + c */

    return hash;
}

/**
 * Save our frequency for later reuse.
 */
void RadioInterface::saveFreq(float freq)
{
    savedFreq = freq;
}

/**
 * Save our channel for later reuse.
 */
void RadioInterface::saveChannelNum(uint32_t channel_num)
{
    savedChannelNum = channel_num;
}

/**
 * Save our frequency for later reuse.
 */
float RadioInterface::getFreq()
{
    return savedFreq;
}

/**
 * Save our channel for later reuse.
 */
uint32_t RadioInterface::getChannelNum()
{
    return savedChannelNum;
}

struct ModemConfig {
    float bw;
    uint8_t sf;
    uint8_t cr;
};

ModemConfig settingsForPreset(bool wide, meshtastic_Config_LoRaConfig_ModemPreset preset)
{ // Add throttle/dethrottles to each of these?
    ModemConfig cfg = {0};
    switch (preset) {
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:
        cfg.bw = wide ? 1625.0 : 500;
        cfg.cr = 5;
        cfg.sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:
        cfg.bw = wide ? 812.5 : 250;
        cfg.cr = 5;
        cfg.sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:
        cfg.bw = wide ? 812.5 : 250;
        cfg.cr = 5;
        cfg.sf = 8;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:
        cfg.bw = wide ? 812.5 : 250;
        cfg.cr = 5;
        cfg.sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:
        cfg.bw = wide ? 812.5 : 250;
        cfg.cr = 5;
        cfg.sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:
        cfg.bw = wide ? 1625.0 : 500;
        cfg.cr = 8;
        cfg.sf = 11;
        break;
    default: // Config_LoRaConfig_ModemPreset_LONG_FAST is default. Gracefully use this is preset is something illegal.
        cfg.bw = wide ? 812.5 : 250;
        cfg.cr = 5;
        cfg.sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:
        cfg.bw = wide ? 406.25 : 125;
        cfg.cr = 8;
        cfg.sf = 11;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:
        cfg.bw = wide ? 406.25 : 125;
        cfg.cr = 8;
        cfg.sf = 12;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST:
        cfg.bw = 125;
        cfg.cr = 5;
        cfg.sf = 9;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW:
        cfg.bw = 125;
        cfg.cr = 5;
        cfg.sf = 10;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST:
        cfg.bw = 62.5;
        cfg.cr = 6;
        cfg.sf = 7;
        break;
    case meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW:
        cfg.bw = 62.5;
        cfg.cr = 6;
        cfg.sf = 8;
        break;
    }
    return cfg;
}

bool RadioInterface::validateModemConfig(meshtastic_Config_LoRaConfig &loraConfig)
{
    bool validConfig = true;
    char err_string[160];

    const RegionInfo *newRegion = getRegion(loraConfig.region);
    if (!newRegion) { // copilot said I had to check for null pointer
        LOG_ERROR("Invalid region code %d", loraConfig.region);
        return false;
    }

    if (newRegion->licensedOnly && !devicestate.owner.is_licensed) {
        snprintf(err_string, sizeof(err_string), "Selected region %s is not permitted without licensed mode activated",
                 newRegion->name);

        LOG_ERROR("%s", err_string);
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        cn->level = meshtastic_LogRecord_Level_ERROR;
        snprintf(cn->message, sizeof(cn->message), "%s", err_string);
        service->sendClientNotification(cn);
        return false;
        LOG_WARN("Region code %s not permitted without license, reverting", newRegion->name);
        return false;
    }

    auto cfg = settingsForPreset(newRegion->wideLora, loraConfig.modem_preset);

    // early check - if we use preset, make sure it's on available preset list
    if (loraConfig.use_preset) {
        bool preset_valid = false;

        for (size_t i = 0; i < sizeof(newRegion->availablePresets);
             i++) { // copilot says int should be size_t or auto : preset ???
            if (loraConfig.modem_preset == newRegion->availablePresets[i]) {
                preset_valid = true;
                break;
            }
        }

        if (!preset_valid) {
            const char *presetName =
                DisplayFormatters::getModemPresetDisplayName(loraConfig.modem_preset, false, loraConfig.use_preset);

            snprintf(err_string, sizeof(err_string), "Selected preset %s is not on a list of available presets for region %s",
                     presetName, newRegion->name);

            LOG_ERROR("%s", err_string);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            cn->level = meshtastic_LogRecord_Level_ERROR;
            snprintf(cn->message, sizeof(cn->message), "%s", err_string);
            service->sendClientNotification(cn);
            return false;
        }
    } // end if use_preset

    float bw;
    if (loraConfig.use_preset) {
        bw = cfg.bw;
    } else {
        bw = loraConfig.bandwidth;
    }

    // this is probably wrong (?) as you can still select last channel in a band, set
    // wide bandwidth and transmit outside the band and the check will not catch it // phaseloop
    // this only makes sense if you happen to be in the center of the region band
    if ((newRegion->freqEnd - newRegion->freqStart) < bw / 1000) {
        const float regionSpanKHz = (newRegion->freqEnd - newRegion->freqStart) * 1000.0f;
        const float requestedBwKHz = bw;
        const bool isWideRequest = requestedBwKHz >= 499.5f; // treat as 500 kHz preset
        const char *presetName =
            DisplayFormatters::getModemPresetDisplayName(loraConfig.modem_preset, false, loraConfig.use_preset);
        const char *defaultPresetName = DisplayFormatters::getModemPresetDisplayName(newRegion->defaultPreset, false, true);

        // actual falling back is done in applyModemSettings()
        if (isWideRequest) {
            snprintf(err_string, sizeof(err_string), "%s region too narrow for 500kHz preset (%s). Falling back to %s.",
                     newRegion->name, presetName, defaultPresetName);
        } else {
            snprintf(err_string, sizeof(err_string), "%s region span %.0fkHz < requested %.0fkHz. Falling back to %s.",
                     newRegion->name, regionSpanKHz, requestedBwKHz, defaultPresetName);
        }
        LOG_ERROR("%s", err_string);
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        cn->level = meshtastic_LogRecord_Level_ERROR;
        snprintf(cn->message, sizeof(cn->message), "%s", err_string);
        service->sendClientNotification(cn);

        // Set to default modem preset
        loraConfig.use_preset = true;
        loraConfig.modem_preset = newRegion->defaultPreset;
    }

    return validConfig;
}

/**
 * Pull our channel settings etc... from protobufs to the dumb interface settings
 */
void RadioInterface::applyModemConfig()
{
    // Set up default configuration
    // No Sync Words in LORA mode
    meshtastic_Config_LoRaConfig &loraConfig = config.lora;
    const RegionInfo *newRegion = getRegion(loraConfig.region);

    if (loraConfig.use_preset) {
        if (!validateModemConfig(loraConfig)) {
            loraConfig.modem_preset = newRegion->defaultPreset;
        }

        auto settings = settingsForPreset(myRegion->wideLora, loraConfig.modem_preset);
        sf = settings.sf;
        bw = settings.bw;
        // If custom CR is being used already, check if the new preset is higher
        if (loraConfig.coding_rate >= 5 && loraConfig.coding_rate <= 8 && loraConfig.coding_rate < settings.cr) {
            cr = settings.cr;
            LOG_INFO("Default Coding Rate is higher than custom setting, using %u", cr);
        }
        // If the custom CR is higher than the preset, use it
        else if (loraConfig.coding_rate >= 5 && loraConfig.coding_rate <= 8 && loraConfig.coding_rate > settings.cr) {
            cr = loraConfig.coding_rate;
            LOG_INFO("Using custom Coding Rate %u", cr);
        } else {
            sf = loraConfig.spread_factor;
            cr = loraConfig.coding_rate;
            bw = loraConfig.bandwidth;

            if (bw == 31) // This parameter is not an integer
                bw = 31.25;
            if (bw == 62) // Fix for 62.5Khz bandwidth
                bw = 62.5;
            if (bw == 200)
                bw = 203.125;
            if (bw == 400)
                bw = 406.25;
            if (bw == 800)
                bw = 812.5;
            if (bw == 1600)
                bw = 1625.0;
        }

        if ((myRegion->freqEnd - myRegion->freqStart) < bw / 1000) {
            const float regionSpanKHz = (myRegion->freqEnd - myRegion->freqStart) * 1000.0f;
            const float requestedBwKHz = bw;
            const bool isWideRequest = requestedBwKHz >= 499.5f; // treat as 500 kHz preset
            const char *presetName =
                DisplayFormatters::getModemPresetDisplayName(loraConfig.modem_preset, false, loraConfig.use_preset);

            char err_string[160];
            if (isWideRequest) {
                snprintf(err_string, sizeof(err_string), "%s region too narrow for 500kHz preset (%s). Falling back to LongFast.",
                         myRegion->name, presetName);
            } else {
                snprintf(err_string, sizeof(err_string), "%s region span %.0fkHz < requested %.0fkHz. Falling back to LongFast.",
                         myRegion->name, regionSpanKHz, requestedBwKHz);
            }
            LOG_ERROR("%s", err_string);
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_INVALID_RADIO_SETTING);

            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            cn->level = meshtastic_LogRecord_Level_ERROR;
            snprintf(cn->message, sizeof(cn->message), "%s", err_string);
            service->sendClientNotification(cn);

            // Set to default modem preset
            loraConfig.use_preset = true;
        }

        auto settings = settingsForPreset(myRegion->wideLora, loraConfig.modem_preset);
        sf = settings.sf;
        cr = settings.cr;
        bw = settings.bw;
    }

    power = loraConfig.tx_power;

    if ((power == 0) || ((power > myRegion->powerLimit) && !devicestate.owner.is_licensed))
        power = myRegion->powerLimit;

    if (power == 0)
        power = 17; // Default to this power level if we don't have a valid regional power limit (powerLimit of myRegion defaults
                    // to 0, currently no region has an actual power limit of 0 [dBm] so we can assume regions which have this
                    // variable set to 0 don't have a valid power limit)

    // Set final tx_power back onto config
    loraConfig.tx_power = (int8_t)power; // cppcheck-suppress assignmentAddressToInteger

    // Calculate number of channels:
    // spacing = gap between channels (0 for continuous spectrum) and at the beginning of the band
    // padding = gap at the beginning and end of the channel (0 for no padding)
    float channelSpacing = myRegion->spacing + (myRegion->padding * 2) + (bw / 1000); // in MHz
    uint32_t numChannels = round((myRegion->freqEnd - myRegion->freqStart + myRegion->spacing) / channelSpacing);

    // If user has manually specified a channel num, then use that, otherwise generate one by hashing the name
    const char *channelName = channels.getName(channels.getPrimaryIndex());
    // channel_num is actually (channel_num - 1), since modulus (%) returns values from 0 to (numChannels - 1)
    uint32_t channel_num = (loraConfig.channel_num ? loraConfig.channel_num - 1 : hash(channelName)) % numChannels;

    // Check if we use the default frequency slot
    if (myRegion->overrideSlot == 0) {
        RadioInterface::uses_default_frequency_slot = true;
        channel_num =
            hash(DisplayFormatters::getModemPresetDisplayName(config.lora.modem_preset, false, config.lora.use_preset)) %
            numChannels;
    }
    // If we have an override slot, use it
    // Note: overrideSlot is the same, regardless of which preset we use, so it should only be used where presets are limited.
    else {
        RadioInterface::uses_default_frequency_slot = false;
        channel_num = myRegion->overrideSlot - 1;
    }

    // Calculate frequency: freqStart is band edge, add half bandwidth (plus optional padding) to get middle of first channel
    // subsequent channels are spaced by channelSpacing
    float freq = myRegion->freqStart + (bw / 2000) + myRegion->padding + (channel_num * channelSpacing); // in MHz

    // override if we have a verbatim frequency
    if (loraConfig.override_frequency) {
        freq = loraConfig.override_frequency;
        channel_num = -1;
    }

    saveChannelNum(channel_num);
    saveFreq(freq + loraConfig.frequency_offset);

    slotTimeMsec = computeSlotTimeMsec();
    preambleTimeMsec = preambleLength * (pow_of_2(sf) / bw);

    LOG_INFO("Radio freq=%.3f, config.lora.frequency_offset=%.3f", freq, loraConfig.frequency_offset);
    LOG_INFO("Set radio: region=%s, name=%s, config=%u, ch=%d, power=%d", myRegion->name, channelName, loraConfig.modem_preset,
             channel_num, power);
    LOG_INFO("myRegion->freqStart -> myRegion->freqEnd: %f -> %f (%f MHz)", myRegion->freqStart, myRegion->freqEnd,
             myRegion->freqEnd - myRegion->freqStart);
    LOG_INFO("numChannels: %d x %.3fkHz", numChannels, bw);
    if (myRegion->overrideSlot != 0) {
        LOG_INFO("Using region override slot: %d", myRegion->overrideSlot);
    }
    LOG_INFO("channel_num: %d", channel_num + 1);
    LOG_INFO("frequency: %f", getFreq());
    LOG_INFO("Slot time: %u msec, preamble time: %u msec", slotTimeMsec, preambleTimeMsec);
}

/** Slottime is the time to detect a transmission has started, consisting of:
  - CAD duration;
  - roundtrip air propagation time (assuming max. 30km between nodes);
  - Tx/Rx turnaround time (maximum of SX126x and SX127x);
  - MAC processing time (measured on T-beam) */
uint32_t RadioInterface::computeSlotTimeMsec()
{
    float sumPropagationTurnaroundMACTime = 0.2 + 0.4 + 7; // in milliseconds
    float symbolTime = pow_of_2(sf) / bw;                  // in milliseconds

    if (myRegion->wideLora) {
        // CAD duration derived from AN1200.22 of SX1280
        return (NUM_SYM_CAD_24GHZ + (2 * sf + 3) / 32) * symbolTime + sumPropagationTurnaroundMACTime;
    } else {
        // CAD duration for SX127x is max. 2.25 symbols, for SX126x it is number of symbols + 0.5 symbol
        return max(2.25, NUM_SYM_CAD + 0.5) * symbolTime + sumPropagationTurnaroundMACTime;
    }
}

/**
 * Some regulatory regions limit xmit power.
 * This function should be called by subclasses after setting their desired power.  It might lower it
 */
void RadioInterface::limitPower(int8_t loraMaxPower)
{
    uint8_t maxPower = 255; // No limit

    if (myRegion->powerLimit)
        maxPower = myRegion->powerLimit;

    if ((power > maxPower) && !devicestate.owner.is_licensed) {
        LOG_INFO("Lower transmit power because of regulatory limits");
        power = maxPower;
    }

#ifdef ARCH_PORTDUINO
    size_t num_pa_points = portduino_config.num_pa_points;
    const uint16_t *tx_gain = portduino_config.tx_gain_lora;
#else
    size_t num_pa_points = NUM_PA_POINTS;
    const uint16_t tx_gain[NUM_PA_POINTS] = {TX_GAIN_LORA};
#endif

    if (num_pa_points == 1) {
        if (tx_gain[0] > 0 && !devicestate.owner.is_licensed) {
            LOG_INFO("Requested Tx power: %d dBm; Device LoRa Tx gain: %d dB", power, tx_gain[0]);
            power -= tx_gain[0];
        }
    } else if (!devicestate.owner.is_licensed) {
        // we have an array of PA gain values.  Find the highest power setting that works.
        for (int radio_dbm = 0; radio_dbm < num_pa_points; radio_dbm++) {
            if (((radio_dbm + tx_gain[radio_dbm]) > power) ||
                ((radio_dbm == (num_pa_points - 1)) && ((radio_dbm + tx_gain[radio_dbm]) <= power))) {
                // we've exceeded the power limit, or hit the max we can do
                LOG_INFO("Requested Tx power: %d dBm; Device LoRa Tx gain: %d dB", power, tx_gain[radio_dbm]);
                power -= tx_gain[radio_dbm];
                break;
            }
        }
    }

    if (power > loraMaxPower) // Clamp power to maximum defined level
        power = loraMaxPower;

    LOG_INFO("Final Tx power: %d dBm", power);
}

void RadioInterface::deliverToReceiver(meshtastic_MeshPacket *p)
{
    if (router) {
        p->transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
        router->enqueueReceivedMessage(p);
    }
}

/***
 * given a packet set sendingPacket and decode the protobufs into radiobuf.  Returns # of payload bytes to send
 */
size_t RadioInterface::beginSending(meshtastic_MeshPacket *p)
{
    assert(!sendingPacket);

    // LOG_DEBUG("Send queued packet on mesh (txGood=%d,rxGood=%d,rxBad=%d)", rf95.txGood(), rf95.rxGood(), rf95.rxBad());
    assert(p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag); // It should have already been encoded by now

    radioBuffer.header.from = p->from;
    radioBuffer.header.to = p->to;
    radioBuffer.header.id = p->id;
    radioBuffer.header.channel = p->channel;
    radioBuffer.header.next_hop = p->next_hop;
    radioBuffer.header.relay_node = p->relay_node;
    if (p->hop_limit > HOP_MAX) {
        LOG_WARN("hop limit %d is too high, setting to %d", p->hop_limit, HOP_RELIABLE);
        p->hop_limit = HOP_RELIABLE;
    }
    radioBuffer.header.flags =
        p->hop_limit | (p->want_ack ? PACKET_FLAGS_WANT_ACK_MASK : 0) | (p->via_mqtt ? PACKET_FLAGS_VIA_MQTT_MASK : 0);
    radioBuffer.header.flags |= (p->hop_start << PACKET_FLAGS_HOP_START_SHIFT) & PACKET_FLAGS_HOP_START_MASK;

    // if the sender nodenum is zero, that means uninitialized
    assert(radioBuffer.header.from);
    assert(p->encrypted.size <= sizeof(radioBuffer.payload));
    memcpy(radioBuffer.payload, p->encrypted.bytes, p->encrypted.size);

    sendingPacket = p;
    return p->encrypted.size + sizeof(PacketHeader);
}