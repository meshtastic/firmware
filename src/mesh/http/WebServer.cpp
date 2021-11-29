#include "main.h"
#include "mesh/http/WebServer.h"
#include "NodeDB.h"
#include "mesh/http/WiFiAPClient.h"
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>

#include <WebServer.h>
#include <WiFi.h>

#ifndef NO_ESP32
#include "esp_task_wdt.h"
#endif

// Persistant Data Storage
#include <Preferences.h>
Preferences prefs;

/*
  Including the esp32_https_server library will trigger a compile time error. I've
  tracked it down to a reoccurrance of this bug:
    https://gcc.gnu.org/bugzilla/show_bug.cgi?id=57824
  The work around is described here:
    https://forums.xilinx.com/t5/Embedded-Development-Tools/Error-with-Standard-Libaries-in-Zynq/td-p/450032

  Long story short is we need "#undef str" before including the esp32_https_server.
    - Jm Casler (jm@casler.org) Oct 2020
*/
#undef str

// Includes for the https server
//   https://github.com/fhessel/esp32_https_server
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <HTTPSServer.hpp>
#include <HTTPServer.hpp>
#include <SSLCert.hpp>

// The HTTPS Server comes in a separate namespace. For easier use, include it here.
using namespace httpsserver;
#include "mesh/http/ContentHandler.h"

static SSLCert *cert;
static HTTPSServer *secureServer;
static HTTPServer *insecureServer;

volatile bool isWebServerReady;
volatile bool isCertReady;

static void handleWebResponse()
{
    if (isWifiAvailable()) {

        if (isWebServerReady) {
            // We're going to handle the DNS responder here so it
            // will be ignored by the NRF boards.
            handleDNSResponse();

            if(secureServer)
                secureServer->loop();
            insecureServer->loop();
        }

        /*
            Slow down the CPU if we have not received a request within the last few
            seconds.
        */

        if (millis() - getTimeSpeedUp() >= (25 * 1000)) {
            setCpuFrequencyMhz(80);
            setTimeSpeedUp();
        }
    }
}

static void taskCreateCert(void *parameter)
{
    prefs.begin("MeshtasticHTTPS", false);

    // Delete the saved certs (used in debugging)
    if (0) {
        DEBUG_MSG("Deleting any saved SSL keys ...\n");
        // prefs.clear();
        prefs.remove("PK");
        prefs.remove("cert");
    }

    DEBUG_MSG("Checking if we have a previously saved SSL Certificate.\n");

    size_t pkLen = prefs.getBytesLength("PK");
    size_t certLen = prefs.getBytesLength("cert");

    if (pkLen && certLen) {
        DEBUG_MSG("Existing SSL Certificate found!\n");

        uint8_t *pkBuffer = new uint8_t[pkLen];
        prefs.getBytes("PK", pkBuffer, pkLen);

        uint8_t *certBuffer = new uint8_t[certLen];
        prefs.getBytes("cert", certBuffer, certLen);

        cert = new SSLCert(certBuffer, certLen, pkBuffer, pkLen);

        DEBUG_MSG("Retrieved Private Key: %d Bytes\n", cert->getPKLength());
        // DEBUG_MSG("Retrieved Private Key: " + String(cert->getPKLength()) + " Bytes");
        // for (int i = 0; i < cert->getPKLength(); i++)
        //  Serial.print(cert->getPKData()[i], HEX);
        // Serial.println();

        DEBUG_MSG("Retrieved Certificate: %d Bytes\n", cert->getCertLength());
        // for (int i = 0; i < cert->getCertLength(); i++)
        //  Serial.print(cert->getCertData()[i], HEX);
        // Serial.println();
    } else {
        DEBUG_MSG("Creating the certificate. This may take a while. Please wait...\n");
        yield();
        cert = new SSLCert();
        yield();
        int createCertResult = createSelfSignedCert(*cert, KEYSIZE_2048, "CN=meshtastic.local,O=Meshtastic,C=US",
                                                    "20190101000000", "20300101000000");
        yield();

        if (createCertResult != 0) {
            DEBUG_MSG("Creating the certificate failed\n");

            // Serial.printf("Creating the certificate failed. Error Code = 0x%02X, check SSLCert.hpp for details",
            //              createCertResult);
            // while (true)
            //    delay(500);
        } else {
            DEBUG_MSG("Creating the certificate was successful\n");

            DEBUG_MSG("Created Private Key: %d Bytes\n", cert->getPKLength());
            // for (int i = 0; i < cert->getPKLength(); i++)
            //  Serial.print(cert->getPKData()[i], HEX);
            // Serial.println();

            DEBUG_MSG("Created Certificate: %d Bytes\n", cert->getCertLength());
            // for (int i = 0; i < cert->getCertLength(); i++)
            //  Serial.print(cert->getCertData()[i], HEX);
            // Serial.println();

            prefs.putBytes("PK", (uint8_t *)cert->getPKData(), cert->getPKLength());
            prefs.putBytes("cert", (uint8_t *)cert->getCertData(), cert->getCertLength());
        }
    }

    isCertReady = true;

    // Must delete self, can't just fall out
    vTaskDelete(NULL);
}

void createSSLCert()
{
    if (isWifiAvailable() && !isCertReady) {

        // Create a new process just to handle creating the cert.
        //   This is a workaround for Bug: https://github.com/fhessel/esp32_https_server/issues/48
        //  jm@casler.org (Oct 2020)
        xTaskCreate(taskCreateCert, /* Task function. */
                    "createCert",   /* String with name of task. */
                    16384,          /* Stack size in bytes. */
                    NULL,           /* Parameter passed as input of the task */
                    16,             /* Priority of the task. */
                    NULL);          /* Task handle. */

        DEBUG_MSG("Waiting for SSL Cert to be generated.\n");
        int seconds = 0;
        while (!isCertReady) {
            DEBUG_MSG(".");
            delay(1000);
            yield();
            esp_task_wdt_reset();
            seconds++;
            if ((seconds == 3) && screen) {
                screen->setSSLFrames();
            }
        }
        DEBUG_MSG("SSL Cert Ready!\n");
    }
}

WebServerThread *webServerThread;

WebServerThread::WebServerThread() : concurrency::OSThread("WebServerThread") {}

int32_t WebServerThread::runOnce()
{
    // DEBUG_MSG("WebServerThread::runOnce()\n");
    handleWebResponse();

    // Loop every 5ms.
    return (5);
}

void initWebServer()
{
    DEBUG_MSG("Initializing Web Server ...\n");

#if 0
// this seems to be a copypaste dup of taskCreateCert
    prefs.begin("MeshtasticHTTPS", false);

    size_t pkLen = prefs.getBytesLength("PK");
    size_t certLen = prefs.getBytesLength("cert");

    DEBUG_MSG("Checking if we have a previously saved SSL Certificate.\n");

    if (pkLen && certLen) {

        uint8_t *pkBuffer = new uint8_t[pkLen];
        prefs.getBytes("PK", pkBuffer, pkLen);

        uint8_t *certBuffer = new uint8_t[certLen];
        prefs.getBytes("cert", certBuffer, certLen);

        cert = new SSLCert(certBuffer, certLen, pkBuffer, pkLen);

        DEBUG_MSG("Retrieved Private Key: %d Bytes\n", cert->getPKLength());
        // DEBUG_MSG("Retrieved Private Key: " + String(cert->getPKLength()) + " Bytes");
        // for (int i = 0; i < cert->getPKLength(); i++)
        //  Serial.print(cert->getPKData()[i], HEX);
        // Serial.println();

        DEBUG_MSG("Retrieved Certificate: %d Bytes\n", cert->getCertLength());
        // for (int i = 0; i < cert->getCertLength(); i++)
        //  Serial.print(cert->getCertData()[i], HEX);
        // Serial.println();
    } else {
        DEBUG_MSG("Web Server started without SSL keys! How did this happen?\n");
    }
#endif

    // We can now use the new certificate to setup our server as usual.
    secureServer = new HTTPSServer(cert);
    insecureServer = new HTTPServer();

    registerHandlers(insecureServer, secureServer);

    if(secureServer) {
        DEBUG_MSG("Starting Secure Web Server...\n");
        secureServer->start();
    }
    DEBUG_MSG("Starting Insecure Web Server...\n");    
    insecureServer->start();
    if (insecureServer->isRunning()) {
        DEBUG_MSG("Web Servers Ready! :-) \n");
        isWebServerReady = true;
    } else {
        DEBUG_MSG("Web Servers Failed! ;-( \n");
    }
}
