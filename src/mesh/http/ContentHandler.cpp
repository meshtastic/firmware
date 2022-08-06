#include "NodeDB.h"
#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "airtime.h"
#include "main.h"
#include "mesh/http/ContentHelper.h"
#include "mesh/http/WebServer.h"
#include "mesh/http/WiFiAPClient.h"
#include "power.h"
#include "sleep.h"
#include <FSCommon.h>
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>
#include <json11.hpp>

#ifdef ARCH_ESP32
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

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
HTTPClient httpClient;

#define DEST_FS_USES_LITTLEFS
#define ESP_ARDUINO_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(1, 0, 4)

// We need to specify some content-type mapping, so the resources get delivered with the
// right content type and are displayed correctly in the browser
char contentTypes[][2][32] = {{".txt", "text/plain"},     {".html", "text/html"},
                              {".js", "text/javascript"}, {".png", "image/png"},
                              {".jpg", "image/jpg"},      {".gz", "application/gzip"},
                              {".gif", "image/gif"},      {".json", "application/json"},
                              {".css", "text/css"},       {".ico", "image/vnd.microsoft.icon"},
                              {".svg", "image/svg+xml"},  {"", ""}};

// const char *tarURL = "https://www.casler.org/temp/meshtastic-web.tar";
// const char *tarURL = "https://api-production-871d.up.railway.app/mirror/webui";
// const char *certificate = NULL; // change this as needed, leave as is for no TLS check (yolo security)

// Our API to handle messages to and from the radio.
HttpAPI webAPI;

void registerHandlers(HTTPServer *insecureServer, HTTPSServer *secureServer)
{

    // For every resource available on the server, we need to create a ResourceNode
    // The ResourceNode links URL and HTTP method to a handler function

    ResourceNode *nodeAPIv1ToRadioOptions = new ResourceNode("/api/v1/toradio", "OPTIONS", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1ToRadio = new ResourceNode("/api/v1/toradio", "PUT", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1FromRadio = new ResourceNode("/api/v1/fromradio", "GET", &handleAPIv1FromRadio);

    ResourceNode *nodeHotspotApple = new ResourceNode("/hotspot-detect.html", "GET", &handleHotspot);
    ResourceNode *nodeHotspotAndroid = new ResourceNode("/generate_204", "GET", &handleHotspot);

    ResourceNode *nodeAdmin = new ResourceNode("/admin", "GET", &handleAdmin);
    ResourceNode *nodeAdminSettings = new ResourceNode("/admin/settings", "GET", &handleAdminSettings);
    ResourceNode *nodeAdminSettingsApply = new ResourceNode("/admin/settings/apply", "POST", &handleAdminSettingsApply);
    //    ResourceNode *nodeAdminFs = new ResourceNode("/admin/fs", "GET", &handleFs);
    //    ResourceNode *nodeUpdateFs = new ResourceNode("/admin/fs/update", "POST", &handleUpdateFs);
    //    ResourceNode *nodeDeleteFs = new ResourceNode("/admin/fs/delete", "GET", &handleDeleteFsContent);

    ResourceNode *nodeRestart = new ResourceNode("/restart", "POST", &handleRestart);
    ResourceNode *nodeFormUpload = new ResourceNode("/upload", "POST", &handleFormUpload);

    ResourceNode *nodeJsonScanNetworks = new ResourceNode("/json/scanNetworks", "GET", &handleScanNetworks);
    ResourceNode *nodeJsonBlinkLED = new ResourceNode("/json/blink", "POST", &handleBlinkLED);
    ResourceNode *nodeJsonReport = new ResourceNode("/json/report", "GET", &handleReport);
    ResourceNode *nodeJsonFsBrowseStatic = new ResourceNode("/json/fs/browse/static", "GET", &handleFsBrowseStatic);
    ResourceNode *nodeJsonDelete = new ResourceNode("/json/fs/delete/static", "DELETE", &handleFsDeleteStatic);

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
    secureServer->registerNode(nodeJsonFsBrowseStatic);
    secureServer->registerNode(nodeJsonDelete);
    secureServer->registerNode(nodeJsonReport);
    //    secureServer->registerNode(nodeUpdateFs);
    //    secureServer->registerNode(nodeDeleteFs);
    secureServer->registerNode(nodeAdmin);
    //    secureServer->registerNode(nodeAdminFs);
    secureServer->registerNode(nodeAdminSettings);
    secureServer->registerNode(nodeAdminSettingsApply);
    secureServer->registerNode(nodeRoot); // This has to be last

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
    insecureServer->registerNode(nodeJsonFsBrowseStatic);
    insecureServer->registerNode(nodeJsonDelete);
    insecureServer->registerNode(nodeJsonReport);
    //    insecureServer->registerNode(nodeUpdateFs);
    //    insecureServer->registerNode(nodeDeleteFs);
    insecureServer->registerNode(nodeAdmin);
    //    insecureServer->registerNode(nodeAdminFs);
    insecureServer->registerNode(nodeAdminSettings);
    insecureServer->registerNode(nodeAdminSettingsApply);
    insecureServer->registerNode(nodeRoot); // This has to be last
}

void handleAPIv1FromRadio(HTTPRequest *req, HTTPResponse *res)
{

    DEBUG_MSG("webAPI handleAPIv1FromRadio\n");

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

    DEBUG_MSG("webAPI handleAPIv1FromRadio, len %d\n", len);
}

void handleAPIv1ToRadio(HTTPRequest *req, HTTPResponse *res)
{
    DEBUG_MSG("webAPI handleAPIv1ToRadio\n");

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
    DEBUG_MSG("webAPI handleAPIv1ToRadio\n");
}

void htmlDeleteDir(const char *dirname)
{
    File root = FSCom.open(dirname);
    if (!root) {
        return;
    }
    if (!root.isDirectory()) {
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory() && !String(file.name()).endsWith(".")) {
            htmlDeleteDir(file.name());
            file.close();
        } else {
            String fileName = String(file.name());
            file.close();
            DEBUG_MSG("    %s\n", fileName.c_str());
            FSCom.remove(fileName);
        }
        file = root.openNextFile();
    }
    root.close();
}

std::vector<std::map<char *, char *>> *htmlListDir(std::vector<std::map<char *, char *>> *fileList, const char *dirname,
                                                   uint8_t levels)
{
    File root = FSCom.open(dirname);
    if (!root) {
        return NULL;
    }
    if (!root.isDirectory()) {
        return NULL;
    }

    // iterate over the file list
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory() && !String(file.name()).endsWith(".")) {
            if (levels) {
                htmlListDir(fileList, file.name(), levels - 1);
            }
        } else {
            std::map<char *, char *> thisFileMap;
            thisFileMap[strdup("size")] = strdup(String(file.size()).c_str());
            thisFileMap[strdup("name")] = strdup(String(file.name()).substring(1).c_str());
            if (String(file.name()).substring(1).endsWith(".gz")) {
                String modifiedFile = String(file.name()).substring(1);
                modifiedFile.remove((modifiedFile.length() - 3), 3);
                thisFileMap[strdup("nameModified")] = strdup(modifiedFile.c_str());
            }
            fileList->push_back(thisFileMap);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    return fileList;
}

void handleFsBrowseStatic(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "application/json");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    using namespace json11;
    auto fileList = htmlListDir(new std::vector<std::map<char *, char *>>(), "/", 10);

    // create json output structure
    Json filesystemObj = Json::object{
        {"total", String(FSCom.totalBytes()).c_str()},
        {"used", String(FSCom.usedBytes()).c_str()},
        {"free", String(FSCom.totalBytes() - FSCom.usedBytes()).c_str()},
    };

    Json jsonObjInner = Json::object{{"files", Json(*fileList)}, {"filesystem", filesystemObj}};

    Json jsonObjOuter = Json::object{{"data", jsonObjInner}, {"status", "ok"}};

    // serialize and write it to the stream
    std::string jsonStr = jsonObjOuter.dump();
    res->print(jsonStr.c_str());
}

void handleFsDeleteStatic(HTTPRequest *req, HTTPResponse *res)
{
    using namespace json11;

    ResourceParameters *params = req->getParams();
    std::string paramValDelete;

    res->setHeader("Content-Type", "application/json");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "DELETE");
    if (params->getQueryParameter("delete", paramValDelete)) {
        std::string pathDelete = "/" + paramValDelete;
        if (FSCom.remove(pathDelete.c_str())) {
            Serial.println(pathDelete.c_str());
            Json jsonObjOuter = Json::object{{"status", "ok"}};
            std::string jsonStr = jsonObjOuter.dump();
            res->print(jsonStr.c_str());
            return;
        } else {
            Serial.println(pathDelete.c_str());
            Json jsonObjOuter = Json::object{{"status", "Error"}};
            std::string jsonStr = jsonObjOuter.dump();
            res->print(jsonStr.c_str());
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

        // Try to open the file
        File file;

        bool has_set_content_type = false;

        if (filename == "/static/") {
            filename = "/static/index.html";
            filenameGzip = "/static/index.html.gz";
        }

        if (FSCom.exists(filename.c_str())) {
            file = FSCom.open(filename.c_str());
            if (!file.available()) {
                DEBUG_MSG("File not available - %s\n", filename.c_str());
            }
        } else if (FSCom.exists(filenameGzip.c_str())) {
            file = FSCom.open(filenameGzip.c_str());
            res->setHeader("Content-Encoding", "gzip");
            if (!file.available()) {
                DEBUG_MSG("File not available - %s\n", filenameGzip.c_str());
            }
        } else {
            has_set_content_type = true;
            filenameGzip = "/static/index.html.gz";
            file = FSCom.open(filenameGzip.c_str());
            res->setHeader("Content-Type", "text/html");
            if (!file.available()) {
                DEBUG_MSG("File not available - %s\n", filenameGzip.c_str());
                res->println("Web server is running.<br><br>The content you are looking for can't be found. Please see: <a "
                             "href=https://meshtastic.org/docs/getting-started/faq#wifi--web-browser>FAQ</a>.<br><br><a "
                             "href=/admin>admin</a>");

                return;

            } else {
                res->setHeader("Content-Encoding", "gzip");
            }
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

        // Read the file and write it to the HTTP response body
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
        DEBUG_MSG("ERROR: This should not have happened...\n");
        res->println("ERROR: This should not have happened...");
    }
}

void handleFormUpload(HTTPRequest *req, HTTPResponse *res)
{

    DEBUG_MSG("Form Upload - Disabling keep-alive\n");
    res->setHeader("Connection", "close");

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

        // You should check file name validity and all that, but we skip that to make the core
        // concepts of the body parser functionality easier to understand.
        std::string pathname = "/static/" + filename;

        // Create a new file to stream the data into
        File file = FSCom.open(pathname.c_str(), "w");
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
            if (FSCom.totalBytes() - FSCom.usedBytes() < 51200) {
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
    using namespace json11;

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

    // data->airtime->tx_log
    std::vector<String> txLogValues;
    uint32_t *logArray;
    logArray = airTime->airtimeReport(TX_LOG);
    for (int i = 0; i < airTime->getPeriodsToLog(); i++) {
        uint32_t tmp;
        tmp = *(logArray + i);
        txLogValues.push_back(String(tmp));
    }

    // data->airtime->rx_log
    std::vector<String> rxLogValues;
    logArray = airTime->airtimeReport(RX_LOG);
    for (int i = 0; i < airTime->getPeriodsToLog(); i++) {
        uint32_t tmp;
        tmp = *(logArray + i);
        rxLogValues.push_back(String(tmp));
    }

    // data->airtime->rx_all_log
    std::vector<String> rxAllLogValues;
    logArray = airTime->airtimeReport(RX_ALL_LOG);
    for (int i = 0; i < airTime->getPeriodsToLog(); i++) {
        uint32_t tmp;
        tmp = *(logArray + i);
        rxAllLogValues.push_back(String(tmp));
    }

    Json jsonObjAirtime = Json::object{
        {"tx_log", Json(txLogValues)},
        {"rx_log", Json(rxLogValues)},
        {"rx_all_log", Json(rxAllLogValues)},
        {"channel_utilization", Json(airTime->channelUtilizationPercent())},
        {"utilization_tx", Json(airTime->utilizationTXPercent())},
        {"seconds_since_boot", Json(int(airTime->getSecondsSinceBoot()))},
        {"seconds_per_period", Json(int(airTime->getSecondsPerPeriod()))},
        {"periods_to_log", Json(airTime->getPeriodsToLog())},
    };

    // data->wifi
    String ipStr;
    if (config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPoint || config.wifi.mode == Config_WiFiConfig_WiFiMode_AccessPointHidden || isSoftAPForced()) {
        ipStr = String(WiFi.softAPIP().toString());
    } else {
        ipStr = String(WiFi.localIP().toString());
    }
    Json jsonObjWifi = Json::object{{"rssi", String(WiFi.RSSI())}, {"ip", ipStr.c_str()}};

    // data->memory
    Json jsonObjMemory = Json::object{{"heap_total", Json(int(ESP.getHeapSize()))},
                                      {"heap_free", Json(int(ESP.getFreeHeap()))},
                                      {"psram_total", Json(int(ESP.getPsramSize()))},
                                      {"psram_free", Json(int(ESP.getFreePsram()))},
                                      {"fs_total", String(FSCom.totalBytes()).c_str()},
                                      {"fs_used", String(FSCom.usedBytes()).c_str()},
                                      {"fs_free", String(FSCom.totalBytes() - FSCom.usedBytes()).c_str()}};

    // data->power
    Json jsonObjPower = Json::object{{"battery_percent", Json(powerStatus->getBatteryChargePercent())},
                                     {"battery_voltage_mv", Json(powerStatus->getBatteryVoltageMv())},
                                     {"has_battery", BoolToString(powerStatus->getHasBattery())},
                                     {"has_usb", BoolToString(powerStatus->getHasUSB())},
                                     {"is_charging", BoolToString(powerStatus->getIsCharging())}};

    // data->device
    Json jsonObjDevice = Json::object{{"reboot_counter", Json(int(myNodeInfo.reboot_count))}};

    // data->radio
    Json jsonObjRadio = Json::object{{"frequency", Json(RadioLibInterface::instance->getFreq())},
                                     {"lora_channel", Json(int(RadioLibInterface::instance->getChannelNum()))}};

    // collect data to inner data object
    Json jsonObjInner = Json::object{{"airtime", jsonObjAirtime}, {"wifi", jsonObjWifi},     {"memory", jsonObjMemory},
                                     {"power", jsonObjPower},     {"device", jsonObjDevice}, {"radio", jsonObjRadio}};

    // create json output structure
    Json jsonObjOuter = Json::object{{"data", jsonObjInner}, {"status", "ok"}};
    // serialize and write it to the stream
    std::string jsonStr = jsonObjOuter.dump();
    res->print(jsonStr.c_str());
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

void handleDeleteFsContent(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>\n");
    res->println("Deleting Content in /static/*");

    DEBUG_MSG("Deleting files from /static/* : \n");

    htmlDeleteDir("/static");

    res->println("<p><hr><p><a href=/admin>Back to admin</a>\n");
}

void handleAdmin(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>\n");
    res->println("<a href=/admin/settings>Settings</a><br>\n");
    res->println("<a href=/admin/fs>Manage Web Content</a><br>\n");
    res->println("<a href=/json/report>Device Report</a><br>\n");
}

void handleAdminSettings(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>\n");
    res->println("This isn't done.\n");
    res->println("<form action=/admin/settings/apply method=post>\n");
    res->println("<table border=1>\n");
    res->println("<tr><td>Set?</td><td>Setting</td><td>current value</td><td>new value</td></tr>\n");
    res->println("<tr><td><input type=checkbox></td><td>WiFi SSID</td><td>false</td><td><input type=radio></td></tr>\n");
    res->println("<tr><td><input type=checkbox></td><td>WiFi Password</td><td>false</td><td><input type=radio></td></tr>\n");
    res->println(
        "<tr><td><input type=checkbox></td><td>Smart Position Update</td><td>false</td><td><input type=radio></td></tr>\n");
    res->println("</table>\n");
    res->println("<table>\n");
    res->println("<input type=submit value=Apply New Settings>\n");
    res->println("<form>\n");
    res->println("<p><hr><p><a href=/admin>Back to admin</a>\n");
}

void handleAdminSettingsApply(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "POST");
    res->println("<h1>Meshtastic</h1>\n");
    res->println(
        "<html><head><meta http-equiv=\"refresh\" content=\"1;url=/admin/settings\" /><title>Settings Applied. </title>");

    res->println("Settings Applied. Please wait.\n");
}

void handleFs(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>\n");
    res->println("<a href=/admin/fs/delete>Delete Web Content</a><p><form action=/admin/fs/update "
                 "method=post><input type=submit value=UPDATE_WEB_CONTENT></form>Be patient!");
    res->println("<p><hr><p><a href=/admin>Back to admin</a>\n");
}

void handleRestart(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>\n");
    res->println("Restarting");

    DEBUG_MSG("***** Restarted on HTTP(s) Request *****\n");
    webServerThread->requestRestart = (millis() / 1000) + 5;
}

void handleBlinkLED(HTTPRequest *req, HTTPResponse *res)
{
    using namespace json11;

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
#if HAS_SCREEN
        screen->blink();
#endif
    }

    Json jsonObjOuter = Json::object{{"status", "ok"}};
    std::string jsonStr = jsonObjOuter.dump();
    res->print(jsonStr.c_str());
}

void handleScanNetworks(HTTPRequest *req, HTTPResponse *res)
{
    using namespace json11;

    res->setHeader("Content-Type", "application/json");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");
    // res->setHeader("Content-Type", "text/html");

    int n = WiFi.scanNetworks();

    // build list of network objects
    std::vector<Json> networkObjs;
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            char ssidArray[50];
            String ssidString = String(WiFi.SSID(i));
            ssidString.replace("\"", "\\\"");
            ssidString.toCharArray(ssidArray, 50);

            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
                Json thisNetwork = Json::object{{"ssid", ssidArray}, {"rssi", WiFi.RSSI(i)}};
                networkObjs.push_back(thisNetwork);
            }
            // Yield some cpu cycles to IP stack.
            //   This is important in case the list is large and it takes us time to return
            //   to the main loop.
            yield();
        }
    }

    // build output structure
    Json jsonObjOuter = Json::object{{"data", networkObjs}, {"status", "ok"}};

    // serialize and write it to the stream
    std::string jsonStr = jsonObjOuter.dump();
    res->print(jsonStr.c_str());
}
