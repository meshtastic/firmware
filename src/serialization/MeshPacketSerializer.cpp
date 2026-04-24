#if ARCH_PORTDUINO
#include "MeshPacketSerializer.h"
#include "NodeDB.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "modules/RoutingModule.h"
#include <DebugConfiguration.h>
#include <json/json.h>
#include <memory>
#include <mesh-pb-constants.h>
#if defined(ARCH_ESP32)
#include "../mesh/generated/meshtastic/paxcount.pb.h"
#endif
#include "mesh/generated/meshtastic/remote_hardware.pb.h"
#include <sys/types.h>

static const char *errStr = "Error decoding proto for %s message!";

static std::string writeCompact(const Json::Value &v)
{
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    b["emitUTF8"] = true;
    return Json::writeString(b, v);
}

static bool tryParseJson(const char *s, Json::Value &out)
{
    Json::CharReaderBuilder b;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    std::string errs;
    const char *end = s + strlen(s);
    return reader->parse(s, end, &out, &errs);
}

std::string MeshPacketSerializer::JsonSerialize(const meshtastic_MeshPacket *mp, bool shouldLog)
{
    std::string msgType;
    Json::Value jsonObj(Json::objectValue);

    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        Json::Value msgPayload(Json::objectValue);
        switch (mp->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP: {
            msgType = "text";
            if (shouldLog)
                LOG_DEBUG("got text message of size %u", mp->decoded.payload.size);

            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0;
            Json::Value parsed;
            if (tryParseJson(payloadStr, parsed)) {
                if (shouldLog)
                    LOG_INFO("text message payload is of type json");
                jsonObj["payload"] = parsed;
            } else {
                if (shouldLog)
                    LOG_INFO("text message payload is of type plaintext");
                msgPayload["text"] = payloadStr;
                jsonObj["payload"] = msgPayload;
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
                    if (decoded->variant.device_metrics.has_battery_level) {
                        msgPayload["battery_level"] = (int)decoded->variant.device_metrics.battery_level;
                    }
                    msgPayload["voltage"] = decoded->variant.device_metrics.voltage;
                    msgPayload["channel_utilization"] = decoded->variant.device_metrics.channel_utilization;
                    msgPayload["air_util_tx"] = decoded->variant.device_metrics.air_util_tx;
                    msgPayload["uptime_seconds"] = (Json::UInt)decoded->variant.device_metrics.uptime_seconds;
                } else if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
                    if (decoded->variant.environment_metrics.has_temperature) {
                        msgPayload["temperature"] = decoded->variant.environment_metrics.temperature;
                    }
                    if (decoded->variant.environment_metrics.has_relative_humidity) {
                        msgPayload["relative_humidity"] = decoded->variant.environment_metrics.relative_humidity;
                    }
                    if (decoded->variant.environment_metrics.has_barometric_pressure) {
                        msgPayload["barometric_pressure"] = decoded->variant.environment_metrics.barometric_pressure;
                    }
                    if (decoded->variant.environment_metrics.has_gas_resistance) {
                        msgPayload["gas_resistance"] = decoded->variant.environment_metrics.gas_resistance;
                    }
                    if (decoded->variant.environment_metrics.has_voltage) {
                        msgPayload["voltage"] = decoded->variant.environment_metrics.voltage;
                    }
                    if (decoded->variant.environment_metrics.has_current) {
                        msgPayload["current"] = decoded->variant.environment_metrics.current;
                    }
                    if (decoded->variant.environment_metrics.has_lux) {
                        msgPayload["lux"] = decoded->variant.environment_metrics.lux;
                    }
                    if (decoded->variant.environment_metrics.has_white_lux) {
                        msgPayload["white_lux"] = decoded->variant.environment_metrics.white_lux;
                    }
                    if (decoded->variant.environment_metrics.has_iaq) {
                        msgPayload["iaq"] = (Json::UInt)decoded->variant.environment_metrics.iaq;
                    }
                    if (decoded->variant.environment_metrics.has_distance) {
                        msgPayload["distance"] = decoded->variant.environment_metrics.distance;
                    }
                    if (decoded->variant.environment_metrics.has_wind_speed) {
                        msgPayload["wind_speed"] = decoded->variant.environment_metrics.wind_speed;
                    }
                    if (decoded->variant.environment_metrics.has_wind_direction) {
                        msgPayload["wind_direction"] = (Json::UInt)decoded->variant.environment_metrics.wind_direction;
                    }
                    if (decoded->variant.environment_metrics.has_wind_gust) {
                        msgPayload["wind_gust"] = decoded->variant.environment_metrics.wind_gust;
                    }
                    if (decoded->variant.environment_metrics.has_wind_lull) {
                        msgPayload["wind_lull"] = decoded->variant.environment_metrics.wind_lull;
                    }
                    if (decoded->variant.environment_metrics.has_radiation) {
                        msgPayload["radiation"] = decoded->variant.environment_metrics.radiation;
                    }
                    if (decoded->variant.environment_metrics.has_ir_lux) {
                        msgPayload["ir_lux"] = decoded->variant.environment_metrics.ir_lux;
                    }
                    if (decoded->variant.environment_metrics.has_uv_lux) {
                        msgPayload["uv_lux"] = decoded->variant.environment_metrics.uv_lux;
                    }
                    if (decoded->variant.environment_metrics.has_weight) {
                        msgPayload["weight"] = decoded->variant.environment_metrics.weight;
                    }
                    if (decoded->variant.environment_metrics.has_rainfall_1h) {
                        msgPayload["rainfall_1h"] = decoded->variant.environment_metrics.rainfall_1h;
                    }
                    if (decoded->variant.environment_metrics.has_rainfall_24h) {
                        msgPayload["rainfall_24h"] = decoded->variant.environment_metrics.rainfall_24h;
                    }
                    if (decoded->variant.environment_metrics.has_soil_moisture) {
                        msgPayload["soil_moisture"] = (Json::UInt)decoded->variant.environment_metrics.soil_moisture;
                    }
                    if (decoded->variant.environment_metrics.has_soil_temperature) {
                        msgPayload["soil_temperature"] = decoded->variant.environment_metrics.soil_temperature;
                    }
                } else if (decoded->which_variant == meshtastic_Telemetry_air_quality_metrics_tag) {
                    if (decoded->variant.air_quality_metrics.has_pm10_standard) {
                        msgPayload["pm10"] = (Json::UInt)decoded->variant.air_quality_metrics.pm10_standard;
                    }
                    if (decoded->variant.air_quality_metrics.has_pm25_standard) {
                        msgPayload["pm25"] = (Json::UInt)decoded->variant.air_quality_metrics.pm25_standard;
                    }
                    if (decoded->variant.air_quality_metrics.has_pm100_standard) {
                        msgPayload["pm100"] = (Json::UInt)decoded->variant.air_quality_metrics.pm100_standard;
                    }
                    if (decoded->variant.air_quality_metrics.has_co2) {
                        msgPayload["co2"] = (Json::UInt)decoded->variant.air_quality_metrics.co2;
                    }
                    if (decoded->variant.air_quality_metrics.has_co2_temperature) {
                        msgPayload["co2_temperature"] = decoded->variant.air_quality_metrics.co2_temperature;
                    }
                    if (decoded->variant.air_quality_metrics.has_co2_humidity) {
                        msgPayload["co2_humidity"] = decoded->variant.air_quality_metrics.co2_humidity;
                    }
                    if (decoded->variant.air_quality_metrics.has_form_formaldehyde) {
                        msgPayload["form_formaldehyde"] = decoded->variant.air_quality_metrics.form_formaldehyde;
                    }
                    if (decoded->variant.air_quality_metrics.has_form_temperature) {
                        msgPayload["form_temperature"] = decoded->variant.air_quality_metrics.form_temperature;
                    }
                    if (decoded->variant.air_quality_metrics.has_form_humidity) {
                        msgPayload["form_humidity"] = decoded->variant.air_quality_metrics.form_humidity;
                    }
                } else if (decoded->which_variant == meshtastic_Telemetry_power_metrics_tag) {
                    if (decoded->variant.power_metrics.has_ch1_voltage) {
                        msgPayload["voltage_ch1"] = decoded->variant.power_metrics.ch1_voltage;
                    }
                    if (decoded->variant.power_metrics.has_ch1_current) {
                        msgPayload["current_ch1"] = decoded->variant.power_metrics.ch1_current;
                    }
                    if (decoded->variant.power_metrics.has_ch2_voltage) {
                        msgPayload["voltage_ch2"] = decoded->variant.power_metrics.ch2_voltage;
                    }
                    if (decoded->variant.power_metrics.has_ch2_current) {
                        msgPayload["current_ch2"] = decoded->variant.power_metrics.ch2_current;
                    }
                    if (decoded->variant.power_metrics.has_ch3_voltage) {
                        msgPayload["voltage_ch3"] = decoded->variant.power_metrics.ch3_voltage;
                    }
                    if (decoded->variant.power_metrics.has_ch3_current) {
                        msgPayload["current_ch3"] = decoded->variant.power_metrics.ch3_current;
                    }
                }
                jsonObj["payload"] = msgPayload;
            } else if (shouldLog) {
                LOG_ERROR(errStr, msgType.c_str());
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
                msgPayload["id"] = decoded->id;
                msgPayload["longname"] = decoded->long_name;
                msgPayload["shortname"] = decoded->short_name;
                msgPayload["hardware"] = (int)decoded->hw_model;
                msgPayload["role"] = (int)decoded->role;
                jsonObj["payload"] = msgPayload;
            } else if (shouldLog) {
                LOG_ERROR(errStr, msgType.c_str());
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
                    msgPayload["time"] = (Json::UInt)decoded->time;
                }
                if ((int)decoded->timestamp) {
                    msgPayload["timestamp"] = (Json::UInt)decoded->timestamp;
                }
                msgPayload["latitude_i"] = (int)decoded->latitude_i;
                msgPayload["longitude_i"] = (int)decoded->longitude_i;
                if ((int)decoded->altitude) {
                    msgPayload["altitude"] = (int)decoded->altitude;
                }
                if ((int)decoded->ground_speed) {
                    msgPayload["ground_speed"] = (Json::UInt)decoded->ground_speed;
                }
                if (int(decoded->ground_track)) {
                    msgPayload["ground_track"] = (Json::UInt)decoded->ground_track;
                }
                if (int(decoded->sats_in_view)) {
                    msgPayload["sats_in_view"] = (Json::UInt)decoded->sats_in_view;
                }
                if ((int)decoded->PDOP) {
                    msgPayload["PDOP"] = (int)decoded->PDOP;
                }
                if ((int)decoded->HDOP) {
                    msgPayload["HDOP"] = (int)decoded->HDOP;
                }
                if ((int)decoded->VDOP) {
                    msgPayload["VDOP"] = (int)decoded->VDOP;
                }
                if ((int)decoded->precision_bits) {
                    msgPayload["precision_bits"] = (int)decoded->precision_bits;
                }
                jsonObj["payload"] = msgPayload;
            } else if (shouldLog) {
                LOG_ERROR(errStr, msgType.c_str());
            }
            break;
        }
        case meshtastic_PortNum_WAYPOINT_APP: {
            msgType = "waypoint";
            meshtastic_Waypoint scratch;
            meshtastic_Waypoint *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Waypoint_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["id"] = (Json::UInt)decoded->id;
                msgPayload["name"] = decoded->name;
                msgPayload["description"] = decoded->description;
                msgPayload["expire"] = (Json::UInt)decoded->expire;
                msgPayload["locked_to"] = (Json::UInt)decoded->locked_to;
                msgPayload["latitude_i"] = (int)decoded->latitude_i;
                msgPayload["longitude_i"] = (int)decoded->longitude_i;
                jsonObj["payload"] = msgPayload;
            } else if (shouldLog) {
                LOG_ERROR(errStr, msgType.c_str());
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
                msgPayload["node_id"] = (Json::UInt)decoded->node_id;
                msgPayload["node_broadcast_interval_secs"] = (Json::UInt)decoded->node_broadcast_interval_secs;
                msgPayload["last_sent_by_id"] = (Json::UInt)decoded->last_sent_by_id;
                msgPayload["neighbors_count"] = (Json::UInt)decoded->neighbors_count;
                Json::Value neighbors(Json::arrayValue);
                for (uint8_t i = 0; i < decoded->neighbors_count; i++) {
                    Json::Value neighborObj(Json::objectValue);
                    neighborObj["node_id"] = (Json::UInt)decoded->neighbors[i].node_id;
                    neighborObj["snr"] = (int)decoded->neighbors[i].snr;
                    neighbors.append(neighborObj);
                }
                msgPayload["neighbors"] = neighbors;
                jsonObj["payload"] = msgPayload;
            } else if (shouldLog) {
                LOG_ERROR(errStr, msgType.c_str());
            }
            break;
        }
        case meshtastic_PortNum_TRACEROUTE_APP: {
            if (mp->decoded.request_id) {
                msgType = "traceroute";
                meshtastic_RouteDiscovery scratch;
                meshtastic_RouteDiscovery *decoded = NULL;
                memset(&scratch, 0, sizeof(scratch));
                if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_RouteDiscovery_msg,
                                         &scratch)) {
                    decoded = &scratch;
                    Json::Value route(Json::arrayValue);
                    Json::Value routeBack(Json::arrayValue);
                    Json::Value snrTowards(Json::arrayValue);
                    Json::Value snrBack(Json::arrayValue);

                    auto addToRoute = [](Json::Value *r, NodeNum num) {
                        char long_name[40] = "Unknown";
                        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
                        bool name_known = node ? node->has_user : false;
                        if (name_known)
                            memcpy(long_name, node->user.long_name, sizeof(long_name));
                        r->append(long_name);
                    };
                    addToRoute(&route, mp->to);
                    for (uint8_t i = 0; i < decoded->route_count; i++) {
                        addToRoute(&route, decoded->route[i]);
                    }
                    addToRoute(&route, mp->from);

                    addToRoute(&routeBack, mp->from);
                    for (uint8_t i = 0; i < decoded->route_back_count; i++) {
                        addToRoute(&routeBack, decoded->route_back[i]);
                    }
                    addToRoute(&routeBack, mp->to);

                    for (uint8_t i = 0; i < decoded->snr_back_count; i++) {
                        snrBack.append((float)decoded->snr_back[i] / 4);
                    }
                    for (uint8_t i = 0; i < decoded->snr_towards_count; i++) {
                        snrTowards.append((float)decoded->snr_towards[i] / 4);
                    }

                    msgPayload["route"] = route;
                    msgPayload["route_back"] = routeBack;
                    msgPayload["snr_back"] = snrBack;
                    msgPayload["snr_towards"] = snrTowards;
                    jsonObj["payload"] = msgPayload;
                } else if (shouldLog) {
                    LOG_ERROR(errStr, msgType.c_str());
                }
            }
            break;
        }
        case meshtastic_PortNum_DETECTION_SENSOR_APP: {
            msgType = "detection";
            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0;
            msgPayload["text"] = payloadStr;
            jsonObj["payload"] = msgPayload;
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
                msgPayload["wifi_count"] = (Json::UInt)decoded->wifi;
                msgPayload["ble_count"] = (Json::UInt)decoded->ble;
                msgPayload["uptime"] = (Json::UInt)decoded->uptime;
                jsonObj["payload"] = msgPayload;
            } else if (shouldLog) {
                LOG_ERROR(errStr, msgType.c_str());
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
                    msgPayload["gpio_value"] = (Json::UInt)decoded->gpio_value;
                    jsonObj["payload"] = msgPayload;
                } else if (decoded->type == meshtastic_HardwareMessage_Type_READ_GPIOS_REPLY) {
                    msgType = "gpios_read_reply";
                    msgPayload["gpio_value"] = (Json::UInt)decoded->gpio_value;
                    msgPayload["gpio_mask"] = (Json::UInt)decoded->gpio_mask;
                    jsonObj["payload"] = msgPayload;
                }
            } else if (shouldLog) {
                LOG_ERROR(errStr, "RemoteHardware");
            }
            break;
        }
        default:
            break;
        }
    } else if (shouldLog) {
        LOG_WARN("Couldn't convert encrypted payload of MeshPacket to JSON");
    }

    jsonObj["id"] = (Json::UInt)mp->id;
    jsonObj["timestamp"] = (Json::UInt)mp->rx_time;
    jsonObj["to"] = (Json::UInt)mp->to;
    jsonObj["from"] = (Json::UInt)mp->from;
    jsonObj["channel"] = (Json::UInt)mp->channel;
    jsonObj["type"] = msgType;
    jsonObj["sender"] = nodeDB->getNodeId();
    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = (int)mp->rx_rssi;
    if (mp->rx_snr != 0)
        jsonObj["snr"] = (float)mp->rx_snr;
    const int8_t hopsAway = getHopsAway(*mp);
    if (hopsAway >= 0) {
        jsonObj["hops_away"] = (Json::UInt)(hopsAway);
        jsonObj["hop_start"] = (Json::UInt)(mp->hop_start);
    }

    std::string jsonStr = writeCompact(jsonObj);

    if (shouldLog)
        LOG_INFO("serialized json message: %s", jsonStr.c_str());

    return jsonStr;
}

std::string MeshPacketSerializer::JsonSerializeEncrypted(const meshtastic_MeshPacket *mp)
{
    Json::Value jsonObj(Json::objectValue);

    jsonObj["id"] = (Json::UInt)mp->id;
    jsonObj["time_ms"] = (double)millis();
    jsonObj["timestamp"] = (Json::UInt)mp->rx_time;
    jsonObj["to"] = (Json::UInt)mp->to;
    jsonObj["from"] = (Json::UInt)mp->from;
    jsonObj["channel"] = (Json::UInt)mp->channel;
    jsonObj["want_ack"] = mp->want_ack;

    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = (int)mp->rx_rssi;
    if (mp->rx_snr != 0)
        jsonObj["snr"] = (float)mp->rx_snr;
    const int8_t hopsAway = getHopsAway(*mp);
    if (hopsAway >= 0) {
        jsonObj["hops_away"] = (Json::UInt)(hopsAway);
        jsonObj["hop_start"] = (Json::UInt)(mp->hop_start);
    }
    jsonObj["size"] = (Json::UInt)mp->encrypted.size;
    auto encryptedStr = bytesToHex(mp->encrypted.bytes, mp->encrypted.size);
    jsonObj["bytes"] = encryptedStr;

    return writeCompact(jsonObj);
}
#endif
