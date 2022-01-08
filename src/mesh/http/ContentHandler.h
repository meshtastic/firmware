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
void handleUpdateSPIFFS(HTTPRequest *req, HTTPResponse *res);
void handleDeleteSPIFFSContent(HTTPRequest *req, HTTPResponse *res);
void handleSPIFFS(HTTPRequest *req, HTTPResponse *res);
void handleAdmin(HTTPRequest *req, HTTPResponse *res);
void handleAdminSettings(HTTPRequest *req, HTTPResponse *res);
void handleAdminSettingsApply(HTTPRequest *req, HTTPResponse *res);


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


