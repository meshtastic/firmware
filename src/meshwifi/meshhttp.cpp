#include "meshwifi/meshhttp.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"
#include "meshhttpStatic.h"
#include "meshwifi/meshwifi.h"
#include "sleep.h"
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
void handleBasicHTML(HTTPRequest *req, HTTPResponse *res);
void handleBasicJS(HTTPRequest *req, HTTPResponse *res);
void handleScriptsScriptJS(HTTPRequest *req, HTTPResponse *res);
void handleStatic(HTTPRequest *req, HTTPResponse *res);
void handle404(HTTPRequest *req, HTTPResponse *res);

void middlewareSpeedUp240(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);
void middlewareSpeedUp160(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);
void middlewareSession(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);

bool isWebServerReady = 0;
bool isCertReady = 0;

uint32_t timeSpeedUp = 0;

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

    // Slow down the CPU if we have not received a request within the last
    //   2 minutes.
    if (millis() - timeSpeedUp >= (2 * 60 * 1000)) {
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
        cert = new SSLCert();
        // disableCore1WDT();
        int createCertResult = createSelfSignedCert(*cert, KEYSIZE_2048, "CN=meshtastic.local,O=Meshtastic,C=US",
                                                    "20190101000000", "20300101000000");
        // enableCore1WDT();

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
    if (isCertReady) {
        DEBUG_MSG(".\n");
        delayMicroseconds(1000);
    }
    DEBUG_MSG("SSL Cert Ready!\n");
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
    ResourceNode *nodeScriptScriptsJS = new ResourceNode("/scripts/script.js", "GET", &handleScriptsScriptJS);
    ResourceNode *nodeBasicHTML = new ResourceNode("/basic.html", "GET", &handleBasicHTML);
    ResourceNode *nodeBasicJS = new ResourceNode("/basic.js", "GET", &handleBasicJS);
    ResourceNode *nodeStatic = new ResourceNode("/static/*", "GET", &handleStatic);
    ResourceNode *node404 = new ResourceNode("", "GET", &handle404);

    // Secure nodes
    secureServer->registerNode(nodeAPIv1ToRadioOptions);
    secureServer->registerNode(nodeAPIv1ToRadio);
    secureServer->registerNode(nodeAPIv1FromRadio);
    secureServer->registerNode(nodeHotspot);
    secureServer->registerNode(nodeFavicon);
    secureServer->registerNode(nodeRoot);
    secureServer->registerNode(nodeScriptScriptsJS);
    secureServer->registerNode(nodeBasicHTML);
    secureServer->registerNode(nodeBasicJS);
    secureServer->registerNode(nodeStatic);
    secureServer->setDefaultNode(node404);

    secureServer->addMiddleware(&middlewareSpeedUp240);

    // Insecure nodes
    insecureServer->registerNode(nodeAPIv1ToRadioOptions);
    insecureServer->registerNode(nodeAPIv1ToRadio);
    insecureServer->registerNode(nodeAPIv1FromRadio);
    insecureServer->registerNode(nodeHotspot);
    insecureServer->registerNode(nodeFavicon);
    insecureServer->registerNode(nodeRoot);
    insecureServer->registerNode(nodeScriptScriptsJS);
    insecureServer->registerNode(nodeBasicHTML);
    insecureServer->registerNode(nodeBasicJS);
    insecureServer->registerNode(nodeStatic);
    insecureServer->setDefaultNode(node404);

    insecureServer->addMiddleware(&middlewareSpeedUp160);

    DEBUG_MSG("Starting Web Server...\n");
    secureServer->start();
    insecureServer->start();
    if (secureServer->isRunning() && insecureServer->isRunning()) {
        DEBUG_MSG("Web Server Ready\n");
        isWebServerReady = 1;
    }
}

void middlewareSpeedUp240(HTTPRequest *req, HTTPResponse *res, std::function<void()> next)
{
    // We want to print the response status, so we need to call next() first.
    next();

    setCpuFrequencyMhz(240);
    timeSpeedUp = millis();
}

void middlewareSpeedUp160(HTTPRequest *req, HTTPResponse *res, std::function<void()> next)
{
    // We want to print the response status, so we need to call next() first.
    next();

    // If the frequency is 240mhz, we have recently gotten a HTTPS request.
    //   In that case, leave the frequency where it is and just update the
    //   countdown timer (timeSpeedUp).
    if (getCpuFrequencyMhz() != 240) {
        setCpuFrequencyMhz(160);
    }
    timeSpeedUp = millis();
}

void handleStatic(HTTPRequest *req, HTTPResponse *res)
{
    // Get access to the parameters
    ResourceParameters *params = req->getParams();

    // Set a default content type
    res->setHeader("Content-Type", "text/plain");

    std::string parameter1;
    // Print the first parameter value
    if (params->getPathParameter(0, parameter1)) {
        if (parameter1 == "meshtastic.js") {
            res->setHeader("Content-Encoding", "gzip");
            res->setHeader("Content-Type", "application/json");
            res->write(STATIC_MESHTASTIC_JS_DATA, STATIC_MESHTASTIC_JS_LENGTH);
            return;

        } else if (parameter1 == "style.css") {
            res->setHeader("Content-Encoding", "gzip");
            res->setHeader("Content-Type", "text/css");
            res->write(STATIC_STYLE_CSS_DATA, STATIC_STYLE_CSS_LENGTH);
            return;

        } else {
            res->print("Parameter 1: ");
            res->printStd(parameter1);

            return;
        }

    } else {
        res->println("ERROR: This should not have happened...");
    }
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

    // The response implements the Print interface, so you can use it just like
    // you would write to Serial etc.
    res->println("<!DOCTYPE html>");
    res->println("<meta http-equiv=\"refresh\" content=\"0;url=http://meshtastic.org/\" />\n");
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
        if (valueAll == "true") {
            while (len) {
                len = webAPI.getFromRadio(txBuf);
                res->write(txBuf, len);
            }
        } else {
            len = webAPI.getFromRadio(txBuf);
            res->write(txBuf, len);
        }
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

/*
    To convert text to c strings:

    https://tomeko.net/online_tools/cpp_text_escape.php?lang=en
*/
void handleRoot(HTTPRequest *req, HTTPResponse *res)
{

    String out = "";
    out +=
        "<!DOCTYPE html>\n"
        "<html lang=\"en\" >\n"
        "<!-- Updated 20200923 - Change JSON input -->\n"
        "<!-- Updated 20200924 - Replace FontAwesome with SVG -->\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>Meshtastic - Chat</title>\n"
        "  <link rel=\"stylesheet\" href=\"static/style.css\">\n"
        "\n"
        "</head>\n"
        "<body>\n"
        "<center><h1>This area is under development. Please don't file bugs.</h1></center><!-- Add SVG for Symbols -->\n"
        "<svg aria-hidden=\"true\" style=\"position: absolute; width: 0; height: 0; overflow: hidden;\" version=\"1.1\" "
        "xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
        "<defs>\n"
        "<symbol id=\"icon-map-marker\" viewBox=\"0 0 16 28\">\n"
        "<path d=\"M12 10c0-2.203-1.797-4-4-4s-4 1.797-4 4 1.797 4 4 4 4-1.797 4-4zM16 10c0 0.953-0.109 1.937-0.516 2.797l-5.688 "
        "12.094c-0.328 0.688-1.047 1.109-1.797 1.109s-1.469-0.422-1.781-1.109l-5.703-12.094c-0.406-0.859-0.516-1.844-0.516-2.797 "
        "0-4.422 3.578-8 8-8s8 3.578 8 8z\"></path>\n"
        "</symbol>\n"
        "<symbol id=\"icon-circle\" viewBox=\"0 0 24 28\">\n"
        "<path d=\"M24 14c0 6.625-5.375 12-12 12s-12-5.375-12-12 5.375-12 12-12 12 5.375 12 12z\"></path>\n"
        "</symbol>\n"
        "</defs>\n"
        "</svg>\n"
        "<div class=\"grid\">\n"
        "\t<div class=\"top\">\n"
        "\t\t<div class=\"top-text\">Meshtastic - Chat</div>\n"
        "\t</div>\n"
        "\n"
        "\t<div class=\"side clearfix\">\n"
        "    <div class=\"channel-list\" id=\"channel-list\">\n"
        "\t  <div class=\"side-header\">\n"
        "\t\t<div class=\"side-text\">Users</div>\n"
        "\t  </div>\n"
        "      <ul class=\"list\" id='userlist-id'>\n"
        "      </ul>\n"
        "    </div>\n"
        "    </div>\n"
        "    <div class=\"content\">\n"
        "      <div class=\"content-header clearfix\">\n"
        "<!--      <div class=\"content-about\"> -->\n"
        "          <div class=\"content-from\">\n"
        "\t\t      <span class=\"content-from-highlight\" id=\"content-from-id\">All Users</span>\n"
        "\t\t  </div>\n"
        "<!--      </div> -->\n"
        "      </div> <!-- end content-header -->\n"
        "      \n"
        "      <div class=\"content-history\" id='chat-div-id'>\n"
        "        <ul id='chat-history-id'>\n"
        "\t\t</ul>\n"
        "        \n"
        "      </div> <!-- end content-history -->\n"
        "      \n"
        "      <div class=\"content-message clearfix\">\n"
        "        <textarea name=\"message-to-send\" id=\"message-to-send\" placeholder =\"Type your message\" "
        "rows=\"3\"></textarea>\n"
        "                \n"
        "       \n"
        "        <button>Send</button>\n"
        "\n"
        "      </div> <!-- end content-message -->\n"
        "      \n"
        "    </div> <!-- end content -->\n"
        "    \n"
        "  </div> <!-- end container -->\n"
        "\n"
        "<script  src=\"/scripts/script.js\"></script>\n"
        "\n"
        "</body>\n"
        "</html>\n"
        "";

    // Status code is 200 OK by default.
    // We want to deliver a simple HTML page, so we send a corresponding content type:
    res->setHeader("Content-Type", "text/html");

    // The response implements the Print interface, so you can use it just like
    // you would write to Serial etc.
    res->print(out);
}

void handleScriptsScriptJS(HTTPRequest *req, HTTPResponse *res)
{
    String out = "";
    out += "String.prototype.toHHMMSS = function () {\n"
           "    var sec_num = parseInt(this, 10); // don't forget the second param\n"
           "    var hours   = Math.floor(sec_num / 3600);\n"
           "    var minutes = Math.floor((sec_num - (hours * 3600)) / 60);\n"
           "    var seconds = sec_num - (hours * 3600) - (minutes * 60);\n"
           "\n"
           "    if (hours   < 10) {hours   = \"0\"+hours;}\n"
           "    if (minutes < 10) {minutes = \"0\"+minutes;}\n"
           "    if (seconds < 10) {seconds = \"0\"+seconds;}\n"
           "//    return hours+':'+minutes+':'+seconds;\n"
           "\treturn hours+'h'+minutes+'m';\n"
           "}\n"
           "String.prototype.padLeft = function (length, character) { \n"
           "    return new Array(length - this.length + 1).join(character || ' ') + this; \n"
           "};\n"
           "\n"
           "Date.prototype.toFormattedString = function () {\n"
           "    return [String(this.getFullYear()).substr(2, 2),\n"
           "\t\t\tString(this.getMonth()+1).padLeft(2, '0'),\n"
           "            String(this.getDate()).padLeft(2, '0')].join(\"/\") + \" \" +\n"
           "           [String(this.getHours()).padLeft(2, '0'),\n"
           "            String(this.getMinutes()).padLeft(2, '0')].join(\":\");\n"
           "};\n"
           "\n"
           "function getData(file) {\n"
           "\tfetch(file)\n"
           "\t.then(function (response) {\n"
           "\t\treturn response.json();\n"
           "\t})\n"
           "\t.then(function (datafile) {\n"
           "\t\tupdateData(datafile);\n"
           "\t})\n"
           "\t.catch(function (err) {\n"
           "\t\tconsole.log('error: ' + err);\n"
           "\t});\n"
           "}\n"
           "\t\n"
           "function updateData(datafile) {\n"
           "//  Update System Details\n"
           "\tupdateSystem(datafile);\n"
           "//\tUpdate Userlist and message count\n"
           "\tupdateUsers(datafile);\n"
           "//  Update Chat\n"
           "\tupdateChat(datafile);\n"
           "}\n"
           "\n"
           "function updateSystem(datafile) {\n"
           "//  Update System Info \n"
           "\tvar sysContainer = document.getElementById(\"content-from-id\");\n"
           "\tvar newHTML = datafile.data.system.channel;\n"
           "\tvar myDate = new Date( datafile.data.system.timeGPS *1000);\n"
           "\tnewHTML += ' @' + myDate.toFormattedString();\n"
           "\tvar newSec = datafile.data.system.timeSinceStart;\n"
           "\tvar strsecondUp = newSec.toString();\n"
           "\tnewHTML += ' Up:' + strsecondUp.toHHMMSS();\n"
           "\tsysContainer.innerHTML = newHTML;\n"
           "}\n"
           "\n"
           "function updateUsers(datafile) {\n"
           "\tvar mainContainer = document.getElementById(\"userlist-id\");\n"
           "\tvar htmlUsers = '';\n"
           "\tvar timeBase = datafile.data.system.timeSinceStart;\n"
           "//\tvar lookup = {};\n"
           "    for (var i = 0; i < datafile.data.users.length; i++) {\n"
           "        htmlUsers += formatUsers(datafile.data.users[i],timeBase);\n"
           "\t}\n"
           "\tmainContainer.innerHTML = htmlUsers;\n"
           "}\n"
           "\n"
           "function formatUsers(user,timeBase) {\n"
           "\tnewHTML = '<li class=\"clearfix\">';\n"
           "    newHTML += '<div class=\"channel-name clearfix\">' + user.NameLong + '(' + user.NameShort + ')</div>';\n"
           "    newHTML += '<div class=\"message-count clearfix\">';\n"
           "\tvar secondsLS = timeBase - user.lastSeen;\n"
           "\tvar strsecondsLS = secondsLS.toString();\n"
           "\tnewHTML += '<svg class=\"icon icon-circle '+onlineStatus(secondsLS)+'\"><use "
           "xlink:href=\"#icon-circle\"></use></svg></i>Seen: '+strsecondsLS.toHHMMSS()+' ago&nbsp;';\n"
           "\tif (user.lat == 0 || user.lon == 0) {\n"
           "\t\tnewHTML += '';\n"
           "\t} else {\n"
           "\t\tnewHTML += '<div class=\"tooltip\"><svg class=\"icon icon-map-marker\"><use "
           "xlink:href=\"#icon-map-marker\"></use></svg><span class=\"tooltiptext\">lat:' + user.lat + ' lon:'+ user.lon+ "
           "'</span>';\n"
           "\t}\n"
           "    newHTML += '</div></div>';\n"
           "    newHTML += '</li>';\n"
           "\treturn(newHTML);\n"
           "}\n"
           "\n"
           "function onlineStatus(time) {\n"
           "\tif (time < 3600) {\n"
           "\t\treturn \"online\"\n"
           "\t} else {\n"
           "\t\treturn \"offline\"\n"
           "\t}\n"
           "}\n"
           "\n"
           "function updateChat(datafile) {\n"
           "//  Update Chat\n"
           "\tvar chatContainer = document.getElementById(\"chat-history-id\");\n"
           "\tvar htmlChat = '';\n"
           "\tvar timeBase = datafile.data.system.timeSinceStart;\n"
           "\tfor (var i = 0; i < datafile.data.chat.length; i++) {\n"
           "\t\thtmlChat += formatChat(datafile.data.chat[i],timeBase);\n"
           "\t}\n"
           "\tchatContainer.innerHTML = htmlChat;\n"
           "\tscrollHistory();\n"
           "}\n"
           "\n"
           "function formatChat(data,timeBase) {\n"
           "\tvar secondsTS = timeBase - data.timestamp;\n"
           "\tvar strsecondsTS = secondsTS.toString();\n"
           "\tnewHTML = '<li class=\"clearfix\">';\n"
           "\tif (data.local == 1) {\n"
           "\t\tnewHTML += '<div class=\"message-data\">';\n"
           "\t\tnewHTML += '<span class=\"message-data-name\" >' + data.NameLong + '(' + data.NameShort + ')</span>';\n"
           "\t\tnewHTML += '<span class=\"message-data-time\" >' + strsecondsTS.toHHMMSS() + ' ago</span>';\n"
           "\t\tnewHTML += '</div>';\n"
           "\t\tnewHTML += '<div class=\"message my-message\">' + data.chatLine + '</div>';\n"
           "\t} else {\n"
           "\t\tnewHTML += '<div class=\"message-data align-right\">';\n"
           "\t\tnewHTML += '<span class=\"message-data-time\" >' + strsecondsTS.toHHMMSS() + ' ago</span> &nbsp; &nbsp;';\n"
           "\t\tnewHTML += '<span class=\"message-data-name\" >' + data.NameLong + '(' + data.NameShort + ')</span>';\n"
           "//\t\tnewHTML += '<i class=\"fa fa-circle online\"></i>';\n"
           "\t\tnewHTML += '</div>';\n"
           "\t\tnewHTML += '<div class=\"message other-message float-right\">' + data.chatLine + '</div>';\n"
           "\t}\n"
           "\n"
           "    newHTML += '</li>';\n"
           "\treturn(newHTML);\t\n"
           "}\n"
           "\n"
           "function scrollHistory() {\n"
           "\tvar chatContainer = document.getElementById(\"chat-div-id\");\n"
           "\tchatContainer.scrollTop = chatContainer.scrollHeight;\n"
           "}\n"
           "\n"
           "\n"
           "getData('/json/chat/history/dummy');\n"
           "\n"
           "\n"
           "//window.onload=function(){\n"
           "//\talert('onload');\n"
           "//  Async - Run scroll 0.5sec after onload event\n"
           "//\tsetTimeout(scrollHistory(),500);\n"
           "// }";

    // Status code is 200 OK by default.
    // We want to deliver a simple HTML page, so we send a corresponding content type:
    res->setHeader("Content-Type", "text/html");

    // The response implements the Print interface, so you can use it just like
    // you would write to Serial etc.
    res->print(out);
}

void handleFavicon(HTTPRequest *req, HTTPResponse *res)
{
    // Set Content-Type
    res->setHeader("Content-Type", "image/vnd.microsoft.icon");
    // Write data from header file
    res->write(FAVICON_DATA, FAVICON_LENGTH);
}

/*
    To convert text to c strings:

    https://tomeko.net/online_tools/cpp_text_escape.php?lang=en
*/
void handleBasicJS(HTTPRequest *req, HTTPResponse *res)
{
    String out = "";
    out += "var meshtasticClient;\n"
           "var connectionOne;\n"
           "\n"
           "\n"
           "// Important: the connect action must be called from a user interaction (e.g. button press), otherwise the browsers "
           "won't allow the connect\n"
           "function connect() {\n"
           "\n"
           "    // Create new connection\n"
           "    var httpconn = new meshtasticjs.IHTTPConnection();\n"
           "\n"
           "    // Set connection params\n"
           "    let sslActive;\n"
           "    if (window.location.protocol === 'https:') {\n"
           "        sslActive = true;\n"
           "    } else {\n"
           "        sslActive = false;\n"
           "    }\n"
           "    let deviceIp = window.location.hostname; // Your devices IP here\n"
           "   \n"
           "\n"
           "    // Add event listeners that get called when a new packet is received / state of device changes\n"
           "    httpconn.addEventListener('fromRadio', function(packet) { console.log(packet)});\n"
           "\n"
           "    // Connect to the device async, then send a text message\n"
           "    httpconn.connect(deviceIp, sslActive)\n"
           "    .then(result => { \n"
           "\n"
           "        alert('device has been configured')\n"
           "        // This gets called when the connection has been established\n"
           "        // -> send a message over the mesh network. If no recipient node is provided, it gets sent as a broadcast\n"
           "        return httpconn.sendText('meshtastic is awesome');\n"
           "\n"
           "    })\n"
           "    .then(result => { \n"
           "\n"
           "        // This gets called when the message has been sucessfully sent\n"
           "        console.log('Message sent!');})\n"
           "\n"
           "    .catch(error => { console.log(error); });\n"
           "\n"
           "}";

    // Status code is 200 OK by default.
    // We want to deliver a simple HTML page, so we send a corresponding content type:
    res->setHeader("Content-Type", "text/javascript");

    // The response implements the Print interface, so you can use it just like
    // you would write to Serial etc.
    res->print(out);
}

/*
    To convert text to c strings:

    https://tomeko.net/online_tools/cpp_text_escape.php?lang=en
*/
void handleBasicHTML(HTTPRequest *req, HTTPResponse *res)
{
    String out = "";
    out += "<!doctype html>\n"
           "<html class=\"no-js\" lang=\"\">\n"
           "\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <title></title>\n"
           "\n"
           "  <script src=\"/static/meshtastic.js\"></script>\n"
           "  <script src=\"basic.js\"></script>\n"
           "</head>\n"
           "\n"
           "<body>\n"
           "\n"
           "  <button id=\"connect_button\" onclick=\"connect()\">Connect to Meshtastic device</button>\n"
           " \n"
           "</body>\n"
           "\n"
           "</html>";

    // Status code is 200 OK by default.
    // We want to deliver a simple HTML page, so we send a corresponding content type:
    res->setHeader("Content-Type", "text/html");

    // The response implements the Print interface, so you can use it just like
    // you would write to Serial etc.
    res->print(out);
}