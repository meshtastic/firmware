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
    webserver.on("/json/chat/history/dummy", handleJSONChatHistoryDummy);
    webserver.on("/json/chat/history/user", handleJSONChatHistory);
    webserver.on("/json/stats", handleJSONChatHistory);
    webserver.on("/hotspot-detect.html", handleHotspot);
    webserver.on("/css/style.css", handleStyleCSS);
    webserver.on("/scripts/script.js", handleScriptsScriptJS);
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

/*
    To convert text to c strings:

    https://tomeko.net/online_tools/cpp_text_escape.php?lang=en
*/
void handleRoot()
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
        "  <link rel=\"stylesheet\" href=\"css/style.css\">\n"
        "\n"
        "</head>\n"
        "<body>\n"
        "<!-- Add SVG for Symbols -->\n"
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
        ".icon {\n"
        "  display: inline-block;\n"
        "  width: 1em;\n"
        "  height: 1em;\n"
        "  stroke-width: 0;\n"
        "  stroke: currentColor;\n"
        "  fill: currentColor;\n"
        "}\n"
        "\n"
        ".icon-map-marker {\n"
        "  width: 0.5714285714285714em;\n"
        "}\n"
        "\n"
        ".icon-circle {\n"
        "  width: 0.8571428571428571em;\n"
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
        ".content .content-header .content-from .content-from-highlight {\n"
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
        "/* Tooltip container */\n"
        ".tooltip {\n"
        "  color: #86BB71;\n"
        "  position: relative;\n"
        "  display: inline-block;\n"
        "  border-bottom: 1px dotted black; /* If you want dots under the hoverable text */\n"
        "}\n"
        "/* Tooltip text */\n"
        ".tooltip .tooltiptext {\n"
        "  visibility: hidden;\n"
        "  width: 120px;\n"
        "  background-color: #444753;\n"
        "  color: #fff;\n"
        "  text-align: center;\n"
        "  padding: 5px 0;\n"
        "  border-radius: 6px;\n"
        "   /* Position the tooltip text - see examples below! */\n"
        "  position: absolute;\n"
        "  z-index: 1;\n"
        "}\n"
        "\n"
        "/* Show the tooltip text when you mouse over the tooltip container */\n"
        ".tooltip:hover .tooltiptext {\n"
        "  visibility: visible;\n"
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

    webserver.send(200, "text/css", out);
    return;
}

void handleScriptsScriptJS()
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

    webserver.send(200, "text/javascript", out);
    return;
}

void handleJSONChatHistoryDummy()
{
    String out = "";
    out += "{\n"
           "\t\"data\": {\n"
           "\t\t\"system\": {\n"
           "\t\t\t\"timeSinceStart\": 3213544,\n"
           "\t\t\t\"timeGPS\": 1600830985,\n"
           "\t\t\t\"channel\": \"ourSecretPlace\"\n"
           "\t\t},\n"
           "\t\t\"users\": [{\n"
           "\t\t\t\t\"NameShort\": \"J\",\n"
           "\t\t\t\t\"NameLong\": \"John\",\n"
           "\t\t\t\t\"lastSeen\": 3207544,\n"
           "\t\t\t\t\"lat\" : -2.882243,\n"
           "\t\t\t\t\"lon\" : -111.038580\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"NameShort\": \"D\",\n"
           "\t\t\t\t\"NameLong\": \"David\",\n"
           "\t\t\t\t\"lastSeen\": 3212544,\n"
           "\t\t\t\t\"lat\" : -12.24452,\n"
           "\t\t\t\t\"lon\" : -61.87351\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"NameShort\": \"P\",\n"
           "\t\t\t\t\"NameLong\": \"Peter\",\n"
           "\t\t\t\t\"lastSeen\": 3213444,\n"
           "\t\t\t\t\"lat\" : 0,\n"
           "\t\t\t\t\"lon\" : 0\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"NameShort\": \"M\",\n"
           "\t\t\t\t\"NameLong\": \"Mary\",\n"
           "\t\t\t\t\"lastSeen\": 3211544,\n"
           "\t\t\t\t\"lat\" : 16.45478,\n"
           "\t\t\t\t\"lon\" : 11.40166\n"
           "\t\t\t}\n"
           "\t\t],\n"
           "\t\t\"chat\": [{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"J\",\n"
           "\t\t\t\t\"NameLong\": \"John\",\n"
           "\t\t\t\t\"chatLine\": \"Hello\",\n"
           "\t\t\t\t\"timestamp\" : 3203544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"D\",\n"
           "\t\t\t\t\"NameLong\": \"David\",\n"
           "\t\t\t\t\"chatLine\": \"Hello There\",\n"
           "\t\t\t\t\"timestamp\" : 3204544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"J\",\n"
           "\t\t\t\t\"NameLong\": \"John\",\n"
           "\t\t\t\t\"chatLine\": \"Where you been?\",\n"
           "\t\t\t\t\"timestamp\" : 3205544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"D\",\n"
           "\t\t\t\t\"NameLong\": \"David\",\n"
           "\t\t\t\t\"chatLine\": \"I was on Channel 2\",\n"
           "\t\t\t\t\"timestamp\" : 3206544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"J\",\n"
           "\t\t\t\t\"NameLong\": \"John\",\n"
           "\t\t\t\t\"chatLine\": \"With Mary again?\",\n"
           "\t\t\t\t\"timestamp\" : 3207544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"D\",\n"
           "\t\t\t\t\"NameLong\": \"David\",\n"
           "\t\t\t\t\"chatLine\": \"She's better looking than you\",\n"
           "\t\t\t\t\"timestamp\" : 3208544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"M\",\n"
           "\t\t\t\t\"NameLong\": \"Mary\",\n"
           "\t\t\t\t\"chatLine\": \"Well, Hi\",\n"
           "\t\t\t\t\"timestamp\" : 3209544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"D\",\n"
           "\t\t\t\t\"NameLong\": \"David\",\n"
           "\t\t\t\t\"chatLine\": \"You're Here\",\n"
           "\t\t\t\t\"timestamp\" : 3210544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"M\",\n"
           "\t\t\t\t\"NameLong\": \"Mary\",\n"
           "\t\t\t\t\"chatLine\": \"Wanted to say Howdy.\",\n"
           "\t\t\t\t\"timestamp\" : 3211544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 0,\n"
           "\t\t\t\t\"NameShort\": \"D\",\n"
           "\t\t\t\t\"NameLong\": \"David\",\n"
           "\t\t\t\t\"chatLine\": \"Better come down and visit sometime\",\n"
           "\t\t\t\t\"timestamp\" : 3212544\n"
           "\t\t\t},\n"
           "\t\t\t{\n"
           "\t\t\t\t\"local\": 1,\n"
           "\t\t\t\t\"NameShort\": \"P\",\n"
           "\t\t\t\t\"NameLong\": \"Peter\",\n"
           "\t\t\t\t\"chatLine\": \"Where is everybody?\",\n"
           "\t\t\t\t\"timestamp\" : 3213444\n"
           "\t\t\t}\n"
           "\t\t]\n"
           "\t}\n"
           "}";

    webserver.send(200, "application/json", out);
    return;
}