/* based on https://github.com/arcao/Syslog

MIT License

Copyright (c) 2016 Martin Sloup

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/

#include "configuration.h"

#include "DebugConfiguration.h"

#if HAS_NETWORKING

Syslog::Syslog(UDP &client)
{
    this->_client = &client;
    this->_server = NULL;
    this->_port = 0;
    this->_deviceHostname = SYSLOG_NILVALUE;
    this->_appName = SYSLOG_NILVALUE;
    this->_priDefault = LOGLEVEL_KERN;
}

Syslog &Syslog::server(const char *server, uint16_t port)
{
    if (this->_ip.fromString(server)) {
        this->_server = NULL;
    } else {
        this->_server = server;
    }
    this->_port = port;
    return *this;
}

Syslog &Syslog::server(IPAddress ip, uint16_t port)
{
    this->_ip = ip;
    this->_server = NULL;
    this->_port = port;
    return *this;
}

Syslog &Syslog::deviceHostname(const char *deviceHostname)
{
    this->_deviceHostname = (deviceHostname == NULL) ? SYSLOG_NILVALUE : deviceHostname;
    return *this;
}

Syslog &Syslog::appName(const char *appName)
{
    this->_appName = (appName == NULL) ? SYSLOG_NILVALUE : appName;
    return *this;
}

Syslog &Syslog::defaultPriority(uint16_t pri)
{
    this->_priDefault = pri;
    return *this;
}

Syslog &Syslog::logMask(uint8_t priMask)
{
    this->_priMask = priMask;
    return *this;
}

void Syslog::enable()
{
    this->_enabled = true;
}

void Syslog::disable()
{
    this->_enabled = false;
}

bool Syslog::isEnabled()
{
    return this->_enabled;
}

bool Syslog::vlogf(uint16_t pri, const char *fmt, va_list args)
{
    return this->vlogf(pri, this->_appName, fmt, args);
}

bool Syslog::vlogf(uint16_t pri, const char *appName, const char *fmt, va_list args)
{
    char *message;
    size_t initialLen;
    size_t len;
    bool result;

    initialLen = strlen(fmt);

    message = new char[initialLen + 1];

    len = vsnprintf(message, initialLen + 1, fmt, args);
    if (len > initialLen) {
        delete[] message;
        message = new char[len + 1];

        vsnprintf(message, len + 1, fmt, args);
    }

    result = this->_sendLog(pri, appName, message);

    delete[] message;
    return result;
}

inline bool Syslog::_sendLog(uint16_t pri, const char *appName, const char *message)
{
    int result;

    if (!this->_enabled)
        return false;

    if ((this->_server == NULL && this->_ip == INADDR_NONE) || this->_port == 0)
        return false;

    // Check priority against priMask values.
    if ((LOG_MASK(LOG_PRI(pri)) & this->_priMask) == 0)
        return true;

    // Set default facility if none specified.
    if ((pri & LOG_FACMASK) == 0)
        pri = LOG_MAKEPRI(LOG_FAC(this->_priDefault), pri);

    if (this->_server != NULL) {
        result = this->_client->beginPacket(this->_server, this->_port);
    } else {
        result = this->_client->beginPacket(this->_ip, this->_port);
    }

    if (result != 1)
        return false;

    this->_client->print('<');
    this->_client->print(pri);
    this->_client->print(F(">1 - "));
    this->_client->print(this->_deviceHostname);
    this->_client->print(' ');
    this->_client->print(appName);
    this->_client->print(F(" - - - \xEF\xBB\xBF"));
    this->_client->print(F("["));
    this->_client->print(int(millis() / 1000));
    this->_client->print(F("]: "));
    this->_client->print(message);
    this->_client->endPacket();

    return true;
}

#endif
