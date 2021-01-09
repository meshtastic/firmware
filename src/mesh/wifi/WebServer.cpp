#include "mesh/wifi/WebServer.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "airtime.h"
#include "esp_task_wdt.h"
#include "main.h"
#include "mesh/wifi/ContentHelper.h"
#include "mesh/wifi/ContentStatic.h"
#include "mesh/wifi/WiFiAPClient.h"
#include "sleep.h"
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>


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

SSLCert *cert;
HTTPSServer *secureServer;
HTTPServer *insecureServer;

// Our API to handle messages to and from the radio.
HttpAPI webAPI;

// Declare some handler functions for the various URLs on the server
void handleAPIv1FromRadio(HTTPRequest *req, HTTPResponse *res);
void handleAPIv1ToRadio(HTTPRequest *req, HTTPResponse *res);
void handleStyleCSS(HTTPRequest *req, HTTPResponse *res);
void handleHotspot(HTTPRequest *req, HTTPResponse *res);
void handleFavicon(HTTPRequest *req, HTTPResponse *res);
void handleRoot(HTTPRequest *req, HTTPResponse *res);
void handleStaticBrowse(HTTPRequest *req, HTTPResponse *res);
void handleStaticPost(HTTPRequest *req, HTTPResponse *res);
void handleStatic(HTTPRequest *req, HTTPResponse *res);
void handleRestart(HTTPRequest *req, HTTPResponse *res);
void handle404(HTTPRequest *req, HTTPResponse *res);
void handleFormUpload(HTTPRequest *req, HTTPResponse *res);
void handleScanNetworks(HTTPRequest *req, HTTPResponse *res);
void handleSpiffsBrowseStatic(HTTPRequest *req, HTTPResponse *res);
void handleSpiffsDeleteStatic(HTTPRequest *req, HTTPResponse *res);
void handleBlinkLED(HTTPRequest *req, HTTPResponse *res);
void handleReport(HTTPRequest *req, HTTPResponse *res);

void middlewareSpeedUp240(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);
void middlewareSpeedUp160(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);
void middlewareSession(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);

bool isWebServerReady = 0;
bool isCertReady = 0;

uint32_t timeSpeedUp = 0;

// We need to specify some content-type mapping, so the resources get delivered with the
// right content type and are displayed correctly in the browser
char contentTypes[][2][32] = {{".txt", "text/plain"},     {".html", "text/html"},
                              {".js", "text/javascript"}, {".png", "image/png"},
                              {".jpg", "image/jpg"},      {".gz", "application/gzip"},
                              {".gif", "image/gif"},      {".json", "application/json"},
                              {".css", "text/css"},       {".ico", "image/vnd.microsoft.icon"},
                              {".svg", "image/svg+xml"},  {"", ""}};

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
    if (millis() - timeSpeedUp >= (25 * 1000)) {
        setCpuFrequencyMhz(80);
        timeSpeedUp = millis();
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

    // For every resource available on the server, we need to create a ResourceNode
    // The ResourceNode links URL and HTTP method to a handler function

    ResourceNode *nodeAPIv1ToRadioOptions = new ResourceNode("/api/v1/toradio", "OPTIONS", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1ToRadio = new ResourceNode("/api/v1/toradio", "PUT", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1FromRadio = new ResourceNode("/api/v1/fromradio", "GET", &handleAPIv1FromRadio);

    ResourceNode *nodeHotspot = new ResourceNode("/hotspot-detect.html", "GET", &handleHotspot);
    ResourceNode *nodeFavicon = new ResourceNode("/favicon.ico", "GET", &handleFavicon);
    ResourceNode *nodeRoot = new ResourceNode("/", "GET", &handleRoot);
    ResourceNode *nodeStaticBrowse = new ResourceNode("/static", "GET", &handleStaticBrowse);
    ResourceNode *nodeStaticPOST = new ResourceNode("/static", "POST", &handleStaticPost);
    ResourceNode *nodeStatic = new ResourceNode("/static/*", "GET", &handleStatic);
    ResourceNode *nodeRestart = new ResourceNode("/restart", "POST", &handleRestart);
    ResourceNode *node404 = new ResourceNode("", "GET", &handle404);
    ResourceNode *nodeFormUpload = new ResourceNode("/upload", "POST", &handleFormUpload);
    ResourceNode *nodeJsonScanNetworks = new ResourceNode("/json/scanNetworks", "GET", &handleScanNetworks);
    ResourceNode *nodeJsonBlinkLED = new ResourceNode("/json/blink", "POST", &handleBlinkLED);
    ResourceNode *nodeJsonReport = new ResourceNode("/json/report", "GET", &handleReport);
    ResourceNode *nodeJsonSpiffsBrowseStatic = new ResourceNode("/json/spiffs/browse/static/", "GET", &handleSpiffsBrowseStatic);
    ResourceNode *nodeJsonDelete = new ResourceNode("/json/spiffs/delete/static", "DELETE", &handleSpiffsDeleteStatic);

    // Secure nodes
    secureServer->registerNode(nodeAPIv1ToRadioOptions);
    secureServer->registerNode(nodeAPIv1ToRadio);
    secureServer->registerNode(nodeAPIv1FromRadio);
    secureServer->registerNode(nodeHotspot);
    secureServer->registerNode(nodeFavicon);
    secureServer->registerNode(nodeRoot);
    secureServer->registerNode(nodeStaticBrowse);
    secureServer->registerNode(nodeStaticPOST);
    secureServer->registerNode(nodeStatic);
    secureServer->registerNode(nodeRestart);
    secureServer->registerNode(nodeFormUpload);
    secureServer->registerNode(nodeJsonScanNetworks);
    secureServer->registerNode(nodeJsonBlinkLED);
    secureServer->registerNode(nodeJsonSpiffsBrowseStatic);
    secureServer->registerNode(nodeJsonDelete);
    secureServer->registerNode(nodeJsonReport);
    secureServer->setDefaultNode(node404);

    secureServer->addMiddleware(&middlewareSpeedUp240);

    // Insecure nodes
    insecureServer->registerNode(nodeAPIv1ToRadioOptions);
    insecureServer->registerNode(nodeAPIv1ToRadio);
    insecureServer->registerNode(nodeAPIv1FromRadio);
    insecureServer->registerNode(nodeHotspot);
    insecureServer->registerNode(nodeFavicon);
    insecureServer->registerNode(nodeRoot);
    insecureServer->registerNode(nodeStaticBrowse);
    insecureServer->registerNode(nodeStaticPOST);
    insecureServer->registerNode(nodeStatic);
    insecureServer->registerNode(nodeRestart);
    insecureServer->registerNode(nodeFormUpload);
    insecureServer->registerNode(nodeJsonScanNetworks);
    insecureServer->registerNode(nodeJsonBlinkLED);
    insecureServer->registerNode(nodeJsonSpiffsBrowseStatic);
    insecureServer->registerNode(nodeJsonDelete);
    insecureServer->registerNode(nodeJsonReport);
    insecureServer->setDefaultNode(node404);

    insecureServer->addMiddleware(&middlewareSpeedUp160);

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

void middlewareSpeedUp240(HTTPRequest *req, HTTPResponse *res, std::function<void()> next)
{
    // We want to print the response status, so we need to call next() first.
    next();

    // Phone (or other device) has contacted us over WiFi. Keep the radio turned on.
    //   TODO: This should go into its own middleware layer separate from the speedup.
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);

    setCpuFrequencyMhz(240);
    timeSpeedUp = millis();
}

void middlewareSpeedUp160(HTTPRequest *req, HTTPResponse *res, std::function<void()> next)
{
    // We want to print the response status, so we need to call next() first.
    next();

    // Phone (or other device) has contacted us over WiFi. Keep the radio turned on.
    //   TODO: This should go into its own middleware layer separate from the speedup.
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);

    // If the frequency is 240mhz, we have recently gotten a HTTPS request.
    //   In that case, leave the frequency where it is and just update the
    //   countdown timer (timeSpeedUp).
    if (getCpuFrequencyMhz() != 240) {
        setCpuFrequencyMhz(160);
    }
    timeSpeedUp = millis();
}

void handleAPIv1FromRadio(HTTPRequest *req, HTTPResponse *res)
{

    DEBUG_MSG("+++++++++++++++ webAPI handleAPIv1FromRadio\n");

    /*
        For documentation, see:
            https://github.com/meshtastic/Meshtastic-device/wiki/HTTP-REST-API-discussion
            https://github.com/meshtastic/Meshtastic-device/blob/master/docs/software/device-api.md

        Example:
            http://10.10.30.198/api/v1/fromradio
    */

    // Get access to the parameters
    ResourceParameters *params = req->getParams();

    // std::string paramAll = "all";
    std::string valueAll;

    // Status code is 200 OK by default.
    res->setHeader("Content-Type", "application/x-protobuf");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "PUT, GET");
    res->setHeader("X-Protobuf-Schema", "https://raw.githubusercontent.com/meshtastic/Meshtastic-protobufs/master/mesh.proto");

    uint8_t txBuf[MAX_STREAM_BUF_SIZE];
    uint32_t len = 1;

    if (params->getQueryParameter("all", valueAll)) {

        // If all is ture, return all the buffers we have available
        //   to us at this point in time.
        if (valueAll == "true") {
            while (len) {
                len = webAPI.getFromRadio(txBuf);
                res->write(txBuf, len);
            }

            // Otherwise, just return one protobuf
        } else {
            len = webAPI.getFromRadio(txBuf);
            res->write(txBuf, len);
        }

        // the param "all" was not spcified. Return just one protobuf
    } else {
        len = webAPI.getFromRadio(txBuf);
        res->write(txBuf, len);
    }

    DEBUG_MSG("--------------- webAPI handleAPIv1FromRadio, len %d\n", len);
}

void handleAPIv1ToRadio(HTTPRequest *req, HTTPResponse *res)
{
    DEBUG_MSG("+++++++++++++++ webAPI handleAPIv1ToRadio\n");

    /*
        For documentation, see:
            https://github.com/meshtastic/Meshtastic-device/wiki/HTTP-REST-API-discussion
            https://github.com/meshtastic/Meshtastic-device/blob/master/docs/software/device-api.md

        Example:
            http://10.10.30.198/api/v1/toradio
    */

    // Status code is 200 OK by default.

    res->setHeader("Content-Type", "application/x-protobuf");
    res->setHeader("Access-Control-Allow-Headers", "Content-Type");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    res->setHeader("X-Protobuf-Schema", "https://raw.githubusercontent.com/meshtastic/Meshtastic-protobufs/master/mesh.proto");

    if (req->getMethod() == "OPTIONS") {
        res->setStatusCode(204); // Success with no content
        res->print("");
        return;
    }

    byte buffer[MAX_TO_FROM_RADIO_SIZE];
    size_t s = req->readBytes(buffer, MAX_TO_FROM_RADIO_SIZE);

    DEBUG_MSG("Received %d bytes from PUT request\n", s);
    webAPI.handleToRadio(buffer, s);

    res->write(buffer, s);
    DEBUG_MSG("--------------- webAPI handleAPIv1ToRadio\n");
}

void handleStaticPost(HTTPRequest *req, HTTPResponse *res)
{
    // Assume POST request. Contains submitted data.
    res->println("<html><head><title>File Edited</title><meta http-equiv=\"refresh\" content=\"1;url=/static\" "
                 "/><head><body><h1>File Edited</h1>");

    // The form is submitted with the x-www-form-urlencoded content type, so we need the
    // HTTPURLEncodedBodyParser to read the fields.
    // Note that the content of the file's content comes from a <textarea>, so we
    // can use the URL encoding here, since no file upload from an <input type="file"
    // is involved.
    HTTPURLEncodedBodyParser parser(req);

    // The bodyparser will consume the request body. That means you can iterate over the
    // fields only ones. For that reason, we need to create variables for all fields that
    // we expect. So when parsing is done, you can process the field values from your
    // temporary variables.
    std::string filename;
    bool savedFile = false;

    // Iterate over the fields from the request body by calling nextField(). This function
    // will update the field name and value of the body parsers. If the last field has been
    // reached, it will return false and the while loop stops.
    while (parser.nextField()) {
        // Get the field name, so that we can decide what the value is for
        std::string name = parser.getFieldName();

        if (name == "filename") {
            // Read the filename from the field's value, add the /public prefix and store it in
            // the filename variable.
            char buf[512];
            size_t readLength = parser.read((byte *)buf, 512);
            // filename = std::string("/public/") + std::string(buf, readLength);
            filename = std::string(buf, readLength);

        } else if (name == "content") {
            // Browsers must return the fields in the order that they are placed in
            // the HTML form, so if the broweser behaves correctly, this condition will
            // never be true. We include it for safety reasons.
            if (filename == "") {
                res->println("<p>Error: form contained content before filename.</p>");
                break;
            }

            // With parser.read() and parser.endOfField(), we can stream the field content
            // into a buffer. That allows handling arbitrarily-sized field contents. Here,
            // we use it and write the file contents directly to the SPIFFS:
            size_t fieldLength = 0;
            File file = SPIFFS.open(filename.c_str(), "w");
            savedFile = true;
            while (!parser.endOfField()) {
                byte buf[512];
                size_t readLength = parser.read(buf, 512);
                file.write(buf, readLength);
                fieldLength += readLength;
            }
            file.close();
            res->printf("<p>Saved %d bytes to %s</p>", int(fieldLength), filename.c_str());

        } else {
            res->printf("<p>Unexpected field %s</p>", name.c_str());
        }
    }
    if (!savedFile) {
        res->println("<p>No file to save...</p>");
    }
    res->println("</body></html>");
}

void handleSpiffsBrowseStatic(HTTPRequest *req, HTTPResponse *res)
{
    // jm

    res->setHeader("Content-Type", "application/json");
    // res->setHeader("Content-Type", "text/html");

    File root = SPIFFS.open("/");

    if (root.isDirectory()) {
        res->println("{");
        res->println("\"data\": {");

        File file = root.openNextFile();
        res->print("\"files\": [");
        bool firstFile = 1;
        while (file) {
            String filePath = String(file.name());
            if (filePath.indexOf("/static") == 0) {
                if (firstFile) {
                    firstFile = 0;
                } else {
                    res->println(",");
                }

                res->println("{");

                if (String(file.name()).substring(1).endsWith(".gz")) {
                    String modifiedFile = String(file.name()).substring(1);
                    modifiedFile.remove((modifiedFile.length() - 3), 3);
                    res->print("\"nameModified\": \"" + modifiedFile + "\",");
                    res->print("\"name\": \"" + String(file.name()).substring(1) + "\",");

                } else {
                    res->print("\"name\": \"" + String(file.name()).substring(1) + "\",");
                }
                res->print("\"size\": " + String(file.size()));
                res->print("}");
            }

            file = root.openNextFile();
        }
        res->print("],");
        res->print("\"filesystem\" : {");
        res->print("\"total\" : " + String(SPIFFS.totalBytes()) + ",");
        res->print("\"used\" : " + String(SPIFFS.usedBytes()) + ",");
        res->print("\"free\" : " + String(SPIFFS.totalBytes() - SPIFFS.usedBytes()));
        res->println("}");
        res->println("},");
        res->println("\"status\": \"ok\"");
        res->println("}");
    }
}

void handleSpiffsDeleteStatic(HTTPRequest *req, HTTPResponse *res)
{
    ResourceParameters *params = req->getParams();
    std::string paramValDelete;

    res->setHeader("Content-Type", "application/json");
    if (params->getQueryParameter("delete", paramValDelete)) {
        std::string pathDelete = "/" + paramValDelete;
        if (SPIFFS.remove(pathDelete.c_str())) {
            Serial.println(pathDelete.c_str());
            res->println("{");
            res->println("\"status\": \"ok\"");
            res->println("}");
            return;
        } else {
            Serial.println(pathDelete.c_str());
            res->println("{");
            res->println("\"status\": \"Error\"");
            res->println("}");
            return;
        }
    }
}

void handleStaticBrowse(HTTPRequest *req, HTTPResponse *res)
{
    // Get access to the parameters
    ResourceParameters *params = req->getParams();
    std::string paramValDelete;
    std::string paramValEdit;

    DEBUG_MSG("Static Browse - Disabling keep-alive\n");
    res->setHeader("Connection", "close");

    // Set a default content type
    res->setHeader("Content-Type", "text/html");

    if (params->getQueryParameter("delete", paramValDelete)) {
        std::string pathDelete = "/" + paramValDelete;
        if (SPIFFS.remove(pathDelete.c_str())) {
            Serial.println(pathDelete.c_str());
            res->println("<html><head><meta http-equiv=\"refresh\" content=\"1;url=/static\" /><title>File "
                         "deleted!</title></head><body><h1>File deleted!</h1>");
            res->println("<meta http-equiv=\"refresh\" 1;url=/static\" />\n");
            res->println("</body></html>");

            return;
        } else {
            Serial.println(pathDelete.c_str());
            res->println("<html><head><meta http-equiv=\"refresh\" content=\"1;url=/static\" /><title>Error deleteing "
                         "file!</title></head><body><h1>Error deleteing file!</h1>");
            res->println("Error deleteing file!<br>");

            return;
        }
    }

    if (params->getQueryParameter("edit", paramValEdit)) {
        std::string pathEdit = "/" + paramValEdit;
        res->println("<html><head><title>Edit "
                     "file</title></head><body><h1>Edit file - ");

        res->println(pathEdit.c_str());

        res->println("</h1>");
        res->println("<form method=post action=/static enctype=application/x-www-form-urlencoded>");
        res->printf("<input name=\"filename\" type=\"hidden\" value=\"%s\">", pathEdit.c_str());
        res->print("<textarea id=id name=content rows=20 cols=80>");

        // Try to open the file from SPIFFS
        File file = SPIFFS.open(pathEdit.c_str());

        if (file.available()) {
            // Read the file from SPIFFS and write it to the HTTP response body
            size_t length = 0;
            do {
                char buffer[256];
                length = file.read((uint8_t *)buffer, 256);
                std::string bufferString(buffer, length);

                // Escape gt and lt
                replaceAll(bufferString, "<", "&lt;");
                replaceAll(bufferString, ">", "&gt;");

                res->write((uint8_t *)bufferString.c_str(), bufferString.size());
            } while (length > 0);
        } else {
            res->println("Error: File not found");
        }

        res->println("</textarea><br>");
        res->println("<input type=submit value=Submit>");
        res->println("</form>");
        res->println("</body></html>");

        return;
    }

    res->println("<h2>Upload new file</h2>");
    res->println("<p>This form allows you to upload files. Keep your filenames small and files under 200k.</p>");
    res->println("<form method=\"POST\" action=\"/upload\" enctype=\"multipart/form-data\">");
    res->println("file: <input type=\"file\" name=\"file\"><br>");
    res->println("<input type=\"submit\" value=\"Upload\">");
    res->println("</form>");

    res->println("<h2>All Files</h2>");

    File root = SPIFFS.open("/");
    if (root.isDirectory()) {
        res->println("<script type=\"text/javascript\">function confirm_delete() {return confirm('Are you sure?');}</script>");

        res->println("<table>");
        res->println("<tr>");
        res->println("<td>File");
        res->println("</td>");
        res->println("<td>Size");
        res->println("</td>");
        res->println("<td colspan=2>Actions");
        res->println("</td>");
        res->println("</tr>");

        File file = root.openNextFile();
        while (file) {
            String filePath = String(file.name());
            if (filePath.indexOf("/static") == 0) {
                res->println("<tr>");
                res->println("<td>");

                if (String(file.name()).substring(1).endsWith(".gz")) {
                    String modifiedFile = String(file.name()).substring(1);
                    modifiedFile.remove((modifiedFile.length() - 3), 3);
                    res->print("<a href=\"" + modifiedFile + "\">" + String(file.name()).substring(1) + "</a>");
                } else {
                    res->print("<a href=\"" + String(file.name()).substring(1) + "\">" + String(file.name()).substring(1) +
                               "</a>");
                }
                res->println("</td>");
                res->println("<td>");
                res->print(String(file.size()));
                res->println("</td>");
                res->println("<td>");
                res->print("<a href=\"/static?delete=" + String(file.name()).substring(1) +
                           "\" onclick=\"return confirm_delete()\">Delete</a> ");
                res->println("</td>");
                res->println("<td>");
                if (!String(file.name()).substring(1).endsWith(".gz")) {
                    res->print("<a href=\"/static?edit=" + String(file.name()).substring(1) + "\">Edit</a>");
                }
                res->println("</td>");
                res->println("</tr>");
            }

            file = root.openNextFile();
        }
        res->println("</table>");

        res->print("<br>");
        // res->print("Total : " + String(SPIFFS.totalBytes()) + " Bytes<br>");
        res->print("Used : " + String(SPIFFS.usedBytes()) + " Bytes<br>");
        res->print("Free : " + String(SPIFFS.totalBytes() - SPIFFS.usedBytes()) + " Bytes<br>");
    }
}

void handleStatic(HTTPRequest *req, HTTPResponse *res)
{
    // Get access to the parameters
    ResourceParameters *params = req->getParams();

    std::string parameter1;
    // Print the first parameter value
    if (params->getPathParameter(0, parameter1)) {

        std::string filename = "/static/" + parameter1;
        std::string filenameGzip = "/static/" + parameter1 + ".gz";

        if (!SPIFFS.exists(filename.c_str()) && !SPIFFS.exists(filenameGzip.c_str())) {
            // Send "404 Not Found" as response, as the file doesn't seem to exist
            res->setStatusCode(404);
            res->setStatusText("Not found");
            res->println("404 Not Found");
            res->printf("<p>File not found: %s</p>\n", filename.c_str());
            return;
        }

        // Try to open the file from SPIFFS
        File file;

        if (SPIFFS.exists(filename.c_str())) {
            file = SPIFFS.open(filename.c_str());
            if (!file.available()) {
                DEBUG_MSG("File not available - %s\n", filename.c_str());
            }

        } else if (SPIFFS.exists(filenameGzip.c_str())) {
            file = SPIFFS.open(filenameGzip.c_str());
            res->setHeader("Content-Encoding", "gzip");
            if (!file.available()) {
                DEBUG_MSG("File not available\n");
            }
        }

        res->setHeader("Content-Length", httpsserver::intToString(file.size()));

        bool has_set_content_type = false;
        // Content-Type is guessed using the definition of the contentTypes-table defined above
        int cTypeIdx = 0;
        do {
            if (filename.rfind(contentTypes[cTypeIdx][0]) != std::string::npos) {
                res->setHeader("Content-Type", contentTypes[cTypeIdx][1]);
                has_set_content_type = true;
                break;
            }
            cTypeIdx += 1;
        } while (strlen(contentTypes[cTypeIdx][0]) > 0);

        if (!has_set_content_type) {
            // Set a default content type
            res->setHeader("Content-Type", "application/octet-stream");
        }

        // Read the file from SPIFFS and write it to the HTTP response body
        size_t length = 0;
        do {
            char buffer[256];
            length = file.read((uint8_t *)buffer, 256);
            std::string bufferString(buffer, length);
            res->write((uint8_t *)bufferString.c_str(), bufferString.size());
        } while (length > 0);

        file.close();

        return;

    } else {
        res->println("ERROR: This should not have happened...");
    }
}

void handleFormUpload(HTTPRequest *req, HTTPResponse *res)
{

    DEBUG_MSG("Form Upload - Disabling keep-alive\n");
    res->setHeader("Connection", "close");

    DEBUG_MSG("Form Upload - Set frequency to 240mhz\n");
    // The upload process is very CPU intensive. Let's speed things up a bit.
    setCpuFrequencyMhz(240);

    // First, we need to check the encoding of the form that we have received.
    // The browser will set the Content-Type request header, so we can use it for that purpose.
    // Then we select the body parser based on the encoding.
    // Actually we do this only for documentary purposes, we know the form is going
    // to be multipart/form-data.
    DEBUG_MSG("Form Upload - Creating body parser reference\n");
    HTTPBodyParser *parser;
    std::string contentType = req->getHeader("Content-Type");

    // The content type may have additional properties after a semicolon, for exampel:
    // Content-Type: text/html;charset=utf-8
    // Content-Type: multipart/form-data;boundary=------s0m3w31rdch4r4c73rs
    // As we're interested only in the actual mime _type_, we strip everything after the
    // first semicolon, if one exists:
    size_t semicolonPos = contentType.find(";");
    if (semicolonPos != std::string::npos) {
        contentType = contentType.substr(0, semicolonPos);
    }

    // Now, we can decide based on the content type:
    if (contentType == "multipart/form-data") {
        DEBUG_MSG("Form Upload - multipart/form-data\n");
        parser = new HTTPMultipartBodyParser(req);
    } else {
        Serial.printf("Unknown POST Content-Type: %s\n", contentType.c_str());
        return;
    }

    res->println("<html><head><meta http-equiv=\"refresh\" content=\"1;url=/static\" /><title>File "
                 "Upload</title></head><body><h1>File Upload</h1>");

    // We iterate over the fields. Any field with a filename is uploaded.
    // Note that the BodyParser consumes the request body, meaning that you can iterate over the request's
    // fields only a single time. The reason for this is that it allows you to handle large requests
    // which would not fit into memory.
    bool didwrite = false;

    // parser->nextField() will move the parser to the next field in the request body (field meaning a
    // form field, if you take the HTML perspective). After the last field has been processed, nextField()
    // returns false and the while loop ends.
    while (parser->nextField()) {
        // For Multipart data, each field has three properties:
        // The name ("name" value of the <input> tag)
        // The filename (If it was a <input type="file">, this is the filename on the machine of the
        //   user uploading it)
        // The mime type (It is determined by the client. So do not trust this value and blindly start
        //   parsing files only if the type matches)
        std::string name = parser->getFieldName();
        std::string filename = parser->getFieldFilename();
        std::string mimeType = parser->getFieldMimeType();
        // We log all three values, so that you can observe the upload on the serial monitor:
        DEBUG_MSG("handleFormUpload: field name='%s', filename='%s', mimetype='%s'\n", name.c_str(), filename.c_str(),
                  mimeType.c_str());

        // Double check that it is what we expect
        if (name != "file") {
            DEBUG_MSG("Skipping unexpected field\n");
            res->println("<p>No file found.</p>");
            return;
        }

        // Double check that it is what we expect
        if (filename == "") {
            DEBUG_MSG("Skipping unexpected field\n");
            res->println("<p>No file found.</p>");
            return;
        }

        // SPIFFS limits the total lenth of a path + file to 31 characters.
        if (filename.length() + 8 > 31) {
            DEBUG_MSG("Uploaded filename too long!\n");
            res->println("<p>Uploaded filename too long! Limit of 23 characters.</p>");
            delete parser;
            return;
        }

        // You should check file name validity and all that, but we skip that to make the core
        // concepts of the body parser functionality easier to understand.
        std::string pathname = "/static/" + filename;

        // Create a new file on spiffs to stream the data into
        File file = SPIFFS.open(pathname.c_str(), "w");
        size_t fileLength = 0;
        didwrite = true;

        // With endOfField you can check whether the end of field has been reached or if there's
        // still data pending. With multipart bodies, you cannot know the field size in advance.
        while (!parser->endOfField()) {
            esp_task_wdt_reset();

            byte buf[512];
            size_t readLength = parser->read(buf, 512);
            // DEBUG_MSG("\n\nreadLength - %i\n", readLength);

            // Abort the transfer if there is less than 50k space left on the filesystem.
            if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < 51200) {
                file.close();
                res->println("<p>Write aborted! Reserving 50k on filesystem.</p>");

                // enableLoopWDT();

                delete parser;
                return;
            }

            // if (readLength) {
            file.write(buf, readLength);
            fileLength += readLength;
            DEBUG_MSG("File Length %i\n", fileLength);
            //}
        }
        // enableLoopWDT();

        file.close();
        res->printf("<p>Saved %d bytes to %s</p>", (int)fileLength, pathname.c_str());
    }
    if (!didwrite) {
        res->println("<p>Did not write any file</p>");
    }
    res->println("</body></html>");
    delete parser;
}

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

/*
    To convert text to c strings:

    https://tomeko.net/online_tools/cpp_text_escape.php?lang=en
*/
void handleRoot(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");

    res->setHeader("Set-Cookie",
                   "mt_session=" + httpsserver::intToString(random(1, 9999999)) + "; Expires=Wed, 20 Apr 2049 4:20:00 PST");

    std::string cookie = req->getHeader("Cookie");
    // String cookieString = cookie.c_str();
    // uint8_t nameIndex = cookieString.indexOf("mt_session");
    // DEBUG_MSG(cookie.c_str());

    std::string filename = "/static/index.html";
    std::string filenameGzip = "/static/index.html.gz";

    if (!SPIFFS.exists(filename.c_str()) && !SPIFFS.exists(filenameGzip.c_str())) {
        // Send "404 Not Found" as response, as the file doesn't seem to exist
        res->setStatusCode(404);
        res->setStatusText("Not found");
        res->println("404 Not Found");
        res->printf("<p>File not found: %s</p>\n", filename.c_str());
        res->printf("<p></p>\n");
        res->printf("<p>You have gotten this error because the filesystem for the web server has not been loaded.</p>\n");
        res->printf("<p>Please review the 'Common Problems' section of the <a "
                    "href=https://github.com/meshtastic/Meshtastic-device/wiki/"
                    "How-to-use-the-Meshtastic-Web-Interface-over-WiFi>web interface</a> documentation.</p>\n");
        return;
    }

    // Try to open the file from SPIFFS
    File file;

    if (SPIFFS.exists(filename.c_str())) {
        file = SPIFFS.open(filename.c_str());
        if (!file.available()) {
            DEBUG_MSG("File not available - %s\n", filename.c_str());
        }

    } else if (SPIFFS.exists(filenameGzip.c_str())) {
        file = SPIFFS.open(filenameGzip.c_str());
        res->setHeader("Content-Encoding", "gzip");
        if (!file.available()) {
            DEBUG_MSG("File not available\n");
        }
    }

    // Read the file from SPIFFS and write it to the HTTP response body
    size_t length = 0;
    do {
        char buffer[256];
        length = file.read((uint8_t *)buffer, 256);
        std::string bufferString(buffer, length);
        res->write((uint8_t *)bufferString.c_str(), bufferString.size());
    } while (length > 0);
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

void handleReport(HTTPRequest *req, HTTPResponse *res)
{

    ResourceParameters *params = req->getParams();
    std::string content;

    if (!params->getQueryParameter("content", content)) {
        content = "json";
    }

    if (content == "json") {
        res->setHeader("Content-Type", "application/json");

    } else {
        res->setHeader("Content-Type", "text/html");
        res->println("<pre>");
    }

    res->println("{");

    res->println("\"data\": {");

    res->println("\"airtime\": {");

    uint16_t *logArray;

    res->print("\"tx_log\": [");

    logArray = airtimeReport(TX_LOG);
    for (int i = 0; i < getPeriodsToLog(); i++) {
        uint16_t tmp;
        tmp = *(logArray + i);
        res->printf("%d", tmp);
        if (i != getPeriodsToLog() - 1) {
            res->print(", ");
        }
    }

    res->println("],");
    res->print("\"rx_log\": [");

    logArray = airtimeReport(RX_LOG);
    for (int i = 0; i < getPeriodsToLog(); i++) {
        uint16_t tmp;
        tmp = *(logArray + i);
        res->printf("%d", tmp);
        if (i != getPeriodsToLog() - 1) {
            res->print(", ");
        }
    }

    res->println("],");
    res->print("\"rx_all_log\": [");

    logArray = airtimeReport(RX_ALL_LOG);
    for (int i = 0; i < getPeriodsToLog(); i++) {
        uint16_t tmp;
        tmp = *(logArray + i);
        res->printf("%d", tmp);
        if (i != getPeriodsToLog() - 1) {
            res->print(", ");
        }
    }

    res->println("],");
    res->printf("\"seconds_since_boot\": %u,\n", getSecondsSinceBoot());
    res->printf("\"seconds_per_period\": %u,\n", getSecondsPerPeriod());
    res->printf("\"periods_to_log\": %u\n", getPeriodsToLog());

    res->println("},");

    res->println("\"wifi\": {");

    res->println("\"rssi\": " + String(WiFi.RSSI()) + ",");

    if (radioConfig.preferences.wifi_ap_mode || isSoftAPForced()) {
        res->println("\"ip\": \"" + String(WiFi.softAPIP().toString().c_str()) + "\"");
    } else {
        res->println("\"ip\": \"" + String(WiFi.localIP().toString().c_str()) + "\"");
    }

    res->println("},");

    res->println("\"test\": 123");

    res->println("},");

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

void handleFavicon(HTTPRequest *req, HTTPResponse *res)
{
    // Set Content-Type
    res->setHeader("Content-Type", "image/vnd.microsoft.icon");
    // Write data from header file
    res->write(FAVICON_DATA, FAVICON_LENGTH);
}

