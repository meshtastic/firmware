#!/usr/bin/env python3
"""Configure the experimental WireGuard module over the Meshtastic admin API."""

from __future__ import annotations

import argparse
import configparser
import json
import sys
import time
from pathlib import Path
from threading import Event
from typing import Any

SECRET = "sekrit"


def list_serial_ports() -> list[dict[str, str]]:
    try:
        from serial.tools import list_ports
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "pyserial is required to list serial ports. Install meshtastic-python in your active Python environment first."
        ) from exc

    return [
        {
            "device": port.device,
            "description": port.description,
            "hwid": port.hwid,
        }
        for port in list_ports.comports()
    ]


def _import_meshtastic():
    try:
        from meshtastic.mesh_interface import MeshInterface
        from meshtastic.protobuf import admin_pb2, mesh_pb2, module_config_pb2
        from meshtastic.serial_interface import SerialInterface
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "meshtastic-python is required. Install it in your active Python environment first."
        ) from exc
    _patch_wireguard_module_config_copy(MeshInterface, mesh_pb2)
    return SerialInterface, admin_pb2, module_config_pb2


def _patch_wireguard_module_config_copy(mesh_interface: Any, mesh_pb2: Any) -> None:
    if getattr(mesh_interface, "_wireguard_patch_applied", False):
        return

    original = mesh_interface._handleFromRadio

    def patched(self: Any, from_radio_bytes: Any) -> None:
        original(self, from_radio_bytes)
        fromRadio = mesh_pb2.FromRadio()
        fromRadio.ParseFromString(from_radio_bytes)
        if not fromRadio.HasField("moduleConfig") or not fromRadio.moduleConfig.HasField("wireguard"):
            return
        self.localNode.moduleConfig.wireguard.CopyFrom(fromRadio.moduleConfig.wireguard)

    mesh_interface._handleFromRadio = patched
    mesh_interface._wireguard_patch_applied = True


def _open_interface(port: str | None):
    serial_interface, _, _ = _import_meshtastic()
    if port:
        return serial_interface(devPath=port)
    return serial_interface()


def _admin_message():
    _, admin_pb2, _ = _import_meshtastic()
    return admin_pb2.AdminMessage()


def _new_wireguard_config():
    _, _, module_config_pb2 = _import_meshtastic()
    return module_config_pb2.ModuleConfig.WireGuardConfig()


def _refresh_wireguard_config(node: Any, delay: float = 5.0) -> None:
    config = _wireguard_config(node)
    admin = _admin_message()
    admin.get_module_config_request = admin.ModuleConfigType.Value("WIREGUARD_CONFIG")
    received = Event()

    def on_response(packet: dict[str, Any]) -> None:
        try:
            raw_admin = packet["decoded"]["admin"]["raw"]
            config.CopyFrom(raw_admin.get_module_config_response.wireguard)
        finally:
            received.set()

    node._sendAdmin(admin, wantResponse=True, onResponse=on_response)
    received.wait(delay)


def _wireguard_config(node: Any):
    try:
        return node.moduleConfig.wireguard
    except AttributeError as exc:
        raise SystemExit(
            "This meshtastic-python protobuf package does not include ModuleConfig.wireguard. "
            "Regenerate/install the Python protobufs from this firmware branch."
        ) from exc


def _enum_name(value: int) -> str:
    try:
        from meshtastic.protobuf import module_config_pb2

        enum = module_config_pb2.ModuleConfig.WireGuardConfig.Status
        return enum.Name(value)
    except Exception:
        return str(value)


def _redact(value: str, show_secrets: bool) -> str:
    if show_secrets or not value:
        return value
    return SECRET


def _to_dict(config: Any, show_secrets: bool = False) -> dict[str, Any]:
    return {
        "enabled": bool(config.enabled),
        "address": config.address,
        "server_addr": config.server_addr,
        "server_port": int(config.server_port),
        "private_key": _redact(config.private_key, show_secrets),
        "public_key": config.public_key,
        "preshared_key": _redact(config.preshared_key, show_secrets),
        "status": _enum_name(int(getattr(config, "status", 0))),
        "last_error": getattr(config, "last_error", ""),
    }


def _set_if_present(config: Any, field: str, value: Any) -> None:
    if value is not None:
        setattr(config, field, value)


def _strip_cidr(address: str) -> str:
    first_address = address.split(",", 1)[0].strip()
    if "/" in first_address:
        first_address = first_address.split("/", 1)[0].strip()
    if not first_address:
        raise SystemExit("WireGuard config Interface.Address is empty.")
    if ":" in first_address:
        raise SystemExit("This firmware currently supports IPv4 WireGuard client addresses only.")
    return first_address


def _parse_endpoint(endpoint: str) -> tuple[str, int]:
    endpoint = endpoint.strip()
    if not endpoint:
        raise SystemExit("WireGuard config Peer.Endpoint is empty.")

    if endpoint.startswith("["):
        end = endpoint.find("]")
        if end == -1 or len(endpoint) <= end + 2 or endpoint[end + 1] != ":":
            raise SystemExit("IPv6 endpoints must use [host]:port syntax.")
        host = endpoint[1:end]
        port_text = endpoint[end + 2 :]
    else:
        if endpoint.count(":") != 1:
            raise SystemExit("Endpoint must be host:port. Use [IPv6-address]:port for IPv6 endpoints.")
        host, port_text = endpoint.rsplit(":", 1)

    host = host.strip()
    if not host:
        raise SystemExit("WireGuard config Peer.Endpoint host is empty.")
    try:
        port = int(port_text)
    except ValueError as exc:
        raise SystemExit(f"WireGuard config Peer.Endpoint port is not an integer: {port_text!r}") from exc
    if port <= 0 or port > 65535:
        raise SystemExit(f"WireGuard config Peer.Endpoint port is out of range: {port}")
    return host, port


def _read_wireguard_config(path: str) -> dict[str, Any]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str.lower
    try:
        with Path(path).open("r", encoding="utf-8") as config_file:
            parser.read_file(config_file)
    except configparser.DuplicateSectionError as exc:
        raise SystemExit("WireGuard configs with multiple [Peer] sections are not supported.") from exc
    except configparser.Error as exc:
        raise SystemExit(f"Unable to parse WireGuard config: {exc}") from exc
    except OSError as exc:
        raise SystemExit(f"Unable to read WireGuard config {path!r}: {exc}") from exc

    if "Interface" not in parser:
        raise SystemExit("WireGuard config is missing an [Interface] section.")
    if "Peer" not in parser:
        raise SystemExit("WireGuard config is missing a [Peer] section.")

    interface = parser["Interface"]
    peer = parser["Peer"]
    values: dict[str, Any] = {}

    if interface.get("address"):
        values["address"] = _strip_cidr(interface["address"])
    if interface.get("privatekey"):
        values["private_key"] = interface["privatekey"].strip()
    if peer.get("publickey"):
        values["public_key"] = peer["publickey"].strip()
    if peer.get("presharedkey"):
        values["preshared_key"] = peer["presharedkey"].strip()
    if peer.get("endpoint"):
        values["server_addr"], values["server_port"] = _parse_endpoint(peer["endpoint"])

    return values


def _apply_config_file_defaults(args: argparse.Namespace) -> None:
    if not getattr(args, "config", None):
        return

    for field, value in _read_wireguard_config(args.config).items():
        if getattr(args, field) is None:
            setattr(args, field, value)


def _write_config(node: Any, config: Any, args: argparse.Namespace) -> Any:
    _apply_config_file_defaults(args)

    outgoing = _new_wireguard_config()
    outgoing.CopyFrom(config)

    if args.enable:
        outgoing.enabled = True
    if args.disable:
        outgoing.enabled = False

    _set_if_present(outgoing, "address", args.address)
    _set_if_present(outgoing, "server_addr", args.server_addr)
    _set_if_present(outgoing, "server_port", args.server_port)
    _set_if_present(outgoing, "private_key", args.private_key)
    _set_if_present(outgoing, "public_key", args.public_key)
    _set_if_present(outgoing, "preshared_key", args.preshared_key)
    outgoing.status = 0
    outgoing.last_error = ""

    admin = _admin_message()
    admin.set_module_config.wireguard.CopyFrom(outgoing)
    on_response = None if node == node.iface.localNode else node.onAckNak
    node._sendAdmin(admin, onResponse=on_response)
    time.sleep(2.0)
    return outgoing


def read_wireguard_config(port: str | None, show_secrets: bool = False) -> dict[str, Any]:
    iface = _open_interface(port)
    try:
        _refresh_wireguard_config(iface.localNode)
        return _to_dict(_wireguard_config(iface.localNode), show_secrets)
    finally:
        iface.close()


def set_wireguard_config(
    port: str | None,
    config_path: str | None = None,
    *,
    enable: bool = False,
    disable: bool = False,
    show_secrets: bool = False,
    address: str | None = None,
    server_addr: str | None = None,
    server_port: int | None = None,
    private_key: str | None = None,
    public_key: str | None = None,
    preshared_key: str | None = None,
) -> dict[str, dict[str, Any]]:
    args = argparse.Namespace(
        config=config_path,
        enable=enable,
        disable=disable,
        show_secrets=show_secrets,
        address=address,
        server_addr=server_addr,
        server_port=server_port,
        private_key=private_key,
        public_key=public_key,
        preshared_key=preshared_key,
    )

    iface = _open_interface(port)
    try:
        node = iface.localNode
        written = _write_config(node, _wireguard_config(node), args)
    finally:
        iface.close()

    return {
        "written": _to_dict(written, show_secrets),
        "confirmed": read_wireguard_config(port, show_secrets),
    }


def do_get(args: argparse.Namespace) -> int:
    print(json.dumps(read_wireguard_config(args.port, args.show_secrets), indent=2))
    return 0


def do_set(args: argparse.Namespace) -> int:
    result = set_wireguard_config(
        args.port,
        args.config,
        enable=args.enable,
        disable=args.disable,
        show_secrets=args.show_secrets,
        address=args.address,
        server_addr=args.server_addr,
        server_port=args.server_port,
        private_key=args.private_key,
        public_key=args.public_key,
        preshared_key=args.preshared_key,
    )
    print(json.dumps(result["written"], indent=2))
    return 0


def do_disable(args: argparse.Namespace) -> int:
    args.enable = False
    args.disable = True
    args.address = None
    args.server_addr = None
    args.server_port = None
    args.private_key = None
    args.public_key = None
    args.preshared_key = None
    return do_set(args)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Serial device path. Omit for meshtastic-python auto-detection.")
    parser.add_argument("--show-secrets", action="store_true", help="Print private and preshared keys in command output.")

    subparsers = parser.add_subparsers(dest="command", required=True)

    get_parser = subparsers.add_parser("get", help="Read WireGuard configuration and runtime status.")
    get_parser.set_defaults(func=do_get)

    set_parser = subparsers.add_parser("set", help="Set one or more WireGuard configuration fields.")
    enabled = set_parser.add_mutually_exclusive_group()
    enabled.add_argument("--enable", action="store_true", help="Enable automatic tunnel startup.")
    enabled.add_argument("--disable", action="store_true", help="Disable automatic tunnel startup.")
    set_parser.add_argument("--config", help="Read settings from a standard WireGuard .conf file.")
    set_parser.add_argument("--address", help="Client tunnel IPv4 address, without subnet mask.")
    set_parser.add_argument("--server-addr", help="WireGuard server hostname or IP address.")
    set_parser.add_argument("--server-port", type=int, help="WireGuard server UDP port.")
    set_parser.add_argument("--private-key", help=f"Client private key. Use {SECRET!r} to preserve the current value.")
    set_parser.add_argument("--public-key", help="Server public key.")
    set_parser.add_argument("--preshared-key", help=f"Optional preshared key. Use {SECRET!r} to preserve the current value.")
    set_parser.set_defaults(func=do_set)

    disable_parser = subparsers.add_parser("disable", help="Disable automatic WireGuard startup.")
    disable_parser.set_defaults(func=do_disable)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
