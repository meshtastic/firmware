#include "meshwifi/meshhttp.h"
#include "NodeDB.h"
#include "configuration.h"
#include "main.h"
#include "meshwifi/meshwifi.h"
#include <WebServer.h>
#include <WiFi.h>

WebServer webserver(80);

// Maximum number of messages for chat history. Don't make this too big -- it'll use a
//   lot of memory!
const uint16_t maxMessages = 50;

struct message_t {
    char sender[10];
    char message[250];
    int32_t gpsLat;
    int32_t gpsLong;
    uint32_t time;
    bool fromMe;
};

struct messages_t {
    message_t history[maxMessages]; 
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
    webserver.on("/css/style.css", handleStyleCSS);
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

//----------------------------------------

void handleRoot()
{

    String out = "";
    out += " < !DOCTYPE html >\n"
           "<html lang=\"en\" >\n"
           "<head>\n"
           "  <meta charset=\"UTF-8\">\n"
           "  <title>Meshtastic - Chat</title>\n"
           "  <link rel=\"stylesheet\" href=\"css/style.css\">\n"
           "\n"
           "</head>\n"
           "<body>\n"
           "<div class=\"grid\">\n"
           "\t<div class=\"top\">\n"
           "\t\t<div class=\"top-text\">Meshtastic - Chat</div>\n"
           "\t</div>\n"
           "\n"
           "\t<div class=\"side clearfix\">\n"
           "    <div class=\"channel-list\" id=\"channel-list\">\n"
           "\t  <div class=\"side-header\">\n"
           "\t\t<div class=\"side-text\">Channels</div>\n"
           "\t  </div>\n"
           "      <ul class=\"list\">\n"
           "        <li class=\"clearfix\">\n"
           "          <div class=\"channel-name clearfix\">HotChannel</div>\n"
           "          <div class=\"message-count clearfix\">\n"
           "\t\t    <i class=\"fa fa-circle online\"></i> 28 messages\n"
           "          </div>\n"
           "        </li>\n"
           "\t\t<li class=\"clearfix\">\n"
           "          <div class=\"channel-name clearfix\">AnotherChannel</div>\n"
           "          <div class=\"message-count clearfix\">\n"
           "\t\t    <i class=\"fa fa-circle online\"></i> 12 messages\n"
           "          </div>\n"
           "        </li>\n"
           "\t\t<li class=\"clearfix\">\n"
           "          <div class=\"channel-name clearfix\">Lost</div>\n"
           "          <div class=\"message-count clearfix\">\n"
           "\t\t    <i class=\"fa fa-circle online\"></i> 6 messages\n"
           "          </div>\n"
           "        </li>\n"
           "      </ul>\n"
           "    </div>\n"
           "    </div>\n"
           "    <div class=\"content\">\n"
           "      <div class=\"content-header clearfix\">\n"
           "<!--      <div class=\"content-about\"> -->\n"
           "          <div class=\"content-from\">Chat from: \n"
           "\t\t      <span class=\"selected-channel\">HotChannel</span>\n"
           "\t\t  </div>\n"
           "<!--      </div> -->\n"
           "      </div> <!-- end content-header -->\n"
           "      \n"
           "      <div class=\"content-history\">\n"
           "        <ul>\n"
           "          <li class=\"clearfix\">\n"
           "            <div class=\"message-data align-right\">\n"
           "              <span class=\"message-data-time\" >10:10 AM, Today</span> &nbsp; &nbsp;\n"
           "              <span class=\"message-data-name\" >Olia</span> <i class=\"fa fa-circle me\"></i>\n"
           "              \n"
           "            </div>\n"
           "            <div class=\"message other-message float-right\">\n"
           "              Hi Vincent, how are you? How is the project coming along?\n"
           "            </div>\n"
           "          </li>\n"
           "          \n"
           "          <li>\n"
           "            <div class=\"message-data\">\n"
           "              <span class=\"message-data-name\"><i class=\"fa fa-circle online\"></i> Vincent</span>\n"
           "              <span class=\"message-data-time\">10:12 AM, Today</span>\n"
           "            </div>\n"
           "            <div class=\"message my-message\">\n"
           "              Are we meeting today? Project has been already finished and I have results to show you.\n"
           "            </div>\n"
           "          </li>\n"
           "          \n"
           "          <li class=\"clearfix\">\n"
           "            <div class=\"message-data align-right\">\n"
           "              <span class=\"message-data-time\" >10:14 AM, Today</span> &nbsp; &nbsp;\n"
           "              <span class=\"message-data-name\" >Olia</span> <i class=\"fa fa-circle me\"></i>\n"
           "              \n"
           "            </div>\n"
           "            <div class=\"message other-message float-right\">\n"
           "              Well I am not sure. The rest of the team is not here yet. Maybe in an hour or so? Have you faced any "
           "problems at the last phase of the project?\n"
           "            </div>\n"
           "          </li>\n"
           "          \n"
           "          <li>\n"
           "            <div class=\"message-data\">\n"
           "              <span class=\"message-data-name\"><i class=\"fa fa-circle online\"></i> Vincent</span>\n"
           "              <span class=\"message-data-time\">10:20 AM, Today</span>\n"
           "            </div>\n"
           "            <div class=\"message my-message\">\n"
           "              Actually everything was fine. I'm very excited to show this to our team.\n"
           "            </div>\n"
           "          </li>\n"
           "          \n"
           "          \n"
           "        </ul>\n"
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
           "\n"
           "</body>\n"
           "</html>";
    webserver.send(200, "text/html", out);
    return;
}

void handleStyleCSS()
{

    String out = "";
    out +=
        "/* latin-ext */\n"
        "@font-face {\n"
        "  font-family: 'Lato';\n"
        "  font-style: normal;\n"
        "  font-weight: 400;\n"
        "  src: local('Lato Regular'), local('Lato-Regular'), url(./Google.woff2) format('woff2');\n"
        "  unicode-range: U+0100-024F, U+0259, U+1E00-1EFF, U+2020, U+20A0-20AB, U+20AD-20CF, U+2113, U+2C60-2C7F, U+A720-A7FF;\n"
        "}\n"
        "\n"
        "*, *:before, *:after {\n"
        "  box-sizing: border-box;\n"
        "}\n"
        "\n"
        "body {\n"
        "  background: #C5DDEB;\n"
        "  font: 14px/20px \"Lato\", Arial, sans-serif;\n"
        "  padding: 40px 0;\n"
        "  color: white;\n"
        "}\n"
        "\n"
        "\n"
        "  \n"
        ".grid {\n"
        "  display: grid;\n"
        "  grid-template-columns:\n"
        "\t1fr 4fr;\n"
        "  grid-template-areas:\n"
        "\t\"header header\"\n"
        "\t\"sidebar content\";\n"
        "  margin: 0 auto;\n"
        "  width: 750px;\n"
        "  background: #444753;\n"
        "  border-radius: 5px;\n"
        "}\n"
        "\n"
        ".top {grid-area: header;}\n"
        ".side {grid-area: sidebar;}\n"
        ".main {grid-area: content;}\n"
        "\n"
        ".top {\n"
        "  border-bottom: 2px solid white;\n"
        "}\n"
        ".top-text {\n"
        "  font-weight: bold;\n"
        "  font-size: 24px;\n"
        "  text-align: center;\n"
        "  padding: 20px;\n"
        "}\n"
        "\n"
        ".side {\n"
        "  width: 260px;\n"
        "  float: left;\n"
        "}\n"
        ".side .side-header {\n"
        "  padding: 20px;\n"
        "  border-bottom: 2px solid white;\n"
        "}\n"
        "\n"
        ".side .side-header .side-text {\n"
        "  padding-left: 10px;\n"
        "  margin-top: 6px;\n"
        "  font-size: 16px;\n"
        "  text-align: left;\n"
        "  font-weight: bold;\n"
        "  \n"
        "}\n"
        "\n"
        ".channel-list ul {\n"
        "  padding: 20px;\n"
        "  height: 570px;\n"
        "  list-style-type: none;\n"
        "}\n"
        ".channel-list ul li {\n"
        "  padding-bottom: 20px;\n"
        "}\n"
        "\n"
        ".channel-list .channel-name {\n"
        "  font-size: 20px;\n"
        "  margin-top: 8px;\n"
        "  padding-left: 8px;\n"
        "}\n"
        "\n"
        ".channel-list .message-count {\n"
        "  padding-left: 16px;\n"
        "  color: #92959E;\n"
        "}\n"
        "\n"
        ".content {\n"
        "  display: flex;\n"
        "  flex-direction: column;\n"
        "  flex-wrap: nowrap;\n"
        "/* width: 490px; */\n"
        "  float: left;\n"
        "  background: #F2F5F8;\n"
        "/*  border-top-right-radius: 5px;\n"
        "  border-bottom-right-radius: 5px; */\n"
        "  color: #434651;\n"
        "}\n"
        ".content .content-header {\n"
        "  flex-grow: 0;\n"
        "  padding: 20px;\n"
        "  border-bottom: 2px solid white;\n"
        "}\n"
        "\n"
        ".content .content-header .content-from {\n"
        "  padding-left: 10px;\n"
        "  margin-top: 6px;\n"
        "  font-size: 20px;\n"
        "  text-align: center;\n"
        "  font-size: 16px;\n"
        "}\n"
        ".content .content-header .content-from .selected-channel {\n"
        "  font-weight: bold;\n"
        "}\n"
        ".content .content-header .content-num-messages {\n"
        "  color: #92959E;\n"
        "}\n"
        "\n"
        ".content .content-history {\n"
        "  flex-grow: 1;\n"
        "  padding: 20px 20px 20px;\n"
        "  border-bottom: 2px solid white;\n"
        "  overflow-y: scroll;\n"
        "  height: 375px;\n"
        "}\n"
        ".content .content-history ul {\n"
        "  list-style-type: none;\n"
        "  padding-inline-start: 10px;\n"
        "}\n"
        ".content .content-history .message-data {\n"
        "  margin-bottom: 10px;\n"
        "}\n"
        ".content .content-history .message-data-time {\n"
        "  color: #a8aab1;\n"
        "  padding-left: 6px;\n"
        "}\n"
        ".content .content-history .message {\n"
        "  color: white;\n"
        "  padding: 8px 10px;\n"
        "  line-height: 20px;\n"
        "  font-size: 14px;\n"
        "  border-radius: 7px;\n"
        "  margin-bottom: 30px;\n"
        "  width: 90%;\n"
        "  position: relative;\n"
        "}\n"
        ".content .content-history .message:after {\n"
        "  bottom: 100%;\n"
        "  left: 7%;\n"
        "  border: solid transparent;\n"
        "  content: \" \";\n"
        "  height: 0;\n"
        "  width: 0;\n"
        "  position: absolute;\n"
        "  pointer-events: none;\n"
        "  border-bottom-color: #86BB71;\n"
        "  border-width: 10px;\n"
        "  margin-left: -10px;\n"
        "}\n"
        ".content .content-history .my-message {\n"
        "  background: #86BB71;\n"
        "}\n"
        ".content .content-history .other-message {\n"
        "  background: #94C2ED;\n"
        "}\n"
        ".content .content-history .other-message:after {\n"
        "  border-bottom-color: #94C2ED;\n"
        "  left: 93%;\n"
        "}\n"
        ".content .content-message {\n"
        "  flex-grow: 0;\n"
        "  padding: 10px;\n"
        "}\n"
        ".content .content-message textarea {\n"
        "  width: 100%;\n"
        "  border: none;\n"
        "  padding: 10px 10px;\n"
        "  font: 14px/22px \"Lato\", Arial, sans-serif;\n"
        "  margin-bottom: 10px;\n"
        "  border-radius: 5px;\n"
        "  resize: none;\n"
        "}\n"
        "\n"
        ".content .content-message button {\n"
        "  float: right;\n"
        "  color: #94C2ED;\n"
        "  font-size: 16px;\n"
        "  text-transform: uppercase;\n"
        "  border: none;\n"
        "  cursor: pointer;\n"
        "  font-weight: bold;\n"
        "  background: #F2F5F8;\n"
        "}\n"
        ".content .content-message button:hover {\n"
        "  color: #75b1e8;\n"
        "}\n"
        "\n"
        ".online, .offline, .me {\n"
        "  margin-right: 3px;\n"
        "  font-size: 10px;\n"
        "}\n"
        "\n"
        ".online {\n"
        "  color: #86BB71;\n"
        "}\n"
        "\n"
        ".offline {\n"
        "  color: #E38968;\n"
        "}\n"
        "\n"
        ".me {\n"
        "  color: #94C2ED;\n"
        "}\n"
        "\n"
        ".align-left {\n"
        "  text-align: left;\n"
        "}\n"
        "\n"
        ".align-right {\n"
        "  text-align: right;\n"
        "}\n"
        "\n"
        ".float-right {\n"
        "  float: right;\n"
        "}\n"
        "\n"
        ".clearfix:after {\n"
        "  visibility: hidden;\n"
        "  display: block;\n"
        "  font-size: 0;\n"
        "  content: \" \";\n"
        "  clear: both;\n"
        "  height: 0;\n"
        "}";

    webserver.send(200, "text/html", out);
    return;
}