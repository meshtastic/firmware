#include "ethBoundedClient.h"

#if HAS_ETHERNET && !defined(USE_WS5500) && !defined(USE_CH390D)

#include "Throttle.h"
#include "UptimeClock.h"
// Here rather than in the header: it defines function-like htons()/ntohs()/htonl()/ntohl() macros.
#ifdef USE_ARDUINO_ETHERNET
#include <utility/w5100.h>
#else
#include <w5100.h>
#endif

// 200 ms is the chip's reset value; 3 retries instead of the default 8 drop an unacknowledged socket after
// 0.2 + 0.4 + 0.8 + 1.6 = 3 s instead of ~32 s, inside the watchdog.
static constexpr uint16_t ETH_TCP_RETRY_TIMEOUT_MS = 200;
static constexpr uint8_t ETH_TCP_RETRY_COUNT = 3;

void ethLimitTcpRetransmission()
{
    Ethernet.setRetransmissionTimeout(ETH_TCP_RETRY_TIMEOUT_MS);
    Ethernet.setRetransmissionCount(ETH_TCP_RETRY_COUNT);
}

void ethFlush(EthernetClient &client, uint32_t timeoutMs)
{
    const uint32_t start = Time::getMillis();
    while (client.connected() && client.availableForWrite() < (int)W5100.SSIZE) {
        if (Throttle::hasElapsed(start, timeoutMs))
            return;
        delay(1);
    }
}

bool ethWaitForWriteSpace(EthernetClient &client, size_t len, uint32_t timeoutMs)
{
    const size_t need = len < W5100.SSIZE ? len : W5100.SSIZE;
    const uint32_t start = Time::getMillis();
    while ((size_t)client.availableForWrite() < need) {
        if (!client.connected() || Throttle::hasElapsed(start, timeoutMs))
            return false;
        delay(1);
    }
    return true;
}

#endif
