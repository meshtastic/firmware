#pragma once
#include "ProtobufModule.h"

typedef enum class RoutingEvent {
    TX_OK = 1,    // Successfully transmitted a packet. Detail = transmission time in ms
    TX_DROP = 2,  // Packet dropped from TX queue.
    TX_DEFER = 3, // Packet deferred to late contention window.
    TX_HWM = 4,   // High water mark of TX queue. Detail = current TX queue length
    RX_OK = 5,    // Successfully received a packet. Detail = reception time in ms
    RX_BAD = 6,   // Failed to receive a packet. Detail = reception time in ms
} RoutingEvent;

class RoutingStatsModule : public ProtobufModule<meshtastic_RoutingStats>, public concurrency::OSThread
{
  public:
    RoutingStatsModule()
        : ProtobufModule("RoutingStats", meshtastic_PortNum_ROUTING_STATS_APP, meshtastic_RoutingStats_fields),
          concurrency::OSThread("RoutingStats")
    {
    }
    void logEvent(RoutingEvent event, meshtastic_MeshPacket *p = NULL, uint32_t detail = 0);

  private:
    unsigned long last_tx_millis = 0;
    unsigned long last_print_millis = 0;
    meshtastic_RoutingStats stats{};
    void printStats(meshtastic_RoutingStats *stats, NodeNum src = 0);
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RoutingStats *decoded) override;
    int32_t runOnce() override;
};

extern RoutingStatsModule *routingStats;