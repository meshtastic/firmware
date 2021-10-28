#pragma once

void registerHandlers(HTTPServer *insecureServer, HTTPSServer *secureServer);

// Declare some handler functions for the various URLs on the server
void handleAPIv1FromRadio(HTTPRequest *req, HTTPResponse *res);
void handleAPIv1ToRadio(HTTPRequest *req, HTTPResponse *res);
void handleHotspot(HTTPRequest *req, HTTPResponse *res);
void handleStatic(HTTPRequest *req, HTTPResponse *res);
void handleRestart(HTTPRequest *req, HTTPResponse *res);
void handleFormUpload(HTTPRequest *req, HTTPResponse *res);
void handleScanNetworks(HTTPRequest *req, HTTPResponse *res);
void handleSpiffsBrowseStatic(HTTPRequest *req, HTTPResponse *res);
void handleSpiffsDeleteStatic(HTTPRequest *req, HTTPResponse *res);
void handleBlinkLED(HTTPRequest *req, HTTPResponse *res);
void handleReport(HTTPRequest *req, HTTPResponse *res);

void middlewareSpeedUp240(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);
void middlewareSpeedUp160(HTTPRequest *req, HTTPResponse *res, std::function<void()> next);

uint32_t getTimeSpeedUp();
void setTimeSpeedUp();


// Interface to the PhoneAPI to access the protobufs with messages
class HttpAPI : public PhoneAPI
{

  public:
    // Nothing here yet

  private:
    // Nothing here yet

  protected:
  
    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() { return true; } // FIXME, be smarter about this
};


