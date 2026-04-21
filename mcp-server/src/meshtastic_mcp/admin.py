"""Device administration: owner, config, channels, messaging, admin actions.

All operations use the same `connect()` context manager so port selection,
port-busy detection, and cleanup are handled uniformly.

Config writes use a dot-path: the first segment names a section (e.g.
`"lora"` in LocalConfig or `"mqtt"` in LocalModuleConfig), remaining segments
walk protobuf fields. Enum fields accept their string names (`"US"` for
`lora.region`) so callers don't need to know the numeric values.
"""

from __future__ import annotations

from typing import Any

from google.protobuf import descriptor as pb_descriptor
from google.protobuf import json_format
from meshtastic.protobuf import localonly_pb2

from .connection import connect


class AdminError(RuntimeError):
    pass


LOCAL_CONFIG_SECTIONS = {f.name for f in localonly_pb2.LocalConfig.DESCRIPTOR.fields}
MODULE_CONFIG_SECTIONS = {
    f.name for f in localonly_pb2.LocalModuleConfig.DESCRIPTOR.fields
}


def _require_confirm(confirm: bool, operation: str) -> None:
    if not confirm:
        raise AdminError(f"{operation} is destructive and requires confirm=True.")


def _message_to_dict(msg: Any) -> dict[str, Any]:
    # `including_default_value_fields` was renamed to
    # `always_print_fields_with_no_presence` in protobuf 5.26+. Pick whichever
    # kwarg the installed version accepts so we work against both.
    kwargs: dict[str, Any] = {"preserving_proto_field_name": True}
    import inspect

    sig = inspect.signature(json_format.MessageToDict)
    if "always_print_fields_with_no_presence" in sig.parameters:
        kwargs["always_print_fields_with_no_presence"] = False
    elif "including_default_value_fields" in sig.parameters:
        kwargs["including_default_value_fields"] = False
    return json_format.MessageToDict(msg, **kwargs)


# ---------- owner ----------------------------------------------------------


def set_owner(
    long_name: str,
    short_name: str | None = None,
    port: str | None = None,
) -> dict[str, Any]:
    if short_name is not None and len(short_name) > 4:
        raise AdminError("short_name must be 4 characters or fewer")
    with connect(port=port) as iface:
        iface.localNode.setOwner(long_name=long_name, short_name=short_name)
    return {
        "ok": True,
        "long_name": long_name,
        "short_name": short_name,
    }


# ---------- config reads ---------------------------------------------------


def _section_container(node, section: str) -> tuple[Any, str]:
    """Return (container_message, parent_name) for a section name.

    Parent is 'localConfig' or 'moduleConfig' so callers know where to call
    writeConfig() after mutating.
    """
    if section in LOCAL_CONFIG_SECTIONS:
        return getattr(node.localConfig, section), "localConfig"
    if section in MODULE_CONFIG_SECTIONS:
        return getattr(node.moduleConfig, section), "moduleConfig"
    raise AdminError(
        f"Unknown config section: {section!r}. "
        f"Valid sections: {sorted(LOCAL_CONFIG_SECTIONS | MODULE_CONFIG_SECTIONS)}"
    )


def get_config(section: str | None = None, port: str | None = None) -> dict[str, Any]:
    """Read one or all config sections.

    `section` may be any name in LocalConfig (device, lora, position, power,
    network, display, bluetooth, security) or LocalModuleConfig (mqtt, serial,
    telemetry, ...). Omit `section` or pass `"all"` for everything.
    """
    with connect(port=port) as iface:
        node = iface.localNode
        if section in (None, "all"):
            lc = _message_to_dict(node.localConfig)
            mc = _message_to_dict(node.moduleConfig)
            return {
                "config": {
                    "localConfig": lc,
                    "moduleConfig": mc,
                }
            }
        container, _parent = _section_container(node, section)
        return {"config": {section: _message_to_dict(container)}}


# ---------- config writes --------------------------------------------------


def _coerce_enum(field: pb_descriptor.FieldDescriptor, value: Any) -> int:
    """Accept an enum value as either its int or its string name."""
    enum_type = field.enum_type
    if isinstance(value, bool):
        raise AdminError(f"{field.name}: expected enum {enum_type.name}, got bool")
    if isinstance(value, int):
        if enum_type.values_by_number.get(value) is None:
            raise AdminError(
                f"{field.name}: {value} is not a valid {enum_type.name} value"
            )
        return value
    if isinstance(value, str):
        upper = value.upper()
        ev = enum_type.values_by_name.get(upper)
        if ev is None:
            valid = sorted(enum_type.values_by_name.keys())
            raise AdminError(
                f"{field.name}: {value!r} is not a valid {enum_type.name}. "
                f"Valid: {valid}"
            )
        return ev.number
    raise AdminError(
        f"{field.name}: expected enum {enum_type.name}, got {type(value).__name__}"
    )


def _coerce_scalar(field: pb_descriptor.FieldDescriptor, value: Any) -> Any:
    t = field.type
    FT = pb_descriptor.FieldDescriptor
    if t == FT.TYPE_ENUM:
        return _coerce_enum(field, value)
    if t == FT.TYPE_BOOL:
        if isinstance(value, bool):
            return value
        if isinstance(value, str):
            return value.strip().lower() in ("true", "yes", "1", "on")
        if isinstance(value, int):
            return bool(value)
    if t in (
        FT.TYPE_INT32,
        FT.TYPE_INT64,
        FT.TYPE_UINT32,
        FT.TYPE_UINT64,
        FT.TYPE_SINT32,
        FT.TYPE_SINT64,
        FT.TYPE_FIXED32,
        FT.TYPE_FIXED64,
    ):
        return int(value)
    if t in (FT.TYPE_FLOAT, FT.TYPE_DOUBLE):
        return float(value)
    if t == FT.TYPE_STRING:
        return str(value)
    if t == FT.TYPE_BYTES:
        if isinstance(value, (bytes, bytearray)):
            return bytes(value)
        return str(value).encode("utf-8")
    raise AdminError(
        f"{field.name}: unsupported field type {t}. Use raw protobuf for this field."
    )


def _walk_to_field(
    root_msg: Any, path_segments: list[str]
) -> tuple[Any, pb_descriptor.FieldDescriptor]:
    """Walk `root_msg` by field names until the leaf; return (parent_msg, leaf_field_descriptor)."""
    msg = root_msg
    for i, name in enumerate(path_segments):
        desc = msg.DESCRIPTOR
        field = desc.fields_by_name.get(name)
        if field is None:
            trail = ".".join(path_segments[:i] or ["<root>"])
            valid = [f.name for f in desc.fields]
            raise AdminError(f"No field {name!r} in {trail}. Valid: {valid}")
        is_last = i == len(path_segments) - 1
        if is_last:
            return msg, field
        if field.type != pb_descriptor.FieldDescriptor.TYPE_MESSAGE:
            raise AdminError(
                f"{'.'.join(path_segments[:i+1])} is a scalar; cannot descend into it"
            )
        msg = getattr(msg, name)
    # path_segments was empty
    raise AdminError("Empty config path")


def set_config(path: str, value: Any, port: str | None = None) -> dict[str, Any]:
    """Set a single config field by dot-path and write it to the device.

    Examples:
        set_config("lora.region", "US")
        set_config("lora.modem_preset", "LONG_FAST")
        set_config("device.role", "ROUTER")
        set_config("mqtt.enabled", True)
        set_config("mqtt.address", "mqtt.example.com")

    """
    segments = [s for s in path.split(".") if s]
    if not segments:
        raise AdminError("path cannot be empty")
    section = segments[0]

    with connect(port=port) as iface:
        node = iface.localNode
        container, parent_name = _section_container(node, section)

        # Treat the section as the root; the rest of the path walks into it.
        leaf_parent, field = _walk_to_field(container, segments[1:] or [])
        # Use `is_repeated` (modern upb protobuf API) rather than the
        # deprecated `label == LABEL_REPEATED` check — the C-extension
        # FieldDescriptor in protobuf >= 5.x doesn't expose `.label` at
        # all, and `is_repeated` is the supported replacement that works
        # across both the pure-python and upb backends.
        if field.is_repeated:
            raise AdminError(
                f"{path!r} is a repeated field; v1 only supports scalar sets. "
                "Use the raw meshtastic CLI for now."
            )
        old_raw = getattr(leaf_parent, field.name)
        coerced = _coerce_scalar(field, value)
        try:
            setattr(leaf_parent, field.name, coerced)
        except (TypeError, ValueError) as exc:
            raise AdminError(f"{path}: {exc}") from exc

        node.writeConfig(section)

        # Stringify enums for the response (so the caller can see the change in
        # the same vocabulary they used to set it).
        if field.type == pb_descriptor.FieldDescriptor.TYPE_ENUM:
            try:
                old_display = field.enum_type.values_by_number[old_raw].name
                new_display = field.enum_type.values_by_number[coerced].name
            except Exception:
                old_display, new_display = old_raw, coerced
        else:
            old_display, new_display = old_raw, coerced

    return {
        "ok": True,
        "path": path,
        "section": section,
        "parent": parent_name,
        "old_value": old_display,
        "new_value": new_display,
    }


# ---------- channels -------------------------------------------------------


def get_channel_url(
    include_all: bool = False, port: str | None = None
) -> dict[str, Any]:
    with connect(port=port) as iface:
        url = iface.localNode.getURL(includeAll=include_all)
    return {"url": url}


def set_channel_url(url: str, port: str | None = None) -> dict[str, Any]:
    with connect(port=port) as iface:
        # setURL replaces the channel set from the URL's contents. It does not
        # return a count; we infer by counting non-DISABLED channels after.
        iface.localNode.setURL(url)
        channels = iface.localNode.channels or []
        active = sum(1 for c in channels if getattr(c, "role", 0) != 0)
    return {"ok": True, "channels_imported": active}


# ---------- messaging ------------------------------------------------------


def send_text(
    text: str,
    to: str | int | None = None,
    channel_index: int = 0,
    want_ack: bool = False,
    port: str | None = None,
) -> dict[str, Any]:
    destination = to if to is not None else "^all"
    with connect(port=port) as iface:
        packet = iface.sendText(
            text,
            destinationId=destination,
            wantAck=want_ack,
            channelIndex=channel_index,
        )
        packet_id = getattr(packet, "id", None)
    return {"ok": True, "packet_id": packet_id, "destination": destination}


# ---------- diagnostics ----------------------------------------------------


def set_debug_log_api(enabled: bool, port: str | None = None) -> dict[str, Any]:
    """Toggle `config.security.debug_log_api_enabled` on the local node.

    When enabled, firmware emits log lines as protobuf `LogRecord` messages
    over the StreamAPI instead of raw text. meshtastic-python surfaces them
    on pubsub topic `meshtastic.log.line`, which flows through the SAME
    SerialInterface our tests already hold open — no `pio device monitor`
    needed, no port-contention with admin/info calls.

    Firmware gate: `src/SerialConsole.cpp` (`usingProtobufs &&
    config.security.debug_log_api_enabled`). Setting persists in NVS; it
    survives reboot. `factory_reset(full=False)` clears it unless it's
    re-applied after reset.

    Previously-documented concurrency hazard (emitLogRecord sharing the
    main packet-emission buffers) has been fixed — see `StreamAPI.h`
    where the log path now owns dedicated `fromRadioScratchLog` /
    `txBufLog` buffers, and `StreamAPI::emitTxBuffer` +
    `StreamAPI::emitLogRecord` both serialize their `stream->write`
    calls via `streamLock`. Leaving the flag on under traffic is safe.
    """
    with connect(port=port) as iface:
        sec = iface.localNode.localConfig.security
        sec.debug_log_api_enabled = bool(enabled)
        iface.localNode.writeConfig("security")
    return {"ok": True, "debug_log_api_enabled": bool(enabled)}


# ---------- admin actions --------------------------------------------------


def reboot(
    port: str | None = None, confirm: bool = False, seconds: int = 10
) -> dict[str, Any]:
    _require_confirm(confirm, "reboot")
    with connect(port=port) as iface:
        iface.localNode.reboot(secs=seconds)
    return {"ok": True, "rebooting_in_s": seconds}


def shutdown(
    port: str | None = None, confirm: bool = False, seconds: int = 10
) -> dict[str, Any]:
    _require_confirm(confirm, "shutdown")
    with connect(port=port) as iface:
        iface.localNode.shutdown(secs=seconds)
    return {"ok": True, "shutting_down_in_s": seconds}


def send_input_event(
    event_code: int | str,
    kb_char: int = 0,
    touch_x: int = 0,
    touch_y: int = 0,
    port: str | None = None,
) -> dict[str, Any]:
    """Inject an InputBroker event (button press / key / gesture) into the UI.

    Wraps `AdminMessage.send_input_event` (handled in firmware at
    src/modules/AdminModule.cpp::handleSendInputEvent). Local-only — no PKI
    warmup needed since the admin message is addressed to `my_node_num`.

    `event_code` accepts an int, a case-insensitive name
    (`"RIGHT"` / `"input_broker_right"`), or an `InputEventCode`. The
    firmware-side enum lives in src/input/InputBroker.h and is mirrored in
    `meshtastic_mcp.input_events`.
    """
    from meshtastic.protobuf import admin_pb2  # type: ignore[import-untyped]

    from .input_events import coerce_event_code

    code = coerce_event_code(event_code)
    if not 0 <= kb_char <= 255:
        raise ValueError(f"kb_char out of u8 range: {kb_char}")
    if not 0 <= touch_x <= 65535:
        raise ValueError(f"touch_x out of u16 range: {touch_x}")
    if not 0 <= touch_y <= 65535:
        raise ValueError(f"touch_y out of u16 range: {touch_y}")

    with connect(port=port) as iface:
        msg = admin_pb2.AdminMessage()
        msg.send_input_event.event_code = code
        msg.send_input_event.kb_char = kb_char
        msg.send_input_event.touch_x = touch_x
        msg.send_input_event.touch_y = touch_y
        iface.localNode._sendAdmin(msg)
    return {"ok": True, "event_code": code, "kb_char": kb_char}


def factory_reset(
    port: str | None = None, confirm: bool = False, full: bool = False
) -> dict[str, Any]:
    """Tell the node to factory-reset its config.

    Works around a meshtastic-python 2.7.8 bug: `Node.factoryReset(full=True)`
    internally does `p.factory_reset_config = True` where the field is
    int32. protobuf 5.x rejects bool→int assignment as a TypeError. We build
    the AdminMessage directly with int values (1=non-full, 2=full) and call
    `_sendAdmin` to sidestep the SDK bug entirely.
    """
    _require_confirm(confirm, "factory_reset")
    from meshtastic.protobuf import admin_pb2  # type: ignore[import-untyped]

    with connect(port=port) as iface:
        msg = admin_pb2.AdminMessage()
        msg.factory_reset_config = 2 if full else 1
        iface.localNode._sendAdmin(msg)
    return {"ok": True, "full": full}
