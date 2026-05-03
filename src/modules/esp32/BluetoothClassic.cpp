
#include "BluetoothClassic.h"
#include "BluetoothSerial.h"
#include "DebugConfiguration.h"
#include "configuration.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

const char *pin = "123456"; // Change this to more secure PIN.

BluetoothSerial SerialBT;

String message = "";
char incomingChar;

void BluetoothClassic::setup()
{
    // Bluetooth device name
    // TODO use getDeviceName()
    SerialBT.begin("meshtastic_btclassic");
    LOG_INFO("The device started, now you can pair it with bluetooth classic!");
    SerialBT.setPin(pin);
    LOG_INFO("Using PIN");
    // TODO setup callback
}

// Bluetooth Event Handler CallBack Function Definition
void BT_EventHandler(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (event == ESP_SPP_START_EVT) {
        LOG_INFO("Initialized SPP");
    } else if (event == ESP_SPP_SRV_OPEN_EVT) {
        LOG_INFO("Client connected");
    } else if (event == ESP_SPP_CLOSE_EVT) {
        LOG_INFO("Client disconnected");
    }
    // else if (event == ESP_SPP_DATA_IND_EVT ) {
    //   LOG_INFO("Data received");
    //   while (SerialBT.available()) {
    //     int incoming = SerialBT.read();
    //     LOG_INFO(incoming);
    //   }
    // }
}

void BluetoothClassic::sendLog(const uint8_t *logMessage, size_t length)
{
    // SerialBT.write(logMessage);
    if (SerialBT.available()) {
        char incomingChar = SerialBT.read();
        if (incomingChar != '\n') {
            message += String(incomingChar);
        } else {
            message = "";
        }
        // LOG_INFO(incomingChar);
    }
    delay(20);
}

// TODO
void BluetoothClassic::shutdown() {}
void BluetoothClassic::deinit() {}
void BluetoothClassic::clearBonds() {}
bool BluetoothClassic::isActive()
{
    return false;
}
bool BluetoothClassic::isConnected()
{
    return false;
}
int BluetoothClassic::getRssi()
{
    return 0;
}
