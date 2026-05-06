# WiFi Node List Report Module

`WifiNodeListReportModule` is an on-device firmware module that periodically sends the local NodeDB to an HTTP endpoint over
WiFi. It is disabled by default and only runs on firmware builds with WiFi support.

The module is intended for gateway-style nodes that should avoid spending LoRa airtime on node inventory reports. It reuses
the local NodeDB data already known to the device and does not require a host computer at runtime.

## Power Gate

Before each report, the module checks whether WiFi use is allowed:

- USB power is connected, or
- the device is charging, or
- battery charge is at or above `battery_threshold_percent`

If the gate is closed, the module skips the report and tries again on the next interval. When firmware WiFi is not otherwise
enabled, the module starts station mode for the report, sends the HTTP POST, then disconnects and turns WiFi off again.

## Payload

Reports are JSON documents posted to the configured URL:

- `type`: `full_snapshot` or `diff`
- `version`: payload version
- `sequence`: module-local report sequence
- `from`: sender node number
- `known_node_count`: current NodeDB count
- `records`: compact node records

Full snapshots include all known nodes. Incremental reports include only nodes whose cached summary changed since the previous
successful report. Records include node id, flags, last-heard age bucket, hops-away, and SNR bucket. Full snapshots and newly
seen node diffs also include short and long names when present. Position is represented only as a coarse hash when
`include_position` is enabled and position already exists in NodeDB.

## Configuration

The module is configured through `ModuleConfig.WifiNodeListReportConfig`:

- `enabled`: default `false`
- `url`: HTTP or HTTPS POST target URL
- `interval_seconds`: incremental report interval, default `3600`, minimum `900`
- `full_snapshot_interval_seconds`: full snapshot interval, default `86400`, minimum `21600`
- `battery_threshold_percent`: minimum battery level when not on USB/charging, default `50`
- `connect_timeout_seconds`: WiFi association timeout, default `30`
- `include_position`: include coarse position hashes, default `false`
- `min_changed_nodes_before_send`: default `1`

The normal network WiFi credentials must also be configured:

- `network.wifi_ssid`
- `network.wifi_psk`

Changing module enablement requires the usual firmware config reboot path so the module instance is created during startup.

Example CLI configuration after rebuilding the CLI from these protobufs:

```bash
meshtastic --set network.wifi_ssid "ssid"
meshtastic --set network.wifi_psk "password"
meshtastic --set wifi_node_list_report.enabled true
meshtastic --set wifi_node_list_report.url "https://example.com/meshtastic/node-list"
meshtastic --set wifi_node_list_report.battery_threshold_percent 60
```

## Operational Notes

This module avoids LoRa airtime for the reports themselves, but WiFi increases power draw while active. Keep the default hourly
diff interval and daily full snapshot interval unless the receiving system needs tighter freshness. On battery-only nodes, set
`battery_threshold_percent` conservatively.

The current implementation builds one JSON document in memory per POST. This is appropriate for moderate NodeDB sizes on ESP32
targets, but very large NodeDBs may need future HTTP chunking or streaming.

## Docker Build

The repo-local Docker wrapper keeps PlatformIO state under `.docker-cache/`:

```bash
tools/docker/pio-run.sh run -e heltec-v3
```
