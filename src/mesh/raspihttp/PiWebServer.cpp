/*
Adds a WebServer and WebService callbacks to meshtastic as Linux Version. The WebServer & Webservices
runs in a real linux thread beside the portdunio threading emulation. It replaces the complete ESP32
Webserver libs including generation of SSL certifcicates,  because the use ESP specific details in
the lib that can't be emulated.

The WebServices adapt to the two major phoneapi functions "handleAPIv1FromRadio,handleAPIv1ToRadio"
The WebServer just adds basaic support to deliver WebContent, so it can be used to
deliver the WebGui definded by the WebClient Project.

Steps to get it running:
1.) Add these Linux Libs to the compile and target machine:

    sudo apt update && \
        apt -y install openssl libssl-dev libopenssl libsdl2-dev \
                     libulfius-dev liborcania-dev

2.) Configure the root directory of the web Content in the config.yaml file.
    The followinng tags should be included and set at your needs

    Example entry in the config.yaml
    Webserver:
        Port: 9001 # Port for Webserver & Webservices
        RootPath: /home/marc/web # Root Dir of WebServer

3.) Checkout the web project
    https://github.com/meshtastic/web.git

    Build it and copy the content of the folder web/dist/* to the folder you did set as "RootPath"

!!!The WebServer should not be used as production system or exposed to the Internet. Its a raw basic version!!!

Author: Marc Philipp Hammermann
mail:   marchammermann@googlemail.com

*/
#ifdef PORTDUINO_LINUX_HARDWARE
#if __has_include(<ulfius.h>)
#include "PiWebServer.h"
#include "NodeDB.h"
#include "PhoneAPI.h"
#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "airtime.h"
#include "graphics/Screen.h"
#include "main.h"
#include "mesh/wifi/WiFiAPClient.h"
#include "sleep.h"
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <orcania.h>
#include <string.h>
#include <ulfius.h>
#include <yder.h>

#include <cstring>
#include <string>

#include "PortduinoFS.h"
#include "platform/portduino/PortduinoGlue.h"

#define DEFAULT_REALM "default_realm"
#define PREFIX ""

struct _file_config configWeb;

// We need to specify some content-type mapping, so the resources get delivered with the
// right content type and are displayed correctly in the browser
char contentTypes[][2][32] = {{".txt", "text/plain"},      {".html", "text/html"},
                              {".js", "text/javascript"},  {".png", "image/png"},
                              {".jpg", "image/jpg"},       {".gz", "application/gzip"},
                              {".gif", "image/gif"},       {".json", "application/json"},
                              {".css", "text/css"},        {".ico", "image/vnd.microsoft.icon"},
                              {".svg", "image/svg+xml"},   {".ts", "text/javascript"},
                              {".tsx", "text/javascript"}, {"", ""}};

#undef str

volatile bool isWebServerReady;
volatile bool isCertReady;

HttpAPI webAPI;

PiWebServerThread *piwebServerThread;

/**
 * Return the filename extension
 */
const char *get_filename_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path)
        return "*";
    if (strchr(dot, '?') != NULL) {
        //*strchr(dot, '?') = '\0';
        const char *empty = "\0";
        return empty;
    }
    return dot;
}

/**
 * Streaming callback function to ease sending large files
 */
static ssize_t callback_static_file_stream(void *cls, uint64_t pos, char *buf, size_t max)
{
    (void)(pos);
    if (cls != NULL) {
        return fread(buf, 1, max, (FILE *)cls);
    } else {
        return U_STREAM_END;
    }
}

/**
 * Cleanup FILE* structure when streaming is complete
 */
static void callback_static_file_stream_free(void *cls)
{
    if (cls != NULL) {
        fclose((FILE *)cls);
    }
}

/**
 * static file callback endpoint that delivers the content for WebServer calls
 */
int callback_static_file(const struct _u_request *request, struct _u_response *response, void *user_data)
{
    size_t length;
    FILE *f;
    char *file_requested, *file_path, *url_dup_save, *real_path = NULL;
    const char *content_type;

    /*
     * Comment this if statement if you don't access static files url from root dir, like /app
     */
    if (request->callback_position > 0) {
        return U_CALLBACK_CONTINUE;
    } else if (user_data != NULL && (configWeb.files_path != NULL)) {
        file_requested = o_strdup(request->http_url);
        url_dup_save = file_requested;

        while (file_requested[0] == '/') {
            file_requested++;
        }
        file_requested += o_strlen(configWeb.url_prefix);
        while (file_requested[0] == '/') {
            file_requested++;
        }

        if (strchr(file_requested, '#') != NULL) {
            *strchr(file_requested, '#') = '\0';
        }

        if (strchr(file_requested, '?') != NULL) {
            *strchr(file_requested, '?') = '\0';
        }

        if (file_requested == NULL || o_strlen(file_requested) == 0 || 0 == o_strcmp("/", file_requested)) {
            o_free(url_dup_save);
            url_dup_save = file_requested = o_strdup("index.html");
        }

        file_path = msprintf("%s/%s", configWeb.files_path, file_requested);
        real_path = realpath(file_path, NULL);
        if (0 == o_strncmp(configWeb.files_path, real_path, o_strlen(configWeb.files_path))) {
            if (access(file_path, F_OK) != -1) {
                f = fopen(file_path, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    length = ftell(f);
                    fseek(f, 0, SEEK_SET);

                    content_type = u_map_get_case(&configWeb.mime_types, get_filename_ext(file_requested));
                    if (content_type == NULL) {
                        content_type = u_map_get(&configWeb.mime_types, "*");
                        LOG_DEBUG("Static File Server - Unknown mime type for extension %s \n", get_filename_ext(file_requested));
                    }
                    u_map_put(response->map_header, "Content-Type", content_type);
                    u_map_copy_into(response->map_header, &configWeb.map_header);

                    if (ulfius_set_stream_response(response, 200, callback_static_file_stream, callback_static_file_stream_free,
                                                   length, STATIC_FILE_CHUNK, f) != U_OK) {
                        LOG_DEBUG("callback_static_file - Error ulfius_set_stream_response\n	");
                    }
                }
            } else {
                if (configWeb.redirect_on_404 == NULL) {
                    ulfius_set_string_body_response(response, 404, "File not found");
                } else {
                    ulfius_add_header_to_response(response, "Location", configWeb.redirect_on_404);
                    response->status = 302;
                }
            }
        } else {
            if (configWeb.redirect_on_404 == NULL) {
                ulfius_set_string_body_response(response, 404, "File not found");
            } else {
                ulfius_add_header_to_response(response, "Location", configWeb.redirect_on_404);
                response->status = 302;
            }
        }

        o_free(file_path);
        o_free(url_dup_save);
        free(real_path); // realpath uses malloc
        return U_CALLBACK_CONTINUE;
    } else {
        LOG_DEBUG("Static File Server - Error, user_data is NULL or inconsistent\n");
        return U_CALLBACK_ERROR;
    }
}

static void handleWebResponse() {}

/*
 * Adapt the radioapi to the Webservice handleAPIv1ToRadio
 * Trigger : WebGui(SAVE)->WebServcice->phoneApi
 */
int handleAPIv1ToRadio(const struct _u_request *req, struct _u_response *res, void *user_data)
{
    LOG_DEBUG("handleAPIv1ToRadio web -> radio  \n");

    ulfius_add_header_to_response(res, "Content-Type", "application/x-protobuf");
    ulfius_add_header_to_response(res, "Access-Control-Allow-Headers", "Content-Type");
    ulfius_add_header_to_response(res, "Access-Control-Allow-Origin", "*");
    ulfius_add_header_to_response(res, "Access-Control-Allow-Methods", "PUT, OPTIONS");
    ulfius_add_header_to_response(res, "X-Protobuf-Schema",
                                  "https://raw.githubusercontent.com/meshtastic/protobufs/master/mesh.proto");

    if (req->http_verb == "OPTIONS") {
        ulfius_set_response_properties(res, U_OPT_STATUS, 204);
        return U_CALLBACK_CONTINUE;
    }

    byte buffer[MAX_TO_FROM_RADIO_SIZE];
    size_t s = req->binary_body_length;

    memcpy(buffer, req->binary_body, MAX_TO_FROM_RADIO_SIZE);

    // FIXME* Problem with portdunio loosing mountpoint maybe because of running in a real sep. thread

    portduinoVFS->mountpoint(configWeb.rootPath);

    LOG_DEBUG("Received %d bytes from PUT request\n", s);
    webAPI.handleToRadio(buffer, s);
    LOG_DEBUG("end web->radio  \n");
    return U_CALLBACK_COMPLETE;
}

/*
 * Adapt the radioapi to the Webservice handleAPIv1FromRadio
 * Trigger : WebGui(POLL)->handleAPIv1FromRadio->phoneapi->Meshtastic(Radio) events
 */
int handleAPIv1FromRadio(const struct _u_request *req, struct _u_response *res, void *user_data)
{

    // LOG_DEBUG("handleAPIv1FromRadio radio -> web\n");
    std::string valueAll;

    // Status code is 200 OK by default.
    ulfius_add_header_to_response(res, "Content-Type", "application/x-protobuf");
    ulfius_add_header_to_response(res, "Access-Control-Allow-Origin", "*");
    ulfius_add_header_to_response(res, "Access-Control-Allow-Methods", "GET");
    ulfius_add_header_to_response(res, "X-Protobuf-Schema",
                                  "https://raw.githubusercontent.com/meshtastic/protobufs/master/mesh.proto");

    uint8_t txBuf[MAX_STREAM_BUF_SIZE];
    uint32_t len = 1;

    if (valueAll == "true") {
        while (len) {
            len = webAPI.getFromRadio(txBuf);
            ulfius_set_response_properties(res, U_OPT_STATUS, 200, U_OPT_BINARY_BODY, txBuf, len);
            const char *tmpa = (const char *)txBuf;
            ulfius_set_string_body_response(res, 200, tmpa);
            // LOG_DEBUG("\n----webAPI response all:----\n");
            // LOG_DEBUG(tmpa);
            // LOG_DEBUG("\n");
        }
        // Otherwise, just return one protobuf
    } else {
        len = webAPI.getFromRadio(txBuf);
        const char *tmpa = (const char *)txBuf;
        ulfius_set_binary_body_response(res, 200, tmpa, len);
        // LOG_DEBUG("\n----webAPI response:\n");
        // LOG_DEBUG(tmpa);
        // LOG_DEBUG("\n");
    }

    // LOG_DEBUG("end radio->web\n", len);
    return U_CALLBACK_COMPLETE;
}

/*
OpenSSL RSA Key Gen
*/
int generate_rsa_key(EVP_PKEY **pkey)
{
    EVP_PKEY_CTX *pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pkey_ctx)
        return -1;
    if (EVP_PKEY_keygen_init(pkey_ctx) <= 0)
        return -1;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048) <= 0)
        return -1;
    if (EVP_PKEY_keygen(pkey_ctx, pkey) <= 0)
        return -1;
    EVP_PKEY_CTX_free(pkey_ctx);
    return 0; // SUCCESS
}

int generate_self_signed_x509(EVP_PKEY *pkey, X509 **x509)
{
    *x509 = X509_new();
    if (!*x509)
        return -1;
    if (X509_set_version(*x509, 2) != 1)
        return -1;
    ASN1_INTEGER_set(X509_get_serialNumber(*x509), 1);
    X509_gmtime_adj(X509_get_notBefore(*x509), 0);
    X509_gmtime_adj(X509_get_notAfter(*x509), 31536000L); // 1 YEAR ACCESS

    X509_set_pubkey(*x509, pkey);

    // SET Subject Name
    X509_NAME *name = X509_get_subject_name(*x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"DE", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char *)"Meshtastic", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"meshtastic.local", -1, -1, 0);
    // Selfsigned,  Issuer = Subject
    X509_set_issuer_name(*x509, name);

    // Certificate signed with our privte key
    if (X509_sign(*x509, pkey, EVP_sha256()) <= 0)
        return -1;

    return 0;
}

char *read_file_into_string(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        LOG_ERROR("Error reading File : %s \n", filename);
        return NULL;
    }

    // Size of file
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    rewind(file);

    // reserve mem for file + 1 byte
    char *buffer = (char *)malloc(filesize + 1);
    if (buffer == NULL) {
        LOG_ERROR("Malloc of mem failed for file : %s \n", filename);
        fclose(file);
        return NULL;
    }

    // read content
    size_t readSize = fread(buffer, 1, filesize, file);
    if (readSize != filesize) {
        LOG_ERROR("Error reading file into buffer\n");
        free(buffer);
        fclose(file);
        return NULL;
    }

    // add terminator sign at the end
    buffer[filesize] = '\0';
    fclose(file);
    return buffer; // return pointer
}

int PiWebServerThread::CheckSSLandLoad()
{
    // read certificate
    cert_pem = read_file_into_string("certificate.pem");
    if (cert_pem == NULL) {
        LOG_ERROR("ERROR SSL Certificate File can't be loaded or is missing\n");
        return 1;
    }
    // read private key
    key_pem = read_file_into_string("private_key.pem");
    if (key_pem == NULL) {
        LOG_ERROR("ERROR file private_key can't be loaded or is missing\n");
        return 2;
    }

    return 0;
}

int PiWebServerThread::CreateSSLCertificate()
{

    EVP_PKEY *pkey = NULL;
    X509 *x509 = NULL;

    if (generate_rsa_key(&pkey) != 0) {
        LOG_ERROR("Error generating RSA-Key.\n");
        return 1;
    }

    if (generate_self_signed_x509(pkey, &x509) != 0) {
        LOG_ERROR("Error generating of X509-Certificat.\n");
        return 2;
    }

    // Ope file to write private key file
    FILE *pkey_file = fopen("private_key.pem", "wb");
    if (!pkey_file) {
        LOG_ERROR("Error opening private key file.\n");
        return 3;
    }
    // write private key file
    PEM_write_PrivateKey(pkey_file, pkey, NULL, NULL, 0, NULL, NULL);
    fclose(pkey_file);

    // open Certificate file
    FILE *x509_file = fopen("certificate.pem", "wb");
    if (!x509_file) {
        LOG_ERROR("Error opening certificate.\n");
        return 4;
    }
    // write cirtificate
    PEM_write_X509(x509_file, x509);
    fclose(x509_file);

    EVP_PKEY_free(pkey);
    X509_free(x509);
    LOG_INFO("Create SSL Certifictate -certificate.pem- succesfull \n");
    return 0;
}

void initWebServer() {}

PiWebServerThread::PiWebServerThread()
{
    int ret, retssl, webservport;

    if (CheckSSLandLoad() != 0) {
        CreateSSLCertificate();
        if (CheckSSLandLoad() != 0) {
            LOG_ERROR("Major Error Gen & Read SSL Certificate\n");
        }
    }

    if (settingsMap[webserverport] != 0) {
        webservport = settingsMap[webserverport];
        LOG_INFO("Using webserver port from yaml config. %i \n", webservport);
    } else {
        LOG_INFO("Webserver port in yaml config set to 0, so defaulting to port 443.\n");
        webservport = 443;
    }

    // Web Content Service Instance
    if (ulfius_init_instance(&instanceWeb, webservport, NULL, DEFAULT_REALM) != U_OK) {
        LOG_ERROR("Webserver couldn't be started, abort execution\n");
    } else {

        LOG_INFO("Webserver started ....\n");
        u_map_init(&configWeb.mime_types);
        u_map_put(&configWeb.mime_types, "*", "application/octet-stream");
        u_map_put(&configWeb.mime_types, ".html", "text/html");
        u_map_put(&configWeb.mime_types, ".htm", "text/html");
        u_map_put(&configWeb.mime_types, ".tsx", "application/javascript");
        u_map_put(&configWeb.mime_types, ".ts", "application/javascript");
        u_map_put(&configWeb.mime_types, ".css", "text/css");
        u_map_put(&configWeb.mime_types, ".js", "application/javascript");
        u_map_put(&configWeb.mime_types, ".json", "application/json");
        u_map_put(&configWeb.mime_types, ".png", "image/png");
        u_map_put(&configWeb.mime_types, ".gif", "image/gif");
        u_map_put(&configWeb.mime_types, ".jpeg", "image/jpeg");
        u_map_put(&configWeb.mime_types, ".jpg", "image/jpeg");
        u_map_put(&configWeb.mime_types, ".ttf", "font/ttf");
        u_map_put(&configWeb.mime_types, ".woff", "font/woff");
        u_map_put(&configWeb.mime_types, ".ico", "image/x-icon");
        u_map_put(&configWeb.mime_types, ".svg", "image/svg+xml");

        webrootpath = settingsStrings[webserverrootpath];

        configWeb.files_path = (char *)webrootpath.c_str();
        configWeb.url_prefix = "";
        configWeb.rootPath = strdup(portduinoVFS->mountpoint());

        u_map_put(instanceWeb.default_headers, "Access-Control-Allow-Origin", "*");
        // Maximum body size sent by the client is 1 Kb
        instanceWeb.max_post_body_size = 1024;
        ulfius_add_endpoint_by_val(&instanceWeb, "GET", PREFIX, "/api/v1/fromradio/*", 1, &handleAPIv1FromRadio, NULL);
        ulfius_add_endpoint_by_val(&instanceWeb, "PUT", PREFIX, "/api/v1/toradio/*", 1, &handleAPIv1ToRadio, configWeb.rootPath);

        // Add callback function to all endpoints for the Web Server
        ulfius_add_endpoint_by_val(&instanceWeb, "GET", NULL, "/*", 2, &callback_static_file, &configWeb);

        // thats for serving without SSL
        // retssl = ulfius_start_framework(&instanceWeb);

        // thats for serving with SSL
        retssl = ulfius_start_secure_framework(&instanceWeb, key_pem, cert_pem);

        if (retssl == U_OK) {
            LOG_INFO("Web Server framework started on port: %i \n", webservport);
            LOG_INFO("Web Server root %s\n", (char *)webrootpath.c_str());
        } else {
            LOG_ERROR("Error starting Web Server framework, error number: %d\n", retssl);
        }
    }
}

PiWebServerThread::~PiWebServerThread()
{
    u_map_clean(&configWeb.mime_types);

    ulfius_stop_framework(&instanceWeb);
    ulfius_stop_framework(&instanceWeb);
    free(configWeb.rootPath);
    ulfius_clean_instance(&instanceService);
    ulfius_clean_instance(&instanceService);
    free(cert_pem);
    LOG_INFO("End framework");
}

#endif
#endif