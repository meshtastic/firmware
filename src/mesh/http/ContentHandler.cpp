#include "NodeDB.h"
#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "airtime.h"
#include "main.h"
#include "mesh/http/ContentHelper.h"
#include "mesh/http/WiFiAPClient.h"
#include "power.h"
#include "sleep.h"
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>
#include <SPIFFS.h>

#ifndef NO_ESP32
#include "esp_task_wdt.h"
#endif

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

// We need to specify some content-type mapping, so the resources get delivered with the
// right content type and are displayed correctly in the browser
char contentTypes[][2][32] = {{".txt", "text/plain"},     {".html", "text/html"},
                              {".js", "text/javascript"}, {".png", "image/png"},
                              {".jpg", "image/jpg"},      {".gz", "application/gzip"},
                              {".gif", "image/gif"},      {".json", "application/json"},
                              {".css", "text/css"},       {".ico", "image/vnd.microsoft.icon"},
                              {".svg", "image/svg+xml"},  {"", ""}};

// Our API to handle messages to and from the radio.
HttpAPI webAPI;

uint32_t numberOfRequests = 0;
uint32_t timeSpeedUp = 0;

uint32_t getTimeSpeedUp()
{
    return timeSpeedUp;
}

void setTimeSpeedUp()
{
    timeSpeedUp = millis();
}

void registerHandlers(HTTPServer *insecureServer, HTTPSServer *secureServer)
{

    // For every resource available on the server, we need to create a ResourceNode
    // The ResourceNode links URL and HTTP method to a handler function

    ResourceNode *nodeAPIv1ToRadioOptions = new ResourceNode("/api/v1/toradio", "OPTIONS", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1ToRadio = new ResourceNode("/api/v1/toradio", "PUT", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1FromRadio = new ResourceNode("/api/v1/fromradio", "GET", &handleAPIv1FromRadio);

    ResourceNode *nodeHotspotApple = new ResourceNode("/hotspot-detect.html", "GET", &handleHotspot);
    ResourceNode *nodeHotspotAndroid = new ResourceNode("/generate_204", "GET", &handleHotspot);

    ResourceNode *nodeRestart = new ResourceNode("/restart", "POST", &handleRestart);
    ResourceNode *nodeFormUpload = new ResourceNode("/upload", "POST", &handleFormUpload);

    ResourceNode *nodeJsonScanNetworks = new ResourceNode("/json/scanNetworks", "GET", &handleScanNetworks);
    ResourceNode *nodeJsonBlinkLED = new ResourceNode("/json/blink", "POST", &handleBlinkLED);
    ResourceNode *nodeJsonReport = new ResourceNode("/json/report", "GET", &handleReport);
    ResourceNode *nodeJsonSpiffsBrowseStatic = new ResourceNode("/json/spiffs/browse/static", "GET", &handleSpiffsBrowseStatic);
    ResourceNode *nodeJsonDelete = new ResourceNode("/json/spiffs/delete/static", "DELETE", &handleSpiffsDeleteStatic);
    
    ResourceNode *nodeRoot = new ResourceNode("/*", "GET", &handleStatic);

    // Secure nodes
    secureServer->registerNode(nodeAPIv1ToRadioOptions);
    secureServer->registerNode(nodeAPIv1ToRadio);
    secureServer->registerNode(nodeAPIv1FromRadio);
    secureServer->registerNode(nodeHotspotApple);
    secureServer->registerNode(nodeHotspotAndroid);
    secureServer->registerNode(nodeRestart);
    secureServer->registerNode(nodeFormUpload);
    secureServer->registerNode(nodeJsonScanNetworks);
    secureServer->registerNode(nodeJsonBlinkLED);
    secureServer->registerNode(nodeJsonSpiffsBrowseStatic);
    secureServer->registerNode(nodeJsonDelete);
    secureServer->registerNode(nodeJsonReport);
    secureServer->registerNode(nodeRoot);

    secureServer->addMiddleware(&middlewareSpeedUp240);

    // Insecure nodes
    insecureServer->registerNode(nodeAPIv1ToRadioOptions);
    insecureServer->registerNode(nodeAPIv1ToRadio);
    insecureServer->registerNode(nodeAPIv1FromRadio);
    insecureServer->registerNode(nodeHotspotApple);
    insecureServer->registerNode(nodeHotspotAndroid);
    insecureServer->registerNode(nodeRestart);
    insecureServer->registerNode(nodeFormUpload);
    insecureServer->registerNode(nodeJsonScanNetworks);
    insecureServer->registerNode(nodeJsonBlinkLED);
    insecureServer->registerNode(nodeJsonSpiffsBrowseStatic);
    insecureServer->registerNode(nodeJsonDelete);
    insecureServer->registerNode(nodeJsonReport);
    insecureServer->registerNode(nodeRoot);

    insecureServer->addMiddleware(&middlewareSpeedUp160);
}

void middlewareSpeedUp240(HTTPRequest *req, HTTPResponse *res, std::function<void()> next)
{
    // We want to print the response status, so we need to call next() first.
    next();

    // Phone (or other device) has contacted us over WiFi. Keep the radio turned on.
    //   TODO: This should go into its own middleware layer separate from the speedup.
    powerFSM.trigger(EVENT_CONTACT_FROM_PHONE);

    setCpuFrequencyMhz(240);
    setTimeSpeedUp();

    numberOfRequests++;
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
    setTimeSpeedUp();

    numberOfRequests++;
}

void handleAPIv1FromRadio(HTTPRequest *req, HTTPResponse *res)
{

    DEBUG_MSG("+++++++++++++++ webAPI handleAPIv1FromRadio\n");

    /*
        For documentation, see:
            https://meshtastic.org/docs/developers/device/http-api
            https://meshtastic.org/docs/developers/device/device-api
    */

    // Get access to the parameters
    ResourceParameters *params = req->getParams();

    // std::string paramAll = "all";
    std::string valueAll;

    // Status code is 200 OK by default.
    res->setHeader("Content-Type", "application/x-protobuf");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");
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
            https://meshtastic.org/docs/developers/device/http-api
            https://meshtastic.org/docs/developers/device/device-api
    */

    res->setHeader("Content-Type", "application/x-protobuf");
    res->setHeader("Access-Control-Allow-Headers", "Content-Type");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    res->setHeader("X-Protobuf-Schema", "https://raw.githubusercontent.com/meshtastic/Meshtastic-protobufs/master/mesh.proto");


    if (req->getMethod() == "OPTIONS") {
        res->setStatusCode(204); // Success with no content
        // res->print(""); @todo remove
        return;
    }

    byte buffer[MAX_TO_FROM_RADIO_SIZE];
    size_t s = req->readBytes(buffer, MAX_TO_FROM_RADIO_SIZE);

    DEBUG_MSG("Received %d bytes from PUT request\n", s);
    webAPI.handleToRadio(buffer, s);

    res->write(buffer, s);
    DEBUG_MSG("--------------- webAPI handleAPIv1ToRadio\n");
}

void handleSpiffsBrowseStatic(HTTPRequest *req, HTTPResponse *res)
{

    res->setHeader("Content-Type", "application/json");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

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
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "DELETE");
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

void handleStatic(HTTPRequest *req, HTTPResponse *res)
{
    // Get access to the parameters
    ResourceParameters *params = req->getParams();

    std::string parameter1;
    // Print the first parameter value
    if (params->getPathParameter(0, parameter1)) {

        std::string filename = "/static/" + parameter1;
        std::string filenameGzip = "/static/" + parameter1 + ".gz";


        // Try to open the file from SPIFFS
        File file;

        bool has_set_content_type = false;

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
        } else {
            has_set_content_type = true;
            filenameGzip = "/static/index.html.gz";
            file = SPIFFS.open(filenameGzip.c_str());
            res->setHeader("Content-Encoding", "gzip");
            res->setHeader("Content-Type", "text/html");
        }

        res->setHeader("Content-Length", httpsserver::intToString(file.size()));

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

void handleReport(HTTPRequest *req, HTTPResponse *res)
{

    ResourceParameters *params = req->getParams();
    std::string content;

    if (!params->getQueryParameter("content", content)) {
        content = "json";
    }

    if (content == "json") {
        res->setHeader("Content-Type", "application/json");
        res->setHeader("Access-Control-Allow-Origin", "*");
        res->setHeader("Access-Control-Allow-Methods", "GET");

    } else {
        res->setHeader("Content-Type", "text/html");
        res->println("<pre>");
    }

    res->println("{");

    res->println("\"data\": {");

    res->println("\"airtime\": {");

    uint32_t *logArray;

    res->print("\"tx_log\": [");

    logArray = airtimeReport(TX_LOG);
    for (int i = 0; i < getPeriodsToLog(); i++) {
        uint32_t tmp;
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
        uint32_t tmp;
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
        uint32_t tmp;
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

    res->printf("\"web_request_count\": %d,\n", numberOfRequests);
    res->println("\"rssi\": " + String(WiFi.RSSI()) + ",");

    if (radioConfig.preferences.wifi_ap_mode || isSoftAPForced()) {
        res->println("\"ip\": \"" + String(WiFi.softAPIP().toString().c_str()) + "\"");
    } else {
        res->println("\"ip\": \"" + String(WiFi.localIP().toString().c_str()) + "\"");
    }

    res->println("},");

    res->println("\"memory\": {");
    res->printf("\"heap_total\": %d,\n", ESP.getHeapSize());
    res->printf("\"heap_free\": %d,\n", ESP.getFreeHeap());
    res->printf("\"psram_total\": %d,\n", ESP.getPsramSize());
    res->printf("\"psram_free\": %d,\n", ESP.getFreePsram());
    res->println("\"spiffs_total\" : " + String(SPIFFS.totalBytes()) + ",");
    res->println("\"spiffs_used\" : " + String(SPIFFS.usedBytes()) + ",");
    res->println("\"spiffs_free\" : " + String(SPIFFS.totalBytes() - SPIFFS.usedBytes()));
    res->println("},");

    res->println("\"power\": {");
    res->printf("\"battery_percent\": %u,\n", powerStatus->getBatteryChargePercent());
    res->printf("\"battery_voltage_mv\": %u,\n", powerStatus->getBatteryVoltageMv());
    res->printf("\"has_battery\": %s,\n", BoolToString(powerStatus->getHasBattery()));
    res->printf("\"has_usb\": %s,\n", BoolToString(powerStatus->getHasUSB()));
    res->printf("\"is_charging\": %s\n", BoolToString(powerStatus->getIsCharging()));
    res->println("},");

    res->println("\"device\": {");
    res->printf("\"reboot_counter\": %d\n", myNodeInfo.reboot_count);
    res->println("},");

    res->println("\"radio\": {");
    res->printf("\"frequecy\": %f,\n", RadioLibInterface::instance->getFreq());
    res->printf("\"lora_channel\": %d\n", RadioLibInterface::instance->getChannelNum());
    res->println("}");

    res->println("},");

    res->println("\"status\": \"ok\"");
    res->println("}");
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
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    // res->println("<!DOCTYPE html>");
    res->println("<meta http-equiv=\"refresh\" content=\"0;url=/\" />\n");
}

void handleRestart(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    DEBUG_MSG("***** Restarted on HTTP(s) Request *****\n");
    res->println("Restarting");

    ESP.restart();
}

void handleBlinkLED(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "application/json");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "POST");

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
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");
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
