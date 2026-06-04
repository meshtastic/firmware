#!/usr/bin/env python3
"""Configure the experimental WireGuard module over the Meshtastic admin API."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any

SECRET = "sekrit"


def _import_meshtastic():
    try:
        from meshtastic.serial_interface import SerialInterface
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "meshtastic-python is required. Install it in your active Python environment first."
        ) from exc
    return SerialInterface


def _open_interface(port: str | None):
    serial_interface = _import_meshtastic()
    if port:
        return serial_interface(devPath=port)
    return serial_interface()


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


def _write_config(node: Any, config: Any, args: argparse.Namespace) -> None:
    if args.enable:
        config.enabled = True
    if args.disable:
        config.enabled = False

    _set_if_present(config, "address", args.address)
    _set_if_present(config, "server_addr", args.server_addr)
    _set_if_present(config, "server_port", args.server_port)
    _set_if_present(config, "private_key", args.private_key)
    _set_if_present(config, "public_key", args.public_key)
    _set_if_present(config, "preshared_key", args.preshared_key)

    node.writeConfig("wireguard")


def do_get(args: argparse.Namespace) -> int:
    iface = _open_interface(args.port)
    try:
        config = _wireguard_config(iface.localNode)
        print(json.dumps(_to_dict(config, args.show_secrets), indent=2))
    finally:
        iface.close()
    return 0


def do_set(args: argparse.Namespace) -> int:
    iface = _open_interface(args.port)
    try:
        node = iface.localNode
        config = _wireguard_config(node)
        _write_config(node, config, args)
        print(json.dumps(_to_dict(config, args.show_secrets), indent=2))
    finally:
        iface.close()
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
