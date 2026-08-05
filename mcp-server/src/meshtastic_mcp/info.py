"""Read-only device queries via meshtastic.SerialInterface."""

from __future__ import annotations

from typing import Any

from .connection import connect


def _primary_channel_name(iface) -> str | None:
    try:
        channels = iface.localNode.channels or []
    except AttributeError:
        return None
    for ch in channels:
        role = getattr(ch, "role", None)
        # Role enum: 0 DISABLED, 1 PRIMARY, 2 SECONDARY
        if role == 1:
            name = getattr(getattr(ch, "settings", None), "name", None)
            return name or "(default)"
    return None


def device_info(port: str | None = None, timeout_s: float = 8.0) -> dict[str, Any]:
    """Return summary info for the connected device."""
    with connect(port=port, timeout_s=timeout_s) as iface:
        my = iface.myInfo
        meta = iface.metadata
        local = iface.localNode

        # Owner (long/short name) is on the local node's user record
        long_name: str | None = None
        short_name: str | None = None
        hw_model: str | int | None = None
        if iface.nodesByNum and my is not None:
            local_rec = iface.nodesByNum.get(my.my_node_num, {})
            user = local_rec.get("user") or {}
            long_name = user.get("longName")
            short_name = user.get("shortName")
            hw_model = user.get("hwModel")

        region = None
        if local is not None and local.localConfig is not None:
            try:
                lora = local.localConfig.lora
                # region is an enum; get its string name
                region = (
                    lora.DESCRIPTOR.fields_by_name["region"]
                    .enum_type.values_by_number[lora.region]
                    .name
                )
            except Exception:
                region = None

        return {
            "port": iface.devPath if hasattr(iface, "devPath") else port,
            "my_node_num": getattr(my, "my_node_num", None),
            "long_name": long_name,
            "short_name": short_name,
            "firmware_version": getattr(meta, "firmware_version", None),
            "hw_model": hw_model,
            "region": region,
            "num_nodes": len(iface.nodesByNum) if iface.nodesByNum else 0,
            "primary_channel": _primary_channel_name(iface),
        }


def _node_record(node_dict: dict[str, Any]) -> dict[str, Any]:
    user = node_dict.get("user") or {}
    position = node_dict.get("position") or None
    device_metrics = node_dict.get("deviceMetrics") or {}
    return {
        "node_num": node_dict.get("num"),
        "user": {
            "long_name": user.get("longName"),
            "short_name": user.get("shortName"),
            "hw_model": user.get("hwModel"),
            "role": user.get("role"),
        },
        "position": (
            {
                "latitude": position.get("latitude"),
                "longitude": position.get("longitude"),
                "altitude": position.get("altitude"),
                "time": position.get("time"),
            }
            if position
            else None
        ),
        "snr": node_dict.get("snr"),
        "rssi": node_dict.get("rssi"),
        "last_heard": node_dict.get("lastHeard"),
        "battery_level": device_metrics.get("batteryLevel"),
        "is_favorite": bool(node_dict.get("isFavorite", False)),
    }


def list_nodes(port: str | None = None, timeout_s: float = 8.0) -> list[dict[str, Any]]:
    """Return the device's node database."""
    with connect(port=port, timeout_s=timeout_s) as iface:
        if not iface.nodesByNum:
            return []
        return [_node_record(n) for n in iface.nodesByNum.values()]
