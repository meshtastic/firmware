#!/usr/bin/env python3
r"""
Lockdown passphrase provisioning / unlock / lock-now over USB serial.

Speaks the AdminMessage.lockdown_auth / FromRadio.lockdown_status wire format
introduced for MESHTASTIC_LOCKDOWN firmware builds. **This tool is the
canonical reference implementation** - downstream clients (Meshtastic-Android,
in-tree TCP/BLE tools) should mirror its packet shape.

==============================================================================
SECURITY MODEL - READ BEFORE EXTENDING
==============================================================================

  * USB-ONLY by design. The passphrase is sent **in cleartext over the USB
    CDC link** between this script and the device's bootloader-managed
    serial channel. The link is local; an attacker would need physical
    access to the cable to read it.
  * DO NOT extend to TCP or BLE transports without first redesigning the
    handshake - both broadcast the wire format over channels an attacker
    can passively sniff or actively MITM.
  * Passphrases entered at a shell prompt land in your shell history. Use
    --passphrase-file (mode 0600) or the interactive prompt for anything
    you care about keeping. --passphrase on the command line requires
    --insecure-passphrase-on-cmdline as an explicit acknowledgement.
  * Passphrase cannot be recovered. There is no firmware-side reset that
    leaves stored data intact; losing the passphrase means factory-erasing
    the device's flash partition.

==============================================================================
REQUIREMENTS
==============================================================================

A meshtastic Python package built against protobufs that include
LockdownAuth (admin.proto tag 104) and LockdownStatus (mesh.proto tag 18).
If your installed package is older than that, regenerate the Python proto
bindings from this repo's protobufs/ submodule and either overlay them into
your site-packages or add them to PYTHONPATH before this script's imports.

==============================================================================
USAGE
==============================================================================

    # Interactive provision (prompts twice for passphrase, confirms intent):
    tools/lockdown_provision.py --port /dev/cu.usbmodem* provision

    # Provision with a passphrase from a 0600-mode file:
    tools/lockdown_provision.py --port /dev/cu.usbmodem* \\
        provision --passphrase-file ~/.lockdown-passphrase

    # Re-authenticate this connection on an already-provisioned device:
    tools/lockdown_provision.py --port /dev/cu.usbmodem* unlock

    # Lock the device immediately (forces reboot into locked state):
    tools/lockdown_provision.py --port /dev/cu.usbmodem* lock-now --yes

    # Turn lockdown OFF (runtime toggle; reverts storage to plaintext, reboots):
    tools/lockdown_provision.py --port /dev/cu.usbmodem* disable

    # Just listen for LockdownStatus notifications:
    tools/lockdown_provision.py --port /dev/cu.usbmodem* watch --seconds 30
"""

from __future__ import annotations

import argparse
import getpass
import os
import stat
import sys
import threading
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
    sys.exit(2)

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
    sys.exit(2)


# Mirrors meshtastic_LockdownStatus_State in mesh.pb.h.
_STATE_NAMES = {
    mesh_pb2.LockdownStatus.STATE_UNSPECIFIED: "UNSPECIFIED",
    mesh_pb2.LockdownStatus.NEEDS_PROVISION: "NEEDS_PROVISION",
    mesh_pb2.LockdownStatus.LOCKED: "LOCKED",
    mesh_pb2.LockdownStatus.UNLOCKED: "UNLOCKED",
    mesh_pb2.LockdownStatus.UNLOCK_FAILED: "UNLOCK_FAILED",
}
# DISABLED arrived with the runtime-toggle schema. Guard so older bindings that
# only know the original five states still import cleanly; without this a
# capable-but-off boot would print an opaque "state=<num>" instead of DISABLED.
if hasattr(mesh_pb2.LockdownStatus, "DISABLED"):
    _STATE_NAMES[mesh_pb2.LockdownStatus.DISABLED] = "DISABLED"


# Internal coordination between the FromRadio listener thread and the
# main thread so we can block until the device replies (M29) instead of
# sleep()ing a fixed window and hoping.
class StatusFuture:
    """Single-shot future for the next LockdownStatus that arrives after arm()."""

    def __init__(self):
        self._event = threading.Event()
        self._status: mesh_pb2.LockdownStatus | None = None

    def deliver(self, status: mesh_pb2.LockdownStatus) -> None:
        if not self._event.is_set():
            self._status = status
            self._event.set()

    def wait(self, timeout: float) -> mesh_pb2.LockdownStatus | None:
        return self._status if self._event.wait(timeout) else None


_STATUS_FUTURE: StatusFuture | None = None


# ---------------------------------------------------------------------------
# Transport guard (M30)
# ---------------------------------------------------------------------------


_NON_LOCAL_PREFIXES = (
    "tcp:",
    "tcp://",
    "ble:",
    "ble://",
    "udp:",
    "udp://",
    "ws:",
    "wss:",
)


def reject_non_usb_port(port: str | None) -> None:
    """Refuse anything that looks like a remote transport.

    The wire format sends the passphrase in cleartext. That's tolerable
    over USB CDC (physical-attacker model) and explicitly NOT tolerable
    over TCP/BLE/UDP. Reject any --port that names one of those schemes
    so a copy-paste of an example into a different shell can't silently
    leak credentials.
    """
    if not port:
        return
    lowered = port.lower()
    for prefix in _NON_LOCAL_PREFIXES:
        if lowered.startswith(prefix):
            sys.stderr.write(
                f"error: refusing --port {port!r}: this tool is USB-only by\n"
                "       design (passphrase is cleartext on the wire). See the\n"
                "       SECURITY MODEL block at the top of this file.\n"
            )
            sys.exit(2)


# ---------------------------------------------------------------------------
# Passphrase input (M26)
# ---------------------------------------------------------------------------


def read_passphrase_from_file(path: str) -> bytes:
    """Read a passphrase from a 0600-mode file.

    Refuse to read if the file is world- or group-readable to avoid
    silently using a passphrase that another user could lift off the
    filesystem.
    """
    try:
        st = os.stat(path)
    except OSError as exc:
        sys.exit(f"error: cannot stat {path}: {exc}")
    mode = stat.S_IMODE(st.st_mode)
    if mode & 0o077:
        sys.exit(
            f"error: {path} mode is {oct(mode)} - must be 0600 (operator-only).\n"
            f"       run: chmod 600 {path}"
        )
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as exc:
        sys.exit(f"error: cannot read {path}: {exc}")
    # Strip a single trailing newline (common when authored with `echo`).
    if raw.endswith(b"\r\n"):
        raw = raw[:-2]
    elif raw.endswith(b"\n"):
        raw = raw[:-1]
    return raw


def prompt_passphrase(confirm: bool) -> bytes:
    """Interactive prompt. confirm=True double-enters and matches."""
    pp = getpass.getpass("passphrase: ").encode("utf-8")
    if confirm:
        pp2 = getpass.getpass("passphrase (confirm): ").encode("utf-8")
        if pp != pp2:
            sys.exit("error: passphrases do not match")
    return pp


def gather_passphrase(args, *, confirm: bool) -> bytes:
    """Resolve the passphrase from --passphrase / --passphrase-file / prompt.

    Order of precedence: argv (with --insecure-passphrase-on-cmdline) >
    --passphrase-file > interactive prompt.
    """
    if args.passphrase is not None:
        if not args.insecure_passphrase_on_cmdline:
            sys.exit(
                "error: --passphrase on argv requires "
                "--insecure-passphrase-on-cmdline.\n"
                "       Reason: argv lands in shell history and is visible via\n"
                "       `ps`. Prefer --passphrase-file or the interactive prompt."
            )
        sys.stderr.write(
            "warning: passphrase passed on argv - visible to other users via\n"
            "         ps(1), and persisted in your shell history file.\n"
        )
        pp = args.passphrase.encode("utf-8")
    elif args.passphrase_file is not None:
        pp = read_passphrase_from_file(args.passphrase_file)
    else:
        pp = prompt_passphrase(confirm)

    if not 1 <= len(pp) <= 32:
        sys.exit(f"error: passphrase must be 1..32 bytes utf-8, got {len(pp)}")
    return pp


# ---------------------------------------------------------------------------
# FromRadio notification interception (L7)
# ---------------------------------------------------------------------------


def install_notification_printer(iface) -> None:
    """Wrap _handleFromRadio to print LockdownStatus frames and feed the future.

    meshtastic-python (as of the version this script was last tested
    against) does not dispatch LockdownStatus on a public pubsub topic.
    We hook the private _handleFromRadio entry point, which is the
    fragility flagged in the audit's L7 finding. If a future lib release
    breaks this, the missing-attr error will be obvious; until then this
    is the only seam available.
    """
    original = getattr(iface, "_handleFromRadio", None)
    if original is None:
        sys.exit(
            "error: meshtastic.serial_interface.SerialInterface has no\n"
            "       _handleFromRadio method. The lib's private API changed -\n"
            "       this tool needs to be updated. See L7 in the audit notes."
        )

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
                if _STATUS_FUTURE is not None:
                    _STATUS_FUTURE.deliver(ls)
        except Exception as exc:  # noqa: BLE001 - best-effort logging only
            print(f"[notif-parse-error] {exc}", flush=True)
        return original(fromRadioBytes)

    iface._handleFromRadio = wrapped


# ---------------------------------------------------------------------------
# LockdownAuth construction + send
# ---------------------------------------------------------------------------


def build_lockdown_auth(
    passphrase: bytes,
    boots: int,
    hours: int,
    max_session_seconds: int,
    lock_now: bool,
    disable: bool = False,
):
    la = admin_pb2.LockdownAuth()
    if passphrase:
        la.passphrase = passphrase
    la.boots_remaining = max(0, min(255, boots))
    la.valid_until_epoch = int(time.time()) + hours * 3600 if hours > 0 else 0
    la.max_session_seconds = max(0, max_session_seconds)
    la.lock_now = lock_now
    if disable:
        # disable lives on the runtime-toggle schema. Fail loudly on older
        # bindings rather than silently sending an unlock the firmware would
        # honour as a normal auth.
        if not hasattr(la, "disable"):
            sys.exit(
                "error: your meshtastic Python package is too old for the\n"
                "       runtime-toggle disable flow (LockdownAuth.disable\n"
                "       missing). Update the protobuf bindings."
            )
        la.disable = True
    return la


def send_lockdown_auth(iface, la, label: str) -> int:
    """Send AdminMessage.lockdown_auth to this node. Returns mp.id on success."""
    if iface.myInfo is None:
        sys.exit(
            "error: device never sent my_info; cannot determine destination nodenum"
        )
    my_node_num = iface.myInfo.my_node_num

    am = admin_pb2.AdminMessage()
    am.lockdown_auth.CopyFrom(la)

    # _generatePacketId is private but stable across recent lib versions.
    generate_id = getattr(iface, "_generatePacketId", None)
    if generate_id is None:
        sys.exit("error: meshtastic lib missing _generatePacketId - see L7 note")
    mp = mesh_pb2.MeshPacket()
    mp.to = my_node_num
    mp.id = generate_id()
    mp.channel = 0
    mp.want_ack = True
    mp.hop_limit = 7
    mp.hop_start = 7
    mp.priority = mesh_pb2.MeshPacket.Priority.RELIABLE
    mp.decoded.portnum = portnums_pb2.PortNum.ADMIN_APP
    mp.decoded.payload = am.SerializeToString()
    # NOTE: pki_encrypted intentionally left False - see top-of-file note in
    # the original tool. Lockdown firmware drops PKI-encrypted ToRadio.

    tr = mesh_pb2.ToRadio()
    tr.packet.CopyFrom(mp)

    send_to_radio = getattr(iface, "_sendToRadio", None)
    if send_to_radio is None:
        sys.exit("error: meshtastic lib missing _sendToRadio - see L7 note")
    print(
        f"[client] sending {label} (to=0x{my_node_num:08x}, id={mp.id}) ...", flush=True
    )
    send_to_radio(tr)
    return mp.id


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------


def _await_status(timeout: float) -> mesh_pb2.LockdownStatus | None:
    if _STATUS_FUTURE is None:
        time.sleep(timeout)
        return None
    print(f"[client] waiting up to {timeout}s for LockdownStatus ...", flush=True)
    return _STATUS_FUTURE.wait(timeout)


def cmd_provision(iface, args) -> int:
    # M27: this is the destructive setup step. Warn explicitly and require
    # a typed confirmation unless --yes was supplied.
    if not args.yes:
        sys.stderr.write(
            "WARNING: first-time provision binds this device to a passphrase\n"
            "         that cannot be recovered. If you lose it, the only way\n"
            "         back is a factory-erase that wipes ALL stored state\n"
            "         (channels, contacts, messages, position, etc.).\n"
        )
        ans = input("Type 'yes' to continue: ").strip().lower()
        if ans != "yes":
            sys.exit("aborted")

    pp = gather_passphrase(args, confirm=True)
    la = build_lockdown_auth(
        pp,
        args.boots,
        args.hours,
        args.max_session_seconds,
        lock_now=False,
    )
    global _STATUS_FUTURE
    _STATUS_FUTURE = StatusFuture()
    send_lockdown_auth(iface, la, "provision/unlock")
    status = _await_status(args.wait)
    if status is None:
        sys.stderr.write("warning: no LockdownStatus received within wait window\n")
        return 1
    return _exit_code_for_status(status)


def cmd_unlock(iface, args) -> int:
    pp = gather_passphrase(args, confirm=False)
    la = build_lockdown_auth(
        pp,
        args.boots,
        args.hours,
        args.max_session_seconds,
        lock_now=False,
    )
    global _STATUS_FUTURE
    _STATUS_FUTURE = StatusFuture()
    send_lockdown_auth(iface, la, "unlock")
    status = _await_status(args.wait)
    if status is None:
        sys.stderr.write("warning: no LockdownStatus received within wait window\n")
        return 1
    return _exit_code_for_status(status)


def cmd_lock(iface, args) -> int:
    if not args.yes:
        sys.stderr.write(
            "WARNING: 'lock' will revoke all current auth and reboot the\n"
            "         device into the locked state. The next connect will\n"
            "         require the passphrase.\n"
        )
        ans = input("Type 'yes' to continue: ").strip().lower()
        if ans != "yes":
            sys.exit("aborted")
    la = build_lockdown_auth(b"", 0, 0, 0, lock_now=True)
    global _STATUS_FUTURE
    _STATUS_FUTURE = StatusFuture()
    send_lockdown_auth(iface, la, "LOCK NOW")
    # Device may not get an UNLOCKED/LOCKED back to us before it reboots;
    # accept the lack of a status as "probably worked" for this command.
    status = _await_status(args.wait)
    if status is None:
        print("[client] no status received (device may already be rebooting)")
        return 0
    return _exit_code_for_status(status)


def cmd_disable(iface, args) -> int:
    # Runtime-toggle OFF. Unlike 'lock' (which reboots back into the locked
    # state), 'disable' turns lockdown off entirely: the firmware re-verifies
    # the passphrase to load the DEK, reverts at-rest encryption to plaintext,
    # then reboots into normal mode. A non-empty passphrase is REQUIRED - the
    # firmware rejects an empty one with UNLOCK_FAILED.
    if not args.yes:
        sys.stderr.write(
            "WARNING: 'disable' turns lockdown OFF on this device. Stored files\n"
            "         are reverted to plaintext, per-connection admin auth is no\n"
            "         longer enforced, and the device reboots into normal mode.\n"
            "         (APPROTECT is NOT reversed.)\n"
        )
        ans = input("Type 'yes' to continue: ").strip().lower()
        if ans != "yes":
            sys.exit("aborted")
    pp = gather_passphrase(args, confirm=False)
    # TTL/session fields are ignored by the firmware on a disable request.
    la = build_lockdown_auth(pp, 0, 0, 0, lock_now=False, disable=True)
    global _STATUS_FUTURE
    _STATUS_FUTURE = StatusFuture()
    send_lockdown_auth(iface, la, "disable")
    # On success the firmware decrypts every stored file before broadcasting
    # DISABLED, so a large node DB can take longer than the default wait - bump
    # --wait if you see no status. The DISABLED broadcast precedes the reboot.
    status = _await_status(args.wait)
    if status is None:
        sys.stderr.write("warning: no LockdownStatus received within wait window\n")
        return 1
    return _exit_code_for_status(status)


def cmd_watch(_iface, args) -> int:
    print(
        f"[client] watching for LockdownStatus notifications for {args.seconds}s - Ctrl-C to exit early",
        flush=True,
    )
    try:
        time.sleep(args.seconds)
    except KeyboardInterrupt:
        print("[client] interrupted")
    return 0


def _exit_code_for_status(status: mesh_pb2.LockdownStatus) -> int:
    """Map the final LockdownStatus to a shell exit code (M29)."""
    if status.state == mesh_pb2.LockdownStatus.UNLOCKED:
        return 0
    # DISABLED is a terminal success: a runtime-toggle 'disable' completed, or a
    # capable-but-off device reported its state. Guarded for older bindings.
    if (
        hasattr(mesh_pb2.LockdownStatus, "DISABLED")
        and status.state == mesh_pb2.LockdownStatus.DISABLED
    ):
        return 0
    if status.state == mesh_pb2.LockdownStatus.UNLOCK_FAILED:
        sys.stderr.write(
            "error: UNLOCK_FAILED"
            + (
                f" - try again in {status.backoff_seconds}s"
                if status.backoff_seconds
                else ""
            )
            + "\n"
        )
        return 4
    if status.state == mesh_pb2.LockdownStatus.LOCKED:
        # Common: the firmware emitted LOCKED before our auth could process,
        # or this is the LOCKED-with-needs_auth that follows a successful
        # provision-then-disconnect cycle. Treat as ambiguous.
        return 3
    if status.state == mesh_pb2.LockdownStatus.NEEDS_PROVISION:
        return 2
    return 1


# ---------------------------------------------------------------------------
# Argparse + entrypoint
# ---------------------------------------------------------------------------


def _add_passphrase_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--passphrase",
        help="passphrase on cmdline (requires --insecure-passphrase-on-cmdline)",
    )
    parser.add_argument(
        "--passphrase-file", help="path to a 0600-mode file containing the passphrase"
    )
    parser.add_argument(
        "--insecure-passphrase-on-cmdline",
        action="store_true",
        help="acknowledge that --passphrase will be visible via ps and shell history",
    )


def _add_ttl_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--boots",
        type=int,
        default=0,
        help="boot-count token TTL (0 = firmware default 50, max 255)",
    )
    parser.add_argument(
        "--hours",
        type=int,
        default=0,
        help="wall-clock token TTL in hours (0 = no time limit)",
    )
    parser.add_argument(
        "--max-session-seconds",
        type=int,
        default=0,
        help="per-boot uptime cap on the unlocked session (0 = unlimited)",
    )


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument(
        "--port",
        help="USB serial device path, e.g. /dev/cu.usbmodem* - TCP/BLE/UDP rejected",
    )
    ap.add_argument(
        "--wait",
        type=float,
        default=8.0,
        help="seconds to wait for response (default: 8)",
    )
    ap.add_argument(
        "--yes", "-y", action="store_true", help="skip interactive confirmation prompts"
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_prov = sub.add_parser("provision", help="first-time set passphrase (binds DEK)")
    _add_passphrase_args(p_prov)
    _add_ttl_args(p_prov)
    p_prov.set_defaults(func=cmd_provision)

    p_unlock = sub.add_parser(
        "unlock", help="re-authenticate this connection with the existing passphrase"
    )
    _add_passphrase_args(p_unlock)
    _add_ttl_args(p_unlock)
    p_unlock.set_defaults(func=cmd_unlock)

    p_lock = sub.add_parser(
        "lock", aliases=["lock-now"], help="send LOCK NOW; device reboots locked"
    )
    p_lock.set_defaults(func=cmd_lock)

    p_disable = sub.add_parser(
        "disable",
        help="turn lockdown OFF (runtime toggle); requires passphrase, reverts to plaintext",
    )
    _add_passphrase_args(p_disable)
    p_disable.set_defaults(func=cmd_disable)

    p_watch = sub.add_parser(
        "watch", help="just listen for LockdownStatus notifications"
    )
    p_watch.add_argument(
        "--seconds", type=float, default=60.0, help="how long to watch (default: 60)"
    )
    p_watch.set_defaults(func=cmd_watch)

    args = ap.parse_args()
    reject_non_usb_port(args.port)

    sys.stderr.write(
        "lockdown_provision: USB-only, passphrase travels cleartext on the cable.\n"
        "                    See SECURITY MODEL block at top of this file.\n"
    )

    print(f"[client] opening serial port (port={args.port or 'auto'}) ...", flush=True)
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
        # Locked device may never send config_complete_id; we can still send
        # lockdown_auth because the firmware reads ToRadio independent of the
        # client's config-download state.
        print(f"[client] handshake timed out ({exc}); proceeding anyway", flush=True)

    rc = 1
    try:
        rc = args.func(iface, args)
    finally:
        print("[client] closing", flush=True)
        iface.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
