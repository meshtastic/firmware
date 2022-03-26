#include "configuration.h"
#include "SerialModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include <Arduino.h>

#include <assert.h>

/*
    SerialModule
        A simple interface to send messages over the mesh network by sending strings
        over a serial port.

        Default is to use RX GPIO 16 and TX GPIO 17.

    Need help with this module? Post your question on the Meshtastic Discourse:
       https://meshtastic.discourse.group

    Basic Usage:

        1) Enable the module by setting serial_module_enabled to 1.
        2) Set the pins (serial_module_rxd / serial_module_rxd) for your preferred RX and TX GPIO pins.
           On tbeam, recommend to use:
                RXD 35
                TXD 15
        3) Set serial_module_timeout to the amount of time to wait before we consider
           your packet as "done".
        4) (Optional) In SerialModule.h set the port to PortNum_TEXT_MESSAGE_APP if you want to
           send messages to/from the general text message channel.
        5) Connect to your device over the serial interface at 38400 8N1.
        6) Send a packet up to 240 bytes in length. This will get relayed over the mesh network.
        7) (Optional) Set serial_module_echo to 1 and any message you send out will be echoed back
           to your device.

    TODO (in this order):
        * Define a verbose RX mode to report on mesh and packet infomration.
            - This won't happen any time soon.

    KNOWN PROBLEMS
        * Until the module is initilized by the startup sequence, the TX pin is in a floating
          state. Device connected to that pin may see this as "noise".
        * Will not work on NRF and the Linux device targets.


*/

#define RXD2 16
#define TXD2 17
#define SERIAL_MODULE_RX_BUFFER 128
#define SERIAL_MODULE_STRING_MAX Constants_DATA_PAYLOAD_LEN
#define SERIAL_MODULE_TIMEOUT 250
#define SERIAL_MODULE_BAUD 38400
#define SERIAL_MODULE_ACK 1

SerialModule *serialModule;
SerialModuleRadio *serialModuleRadio;

SerialModule::SerialModule() : concurrency::OSThread("SerialModule") {}

char serialStringChar[Constants_DATA_PAYLOAD_LEN];

SerialModuleRadio::SerialModuleRadio() : SinglePortModule("SerialModuleRadio", PortNum_SERIAL_APP)
{
    // restrict to the admin channel for rx
    boundChannel = Channels::serialChannel;
}

int32_t SerialModule::runOnce()
{
#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the module
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.serial_module_enabled = 1;
    // radioConfig.preferences.serial_module_rxd = 35;
    // radioConfig.preferences.serial_module_txd = 15;
    // radioConfig.preferences.serial_module_timeout = 1000;
    // radioConfig.preferences.serial_module_echo = 1;

    if (radioConfig.preferences.serial_module_enabled) {

        if (firstTime) {

            // Interface with the serial peripheral from in here.
            DEBUG_MSG("Initializing serial peripheral interface\n");
            
            uint32_t baud = 0;

            if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_Default) {
                baud = 38400;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_2400) {
                baud = 2400;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_4800) {
                baud = 4800;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_9600) {
                baud = 9600;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_19200) {
                baud = 19200;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_38400) {
                baud = 38400;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_57600) {
                baud = 57600;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_115200) {
                baud = 115200;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_230400) {
                baud = 230400;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_460800) {
                baud = 460800;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_576000) {
                baud = 576000;

            } else if (radioConfig.preferences.serial_module_baud == RadioConfig_UserPreferences_Serial_Baud_BAUD_921600) {
                baud = 921600;


            }

            if (radioConfig.preferences.serial_module_rxd && radioConfig.preferences.serial_module_txd) {
                Serial2.begin(baud, SERIAL_8N1, radioConfig.preferences.serial_module_rxd,
                              radioConfig.preferences.serial_module_txd);

            } else {
                Serial2.begin(baud, SERIAL_8N1, RXD2, TXD2);
            }

            if (radioConfig.preferences.serial_module_timeout) {
                Serial2.setTimeout(
                    radioConfig.preferences.serial_module_timeout); // Number of MS to wait to set the timeout for the string.

            } else {
                Serial2.setTimeout(SERIAL_MODULE_TIMEOUT); // Number of MS to wait to set the timeout for the string.
            }

            Serial2.setRxBufferSize(SERIAL_MODULE_RX_BUFFER);

            serialModuleRadio = new SerialModuleRadio();

            firstTime = 0;

        } else {
            String serialString;

            while (Serial2.available()) {
                serialString = Serial2.readString();
                serialString.toCharArray(serialStringChar, Constants_DATA_PAYLOAD_LEN);

                serialModuleRadio->sendPayload();

                DEBUG_MSG("Received: %s\n", serialStringChar);
            }
        }

        return (10);
    } else {
        DEBUG_MSG("Serial Module Disabled\n");

        return (INT32_MAX);
    }
#else
    return INT32_MAX;
#endif
}

MeshPacket *SerialModuleRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void SerialModuleRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = SERIAL_MODULE_ACK;

    p->decoded.payload.size = strlen(serialStringChar); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, serialStringChar, p->decoded.payload.size);

    service.sendToMesh(p);
}

ProcessMessage SerialModuleRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (radioConfig.preferences.serial_module_enabled) {

        auto &p = mp.decoded;
        // DEBUG_MSG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n",
        //          nodeDB.getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

        if (getFrom(&mp) == nodeDB.getNodeNum()) {

            /*
             * If radioConfig.preferences.serial_module_echo is true, then echo the packets that are sent out back to the TX
             * of the serial interface.
             */
            if (radioConfig.preferences.serial_module_echo) {

                // For some reason, we get the packet back twice when we send out of the radio.
                //   TODO: need to find out why.
                if (lastRxID != mp.id) {
                    lastRxID = mp.id;
                    // DEBUG_MSG("* * Message came this device\n");
                    // Serial2.println("* * Message came this device");
                    Serial2.printf("%s", p.payload.bytes);
                }
            }

        } else {

            if (radioConfig.preferences.serial_module_mode == RadioConfig_UserPreferences_Serial_Mode_MODE_Default || radioConfig.preferences.serial_module_mode == RadioConfig_UserPreferences_Serial_Mode_MODE_SIMPLE) {
                // DEBUG_MSG("* * Message came from the mesh\n");
                // Serial2.println("* * Message came from the mesh");
                Serial2.printf("%s", p.payload.bytes);

            } else if (radioConfig.preferences.serial_module_mode == RadioConfig_UserPreferences_Serial_Mode_MODE_PROTO) {

            }
        }

    } else {
        DEBUG_MSG("Serial Module Disabled\n");
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}
