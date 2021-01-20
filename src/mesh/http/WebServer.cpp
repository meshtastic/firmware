#include "mesh/http/WebServer.h"
#include "NodeDB.h"
#include "main.h"
#include "mesh/http/WiFiAPClient.h"
#include "sleep.h"
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

SSLCert *cert;
HTTPSServer *secureServer;
HTTPServer *insecureServer;




bool isWebServerReady = 0;
bool isCertReady = 0;


void handleWebResponse()
{
    if (isWifiAvailable() == 0) {
        return;
    }

    if (isWebServerReady) {
        // We're going to handle the DNS responder here so it
        // will be ignored by the NRF boards.
        handleDNSResponse();

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

void taskCreateCert(void *parameter)
{

    prefs.begin("MeshtasticHTTPS", false);

    // Delete the saved certs
    if (0) {
        DEBUG_MSG("Deleting any saved SSL keys ...\n");
        // prefs.clear();
        prefs.remove("PK");
        prefs.remove("cert");
    }

    size_t pkLen = prefs.getBytesLength("PK");
    size_t certLen = prefs.getBytesLength("cert");

    DEBUG_MSG("Checking if we have a previously saved SSL Certificate.\n");

    if (pkLen && certLen) {
        DEBUG_MSG("Existing SSL Certificate found!\n");
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

    isCertReady = 1;
    vTaskDelete(NULL);
}

void createSSLCert()
{

    if (isWifiAvailable() == 0) {
        return;
    }

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
    while (!isCertReady) {
        DEBUG_MSG(".");
        delay(1000);
        yield();
        esp_task_wdt_reset();
    }
    DEBUG_MSG("SSL Cert Ready!\n");
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

    // We can now use the new certificate to setup our server as usual.
    secureServer = new HTTPSServer(cert);
    insecureServer = new HTTPServer();

    registerHandlers(insecureServer, secureServer);

    DEBUG_MSG("Starting Web Servers...\n");
    secureServer->start();
    insecureServer->start();
    if (secureServer->isRunning() && insecureServer->isRunning()) {
        DEBUG_MSG("HTTP and HTTPS Web Servers Ready! :-) \n");
        isWebServerReady = 1;
    } else {
        DEBUG_MSG("HTTP and HTTPS Web Servers Failed! ;-( \n");
    }
}


// --------


void handle404(HTTPRequest *req, HTTPResponse *res)
{

    // Discard request body, if we received any
    // We do this, as this is the default node and may also server POST/PUT requests
    req->discardRequestBody();

    // Set the response status
    res->setStatusCode(404);
    res->setStatusText("Not Found");

    // Set content type of the response
    res->setHeader("Content-Type", "text/html");

    // Write a tiny HTTP page
    res->println("<!DOCTYPE html>");
    res->println("<html>");
    res->println("<head><title>Not Found</title></head>");
    res->println("<body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body>");
    res->println("</html>");
}

/*
    This supports the Apple Captive Network Assistant (CNA) Portal
*/
void handleHotspot(HTTPRequest *req, HTTPResponse *res)
{
    DEBUG_MSG("Hotspot Request\n");

    /*
        If we don't do a redirect, be sure to return a "Success" message
        otherwise iOS will have trouble detecting that the connection to the SoftAP worked.
    */

    // Status code is 200 OK by default.
    // We want to deliver a simple HTML page, so we send a corresponding content type:
    res->setHeader("Content-Type", "text/html");

    // res->println("<!DOCTYPE html>");
    res->println("<meta http-equiv=\"refresh\" content=\"0;url=/\" />\n");
}


void handleRestart(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");

    DEBUG_MSG("***** Restarted on HTTP(s) Request *****\n");
    res->println("Restarting");

    ESP.restart();
}

void handleBlinkLED(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "application/json");

    ResourceParameters *params = req->getParams();
    std::string blink_target;

    if (!params->getQueryParameter("blink_target", blink_target)) {
        // if no blink_target was supplied in the URL parameters of the
        // POST request, then assume we should blink the LED
        blink_target = "LED";
    }

    if (blink_target == "LED") {
        uint8_t count = 10;
        while (count > 0) {
            setLed(true);
            delay(50);
            setLed(false);
            delay(50);
            count = count - 1;
        }
    } else {
        screen->blink();
    }

    res->println("{");
    res->println("\"status\": \"ok\"");
    res->println("}");
}


void handleScanNetworks(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "application/json");
    // res->setHeader("Content-Type", "text/html");

    int n = WiFi.scanNetworks();
    res->println("{");
    res->println("\"data\": {");
    if (n == 0) {
        // No networks found.
        res->println("\"networks\": []");

    } else {
        res->println("\"networks\": [");

        for (int i = 0; i < n; ++i) {
            char ssidArray[50];
            String ssidString = String(WiFi.SSID(i));
            // String ssidString = String(WiFi.SSID(i)).toCharArray(ssidArray, WiFi.SSID(i).length());
            ssidString.replace("\"", "\\\"");
            ssidString.toCharArray(ssidArray, 50);

            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
                // res->println("{\"ssid\": \"%s\",\"rssi\": -75}, ", String(WiFi.SSID(i).c_str() );

                res->printf("{\"ssid\": \"%s\",\"rssi\": %d}", ssidArray, WiFi.RSSI(i));
                // WiFi.RSSI(i)
                if (i != n - 1) {
                    res->printf(",");
                }
            }
            // Yield some cpu cycles to IP stack.
            //   This is important in case the list is large and it takes us time to return
            //   to the main loop.
            yield();
        }
        res->println("]");
    }
    res->println("},");
    res->println("\"status\": \"ok\"");
    res->println("}");
}

