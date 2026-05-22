#!/usr/bin/env python3
"""
Lockdown passphrase provisioning / unlock / lock-now over USB.

Speaks the AdminMessage.lockdown_auth / FromRadio.lockdown_status wire format
introduced for MESHTASTIC_LOCKDOWN firmware builds.

Requires a meshtastic Python package built against protobufs that include
LockdownAuth (admin.proto, tag 104) and LockdownStatus (mesh.proto, tag 18).
If your installed package is older than that, regenerate the Python proto
bindings from this repo's protobufs/ submodule and add them to PYTHONPATH,
or upgrade the meshtastic pip package once the upstream regen lands.

Examples:
    # First-time provision (and any subsequent unlock):
    tools/lockdown_provision.py provision --passphrase 'correct horse battery'

    # Provision with a 5-boot session token, expiring in 12 hours:
    tools/lockdown_provision.py provision --passphrase pass --boots 5 --hours 12

    # Lock the device immediately (forces reboot into locked state):
    tools/lockdown_provision.py lock

    # Just watch for LockdownStatus notifications without sending anything:
    tools/lockdown_provision.py watch
"""

import argparse
import sys
import time

try:
    import meshtastic
    import meshtastic.mesh_interface
    import meshtastic.serial_interface
    from meshtastic.protobuf import admin_pb2, mesh_pb2, portnums_pb2
except ImportError:
    sys.stderr.write(
        "error: meshtastic Python package not installed\n"
        "  pip install meshtastic   # or: pipx install meshtastic\n"
    )
    sys.exit(1)

# Sanity-check the schema is new enough.
_missing = []
if not hasattr(admin_pb2, "LockdownAuth"):
    _missing.append("admin_pb2.LockdownAuth")
if not hasattr(mesh_pb2, "LockdownStatus"):
    _missing.append("mesh_pb2.LockdownStatus")
if _missing:
    sys.stderr.write(
        "error: your meshtastic Python package is too old for the lockdown\n"
        f"        wire format. Missing: {', '.join(_missing)}\n"
        "        Update to a meshtastic release built against protobufs that\n"
        "        contain AdminMessage.lockdown_auth (tag 104) and\n"
        "        FromRadio.lockdown_status (tag 18). See the firmware repo's\n"
        "        protobufs/ submodule for the proto definitions.\n"
    )
    sys.exit(1)


# Mirrors meshtastic_LockdownStatus_State in mesh.pb.h.
_STATE_NAMES = {
    mesh_pb2.LockdownStatus.STATE_UNSPECIFIED: "UNSPECIFIED",
    mesh_pb2.LockdownStatus.NEEDS_PROVISION: "NEEDS_PROVISION",
    mesh_pb2.LockdownStatus.LOCKED: "LOCKED",
    mesh_pb2.LockdownStatus.UNLOCKED: "UNLOCKED",
    mesh_pb2.LockdownStatus.UNLOCK_FAILED: "UNLOCK_FAILED",
}


def install_notification_printer(iface):
    """Wrap _handleFromRadio so we print LockdownStatus frames.

    The 2.7.x meshtastic lib does not yet dispatch LockdownStatus on a
    pubsub topic, so we intercept FromRadio bytes here. Non-lockdown frames
    delegate back to the original handler unchanged.
    """
    original = iface._handleFromRadio

    def wrapped(fromRadioBytes):
        try:
            fr = mesh_pb2.FromRadio()
            fr.ParseFromString(fromRadioBytes)
            if fr.HasField("lockdown_status"):
                ls = fr.lockdown_status
                state = _STATE_NAMES.get(ls.state, f"state={ls.state}")
                parts = [state]
                if ls.lock_reason:
                    parts.append(f"reason={ls.lock_reason}")
                if ls.boots_remaining:
                    parts.append(f"boots={ls.boots_remaining}")
                if ls.valid_until_epoch:
                    parts.append(f"until={ls.valid_until_epoch}")
                if ls.backoff_seconds:
                    parts.append(f"backoff={ls.backoff_seconds}s")
                print(f"[device:LOCKDOWN] {' '.join(parts)}", flush=True)
        except Exception as exc:  # noqa: BLE001 — best-effort logging only
            print(f"[notif-parse-error] {exc}", flush=True)
        return original(fromRadioBytes)

    iface._handleFromRadio = wrapped


def build_lockdown_auth(passphrase: bytes, boots: int, hours: int, lock_now: bool):
    la = admin_pb2.LockdownAuth()
    if passphrase:
        la.passphrase = passphrase
    la.boots_remaining = max(0, min(255, boots))
    la.valid_until_epoch = int(time.time()) + hours * 3600 if hours > 0 else 0
    la.lock_now = lock_now
    return la


def send_lockdown_auth(iface, la, label: str):
    """Send an AdminMessage.lockdown_auth packet to the local node.

    Bypasses iface.localNode._sendAdmin() because that helper sets
    pki_encrypted=True on the outgoing MeshPacket. The lockdown firmware
    drops PKI-encrypted ToRadio packets it cannot decrypt — and on a locked
    device the security private_key it would need lives on the encrypted
    storage that hasn't been unlocked yet, so the decrypt always fails.

    Wire format mirrors Meshtastic-Android's CommandSenderImpl.kt
    sendLockdownPassphrase / sendLockNow exactly.
    """
    if iface.myInfo is None:
        sys.exit(
            "error: device never sent my_info; cannot determine destination nodenum"
        )
    my_node_num = iface.myInfo.my_node_num

    am = admin_pb2.AdminMessage()
    am.lockdown_auth.CopyFrom(la)

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

    print(
        f"[client] sending {label} (to=0x{my_node_num:08x}, id={mp.id}) ...", flush=True
    )
    iface._sendToRadio(tr)


def cmd_provision(iface, args):
    pp = args.passphrase.encode("utf-8")
    if not 1 <= len(pp) <= 32:
        sys.exit(f"error: passphrase must be 1-32 bytes utf-8, got {len(pp)}")
    la = build_lockdown_auth(pp, args.boots, args.hours, lock_now=False)
    send_lockdown_auth(iface, la, "provision/unlock")


def cmd_lock(iface, _args):
    la = build_lockdown_auth(b"", 0, 0, lock_now=True)
    send_lockdown_auth(iface, la, "LOCK NOW")


def cmd_watch(_iface, _args):
    print(
        "[client] watching for LockdownStatus notifications — Ctrl-C to exit",
        flush=True,
    )


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

    p_lock = sub.add_parser("lock", help="send LOCK NOW; device reboots locked")
    p_lock.set_defaults(func=cmd_lock)

    p_watch = sub.add_parser(
        "watch", help="just listen for LockdownStatus notifications"
    )
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
