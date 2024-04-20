#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "NodeDB.h"
#include "graphics/Screen.h"
#include "main.h"
#include "mesh/http/WebServer.h"
#include "mesh/wifi/WiFiAPClient.h"
#include "sleep.h"
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>
#include <WebServer.h>
#include <WiFi.h>

#ifdef ARCH_ESP32
#include "esp_task_wdt.h"
#endif

// Persistent Data Storage
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
            if (secureServer)
                secureServer->loop();
            insecureServer->loop();
        }
    }
}

static void taskCreateCert(void *parameter)
{
    prefs.begin("MeshtasticHTTPS", false);

#if 0
    // Delete the saved certs (used in debugging)
    LOG_DEBUG("Deleting any saved SSL keys ...\n");
    // prefs.clear();
    prefs.remove("PK");
    prefs.remove("cert");
#endif

    LOG_INFO("Checking if we have a previously saved SSL Certificate.\n");

    size_t pkLen = prefs.getBytesLength("PK");
    size_t certLen = prefs.getBytesLength("cert");

    if (pkLen && certLen) {
        LOG_INFO("Existing SSL Certificate found!\n");

        uint8_t *pkBuffer = new uint8_t[pkLen];
        prefs.getBytes("PK", pkBuffer, pkLen);

        uint8_t *certBuffer = new uint8_t[certLen];
        prefs.getBytes("cert", certBuffer, certLen);

        cert = new SSLCert(certBuffer, certLen, pkBuffer, pkLen);

        LOG_DEBUG("Retrieved Private Key: %d Bytes\n", cert->getPKLength());
        LOG_DEBUG("Retrieved Certificate: %d Bytes\n", cert->getCertLength());
    } else {

        LOG_INFO("Creating the certificate. This may take a while. Please wait...\n");
        yield();
        cert = new SSLCert();
        yield();
        int createCertResult = createSelfSignedCert(*cert, KEYSIZE_2048, "CN=meshtastic.local,O=Meshtastic,C=US",
                                                    "20190101000000", "20300101000000");
        yield();

        if (createCertResult != 0) {
            LOG_ERROR("Creating the certificate failed\n");
        } else {
            LOG_INFO("Creating the certificate was successful\n");

            LOG_DEBUG("Created Private Key: %d Bytes\n", cert->getPKLength());

            LOG_DEBUG("Created Certificate: %d Bytes\n", cert->getCertLength());

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
        bool runLoop = false;

        // Create a new process just to handle creating the cert.
        //   This is a workaround for Bug: https://github.com/fhessel/esp32_https_server/issues/48
        //  jm@casler.org (Oct 2020)
        xTaskCreate(taskCreateCert, /* Task function. */
                    "createCert",   /* String with name of task. */
                    // 16384,          /* Stack size in bytes. */
                    8192,  /* Stack size in bytes. */
                    NULL,  /* Parameter passed as input of the task */
                    16,    /* Priority of the task. */
                    NULL); /* Task handle. */

        LOG_DEBUG("Waiting for SSL Cert to be generated.\n");
        while (!isCertReady) {
            if ((millis() / 500) % 2) {
                if (runLoop) {
                    LOG_DEBUG(".");

                    yield();
                    esp_task_wdt_reset();
#if HAS_SCREEN
                    if (millis() / 1000 >= 3) {
                        screen->setSSLFrames();
                    }
#endif
                }
                runLoop = false;
            } else {
                runLoop = true;
            }
        }
        LOG_INFO("SSL Cert Ready!\n");
    }
}

WebServerThread *webServerThread;

WebServerThread::WebServerThread() : concurrency::OSThread("WebServerThread")
{
    if (!config.network.wifi_enabled) {
        disable();
    }
}

int32_t WebServerThread::runOnce()
{
    if (!config.network.wifi_enabled) {
        disable();
    }

    handleWebResponse();

    if (requestRestart && (millis() / 1000) > requestRestart) {
        ESP.restart();
    }

    // Loop every 5ms.
    return (5);
}

void initWebServer()
{
    LOG_DEBUG("Initializing Web Server ...\n");

    // We can now use the new certificate to setup our server as usual.
    secureServer = new HTTPSServer(cert);
    insecureServer = new HTTPServer();

    registerHandlers(insecureServer, secureServer);

    if (secureServer) {
        LOG_INFO("Starting Secure Web Server...\n");
        secureServer->start();
    }
    LOG_INFO("Starting Insecure Web Server...\n");
    insecureServer->start();
    if (insecureServer->isRunning()) {
        LOG_INFO("Web Servers Ready! :-) \n");
        isWebServerReady = true;
    } else {
        LOG_ERROR("Web Servers Failed! ;-( \n");
    }
}
#endif