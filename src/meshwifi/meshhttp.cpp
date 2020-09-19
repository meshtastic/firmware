#include "meshwifi/meshhttp.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"
#include "meshwifi/meshwifi.h"
#include <WebServer.h>
#include <WiFi.h>

WebServer webserver(80);

String something = "";
String sender = "";

void handleWebResponse()
{
    if (isWifiAvailable() == 0) {
        return;
    }

    // We're going to handle the DNS responder here so it
    // will be ignored by the NRF boards.
    handleDNSResponse();

    webserver.handleClient();
}

void initWebServer()
{
    webserver.onNotFound(handleNotFound);
    // webserver.on("/", handleJSONChatHistory);
    // webserver.on("/json/chat/history", handleJSONChatHistory);
    webserver.on("/hotspot-detect.html", handleHotspot);
    webserver.on("/", []() { webserver.send(200, "text/plain", "Everything is awesome!"); });
    webserver.begin();
}

void handleJSONChatHistory()
{

    String out = "";
    out += "{\n";
    out += "  \"data\" : {\n";
    out += "    \"chat\" : ";
    out += "[";
    out += "\"" + sender + "\"";
    out += ",";
    out += "\"" + something + "\"";
    out += "]\n";

    out += "\n";
    out += "  }\n";
    out += "}\n";

    webserver.send(200, "application/json", out);
    return;
}

void handleNotFound()
{
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += webserver.uri();
    message += "\nMethod: ";
    message += (webserver.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += webserver.args();
    message += "\n";

    for (uint8_t i = 0; i < webserver.args(); i++) {
        message += " " + webserver.argName(i) + ": " + webserver.arg(i) + "\n";
    }
    Serial.println(message);
    webserver.send(404, "text/plain", message);
    /*
     */
}

/*
    This supports the Apple Captive Network Assistant (CNA) Portal
*/
void handleHotspot()
{
    DEBUG_MSG("Hotspot Request\n");

    String out = "";
    // out += "Success\n";
    out += "<meta http-equiv=\"refresh\" content=\"0;url=http://meshtastic.org/\" />\n";
    webserver.send(200, "text/html", out);
    return;
}

void notifyWebUI()
{
    DEBUG_MSG("************ Got a message! ************\n");
    MeshPacket &mp = devicestate.rx_text_message;
    NodeInfo *node = nodeDB.getNode(mp.from);
    sender = (node && node->has_user) ? node->user.long_name : "???";

    static char tempBuf[256]; // mesh.options says this is MeshPacket.encrypted max_size
    assert(mp.decoded.which_payload == SubPacket_data_tag);
    snprintf(tempBuf, sizeof(tempBuf), "%s", mp.decoded.data.payload.bytes);

    something = tempBuf;
}