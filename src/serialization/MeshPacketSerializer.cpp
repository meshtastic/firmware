#include "MeshPacketSerializer.h"
#include "JSON.h"
#include "NodeDB.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "modules/RoutingModule.h"
#include <DebugConfiguration.h>
#include <mesh-pb-constants.h>
#if defined(ARCH_ESP32)
#include "../mesh/generated/meshtastic/paxcount.pb.h"
#endif
#include "mesh/generated/meshtastic/remote_hardware.pb.h"

std::string MeshPacketSerializer::JsonSerialize(const meshtastic_MeshPacket *mp, bool shouldLog)
{
    // the created jsonObj is immutable after creation, so
    // we need to do the heavy lifting before assembling it.
    std::string msgType;
    JSONObject jsonObj;

    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        JSONObject msgPayload;
        switch (mp->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP: {
            msgType = "text";
            // convert bytes to string
            if (shouldLog)
                LOG_DEBUG("got text message of size %u\n", mp->decoded.payload.size);

            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0; // null terminated string
            // check if this is a JSON payload
            JSONValue *json_value = JSON::Parse(payloadStr);
            if (json_value != NULL) {
                if (shouldLog)
                    LOG_INFO("text message payload is of type json\n");

                // if it is, then we can just use the json object
                jsonObj["payload"] = json_value;
            } else {
                // if it isn't, then we need to create a json object
                // with the string as the value
                if (shouldLog)
                    LOG_INFO("text message payload is of type plaintext\n");

                msgPayload["text"] = new JSONValue(payloadStr);
                jsonObj["payload"] = new JSONValue(msgPayload);
            }
            break;
        }
        case meshtastic_PortNum_TELEMETRY_APP: {
            msgType = "telemetry";
            meshtastic_Telemetry scratch;
            meshtastic_Telemetry *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
                decoded = &scratch;
                if (decoded->which_variant == meshtastic_Telemetry_device_metrics_tag) {
                    msgPayload["battery_level"] = new JSONValue((unsigned int)decoded->variant.device_metrics.battery_level);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.device_metrics.voltage);
                    msgPayload["channel_utilization"] = new JSONValue(decoded->variant.device_metrics.channel_utilization);
                    msgPayload["air_util_tx"] = new JSONValue(decoded->variant.device_metrics.air_util_tx);
                    msgPayload["uptime_seconds"] = new JSONValue((unsigned int)decoded->variant.device_metrics.uptime_seconds);
                } else if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
                    msgPayload["temperature"] = new JSONValue(decoded->variant.environment_metrics.temperature);
                    msgPayload["relative_humidity"] = new JSONValue(decoded->variant.environment_metrics.relative_humidity);
                    msgPayload["barometric_pressure"] = new JSONValue(decoded->variant.environment_metrics.barometric_pressure);
                    msgPayload["gas_resistance"] = new JSONValue(decoded->variant.environment_metrics.gas_resistance);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.environment_metrics.voltage);
                    msgPayload["current"] = new JSONValue(decoded->variant.environment_metrics.current);
                    msgPayload["lux"] = new JSONValue(decoded->variant.environment_metrics.lux);
                    msgPayload["white_lux"] = new JSONValue(decoded->variant.environment_metrics.white_lux);
                    msgPayload["iaq"] = new JSONValue((uint)decoded->variant.environment_metrics.iaq);
                    msgPayload["wind_speed"] = new JSONValue(decoded->variant.environment_metrics.wind_speed);
                    msgPayload["wind_direction"] = new JSONValue((uint)decoded->variant.environment_metrics.wind_direction);
                    msgPayload["wind_gust"] = new JSONValue(decoded->variant.environment_metrics.wind_gust);
                    msgPayload["wind_lull"] = new JSONValue(decoded->variant.environment_metrics.wind_lull);
                } else if (decoded->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
                    msgPayload["pm10"] = new JSONValue((unsigned int)decoded->variant.air_quality_metrics.pm10_standard);
                    msgPayload["pm25"] = new JSONValue((unsigned int)decoded->variant.air_quality_metrics.pm25_standard);
                    msgPayload["pm100"] = new JSONValue((unsigned int)decoded->variant.air_quality_metrics.pm100_standard);
                    msgPayload["pm10_e"] = new JSONValue((unsigned int)decoded->variant.air_quality_metrics.pm10_environmental);
                    msgPayload["pm25_e"] = new JSONValue((unsigned int)decoded->variant.air_quality_metrics.pm25_environmental);
                    msgPayload["pm100_e"] = new JSONValue((unsigned int)decoded->variant.air_quality_metrics.pm100_environmental);
                } else if (decoded->which_variant == meshtastic_Telemetry_power_metrics_tag) {
                    msgPayload["voltage_ch1"] = new JSONValue(decoded->variant.power_metrics.ch1_voltage);
                    msgPayload["current_ch1"] = new JSONValue(decoded->variant.power_metrics.ch1_current);
                    msgPayload["voltage_ch2"] = new JSONValue(decoded->variant.power_metrics.ch2_voltage);
                    msgPayload["current_ch2"] = new JSONValue(decoded->variant.power_metrics.ch2_current);
                    msgPayload["voltage_ch3"] = new JSONValue(decoded->variant.power_metrics.ch3_voltage);
                    msgPayload["current_ch3"] = new JSONValue(decoded->variant.power_metrics.ch3_current);
                }
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else if (shouldLog) {
                LOG_ERROR("Error decoding protobuf for telemetry message!\n");
            }
            break;
        }
        case meshtastic_PortNum_NODEINFO_APP: {
            msgType = "nodeinfo";
            meshtastic_User scratch;
            meshtastic_User *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_User_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["id"] = new JSONValue(decoded->id);
                msgPayload["longname"] = new JSONValue(decoded->long_name);
                msgPayload["shortname"] = new JSONValue(decoded->short_name);
                msgPayload["hardware"] = new JSONValue(decoded->hw_model);
                msgPayload["role"] = new JSONValue((int)decoded->role);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else if (shouldLog) {
                LOG_ERROR("Error decoding protobuf for nodeinfo message!\n");
            }
            break;
        }
        case meshtastic_PortNum_POSITION_APP: {
            msgType = "position";
            meshtastic_Position scratch;
            meshtastic_Position *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Position_msg, &scratch)) {
                decoded = &scratch;
                if ((int)decoded->time) {
                    msgPayload["time"] = new JSONValue((unsigned int)decoded->time);
                }
                if ((int)decoded->timestamp) {
                    msgPayload["timestamp"] = new JSONValue((unsigned int)decoded->timestamp);
                }
                msgPayload["latitude_i"] = new JSONValue((int)decoded->latitude_i);
                msgPayload["longitude_i"] = new JSONValue((int)decoded->longitude_i);
                if ((int)decoded->altitude) {
                    msgPayload["altitude"] = new JSONValue((int)decoded->altitude);
                }
                if ((int)decoded->ground_speed) {
                    msgPayload["ground_speed"] = new JSONValue((unsigned int)decoded->ground_speed);
                }
                if (int(decoded->ground_track)) {
                    msgPayload["ground_track"] = new JSONValue((unsigned int)decoded->ground_track);
                }
                if (int(decoded->sats_in_view)) {
                    msgPayload["sats_in_view"] = new JSONValue((unsigned int)decoded->sats_in_view);
                }
                if ((int)decoded->PDOP) {
                    msgPayload["PDOP"] = new JSONValue((int)decoded->PDOP);
                }
                if ((int)decoded->HDOP) {
                    msgPayload["HDOP"] = new JSONValue((int)decoded->HDOP);
                }
                if ((int)decoded->VDOP) {
                    msgPayload["VDOP"] = new JSONValue((int)decoded->VDOP);
                }
                if ((int)decoded->precision_bits) {
                    msgPayload["precision_bits"] = new JSONValue((int)decoded->precision_bits);
                }
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else if (shouldLog) {
                LOG_ERROR("Error decoding protobuf for position message!\n");
            }
            break;
        }
        case meshtastic_PortNum_WAYPOINT_APP: {
            msgType = "position";
            meshtastic_Waypoint scratch;
            meshtastic_Waypoint *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Waypoint_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["id"] = new JSONValue((unsigned int)decoded->id);
                msgPayload["name"] = new JSONValue(decoded->name);
                msgPayload["description"] = new JSONValue(decoded->description);
                msgPayload["expire"] = new JSONValue((unsigned int)decoded->expire);
                msgPayload["locked_to"] = new JSONValue((unsigned int)decoded->locked_to);
                msgPayload["latitude_i"] = new JSONValue((int)decoded->latitude_i);
                msgPayload["longitude_i"] = new JSONValue((int)decoded->longitude_i);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else if (shouldLog) {
                LOG_ERROR("Error decoding protobuf for position message!\n");
            }
            break;
        }
        case meshtastic_PortNum_NEIGHBORINFO_APP: {
            msgType = "neighborinfo";
            meshtastic_NeighborInfo scratch;
            meshtastic_NeighborInfo *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_NeighborInfo_msg,
                                     &scratch)) {
                decoded = &scratch;
                msgPayload["node_id"] = new JSONValue((unsigned int)decoded->node_id);
                msgPayload["node_broadcast_interval_secs"] = new JSONValue((unsigned int)decoded->node_broadcast_interval_secs);
                msgPayload["last_sent_by_id"] = new JSONValue((unsigned int)decoded->last_sent_by_id);
                msgPayload["neighbors_count"] = new JSONValue(decoded->neighbors_count);
                JSONArray neighbors;
                for (uint8_t i = 0; i < decoded->neighbors_count; i++) {
                    JSONObject neighborObj;
                    neighborObj["node_id"] = new JSONValue((unsigned int)decoded->neighbors[i].node_id);
                    neighborObj["snr"] = new JSONValue((int)decoded->neighbors[i].snr);
                    neighbors.push_back(new JSONValue(neighborObj));
                }
                msgPayload["neighbors"] = new JSONValue(neighbors);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else if (shouldLog) {
                LOG_ERROR("Error decoding protobuf for neighborinfo message!\n");
            }
            break;
        }
        case meshtastic_PortNum_TRACEROUTE_APP: {
            if (mp->decoded.request_id) { // Only report the traceroute response
                msgType = "traceroute";
                meshtastic_RouteDiscovery scratch;
                meshtastic_RouteDiscovery *decoded = NULL;
                memset(&scratch, 0, sizeof(scratch));
                if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_RouteDiscovery_msg,
                                         &scratch)) {
                    decoded = &scratch;
                    JSONArray route; // Route this message took
                    // Lambda function for adding a long name to the route
                    auto addToRoute = [](JSONArray *route, NodeNum num) {
                        char long_name[40] = "Unknown";
                        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
                        bool name_known = node ? node->has_user : false;
                        if (name_known)
                            memcpy(long_name, node->user.long_name, sizeof(long_name));
                        route->push_back(new JSONValue(long_name));
                    };
                    addToRoute(&route, mp->to); // Started at the original transmitter (destination of response)
                    for (uint8_t i = 0; i < decoded->route_count; i++) {
                        addToRoute(&route, decoded->route[i]);
                    }
                    addToRoute(&route, mp->from); // Ended at the original destination (source of response)

                    msgPayload["route"] = new JSONValue(route);
                    jsonObj["payload"] = new JSONValue(msgPayload);
                } else if (shouldLog) {
                    LOG_ERROR("Error decoding protobuf for traceroute message!\n");
                }
            }
            break;
        }
        case meshtastic_PortNum_DETECTION_SENSOR_APP: {
            msgType = "detection";
            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0; // null terminated string
            msgPayload["text"] = new JSONValue(payloadStr);
            jsonObj["payload"] = new JSONValue(msgPayload);
            break;
        }
#ifdef ARCH_ESP32
        case meshtastic_PortNum_PAXCOUNTER_APP: {
            msgType = "paxcounter";
            meshtastic_Paxcount scratch;
            meshtastic_Paxcount *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Paxcount_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["wifi_count"] = new JSONValue((unsigned int)decoded->wifi);
                msgPayload["ble_count"] = new JSONValue((unsigned int)decoded->ble);
                msgPayload["uptime"] = new JSONValue((unsigned int)decoded->uptime);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else if (shouldLog) {
                LOG_ERROR("Error decoding protobuf for Paxcount message!\n");
            }
            break;
        }
#endif
        case meshtastic_PortNum_REMOTE_HARDWARE_APP: {
            meshtastic_HardwareMessage scratch;
            meshtastic_HardwareMessage *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_HardwareMessage_msg,
                                     &scratch)) {
                decoded = &scratch;
                if (decoded->type == meshtastic_HardwareMessage_Type_GPIOS_CHANGED) {
                    msgType = "gpios_changed";
                    msgPayload["gpio_value"] = new JSONValue((unsigned int)decoded->gpio_value);
                    jsonObj["payload"] = new JSONValue(msgPayload);
                } else if (decoded->type == meshtastic_HardwareMessage_Type_READ_GPIOS_REPLY) {
                    msgType = "gpios_read_reply";
                    msgPayload["gpio_value"] = new JSONValue((unsigned int)decoded->gpio_value);
                    msgPayload["gpio_mask"] = new JSONValue((unsigned int)decoded->gpio_mask);
                    jsonObj["payload"] = new JSONValue(msgPayload);
                }
            } else if (shouldLog) {
                LOG_ERROR("Error decoding protobuf for RemoteHardware message!\n");
            }
            break;
        }
        // add more packet types here if needed
        default:
            break;
        }
    } else if (shouldLog) {
        LOG_WARN("Couldn't convert encrypted payload of MeshPacket to JSON\n");
    }

    jsonObj["id"] = new JSONValue((unsigned int)mp->id);
    jsonObj["timestamp"] = new JSONValue((unsigned int)mp->rx_time);
    jsonObj["to"] = new JSONValue((unsigned int)mp->to);
    jsonObj["from"] = new JSONValue((unsigned int)mp->from);
    jsonObj["channel"] = new JSONValue((unsigned int)mp->channel);
    jsonObj["type"] = new JSONValue(msgType.c_str());
    jsonObj["sender"] = new JSONValue(owner.id);
    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = new JSONValue((int)mp->rx_rssi);
    if (mp->rx_snr != 0)
        jsonObj["snr"] = new JSONValue((float)mp->rx_snr);
    if (mp->hop_start != 0 && mp->hop_limit <= mp->hop_start) {
        jsonObj["hops_away"] = new JSONValue((unsigned int)(mp->hop_start - mp->hop_limit));
        jsonObj["hop_start"] = new JSONValue((unsigned int)(mp->hop_start));
    }

    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObj);
    std::string jsonStr = value->Stringify();

    if (shouldLog)
        LOG_INFO("serialized json message: %s\n", jsonStr.c_str());

    delete value;
    return jsonStr;
}

std::string MeshPacketSerializer::JsonSerializeEncrypted(const meshtastic_MeshPacket *mp)
{
    JSONObject jsonObj;

    jsonObj["id"] = new JSONValue((unsigned int)mp->id);
    jsonObj["time_ms"] = new JSONValue((double)millis());
    jsonObj["timestamp"] = new JSONValue((unsigned int)mp->rx_time);
    jsonObj["to"] = new JSONValue((unsigned int)mp->to);
    jsonObj["from"] = new JSONValue((unsigned int)mp->from);
    jsonObj["channel"] = new JSONValue((unsigned int)mp->channel);
    jsonObj["want_ack"] = new JSONValue(mp->want_ack);

    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = new JSONValue((int)mp->rx_rssi);
    if (mp->rx_snr != 0)
        jsonObj["snr"] = new JSONValue((float)mp->rx_snr);
    if (mp->hop_start != 0 && mp->hop_limit <= mp->hop_start) {
        jsonObj["hops_away"] = new JSONValue((unsigned int)(mp->hop_start - mp->hop_limit));
        jsonObj["hop_start"] = new JSONValue((unsigned int)(mp->hop_start));
    }
    jsonObj["size"] = new JSONValue((unsigned int)mp->encrypted.size);
    auto encryptedStr = bytesToHex(mp->encrypted.bytes, mp->encrypted.size);
    jsonObj["bytes"] = new JSONValue(encryptedStr.c_str());

    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObj);
    std::string jsonStr = value->Stringify();

    delete value;
    return jsonStr;
}