#include "RoutingStatsModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "airtime.h"

RoutingStatsModule *routingStats;

#define PRINT_STATS_INTERVAL_MS (60 * 1000) // Print our own stats every 60 seconds
#define PRINT_STATS_WINDOW_SECS 900         // If not transmitting stats, reset the window every 15 minutes

/**
 * Log a routing event
 */
void RoutingStatsModule::logEvent(RoutingEvent event, meshtastic_MeshPacket *p, uint32_t detail)
{
    switch (event) {
    case RoutingEvent::TX_OK:
        stats.tx_total++;
        stats.tx_total_ms += detail;
        if (isFromUs(p)) {
            stats.tx_mine++;
            if (!p->hop_start)
                stats.tx_zero++;
        } else
            stats.tx_relayed++;
        break;
    case RoutingEvent::TX_DROP:
        stats.tx_dropped++;
        break;
    case RoutingEvent::TX_DEFER:
        stats.tx_deferred++;
        break;
    case RoutingEvent::TX_HWM:
        if (detail > stats.tx_hwm)
            stats.tx_hwm = detail;
        break;
    case RoutingEvent::RX_OK:
        stats.rx_total++;
        stats.rx_total_ms += detail;
        if (p) {
            if (p->hop_limit == p->hop_start) {
                if (!p->hop_start)
                    stats.rx_zero++;
                else
                    stats.rx_direct++;
            } else if (!p->hop_limit)
                stats.rx_eol++;
        }
        break;
    case RoutingEvent::RX_BAD:
        stats.rx_bad++;
        stats.rx_total_ms += detail;
        break;
    default:
        LOG_WARN("Unknown routing event %d", static_cast<int>(event));
        break;
    }
}

/**
 * Print routing stats to the console
 */
void RoutingStatsModule::printStats(meshtastic_RoutingStats *stats, NodeNum src)
{
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(src);
    const char *short_name = (node && node->has_user) ? node->user.short_name : "????";

    LOG_INFO("Routing stats from %s (0x%08x), last %lu seconds", short_name, src, stats->window_secs);
    LOG_INFO("  Airtime: chutil=%lu%% duty=%lu%% rx=%lums tx=%lums", stats->channel_util_pct, stats->tx_duty_pct,
             stats->rx_total_ms, stats->tx_total_ms);
    LOG_INFO("  TX: packets=%lu relayed=%lu mine=%lu zero-hop=%lu dropped=%lu hwm=%lu", stats->tx_total, stats->tx_relayed,
             stats->tx_mine, stats->tx_zero, stats->tx_dropped, stats->tx_hwm);
    LOG_INFO("  RX: packets=%lu bad=%lu direct=%lu zero-hop=%lu eol=%lu", stats->rx_total, stats->rx_bad, stats->rx_direct,
             stats->rx_zero, stats->rx_eol);
}

/**
 * Handle an incoming routing stats protobuf
 */
bool RoutingStatsModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_RoutingStats *decoded)
{
    printStats(decoded, getFrom(&mp));
    return true;
}

/**
 * Periodic thread wakeup
 */
int32_t RoutingStatsModule::runOnce()
{
    unsigned long now = millis();
    unsigned long next_tx_millis = config.device.routing_stats_broadcast_secs
                                       ? (last_tx_millis + config.device.routing_stats_broadcast_secs * 1000)
                                       : INT32_MAX;
    unsigned long next_print_millis = last_print_millis + PRINT_STATS_INTERVAL_MS;

    // Update 'now' fields
    stats.window_secs = (now - last_tx_millis) / 1000;
    stats.channel_util_pct = airTime->channelUtilizationPercent();
    stats.tx_duty_pct = airTime->utilizationTXPercent();

    if (now >= next_print_millis) {
        printStats(&stats, nodeDB->getNodeNum());
        last_print_millis = now;
        next_print_millis = last_print_millis + PRINT_STATS_INTERVAL_MS;
        if (!config.device.routing_stats_broadcast_secs && (last_tx_millis + PRINT_STATS_WINDOW_SECS * 1000) < now) {
            // Reset stats window if we're not configured to broadcast stats
            stats = {};
            last_tx_millis = now;
        }
    }

    if (next_tx_millis > now)
        return (next_tx_millis > next_print_millis ? next_print_millis : next_tx_millis) - now;

    LOG_DEBUG("Broadcast routing stats for last %d seconds", stats.window_secs);
    meshtastic_MeshPacket *p = allocDataProtobuf(stats);
    p->to = NODENUM_BROADCAST;
    service->sendToMesh(p);

    stats = {};
    last_tx_millis = now;
    next_tx_millis = last_tx_millis + config.device.routing_stats_broadcast_secs * 1000;

    return (next_tx_millis > next_print_millis ? next_print_millis : next_tx_millis) - now;
}
