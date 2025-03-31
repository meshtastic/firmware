#if !MESHTASTIC_EXCLUDE_WEBSERVER
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "airtime.h"
#include "main.h"
#include "mesh/http/ContentHelper.h"
#include "mesh/http/WebServer.h"
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#endif
#include "Led.h"
#include "SPILock.h"
#include "power.h"
#include "serialization/JSON.h"
#include <FSCommon.h>
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>

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

// We need to specify some content-type mapping, so the resources get delivered with the
// right content type and are displayed correctly in the browser
char contentTypes[][2][32] = {{".txt", "text/plain"},     {".html", "text/html"},
                              {".js", "text/javascript"}, {".png", "image/png"},
                              {".jpg", "image/jpg"},      {".gz", "application/gzip"},
                              {".gif", "image/gif"},      {".json", "application/json"},
                              {".css", "text/css"},       {".ico", "image/vnd.microsoft.icon"},
                              {".svg", "image/svg+xml"},  {"", ""}};

// const char *certificate = NULL; // change this as needed, leave as is for no TLS check (yolo security)

// Our API to handle messages to and from the radio.
HttpAPI webAPI;

void registerHandlers(HTTPServer *insecureServer, HTTPSServer *secureServer)
{

    // For every resource available on the server, we need to create a ResourceNode
    // The ResourceNode links URL and HTTP method to a handler function

    ResourceNode *nodeAPIv1ToRadioOptions = new ResourceNode("/api/v1/toradio", "OPTIONS", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1ToRadio = new ResourceNode("/api/v1/toradio", "PUT", &handleAPIv1ToRadio);
    ResourceNode *nodeAPIv1FromRadioOptions = new ResourceNode("/api/v1/fromradio", "OPTIONS", &handleAPIv1FromRadio);
    ResourceNode *nodeAPIv1FromRadio = new ResourceNode("/api/v1/fromradio", "GET", &handleAPIv1FromRadio);

    //    ResourceNode *nodeHotspotApple = new ResourceNode("/hotspot-detect.html", "GET", &handleHotspot);
    //    ResourceNode *nodeHotspotAndroid = new ResourceNode("/generate_204", "GET", &handleHotspot);

    ResourceNode *nodeAdmin = new ResourceNode("/admin", "GET", &handleAdmin);
    //    ResourceNode *nodeAdminSettings = new ResourceNode("/admin/settings", "GET", &handleAdminSettings);
    //    ResourceNode *nodeAdminSettingsApply = new ResourceNode("/admin/settings/apply", "POST", &handleAdminSettingsApply);
    //    ResourceNode *nodeAdminFs = new ResourceNode("/admin/fs", "GET", &handleFs);
    //    ResourceNode *nodeUpdateFs = new ResourceNode("/admin/fs/update", "POST", &handleUpdateFs);
    //    ResourceNode *nodeDeleteFs = new ResourceNode("/admin/fs/delete", "GET", &handleDeleteFsContent);

    ResourceNode *nodeRestart = new ResourceNode("/restart", "POST", &handleRestart);
    ResourceNode *nodeFormUpload = new ResourceNode("/upload", "POST", &handleFormUpload);

    ResourceNode *nodeJsonScanNetworks = new ResourceNode("/json/scanNetworks", "GET", &handleScanNetworks);
    ResourceNode *nodeJsonBlinkLED = new ResourceNode("/json/blink", "POST", &handleBlinkLED);
    ResourceNode *nodeJsonReport = new ResourceNode("/json/report", "GET", &handleReport);
    ResourceNode *nodeJsonNodes = new ResourceNode("/json/nodes", "GET", &handleNodes);
    ResourceNode *nodeJsonFsBrowseStatic = new ResourceNode("/json/fs/browse/static", "GET", &handleFsBrowseStatic);
    ResourceNode *nodeJsonDelete = new ResourceNode("/json/fs/delete/static", "DELETE", &handleFsDeleteStatic);

    ResourceNode *nodeRoot = new ResourceNode("/*", "GET", &handleStatic);

    // Secure nodes
    secureServer->registerNode(nodeAPIv1ToRadioOptions);
    secureServer->registerNode(nodeAPIv1ToRadio);
    secureServer->registerNode(nodeAPIv1FromRadioOptions);
    secureServer->registerNode(nodeAPIv1FromRadio);
    //    secureServer->registerNode(nodeHotspotApple);
    //    secureServer->registerNode(nodeHotspotAndroid);
    secureServer->registerNode(nodeRestart);
    secureServer->registerNode(nodeFormUpload);
    secureServer->registerNode(nodeJsonScanNetworks);
    secureServer->registerNode(nodeJsonBlinkLED);
    secureServer->registerNode(nodeJsonFsBrowseStatic);
    secureServer->registerNode(nodeJsonDelete);
    secureServer->registerNode(nodeJsonReport);
    secureServer->registerNode(nodeJsonNodes);
    //    secureServer->registerNode(nodeUpdateFs);
    //    secureServer->registerNode(nodeDeleteFs);
    secureServer->registerNode(nodeAdmin);
    //    secureServer->registerNode(nodeAdminFs);
    //    secureServer->registerNode(nodeAdminSettings);
    //    secureServer->registerNode(nodeAdminSettingsApply);
    secureServer->registerNode(nodeRoot); // This has to be last

    // Insecure nodes
    insecureServer->registerNode(nodeAPIv1ToRadioOptions);
    insecureServer->registerNode(nodeAPIv1ToRadio);
    insecureServer->registerNode(nodeAPIv1FromRadioOptions);
    insecureServer->registerNode(nodeAPIv1FromRadio);
    //    insecureServer->registerNode(nodeHotspotApple);
    //    insecureServer->registerNode(nodeHotspotAndroid);
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
    //    insecureServer->registerNode(nodeAdminSettings);
    //    insecureServer->registerNode(nodeAdminSettingsApply);
    insecureServer->registerNode(nodeRoot); // This has to be last
}

void handleAPIv1FromRadio(HTTPRequest *req, HTTPResponse *res)
{

    LOG_DEBUG("webAPI handleAPIv1FromRadio");

    /*
        For documentation, see:
            https://meshtastic.org/docs/development/device/http-api
            https://meshtastic.org/docs/development/device/client-api
    */

    // Get access to the parameters
    ResourceParameters *params = req->getParams();

    // std::string paramAll = "all";
    std::string valueAll;

    // Status code is 200 OK by default.
    res->setHeader("Content-Type", "application/x-protobuf");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");
    res->setHeader("X-Protobuf-Schema", "https://raw.githubusercontent.com/meshtastic/protobufs/master/meshtastic/mesh.proto");

    if (req->getMethod() == "OPTIONS") {
        res->setStatusCode(204); // Success with no content
        // res->print(""); @todo remove
        return;
    }

    uint8_t txBuf[MAX_STREAM_BUF_SIZE];
    uint32_t len = 1;

    if (params->getQueryParameter("all", valueAll)) {

        // If all is true, return all the buffers we have available
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

        // the param "all" was not specified. Return just one protobuf
    } else {
        len = webAPI.getFromRadio(txBuf);
        res->write(txBuf, len);
    }

    LOG_DEBUG("webAPI handleAPIv1FromRadio, len %d", len);
}

void handleAPIv1ToRadio(HTTPRequest *req, HTTPResponse *res)
{
    LOG_DEBUG("webAPI handleAPIv1ToRadio");

    /*
        For documentation, see:
            https://meshtastic.org/docs/development/device/http-api
            https://meshtastic.org/docs/development/device/client-api
    */

    res->setHeader("Content-Type", "application/x-protobuf");
    res->setHeader("Access-Control-Allow-Headers", "Content-Type");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "PUT, OPTIONS");
    res->setHeader("X-Protobuf-Schema", "https://raw.githubusercontent.com/meshtastic/protobufs/master/meshtastic/mesh.proto");

    if (req->getMethod() == "OPTIONS") {
        res->setStatusCode(204); // Success with no content
        // res->print(""); @todo remove
        return;
    }

    byte buffer[MAX_TO_FROM_RADIO_SIZE];
    size_t s = req->readBytes(buffer, MAX_TO_FROM_RADIO_SIZE);

    LOG_DEBUG("Received %d bytes from PUT request", s);
    webAPI.handleToRadio(buffer, s);

    res->write(buffer, s);
    LOG_DEBUG("webAPI handleAPIv1ToRadio");
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
            file.flush();
            file.close();
        } else {
            String fileName = String(file.name());
            file.flush();
            file.close();
            LOG_DEBUG("    %s", fileName.c_str());
            FSCom.remove(fileName);
        }
        file = root.openNextFile();
    }
    root.flush();
    root.close();
}

JSONArray htmlListDir(const char *dirname, uint8_t levels)
{
    File root = FSCom.open(dirname, FILE_O_READ);
    JSONArray fileList;
    if (!root) {
        return fileList;
    }
    if (!root.isDirectory()) {
        return fileList;
    }

    // iterate over the file list
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory() && !String(file.name()).endsWith(".")) {
            if (levels) {
#ifdef ARCH_ESP32
                fileList.push_back(new JSONValue(htmlListDir(file.path(), levels - 1)));
#else
                fileList.push_back(new JSONValue(htmlListDir(file.name(), levels - 1)));
#endif
                file.close();
            }
        } else {
            JSONObject thisFileMap;
            thisFileMap["size"] = new JSONValue((int)file.size());
#ifdef ARCH_ESP32
            thisFileMap["name"] = new JSONValue(String(file.path()).substring(1).c_str());
#else
            thisFileMap["name"] = new JSONValue(String(file.name()).substring(1).c_str());
#endif
            if (String(file.name()).substring(1).endsWith(".gz")) {
#ifdef ARCH_ESP32
                String modifiedFile = String(file.path()).substring(1);
#else
                String modifiedFile = String(file.name()).substring(1);
#endif
                modifiedFile.remove((modifiedFile.length() - 3), 3);
                thisFileMap["nameModified"] = new JSONValue(modifiedFile.c_str());
            }
            fileList.push_back(new JSONValue(thisFileMap));
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

    concurrency::LockGuard g(spiLock);
    auto fileList = htmlListDir("/static", 10);

    // create json output structure
    JSONObject filesystemObj;
    filesystemObj["total"] = new JSONValue((int)FSCom.totalBytes());
    filesystemObj["used"] = new JSONValue((int)FSCom.usedBytes());
    filesystemObj["free"] = new JSONValue(int(FSCom.totalBytes() - FSCom.usedBytes()));

    JSONObject jsonObjInner;
    jsonObjInner["files"] = new JSONValue(fileList);
    jsonObjInner["filesystem"] = new JSONValue(filesystemObj);

    JSONObject jsonObjOuter;
    jsonObjOuter["data"] = new JSONValue(jsonObjInner);
    jsonObjOuter["status"] = new JSONValue("ok");

    JSONValue *value = new JSONValue(jsonObjOuter);

    res->print(value->Stringify().c_str());

    delete value;
}

void handleFsDeleteStatic(HTTPRequest *req, HTTPResponse *res)
{
    ResourceParameters *params = req->getParams();
    std::string paramValDelete;

    res->setHeader("Content-Type", "application/json");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "DELETE");

    if (params->getQueryParameter("delete", paramValDelete)) {
        std::string pathDelete = "/" + paramValDelete;
        concurrency::LockGuard g(spiLock);
        if (FSCom.remove(pathDelete.c_str())) {

            LOG_INFO("%s", pathDelete.c_str());
            JSONObject jsonObjOuter;
            jsonObjOuter["status"] = new JSONValue("ok");
            JSONValue *value = new JSONValue(jsonObjOuter);
            res->print(value->Stringify().c_str());
            delete value;
            return;
        } else {

            LOG_INFO("%s", pathDelete.c_str());
            JSONObject jsonObjOuter;
            jsonObjOuter["status"] = new JSONValue("Error");
            JSONValue *value = new JSONValue(jsonObjOuter);
            res->print(value->Stringify().c_str());
            delete value;
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

        concurrency::LockGuard g(spiLock);

        if (FSCom.exists(filename.c_str())) {
            file = FSCom.open(filename.c_str());
            if (!file.available()) {
                LOG_WARN("File not available - %s", filename.c_str());
            }
        } else if (FSCom.exists(filenameGzip.c_str())) {
            file = FSCom.open(filenameGzip.c_str());
            res->setHeader("Content-Encoding", "gzip");
            if (!file.available()) {
                LOG_WARN("File not available - %s", filenameGzip.c_str());
            }
        } else {
            has_set_content_type = true;
            filenameGzip = "/static/index.html.gz";
            file = FSCom.open(filenameGzip.c_str());
            res->setHeader("Content-Type", "text/html");
            if (!file.available()) {

                LOG_WARN("File not available - %s", filenameGzip.c_str());
                res->println("Web server is running.<br><br>The content you are looking for can't be found. Please see: <a "
                             "href=https://meshtastic.org/docs/software/web-client/>FAQ</a>.<br><br><a "
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
        LOG_ERROR("This should not have happened");
        res->println("ERROR: This should not have happened");
    }
}

void handleFormUpload(HTTPRequest *req, HTTPResponse *res)
{

    LOG_DEBUG("Form Upload - Disable keep-alive");
    res->setHeader("Connection", "close");

    // First, we need to check the encoding of the form that we have received.
    // The browser will set the Content-Type request header, so we can use it for that purpose.
    // Then we select the body parser based on the encoding.
    // Actually we do this only for documentary purposes, we know the form is going
    // to be multipart/form-data.
    LOG_DEBUG("Form Upload - Creating body parser reference");
    HTTPBodyParser *parser;
    std::string contentType = req->getHeader("Content-Type");

    // The content type may have additional properties after a semicolon, for example:
    // Content-Type: text/html;charset=utf-8
    // Content-Type: multipart/form-data;boundary=------s0m3w31rdch4r4c73rs
    // As we're interested only in the actual mime _type_, we strip everything after the
    // first semicolon, if one exists:
    size_t semicolonPos = contentType.find(";");
    if (semicolonPos != std::string::npos) {
        contentType.resize(semicolonPos);
    }

    // Now, we can decide based on the content type:
    if (contentType == "multipart/form-data") {
        LOG_DEBUG("Form Upload - multipart/form-data");
        parser = new HTTPMultipartBodyParser(req);
    } else {
        LOG_DEBUG("Unknown POST Content-Type: %s", contentType.c_str());
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
        LOG_DEBUG("handleFormUpload: field name='%s', filename='%s', mimetype='%s'", name.c_str(), filename.c_str(),
                  mimeType.c_str());

        // Double check that it is what we expect
        if (name != "file") {
            LOG_DEBUG("Skip unexpected field");
            res->println("<p>No file found.</p>");
            return;
        }

        // Double check that it is what we expect
        if (filename == "") {
            LOG_DEBUG("Skip unexpected field");
            res->println("<p>No file found.</p>");
            return;
        }

        // You should check file name validity and all that, but we skip that to make the core
        // concepts of the body parser functionality easier to understand.
        std::string pathname = "/static/" + filename;

        concurrency::LockGuard g(spiLock);
        // Create a new file to stream the data into
        File file = FSCom.open(pathname.c_str(), FILE_O_WRITE);
        size_t fileLength = 0;
        didwrite = true;

        // With endOfField you can check whether the end of field has been reached or if there's
        // still data pending. With multipart bodies, you cannot know the field size in advance.
        while (!parser->endOfField()) {
            esp_task_wdt_reset();

            byte buf[512];
            size_t readLength = parser->read(buf, 512);
            // LOG_DEBUG("readLength - %i", readLength);

            // Abort the transfer if there is less than 50k space left on the filesystem.
            if (FSCom.totalBytes() - FSCom.usedBytes() < 51200) {
                file.flush();
                file.close();
                res->println("<p>Write aborted! Reserving 50k on filesystem.</p>");

                // enableLoopWDT();

                delete parser;
                return;
            }

            // if (readLength) {
            file.write(buf, readLength);
            fileLength += readLength;
            LOG_DEBUG("File Length %i", fileLength);
            //}
        }
        // enableLoopWDT();

        file.flush();
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

    // data->airtime->tx_log
    JSONArray txLogValues;
    uint32_t *logArray;
    logArray = airTime->airtimeReport(TX_LOG);
    for (int i = 0; i < airTime->getPeriodsToLog(); i++) {
        txLogValues.push_back(new JSONValue((int)logArray[i]));
    }

    // data->airtime->rx_log
    JSONArray rxLogValues;
    logArray = airTime->airtimeReport(RX_LOG);
    for (int i = 0; i < airTime->getPeriodsToLog(); i++) {
        rxLogValues.push_back(new JSONValue((int)logArray[i]));
    }

    // data->airtime->rx_all_log
    JSONArray rxAllLogValues;
    logArray = airTime->airtimeReport(RX_ALL_LOG);
    for (int i = 0; i < airTime->getPeriodsToLog(); i++) {
        rxAllLogValues.push_back(new JSONValue((int)logArray[i]));
    }

    // data->airtime
    JSONObject jsonObjAirtime;
    jsonObjAirtime["tx_log"] = new JSONValue(txLogValues);
    jsonObjAirtime["rx_log"] = new JSONValue(rxLogValues);
    jsonObjAirtime["rx_all_log"] = new JSONValue(rxAllLogValues);
    jsonObjAirtime["channel_utilization"] = new JSONValue(airTime->channelUtilizationPercent());
    jsonObjAirtime["utilization_tx"] = new JSONValue(airTime->utilizationTXPercent());
    jsonObjAirtime["seconds_since_boot"] = new JSONValue(int(airTime->getSecondsSinceBoot()));
    jsonObjAirtime["seconds_per_period"] = new JSONValue(int(airTime->getSecondsPerPeriod()));
    jsonObjAirtime["periods_to_log"] = new JSONValue(airTime->getPeriodsToLog());

    // data->wifi
    JSONObject jsonObjWifi;
    jsonObjWifi["rssi"] = new JSONValue(WiFi.RSSI());
    jsonObjWifi["ip"] = new JSONValue(WiFi.localIP().toString().c_str());

    // data->memory
    JSONObject jsonObjMemory;
    jsonObjMemory["heap_total"] = new JSONValue((int)memGet.getHeapSize());
    jsonObjMemory["heap_free"] = new JSONValue((int)memGet.getFreeHeap());
    jsonObjMemory["psram_total"] = new JSONValue((int)memGet.getPsramSize());
    jsonObjMemory["psram_free"] = new JSONValue((int)memGet.getFreePsram());
    spiLock->lock();
    jsonObjMemory["fs_total"] = new JSONValue((int)FSCom.totalBytes());
    jsonObjMemory["fs_used"] = new JSONValue((int)FSCom.usedBytes());
    jsonObjMemory["fs_free"] = new JSONValue(int(FSCom.totalBytes() - FSCom.usedBytes()));
    spiLock->unlock();

    // data->power
    JSONObject jsonObjPower;
    jsonObjPower["battery_percent"] = new JSONValue(powerStatus->getBatteryChargePercent());
    jsonObjPower["battery_voltage_mv"] = new JSONValue(powerStatus->getBatteryVoltageMv());
    jsonObjPower["has_battery"] = new JSONValue(BoolToString(powerStatus->getHasBattery()));
    jsonObjPower["has_usb"] = new JSONValue(BoolToString(powerStatus->getHasUSB()));
    jsonObjPower["is_charging"] = new JSONValue(BoolToString(powerStatus->getIsCharging()));

    // data->device
    JSONObject jsonObjDevice;
    jsonObjDevice["reboot_counter"] = new JSONValue((int)myNodeInfo.reboot_count);

    // data->radio
    JSONObject jsonObjRadio;
    jsonObjRadio["frequency"] = new JSONValue(RadioLibInterface::instance->getFreq());
    jsonObjRadio["lora_channel"] = new JSONValue((int)RadioLibInterface::instance->getChannelNum() + 1);

    // collect data to inner data object
    JSONObject jsonObjInner;
    jsonObjInner["airtime"] = new JSONValue(jsonObjAirtime);
    jsonObjInner["wifi"] = new JSONValue(jsonObjWifi);
    jsonObjInner["memory"] = new JSONValue(jsonObjMemory);
    jsonObjInner["power"] = new JSONValue(jsonObjPower);
    jsonObjInner["device"] = new JSONValue(jsonObjDevice);
    jsonObjInner["radio"] = new JSONValue(jsonObjRadio);

    // create json output structure
    JSONObject jsonObjOuter;
    jsonObjOuter["data"] = new JSONValue(jsonObjInner);
    jsonObjOuter["status"] = new JSONValue("ok");
    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObjOuter);
    res->print(value->Stringify().c_str());
    delete value;
}

void handleNodes(HTTPRequest *req, HTTPResponse *res)
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

    JSONArray nodesArray;

    uint32_t readIndex = 0;
    const meshtastic_NodeInfoLite *tempNodeInfo = nodeDB->readNextMeshNode(readIndex);
    while (tempNodeInfo != NULL) {
        if (tempNodeInfo->has_user) {
            JSONObject node;

            char id[16];
            snprintf(id, sizeof(id), "!%08x", tempNodeInfo->num);

            node["id"] = new JSONValue(id);
            node["snr"] = new JSONValue(tempNodeInfo->snr);
            node["via_mqtt"] = new JSONValue(BoolToString(tempNodeInfo->via_mqtt));
            node["last_heard"] = new JSONValue((int)tempNodeInfo->last_heard);
            node["position"] = new JSONValue();

            if (nodeDB->hasValidPosition(tempNodeInfo)) {
                JSONObject position;
                position["latitude"] = new JSONValue((float)tempNodeInfo->position.latitude_i * 1e-7);
                position["longitude"] = new JSONValue((float)tempNodeInfo->position.longitude_i * 1e-7);
                position["altitude"] = new JSONValue((int)tempNodeInfo->position.altitude);
                node["position"] = new JSONValue(position);
            }

            node["long_name"] = new JSONValue(tempNodeInfo->user.long_name);
            node["short_name"] = new JSONValue(tempNodeInfo->user.short_name);
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", tempNodeInfo->user.macaddr[0],
                     tempNodeInfo->user.macaddr[1], tempNodeInfo->user.macaddr[2], tempNodeInfo->user.macaddr[3],
                     tempNodeInfo->user.macaddr[4], tempNodeInfo->user.macaddr[5]);
            node["mac_address"] = new JSONValue(macStr);
            node["hw_model"] = new JSONValue(tempNodeInfo->user.hw_model);

            nodesArray.push_back(new JSONValue(node));
        }
        tempNodeInfo = nodeDB->readNextMeshNode(readIndex);
    }

    // collect data to inner data object
    JSONObject jsonObjInner;
    jsonObjInner["nodes"] = new JSONValue(nodesArray);

    // create json output structure
    JSONObject jsonObjOuter;
    jsonObjOuter["data"] = new JSONValue(jsonObjInner);
    jsonObjOuter["status"] = new JSONValue("ok");
    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObjOuter);
    res->print(value->Stringify().c_str());
    delete value;
}

/*
    This supports the Apple Captive Network Assistant (CNA) Portal
*/
void handleHotspot(HTTPRequest *req, HTTPResponse *res)
{
    LOG_INFO("Hotspot Request");

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
    res->println("<meta http-equiv=\"refresh\" content=\"0;url=/\" />");
}

void handleDeleteFsContent(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>");
    res->println("Delete Content in /static/*");

    LOG_INFO("Delete files from /static/* : ");

    concurrency::LockGuard g(spiLock);
    htmlDeleteDir("/static");

    res->println("<p><hr><p><a href=/admin>Back to admin</a>");
}

void handleAdmin(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>");
    //    res->println("<a href=/admin/settings>Settings</a><br>");
    //    res->println("<a href=/admin/fs>Manage Web Content</a><br>");
    res->println("<a href=/json/report>Device Report</a><br>");
}

void handleAdminSettings(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>");
    res->println("This isn't done.");
    res->println("<form action=/admin/settings/apply method=post>");
    res->println("<table border=1>");
    res->println("<tr><td>Set?</td><td>Setting</td><td>current value</td><td>new value</td></tr>");
    res->println("<tr><td><input type=checkbox></td><td>WiFi SSID</td><td>false</td><td><input type=radio></td></tr>");
    res->println("<tr><td><input type=checkbox></td><td>WiFi Password</td><td>false</td><td><input type=radio></td></tr>");
    res->println(
        "<tr><td><input type=checkbox></td><td>Smart Position Update</td><td>false</td><td><input type=radio></td></tr>");
    res->println("</table>");
    res->println("<table>");
    res->println("<input type=submit value=Apply New Settings>");
    res->println("<form>");
    res->println("<p><hr><p><a href=/admin>Back to admin</a>");
}

void handleAdminSettingsApply(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "POST");
    res->println("<h1>Meshtastic</h1>");
    res->println(
        "<html><head><meta http-equiv=\"refresh\" content=\"1;url=/admin/settings\" /><title>Settings Applied. </title>");

    res->println("Settings Applied. Please wait.");
}

void handleFs(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>");
    res->println("<a href=/admin/fs/delete>Delete Web Content</a><p><form action=/admin/fs/update "
                 "method=post><input type=submit value=UPDATE_WEB_CONTENT></form>Be patient!");
    res->println("<p><hr><p><a href=/admin>Back to admin</a>");
}

void handleRestart(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "text/html");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");

    res->println("<h1>Meshtastic</h1>");
    res->println("Restarting");

    LOG_DEBUG("Restarted on HTTP(s) Request");
    webServerThread->requestRestart = (millis() / 1000) + 5;
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
            ledBlink.set(true);
            delay(50);
            ledBlink.set(false);
            delay(50);
            count = count - 1;
        }
    } else {
#if HAS_SCREEN
        screen->blink();
#endif
    }

    JSONObject jsonObjOuter;
    jsonObjOuter["status"] = new JSONValue("ok");
    JSONValue *value = new JSONValue(jsonObjOuter);
    res->print(value->Stringify().c_str());
    delete value;
}

void handleScanNetworks(HTTPRequest *req, HTTPResponse *res)
{
    res->setHeader("Content-Type", "application/json");
    res->setHeader("Access-Control-Allow-Origin", "*");
    res->setHeader("Access-Control-Allow-Methods", "GET");
    // res->setHeader("Content-Type", "text/html");

    int n = WiFi.scanNetworks();

    // build list of network objects
    JSONArray networkObjs;
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            char ssidArray[50];
            String ssidString = String(WiFi.SSID(i));
            ssidString.replace("\"", "\\\"");
            ssidString.toCharArray(ssidArray, 50);

            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
                JSONObject thisNetwork;
                thisNetwork["ssid"] = new JSONValue(ssidArray);
                thisNetwork["rssi"] = new JSONValue(int(WiFi.RSSI(i)));
                networkObjs.push_back(new JSONValue(thisNetwork));
            }
            // Yield some cpu cycles to IP stack.
            //   This is important in case the list is large and it takes us time to return
            //   to the main loop.
            yield();
        }
    }

    // build output structure
    JSONObject jsonObjOuter;
    jsonObjOuter["data"] = new JSONValue(networkObjs);
    jsonObjOuter["status"] = new JSONValue("ok");

    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObjOuter);
    res->print(value->Stringify().c_str());
    delete value;
}
#endif