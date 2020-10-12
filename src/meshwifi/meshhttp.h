#pragma once

#include <Arduino.h>
#include <functional>
#include "PhoneAPI.h"

void initWebServer();
void createSSLCert();

void handleNotFound();

void handleWebResponse();

void handleJSONChatHistory();

void notifyWebUI();

void handleHotspot();

void handleStyleCSS();
void handleRoot();
void handleScriptsScriptJS();
void handleJSONChatHistoryDummy();

class httpAPI : public PhoneAPI
{

public:
    // Nothing here yet

private:
    // Nothing here yet

protected:
    // Nothing here yet
    
};