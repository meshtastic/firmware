#ifdef NRF52_USE_JSON
#warning 'Using nRF52 Serializer'

#include "ArduinoJson.h"
#include "MeshPacketSerializer.h"
#include "NodeDB.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "mesh/generated/meshtastic/remote_hardware.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "modules/RoutingModule.h"
#include <DebugConfiguration.h>
#include <mesh-pb-constants.h>

StaticJsonDocument<1024> jsonObj;
StaticJsonDocument<1024> arrayObj;

std::string MeshPacketSerializer::JsonSerialize(const meshtastic_MeshPacket *mp, bool shouldLog)
{
    // the created jsonObj is immutable after creation, so
    // we need to do the heavy lifting before assembling it.
    std::string msgType;
    jsonObj.clear();
    arrayObj.clear();

    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        switch (mp->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP: {
            msgType = "text";
            // convert bytes to string
            if (shouldLog)
                LOG_DEBUG("got text message of size %u", mp->decoded.payload.size);

            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0; // null terminated string
            // check if this is a JSON payload
            StaticJsonDocument<512> text_doc;
            DeserializationError error = deserializeJson(text_doc, payloadStr);
            if (error) {
                // if it isn't, then we need to create a json object
                // with the string as the value
                if (shouldLog)
                    LOG_INFO("text message payload is of type plaintext");
                jsonObj["payload"]["text"] = payloadStr;
            } else {
                // if it is, then we can just use the json object
                if (shouldLog)
                    LOG_INFO("text message payload is of type json");
                jsonObj["payload"] = text_doc;
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
                    jsonObj["payload"]["battery_level"] = (unsigned int)decoded->variant.device_metrics.battery_level;
                    jsonObj["payload"]["voltage"] = decoded->variant.device_metrics.voltage;
                    jsonObj["payload"]["channel_utilization"] = decoded->variant.device_metrics.channel_utilization;
                    jsonObj["payload"]["air_util_tx"] = decoded->variant.device_metrics.air_util_tx;
                    jsonObj["payload"]["uptime_seconds"] = (unsigned int)decoded->variant.device_metrics.uptime_seconds;
                } else if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
                    jsonObj["payload"]["temperature"] = decoded->variant.environment_metrics.temperature;
                    jsonObj["payload"]["relative_humidity"] = decoded->variant.environment_metrics.relative_humidity;
                    jsonObj["payload"]["barometric_pressure"] = decoded->variant.environment_metrics.barometric_pressure;
                    jsonObj["payload"]["gas_resistance"] = decoded->variant.environment_metrics.gas_resistance;
                    jsonObj["payload"]["voltage"] = decoded->variant.environment_metrics.voltage;
                    jsonObj["payload"]["current"] = decoded->variant.environment_metrics.current;
                    jsonObj["payload"]["lux"] = decoded->variant.environment_metrics.lux;
                    jsonObj["payload"]["white_lux"] = decoded->variant.environment_metrics.white_lux;
                    jsonObj["payload"]["iaq"] = (uint)decoded->variant.environment_metrics.iaq;
                    jsonObj["payload"]["wind_speed"] = decoded->variant.environment_metrics.wind_speed;
                    jsonObj["payload"]["wind_direction"] = (uint)decoded->variant.environment_metrics.wind_direction;
                    jsonObj["payload"]["wind_gust"] = decoded->variant.environment_metrics.wind_gust;
                    jsonObj["payload"]["wind_lull"] = decoded->variant.environment_metrics.wind_lull;
                    jsonObj["payload"]["radiation"] = decoded->variant.environment_metrics.radiation;
                } else if (decoded->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
                    jsonObj["payload"]["pm10"] = (unsigned int)decoded->variant.air_quality_metrics.pm10_standard;
                    jsonObj["payload"]["pm25"] = (unsigned int)decoded->variant.air_quality_metrics.pm25_standard;
                    jsonObj["payload"]["pm100"] = (unsigned int)decoded->variant.air_quality_metrics.pm100_standard;
                    jsonObj["payload"]["pm10_e"] = (unsigned int)decoded->variant.air_quality_metrics.pm10_environmental;
                    jsonObj["payload"]["pm25_e"] = (unsigned int)decoded->variant.air_quality_metrics.pm25_environmental;
                    jsonObj["payload"]["pm100_e"] = (unsigned int)decoded->variant.air_quality_metrics.pm100_environmental;
                } else if (decoded->which_variant == meshtastic_Telemetry_power_metrics_tag) {
                    jsonObj["payload"]["voltage_ch1"] = decoded->variant.power_metrics.ch1_voltage;
                    jsonObj["payload"]["current_ch1"] = decoded->variant.power_metrics.ch1_current;
                    jsonObj["payload"]["voltage_ch2"] = decoded->variant.power_metrics.ch2_voltage;
                    jsonObj["payload"]["current_ch2"] = decoded->variant.power_metrics.ch2_current;
                    jsonObj["payload"]["voltage_ch3"] = decoded->variant.power_metrics.ch3_voltage;
                    jsonObj["payload"]["current_ch3"] = decoded->variant.power_metrics.ch3_current;
                }
            } else if (shouldLog) {
                LOG_ERROR("Error decoding proto for telemetry message!");
                return "";
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
                jsonObj["payload"]["id"] = decoded->id;
                jsonObj["payload"]["longname"] = decoded->long_name;
                jsonObj["payload"]["shortname"] = decoded->short_name;
                jsonObj["payload"]["hardware"] = decoded->hw_model;
                jsonObj["payload"]["role"] = (int)decoded->role;
            } else if (shouldLog) {
                LOG_ERROR("Error decoding proto for nodeinfo message!");
                return "";
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
                    jsonObj["payload"]["time"] = (unsigned int)decoded->time;
                }
                if ((int)decoded->timestamp) {
                    jsonObj["payload"]["timestamp"] = (unsigned int)decoded->timestamp;
                }
                jsonObj["payload"]["latitude_i"] = (int)decoded->latitude_i;
                jsonObj["payload"]["longitude_i"] = (int)decoded->longitude_i;
                if ((int)decoded->altitude) {
                    jsonObj["payload"]["altitude"] = (int)decoded->altitude;
                }
                if ((int)decoded->ground_speed) {
                    jsonObj["payload"]["ground_speed"] = (unsigned int)decoded->ground_speed;
                }
                if (int(decoded->ground_track)) {
                    jsonObj["payload"]["ground_track"] = (unsigned int)decoded->ground_track;
                }
                if (int(decoded->sats_in_view)) {
                    jsonObj["payload"]["sats_in_view"] = (unsigned int)decoded->sats_in_view;
                }
                if ((int)decoded->PDOP) {
                    jsonObj["payload"]["PDOP"] = (int)decoded->PDOP;
                }
                if ((int)decoded->HDOP) {
                    jsonObj["payload"]["HDOP"] = (int)decoded->HDOP;
                }
                if ((int)decoded->VDOP) {
                    jsonObj["payload"]["VDOP"] = (int)decoded->VDOP;
                }
                if ((int)decoded->precision_bits) {
                    jsonObj["payload"]["precision_bits"] = (int)decoded->precision_bits;
                }
            } else if (shouldLog) {
                LOG_ERROR("Error decoding proto for position message!");
                return "";
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
                jsonObj["payload"]["id"] = (unsigned int)decoded->id;
                jsonObj["payload"]["name"] = decoded->name;
                jsonObj["payload"]["description"] = decoded->description;
                jsonObj["payload"]["expire"] = (unsigned int)decoded->expire;
                jsonObj["payload"]["locked_to"] = (unsigned int)decoded->locked_to;
                jsonObj["payload"]["latitude_i"] = (int)decoded->latitude_i;
                jsonObj["payload"]["longitude_i"] = (int)decoded->longitude_i;
            } else if (shouldLog) {
                LOG_ERROR("Error decoding proto for position message!");
                return "";
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
                jsonObj["payload"]["node_id"] = (unsigned int)decoded->node_id;
                jsonObj["payload"]["node_broadcast_interval_secs"] = (unsigned int)decoded->node_broadcast_interval_secs;
                jsonObj["payload"]["last_sent_by_id"] = (unsigned int)decoded->last_sent_by_id;
                jsonObj["payload"]["neighbors_count"] = decoded->neighbors_count;

                JsonObject neighbors_obj = arrayObj.to<JsonObject>();
                JsonArray neighbors = neighbors_obj.createNestedArray("neighbors");
                JsonObject neighbors_0 = neighbors.createNestedObject();

                for (uint8_t i = 0; i < decoded->neighbors_count; i++) {
                    neighbors_0["node_id"] = (unsigned int)decoded->neighbors[i].node_id;
                    neighbors_0["snr"] = (int)decoded->neighbors[i].snr;
                    neighbors[i + 1] = neighbors_0;
                    neighbors_0.clear();
                }
                neighbors.remove(0);
                jsonObj["payload"]["neighbors"] = neighbors;
            } else if (shouldLog) {
                LOG_ERROR("Error decoding proto for neighborinfo message!");
                return "";
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
                    JsonArray route = arrayObj.createNestedArray("route");

                    auto addToRoute = [](JsonArray *route, NodeNum num) {
                        char long_name[40] = "Unknown";
                        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
                        bool name_known = node ? node->has_user : false;
                        if (name_known)
                            memcpy(long_name, node->user.long_name, sizeof(long_name));
                        route->add(long_name);
                    };

                    addToRoute(&route, mp->to); // route.add(mp->to);
                    for (uint8_t i = 0; i < decoded->route_count; i++) {
                        addToRoute(&route, decoded->route[i]); // route.add(decoded->route[i]);
                    }
                    addToRoute(&route,
                               mp->from); // route.add(mp->from); // Ended at the original destination (source of response)

                    jsonObj["payload"]["route"] = route;
                } else if (shouldLog) {
                    LOG_ERROR("Error decoding proto for traceroute message!");
                    return "";
                }
            } else {
                LOG_WARN("Traceroute response not reported");
                return "";
            }
            break;
        }
        case meshtastic_PortNum_DETECTION_SENSOR_APP: {
            msgType = "detection";
            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0; // null terminated string
            jsonObj["payload"]["text"] = payloadStr;
            break;
        }
        case meshtastic_PortNum_REMOTE_HARDWARE_APP: {
            meshtastic_HardwareMessage scratch;
            meshtastic_HardwareMessage *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_HardwareMessage_msg,
                                     &scratch)) {
                decoded = &scratch;
                if (decoded->type == meshtastic_HardwareMessage_Type_GPIOS_CHANGED) {
                    msgType = "gpios_changed";
                    jsonObj["payload"]["gpio_value"] = (unsigned int)decoded->gpio_value;
                } else if (decoded->type == meshtastic_HardwareMessage_Type_READ_GPIOS_REPLY) {
                    msgType = "gpios_read_reply";
                    jsonObj["payload"]["gpio_value"] = (unsigned int)decoded->gpio_value;
                    jsonObj["payload"]["gpio_mask"] = (unsigned int)decoded->gpio_mask;
                }
            } else if (shouldLog) {
                LOG_ERROR("Error decoding proto for RemoteHardware message!");
                return "";
            }
            break;
        }
        // add more packet types here if needed
        default:
            LOG_WARN("Unsupported packet type %d", mp->decoded.portnum);
            return "";
            break;
        }
    } else if (shouldLog) {
        LOG_WARN("Couldn't convert encrypted payload of MeshPacket to JSON");
        return "";
    }

    jsonObj["id"] = (unsigned int)mp->id;
    jsonObj["timestamp"] = (unsigned int)mp->rx_time;
    jsonObj["to"] = (unsigned int)mp->to;
    jsonObj["from"] = (unsigned int)mp->from;
    jsonObj["channel"] = (unsigned int)mp->channel;
    jsonObj["type"] = msgType.c_str();
    jsonObj["sender"] = owner.id;
    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = (int)mp->rx_rssi;
    if (mp->rx_snr != 0)
        jsonObj["snr"] = (float)mp->rx_snr;
    if (mp->hop_start != 0 && mp->hop_limit <= mp->hop_start) {
        jsonObj["hops_away"] = (unsigned int)(mp->hop_start - mp->hop_limit);
        jsonObj["hop_start"] = (unsigned int)(mp->hop_start);
    }

    // serialize and write it to the stream

    // Serial.printf("serialized json message: \r");
    // serializeJson(jsonObj, Serial);
    // Serial.println("");

    std::string jsonStr = "";
    serializeJson(jsonObj, jsonStr);

    if (shouldLog)
        LOG_INFO("serialized json message: %s", jsonStr.c_str());

    return jsonStr;
}

std::string MeshPacketSerializer::JsonSerializeEncrypted(const meshtastic_MeshPacket *mp)
{
    jsonObj.clear();
    jsonObj["id"] = (unsigned int)mp->id;
    jsonObj["time_ms"] = (double)millis();
    jsonObj["timestamp"] = (unsigned int)mp->rx_time;
    jsonObj["to"] = (unsigned int)mp->to;
    jsonObj["from"] = (unsigned int)mp->from;
    jsonObj["channel"] = (unsigned int)mp->channel;
    jsonObj["want_ack"] = mp->want_ack;

    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = (int)mp->rx_rssi;
    if (mp->rx_snr != 0)
        jsonObj["snr"] = (float)mp->rx_snr;
    if (mp->hop_start != 0 && mp->hop_limit <= mp->hop_start) {
        jsonObj["hops_away"] = (unsigned int)(mp->hop_start - mp->hop_limit);
        jsonObj["hop_start"] = (unsigned int)(mp->hop_start);
    }
    jsonObj["size"] = (unsigned int)mp->encrypted.size;
    auto encryptedStr = bytesToHex(mp->encrypted.bytes, mp->encrypted.size);
    jsonObj["bytes"] = encryptedStr.c_str();

    // serialize and write it to the stream
    std::string jsonStr = "";
    serializeJson(jsonObj, jsonStr);

    return jsonStr;
}
#endif