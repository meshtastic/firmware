#include "LoraInit.h"

// Global LoRa radio type
LoRaRadioType radioType = NO_RADIO;

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

        if (!rIf->reconfigure()) {
            LOG_WARN("Reconfigure failed, rebooting");
            if (screen) {
                screen->showSimpleBanner("Rebooting...");
            }
            rebootAtMsec = millis() + 5000;
        }
    }
}