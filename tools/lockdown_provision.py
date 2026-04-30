#!/usr/bin/env python3
"""
Lockdown passphrase provisioning / unlock / lock-now over USB.

The MESHTASTIC_LOCKDOWN firmware repurposes the security.private_key proto
field as the passphrase transport (no schema changes). This script speaks
that wire format directly via the meshtastic Python library and prints any
LOCKDOWN_* ClientNotification responses the firmware sends back.

Wire format (must stay in sync with src/modules/AdminModule.cpp:935-1040):
    set_config(security):
        private_key.bytes  = passphrase (1-32 bytes raw)
        admin_key[1].bytes[0]   = boots_remaining override (optional)
        admin_key[2].bytes[0:4] = valid_until_epoch LE u32 (optional)

    Special: private_key.size == 1, bytes[0] == 0xFF  ->  LOCK NOW

Examples:
    # First-time provision (and any subsequent unlock):
    tools/lockdown_provision.py provision --passphrase 'correct horse battery'

    # Provision with a 5-boot session token, expiring in 12 hours:
    tools/lockdown_provision.py provision --passphrase pass --boots 5 --hours 12

    # Lock the device immediately (forces reboot into locked state):
    tools/lockdown_provision.py lock

    # Just watch for LOCKDOWN_* notifications without sending anything:
    tools/lockdown_provision.py watch
"""

import argparse
import struct
import sys
import time

try:
    import meshtastic
    import meshtastic.mesh_interface
    import meshtastic.serial_interface
    from meshtastic.protobuf import admin_pb2, config_pb2, mesh_pb2, portnums_pb2
except ImportError:
    sys.stderr.write(
        "error: meshtastic Python package not installed\n"
        "  pip install meshtastic   # or: pipx install meshtastic\n"
    )
    sys.exit(1)


LOCK_NOW_SENTINEL = bytes([0xFF])


def install_notification_printer(iface):
    """Monkey-patch _handleFromRadio to print LOCKDOWN_* ClientNotifications.

    The 2.7.x meshtastic lib does not publish ClientNotification on a pubsub
    topic, so we wrap the FromRadio handler. This is intentionally narrow —
    we only print, never swallow, and always delegate back to the original
    handler.
    """
    original = iface._handleFromRadio

    def wrapped(fromRadioBytes):
        try:
            fr = mesh_pb2.FromRadio()
            fr.ParseFromString(fromRadioBytes)
            if fr.HasField("clientNotification"):
                msg = fr.clientNotification.message
                level = mesh_pb2.LogRecord.Level.Name(fr.clientNotification.level)
                print(f"[device:{level}] {msg}", flush=True)
        except Exception as exc:  # noqa: BLE001 — best-effort logging only
            print(f"[notif-parse-error] {exc}", flush=True)
        return original(fromRadioBytes)

    iface._handleFromRadio = wrapped


def build_security_config(passphrase: bytes, boots: int, hours: int):
    sec = config_pb2.Config.SecurityConfig()
    sec.private_key = passphrase
    if boots > 0 or hours > 0:
        until_epoch = int(time.time()) + hours * 3600 if hours > 0 else 0
        # admin_key[0] is unused by the firmware here but the index packing
        # requires it to exist. An empty ByteString reserves the slot.
        sec.admin_key.append(b"")
        sec.admin_key.append(bytes([max(1, min(255, boots))]))
        sec.admin_key.append(struct.pack("<I", until_epoch))
    return sec


def send_security_config(iface, sec, label: str):
    """Build and send a set_config(security) admin packet to the local node.

    Bypasses iface.localNode._sendAdmin() because that helper sets
    pki_encrypted=True on the outgoing MeshPacket. The lockdown firmware
    drops PKI-encrypted ToRadio packets it cannot decrypt — and on a locked
    device the security private_key it would need lives on the encrypted
    storage that hasn't been unlocked yet, so the decrypt always fails.

    Wire format mirrors Meshtastic-Android's CommandSenderImpl.kt
    sendLockdownPassphrase / sendLockNow exactly.
    """
    if iface.myInfo is None:
        sys.exit("error: device never sent my_info; cannot determine destination nodenum")
    my_node_num = iface.myInfo.my_node_num

    am = admin_pb2.AdminMessage()
    am.set_config.security.CopyFrom(sec)

    mp = mesh_pb2.MeshPacket()
    mp.to = my_node_num
    mp.id = iface._generatePacketId()
    mp.channel = 0
    mp.want_ack = True
    mp.hop_limit = 7
    mp.hop_start = 7
    mp.priority = mesh_pb2.MeshPacket.Priority.RELIABLE
    mp.decoded.portnum = portnums_pb2.PortNum.ADMIN_APP
    mp.decoded.payload = am.SerializeToString()
    # NOTE: pki_encrypted intentionally left False.

    tr = mesh_pb2.ToRadio()
    tr.packet.CopyFrom(mp)

    print(f"[client] sending {label} (to=0x{my_node_num:08x}, id={mp.id}) ...", flush=True)
    iface._sendToRadio(tr)


def cmd_provision(iface, args):
    pp = args.passphrase.encode("utf-8")
    if not 1 <= len(pp) <= 32:
        sys.exit(f"error: passphrase must be 1-32 bytes utf-8, got {len(pp)}")
    sec = build_security_config(pp, args.boots, args.hours)
    send_security_config(iface, sec, "provision/unlock")


def cmd_lock(iface, _args):
    sec = config_pb2.Config.SecurityConfig()
    sec.private_key = LOCK_NOW_SENTINEL
    send_security_config(iface, sec, "LOCK NOW")


def cmd_watch(_iface, _args):
    print("[client] watching for LOCKDOWN_* notifications — Ctrl-C to exit", flush=True)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--port", help="serial device path (auto-detected if omitted)")
    ap.add_argument(
        "--wait",
        type=float,
        default=4.0,
        help="seconds to wait for response after sending (default: 4)",
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_prov = sub.add_parser(
        "provision",
        aliases=["unlock"],
        help="send passphrase (provision on first boot, unlock thereafter)",
    )
    p_prov.add_argument("--passphrase", required=True, help="1-32 byte passphrase")
    p_prov.add_argument(
        "--boots",
        type=int,
        default=0,
        help="boot-count token TTL (0 = firmware default)",
    )
    p_prov.add_argument(
        "--hours",
        type=int,
        default=0,
        help="wall-clock token TTL in hours (0 = no time limit)",
    )
    p_prov.set_defaults(func=cmd_provision)

    p_lock = sub.add_parser("lock", help="send LOCK NOW sentinel; device reboots locked")
    p_lock.set_defaults(func=cmd_lock)

    p_watch = sub.add_parser("watch", help="just listen for LOCKDOWN_* notifications")
    p_watch.add_argument(
        "--seconds",
        type=float,
        default=60.0,
        help="how long to watch (default: 60)",
    )
    p_watch.set_defaults(func=cmd_watch)

    args = ap.parse_args()

    print(f"[client] opening serial port (port={args.port or 'auto'}) ...", flush=True)
    # connectNow=False so we can install the notification printer BEFORE the
    # reader thread starts and we can also tolerate _waitConnected timing out
    # (locked devices may not complete the config handshake the lib expects).
    iface = meshtastic.serial_interface.SerialInterface(
        devPath=args.port,
        noNodes=True,
        connectNow=False,
    )
    install_notification_printer(iface)
    try:
        iface.connect()
        print("[client] config handshake complete", flush=True)
    except meshtastic.mesh_interface.MeshInterface.MeshInterfaceError as exc:
        # Locked device may never send config_complete_id; the reader thread
        # is already running and the port is open, so we can still send.
        print(f"[client] handshake timed out ({exc}); proceeding anyway", flush=True)

    try:
        args.func(iface, args)
        wait = getattr(args, "seconds", args.wait)
        print(f"[client] waiting {wait}s for response ...", flush=True)
        time.sleep(wait)
    finally:
        print("[client] closing", flush=True)
        iface.close()


if __name__ == "__main__":
    main()
