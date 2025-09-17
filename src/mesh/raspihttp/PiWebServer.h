#pragma once
#ifdef PORTDUINO_LINUX_HARDWARE
#if __has_include(<ulfius.h>)
#include "PhoneAPI.h"
#include "ulfius-cfg.h"
#include "ulfius.h"
#include <Arduino.h>
#include <functional>

#define STATIC_FILE_CHUNK 256

void initWebServer();
void createSSLCert();
int callback_static_file(const struct _u_request *request, struct _u_response *response, void *user_data);
const char *get_filename_ext(const char *path);

struct _file_config {
    char *files_path;
    char *url_prefix;
    struct _u_map mime_types;
    struct _u_map map_header;
    char *redirect_on_404;
    char *rootPath;
};

class HttpAPI : public PhoneAPI
{

  public:
    // Nothing here yet

  private:
    // Nothing here yet

  protected:
    /// Check the current underlying physical link to see if the client is currently connected
    virtual bool checkIsConnected() override { return true; } // FIXME, be smarter about this
};

class PiWebServerThread
{
  private:
    char *key_pem = NULL;
    char *cert_pem = NULL;
    // struct _u_map mime_types;
    std::string webrootpath;
    HttpAPI webAPI;

  public:
    PiWebServerThread();
    ~PiWebServerThread();
    int CreateSSLCertificate();
    int CheckSSLandLoad();
    uint32_t requestRestart = 0;
    struct _u_instance instanceWeb;
};

extern PiWebServerThread *piwebServerThread;

#endif
#endif