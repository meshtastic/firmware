# Node List Report Module

`NodeListReportModule` is an on-device firmware module that sends compact summaries of the local NodeDB to one configured
destination node. It runs without a host computer at runtime and is disabled by default.

Reports are direct packets on `PRIVATE_APP`. When the destination public key is known and the sender is not in Ham mode, the
normal router send path uses PKI encryption for the direct packet. If the destination node or its public key is not known, the
module defers instead of sending a clear broadcast-style report.

## Payload

The wire payload is a compact binary format, not JSON. Current packets use the `NLR2` format:

- 16 byte header: magic/version, flags, sequence, unique report id, chunk index, known NodeDB count
- variable length records: node number, record flags, last-heard age bucket, hops-away, SNR bucket, optional coarse position
  hash, optional capped short and long names

Only changed records are sent during incremental reports. Full snapshots walk the entire NodeDB and are chunked across multiple
direct packets using the report id and chunk index. Full snapshot records and newly seen node diff records include capped short
and long names when present. Existing-node diffs for last-heard, hops-away, SNR, or position changes omit names to keep routine
updates small.

## Configuration

The module is configured through `ModuleConfig.NodeListReportConfig`:

- `enabled`: default `false`
- `destination_node`: node number to receive direct reports
- `interval_seconds`: incremental report interval, default `3600`, minimum `900`
- `full_snapshot_interval_seconds`: full snapshot interval, default `86400`, minimum `21600`
- `max_nodes_per_report`: maximum records per packet/chunk, default `10`, firmware hard cap `14`
- `include_position`: include a coarse position hash when position is already present, default `false`
- `include_user_info`: include a short user id/name hash when user info is already present, default `false`
- `min_changed_nodes_before_send`: default `1`

Changing module enablement requires the usual firmware config reboot path so the module instance is created during startup.

## Manual Trigger

Clients can request an immediate report with `AdminMessage.send_node_list_report`.

- `true`: send a full snapshot now
- `false`: send an incremental diff now

The trigger still uses the module's safety checks: the module must be enabled, a non-broadcast destination must be configured,
the destination public key must be known, Ham mode must be off, and airtime/channel-utilization limits must allow a transmit.
If any check fails, firmware returns a bad-request routing response instead of forcing a packet onto the mesh. A full snapshot
trigger starts a chunked snapshot; subsequent chunks are sent about every two minutes, with jitter, until the whole NodeDB has
been covered.

## RF Congestion

Use this only for a specific operational need. Even compact NodeDB summaries consume LoRa airtime. Keep the default one-hour
incremental interval or increase it on busy meshes, keep `max_nodes_per_report` small, and avoid enabling full snapshots more
often than the default daily cadence.

## Docker Build

The repo-local Docker wrapper keeps PlatformIO state under `.docker-cache/`:

```bash
tools/docker/pio-run.sh run -e heltec-v3
```
