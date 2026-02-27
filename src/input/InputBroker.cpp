#include "InputBroker.h"
#include "PowerFSM.h" // needed for event trigger
#include "configuration.h"
#include "graphics/Screen.h"
#include "modules/ExternalNotificationModule.h"

#if ARCH_PORTDUINO
#include "input/LinuxInputImpl.h"
#include "input/SeesawRotary.h"
#include "platform/portduino/PortduinoGlue.h"
#endif

#if !MESHTASTIC_EXCLUDE_INPUTBROKER
#include "input/ExpressLRSFiveWay.h"
#include "input/RotaryEncoderImpl.h"
#include "input/RotaryEncoderInterruptImpl1.h"
#include "input/SerialKeyboardImpl.h"
#include "input/UpDownInterruptImpl1.h"
#include "input/i2cButton.h"
#if HAS_TRACKBALL
#include "input/TrackballInterruptImpl1.h"
#endif

#include "modules/StatusLEDModule.h"

#if !MESHTASTIC_EXCLUDE_I2C
#include "input/cardKbI2cImpl.h"
#endif
#include "input/kbMatrixImpl.h"
#endif

#if HAS_BUTTON || defined(ARCH_PORTDUINO)
#include "input/ButtonThread.h"

#if defined(BUTTON_PIN_TOUCH)
ButtonThread *TouchButtonThread = nullptr;
#if defined(TTGO_T_ECHO_PLUS) && defined(PIN_EINK_EN)
static bool touchBacklightWasOn = false;
static bool touchBacklightActive = false;
#endif
#endif

#if defined(BUTTON_PIN) || defined(ARCH_PORTDUINO)
ButtonThread *UserButtonThread = nullptr;
#endif

#if defined(ALT_BUTTON_PIN)
ButtonThread *BackButtonThread = nullptr;
#endif

#if defined(CANCEL_BUTTON_PIN)
ButtonThread *CancelButtonThread = nullptr;
#endif

#endif

InputBroker *inputBroker = nullptr;

InputBroker::InputBroker()
{
#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
    inputEventQueue = xQueueCreate(5, sizeof(InputEvent));
    pollSoonQueue = xQueueCreate(5, sizeof(InputPollable *));
    xTaskCreate(pollSoonWorker, "input-pollSoon", 2 * 1024, this, 10, &pollSoonTask);
#endif
}

void InputBroker::registerSource(Observable<const InputEvent *> *source)
{
    this->inputEventObserver.observe(source);
}

#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
void InputBroker::requestPollSoon(InputPollable *pollable)
{
    if (xPortInIsrContext() == pdTRUE) {
        xQueueSendFromISR(pollSoonQueue, &pollable, NULL);
    } else {
        xQueueSend(pollSoonQueue, &pollable, 0);
    }
}

void InputBroker::queueInputEvent(const InputEvent *event)
{
    if (xPortInIsrContext() == pdTRUE) {
        xQueueSendFromISR(inputEventQueue, event, NULL);
    } else {
        xQueueSend(inputEventQueue, event, portMAX_DELAY);
    }
}

void InputBroker::processInputEventQueue()
{
    InputEvent event;
    while (xQueueReceive(inputEventQueue, &event, 0)) {
        handleInputEvent(&event);
    }
}
#endif

int InputBroker::handleInputEvent(const InputEvent *event)
{
    powerFSM.trigger(EVENT_INPUT); // todo: not every input should wake, like long hold release

    if (event && event->inputEvent != INPUT_BROKER_NONE && externalNotificationModule &&
        moduleConfig.external_notification.enabled && externalNotificationModule->nagging()) {
        externalNotificationModule->stopNow();
    }

    this->notifyObservers(event);
    return 0;
}

#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
void InputBroker::pollSoonWorker(void *p)
{
    InputBroker *instance = (InputBroker *)p;
    while (true) {
        InputPollable *pollable = NULL;
        xQueueReceive(instance->pollSoonQueue, &pollable, portMAX_DELAY);
        if (pollable) {
            pollable->pollOnce();
        }
    }
    vTaskDelete(NULL);
}
#endif

void InputBroker::Init()
{

#ifdef BUTTON_PIN
#ifdef ARCH_ESP32

#if ESP_ARDUINO_VERSION_MAJOR >= 3
#ifdef BUTTON_NEED_PULLUP
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT_PULLUP);
#else
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT); // default to BUTTON_PIN
#endif
#else
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT); // default to BUTTON_PIN
#ifdef BUTTON_NEED_PULLUP
    gpio_pullup_en((gpio_num_t)(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN));
    delay(10);
#endif
#ifdef BUTTON_NEED_PULLUP2
    gpio_pullup_en((gpio_num_t)BUTTON_NEED_PULLUP2);
    delay(10);
#endif
#endif
#endif
#endif

// buttons are now inputBroker, so have to come after setupModules
#if HAS_BUTTON
    int pullup_sense = 0;
#ifdef INPUT_PULLUP_SENSE
    // Some platforms (nrf52) have a SENSE variant which allows wake from sleep - override what OneButton did
#ifdef BUTTON_SENSE_TYPE
    pullup_sense = BUTTON_SENSE_TYPE;
#else
    pullup_sense = INPUT_PULLUP_SENSE;
#endif
#endif
#if defined(ARCH_PORTDUINO)

    if (portduino_config.userButtonPin.enabled) {

        LOG_DEBUG("Use GPIO%02d for button", portduino_config.userButtonPin.pin);
        UserButtonThread = new ButtonThread("UserButton");
        if (screen) {
            ButtonConfig config;
            config.pinNumber = (uint8_t)portduino_config.userButtonPin.pin;
            config.activeLow = true;
            config.activePullup = true;
            config.pullupSense = INPUT_PULLUP;
            config.intRoutine = []() {
                UserButtonThread->userButton.tick();
                UserButtonThread->setIntervalFromNow(0);
                runASAP = true;
                BaseType_t higherWake = 0;
                concurrency::mainDelay.interruptFromISR(&higherWake);
            };
            config.singlePress = INPUT_BROKER_USER_PRESS;
            config.longPress = INPUT_BROKER_SELECT;
            UserButtonThread->initButton(config);
        }
    }
#endif

#ifdef BUTTON_PIN_TOUCH
    TouchButtonThread = new ButtonThread("BackButton");
    ButtonConfig touchConfig;
    touchConfig.pinNumber = BUTTON_PIN_TOUCH;
    touchConfig.activeLow = true;
    touchConfig.activePullup = true;
    touchConfig.pullupSense = pullup_sense;
    touchConfig.intRoutine = []() {
        TouchButtonThread->userButton.tick();
        TouchButtonThread->setIntervalFromNow(0);
        runASAP = true;
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
    };
    touchConfig.singlePress = INPUT_BROKER_NONE;
    touchConfig.longPress = INPUT_BROKER_BACK;
#if defined(TTGO_T_ECHO_PLUS) && defined(PIN_EINK_EN)
    // On T-Echo Plus the touch pad should only drive the backlight, not UI navigation/sounds
    touchConfig.longPress = INPUT_BROKER_NONE;
    touchConfig.suppressLeadUpSound = true;
    touchConfig.onPress = []() {
        touchBacklightWasOn = uiconfig.screen_brightness == 1;
        if (!touchBacklightWasOn) {
            digitalWrite(PIN_EINK_EN, HIGH);
        }
        touchBacklightActive = true;
    };
    touchConfig.onRelease = []() {
        if (touchBacklightActive && !touchBacklightWasOn) {
            digitalWrite(PIN_EINK_EN, LOW);
        }
        touchBacklightActive = false;
    };
#endif
    TouchButtonThread->initButton(touchConfig);
#endif

#if defined(CANCEL_BUTTON_PIN)
    // Buttons. Moved here cause we need NodeDB to be initialized
    CancelButtonThread = new ButtonThread("CancelButton");
    ButtonConfig cancelConfig;
    cancelConfig.pinNumber = CANCEL_BUTTON_PIN;
    cancelConfig.activeLow = CANCEL_BUTTON_ACTIVE_LOW;
    cancelConfig.activePullup = CANCEL_BUTTON_ACTIVE_PULLUP;
    cancelConfig.pullupSense = pullup_sense;
    cancelConfig.intRoutine = []() {
        CancelButtonThread->userButton.tick();
        CancelButtonThread->setIntervalFromNow(0);
        runASAP = true;
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
    };
    cancelConfig.singlePress = INPUT_BROKER_CANCEL;
    cancelConfig.longPress = INPUT_BROKER_SHUTDOWN;
    cancelConfig.longPressTime = 4000;
    CancelButtonThread->initButton(cancelConfig);
#endif

#if defined(ALT_BUTTON_PIN)
    // Buttons. Moved here cause we need NodeDB to be initialized
    BackButtonThread = new ButtonThread("BackButton");
    ButtonConfig backConfig;
    backConfig.pinNumber = ALT_BUTTON_PIN;
    backConfig.activeLow = ALT_BUTTON_ACTIVE_LOW;
    backConfig.activePullup = ALT_BUTTON_ACTIVE_PULLUP;
    backConfig.pullupSense = pullup_sense;
    backConfig.intRoutine = []() {
        BackButtonThread->userButton.tick();
        BackButtonThread->setIntervalFromNow(0);
        runASAP = true;
        BaseType_t higherWake = 0;
        concurrency::mainDelay.interruptFromISR(&higherWake);
    };
    backConfig.singlePress = INPUT_BROKER_ALT_PRESS;
    backConfig.longPress = INPUT_BROKER_ALT_LONG;
    backConfig.longPressTime = 500;
    BackButtonThread->initButton(backConfig);
#endif

#if defined(BUTTON_PIN)
#if defined(USERPREFS_BUTTON_PIN)
    int _pinNum = config.device.button_gpio ? config.device.button_gpio : USERPREFS_BUTTON_PIN;
#else
    int _pinNum = config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN;
#endif
#ifndef BUTTON_ACTIVE_LOW
#define BUTTON_ACTIVE_LOW true
#endif
#ifndef BUTTON_ACTIVE_PULLUP
#define BUTTON_ACTIVE_PULLUP true
#endif

    // Buttons. Moved here cause we need NodeDB to be initialized
    // If your variant.h has a BUTTON_PIN defined, go ahead and define BUTTON_ACTIVE_LOW and BUTTON_ACTIVE_PULLUP
    UserButtonThread = new ButtonThread("UserButton");
    if (screen) {
        ButtonConfig userConfig;
        userConfig.pinNumber = (uint8_t)_pinNum;
        userConfig.activeLow = BUTTON_ACTIVE_LOW;
        userConfig.activePullup = BUTTON_ACTIVE_PULLUP;
        userConfig.pullupSense = pullup_sense;
        userConfig.intRoutine = []() {
            UserButtonThread->userButton.tick();
            UserButtonThread->setIntervalFromNow(0);
            runASAP = true;
            BaseType_t higherWake = 0;
            concurrency::mainDelay.interruptFromISR(&higherWake);
        };
        userConfig.singlePress = INPUT_BROKER_USER_PRESS;
        userConfig.longPress = INPUT_BROKER_SELECT;
        userConfig.longPressTime = 500;
        userConfig.longLongPress = INPUT_BROKER_SHUTDOWN;
        UserButtonThread->initButton(userConfig);
    } else {
        ButtonConfig userConfigNoScreen;
        userConfigNoScreen.pinNumber = (uint8_t)_pinNum;
        userConfigNoScreen.activeLow = BUTTON_ACTIVE_LOW;
        userConfigNoScreen.activePullup = BUTTON_ACTIVE_PULLUP;
        userConfigNoScreen.pullupSense = pullup_sense;
        userConfigNoScreen.intRoutine = []() {
            UserButtonThread->userButton.tick();
            UserButtonThread->setIntervalFromNow(0);
            runASAP = true;
            BaseType_t higherWake = 0;
            concurrency::mainDelay.interruptFromISR(&higherWake);
        };
        userConfigNoScreen.singlePress = INPUT_BROKER_USER_PRESS;
        userConfigNoScreen.longPress = INPUT_BROKER_NONE;
        userConfigNoScreen.longPressTime = 500;
        userConfigNoScreen.longLongPress = INPUT_BROKER_SHUTDOWN;
        userConfigNoScreen.doublePress = INPUT_BROKER_SEND_PING;
        userConfigNoScreen.triplePress = INPUT_BROKER_GPS_TOGGLE;
        UserButtonThread->initButton(userConfigNoScreen);
    }
#endif
#endif

#if (HAS_BUTTON || ARCH_PORTDUINO) && !MESHTASTIC_EXCLUDE_INPUTBROKER
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
#if defined(T_LORA_PAGER)
        // use a special FSM based rotary encoder version for T-LoRa Pager
        rotaryEncoderImpl = new RotaryEncoderImpl();
        if (!rotaryEncoderImpl->init()) {
            delete rotaryEncoderImpl;
            rotaryEncoderImpl = nullptr;
        }
#elif defined(INPUTDRIVER_ENCODER_TYPE) && (INPUTDRIVER_ENCODER_TYPE == 2)
        upDownInterruptImpl1 = new UpDownInterruptImpl1();
        if (!upDownInterruptImpl1->init()) {
            delete upDownInterruptImpl1;
            upDownInterruptImpl1 = nullptr;
        }
#else
        rotaryEncoderInterruptImpl1 = new RotaryEncoderInterruptImpl1();
        if (!rotaryEncoderInterruptImpl1->init()) {
            delete rotaryEncoderInterruptImpl1;
            rotaryEncoderInterruptImpl1 = nullptr;
        }
#endif
        cardKbI2cImpl = new CardKbI2cImpl();
        cardKbI2cImpl->init();
#if defined(M5STACK_UNITC6L)
        i2cButton = new i2cButtonThread("i2cButtonThread");
#endif
#ifdef INPUTBROKER_MATRIX_TYPE
        kbMatrixImpl = new KbMatrixImpl();
        kbMatrixImpl->init();
#endif // INPUTBROKER_MATRIX_TYPE
#ifdef INPUTBROKER_SERIAL_TYPE
        aSerialKeyboardImpl = new SerialKeyboardImpl();
        aSerialKeyboardImpl->init();
#endif // INPUTBROKER_MATRIX_TYPE
    }
#endif // HAS_BUTTON
#if ARCH_PORTDUINO
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR && portduino_config.i2cdev != "") {
        seesawRotary = new SeesawRotary("SeesawRotary");
        if (!seesawRotary->init()) {
            delete seesawRotary;
            seesawRotary = nullptr;
        }
        aLinuxInputImpl = new LinuxInputImpl();
        aLinuxInputImpl->init();
    }
#endif
#if !MESHTASTIC_EXCLUDE_INPUTBROKER && HAS_TRACKBALL
    if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
        trackballInterruptImpl1 = new TrackballInterruptImpl1();
        trackballInterruptImpl1->init(TB_DOWN, TB_UP, TB_LEFT, TB_RIGHT, TB_PRESS);
    }
#endif
#ifdef INPUTBROKER_EXPRESSLRSFIVEWAY_TYPE
    expressLRSFiveWayInput = new ExpressLRSFiveWay();
#endif
}
