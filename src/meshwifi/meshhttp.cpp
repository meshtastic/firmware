#include "meshwifi/meshhttp.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"
#include "meshwifi/meshwifi.h"
#include <WebServer.h>
#include <WiFi.h>

WebServer webserver(80);

const uint16_t maxMessages = 50;

struct message_t {
    char sender[10];
    char message[250];
    int32_t gpsLat;
    int32_t gpsLong;
    uint32_t time;
    bool fromMe;
};

struct messages_t
{
  message_t history[maxMessages]; // 900 positions to save up to 1200 seconds (15 minutes). uInt for each temerature sensor, Input and Setpoint.
};

messages_t messages_history;

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
    webserver.on("/json/chat/send/channel", handleJSONChatHistory);
    webserver.on("/json/chat/send/user", handleJSONChatHistory);
    webserver.on("/json/chat/history/channel", handleJSONChatHistory);
    webserver.on("/json/chat/history/user", handleJSONChatHistory);
    webserver.on("/json/stats", handleJSONChatHistory);
    webserver.on("/hotspot-detect.html", handleHotspot);
    webserver.on("/", handleRoot);
    webserver.begin();
}

void handleJSONChatHistory()
{
    int i;

    String out = "";
    out += "{\n";
    out += "  \"data\" : {\n";
    out += "    \"chat\" : ";
    out += "[";
    out += "\"" + sender + "\"";
    out += ",";
    out += "\"" + something + "\"";
    out += "]\n";

    for (i = 0; i < maxMessages; i++) {
        out += "[";
        out += "\"" + String(messages_history.history[i].sender) + "\"";
        out += ",";
        out += "\"" + String(messages_history.history[i].message) + "\"";
        out += "]\n";
    }

    out += "\n";
    out += "  }\n";
    out += "}\n";

    webserver.send(200, "application/json", out);
    return;
}

void handleNotFound()
{
    String message = "";
    message += "File Not Found\n\n";
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
}

/*
    This supports the Apple Captive Network Assistant (CNA) Portal
*/
void handleHotspot()
{
    DEBUG_MSG("Hotspot Request\n");

    /*
        If we don't do a redirect, be sure to return a "Success" message
        otherwise iOS will have trouble detecting that the connection to the SoftAP worked.
    */

    String out = "";
    // out += "Success\n";
    out += "<meta http-equiv=\"refresh\" content=\"0;url=http://meshtastic.org/\" />\n";
    webserver.send(200, "text/html", out);
    return;
}

void handleRoot()
{
    DEBUG_MSG("Hotspot Request\n");

    /*
        If we don't do a redirect, be sure to return a "Success" message
        otherwise iOS will have trouble detecting that the connection to the SoftAP worked.
    */

    String out = "";
    // out += "Success\n";
    out += "<!doctype html>\n"
           "<html>\n"
           "<head>\n"
           "<meta charset=\"utf-8\">\n"
           "<title>Meshtastic - WebUI</title>\n"
           "</head>\n"
           "\n"
           "<body>\n"
           "<center>\n"
           "  <form>\n"
           "    <table width=\"900\" height=\"100%\" border=\"0\">\n"
           "      <tbody>\n"
           "        <tr>\n"
           "          <td align=\"center\">Meshtastic - WebUI (Pre-Alpha)</td>\n"
           "        </tr>\n"
           "        <tr>\n"
           "          <td><table width=\"100%\" height=\"100%\" border=\"1\">\n"
           "              <tbody>\n"
           "                <tr>\n"
           "                  <td width=\"50\" height=\"100%\">Channels:<br>\n"
           "                    <br>\n"
           "                    #general<br>\n"
           "                    #stuff<br>\n"
           "                    #things<br>\n"
           "                    <br></td>\n"
           "                  <td height=\"100%\" valign=\"top\"><div>Everything is awesome!</div></td>\n"
           "                </tr>\n"
           "              </tbody>\n"
           "            </table></td>\n"
           "        </tr>\n"
           "        <tr>\n"
           "          <td align=\"center\"><input type=\"text\" size=\"50\">\n"
           "            <input type=\"submit\"></td>\n"
           "        </tr>\n"
           "      </tbody>\n"
           "    </table>\n"
           "  </form>\n"
           "</center>\n"
           "</body>\n"
           "</html>\n";
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