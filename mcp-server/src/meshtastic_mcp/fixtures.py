"""Fake NodeDB fixture push — Portduino file copy + hardware XModem upload.

The fixture pipeline is two-stage:
  1. `bin/gen-fake-nodedb-seed.py` produces a deterministic JSONL describing N
     fake-but-realistic peers. Committed under `test/fixtures/nodedb/`.
  2. `bin/seed-json-to-proto.py` compiles JSONL → binary v25 NodeDatabase
     protobuf with fresh wall-clock timestamps.

This module exposes `push_fake_nodedb(...)`, the MCP tool that:
  - target="portduino": compiles the JSONL into the device's prefs dir on
    the local filesystem (`~/.portduino/<config>/prefs/nodes.proto`).
  - target="hardware":  compiles to a temp file, then streams it over the
    XModem protocol (via the meshtastic SerialInterface/BLEInterface +
    `meshtastic.xmodempacket` pubsub topic) to `/prefs/nodes.proto` on the
    device. Triggers a reboot so the firmware loads the new state on next
    boot.

XModem wire details (mirrors firmware impl at src/xmodem.cpp:115-260):
  * 128-byte chunks; final chunk padded to 128 B with 0x1A (SUB) bytes.
  * CRC16-CCITT (poly 0x1021, init 0x0000).
  * SOH/seq=0 carries the destination filename in `buffer.bytes`. ACK if
    `FSCom.open(filename, FILE_O_WRITE)` succeeds; NAK otherwise.
  * SOH/seq≥1 carries a 128-byte chunk. ACK = advance; NAK = retransmit.
  * EOT after the last chunk flushes + closes the file on-device.

Hardware push requires `confirm=True` (mirrors factory_reset / erase_and_flash
in the .github/copilot-instructions.md "never do these without asking" list).
"""

from __future__ import annotations

import dataclasses
import hashlib
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any, Literal

from .connection import connect, is_tcp_port

# Resolve repo root so the tool works regardless of mcp-server cwd.
_REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
_SEED_DIR = _REPO_ROOT / "test" / "fixtures" / "nodedb"
_COMPILE_SCRIPT = _REPO_ROOT / "bin" / "seed-json-to-proto.py"

_DEFAULT_NODES_FILENAME = "/prefs/nodes.proto"
_XMODEM_CHUNK = 128
_XMODEM_SUB = 0x1A
_ACK_TIMEOUT_INIT_S = 5.0
_ACK_TIMEOUT_CHUNK_S = 2.0
_MAX_CHUNK_RETRIES = 5

_VALID_SIZES = (250, 500, 1000, 2000)


class FixtureError(RuntimeError):
    """Raised for any fixture-push failure (compile, transport, ack timeout, …)."""


# ---------------------------------------------------------------------------
# CRC16-CCITT (poly 0x1021, init 0x0000). Matches the firmware's `crc16_ccitt`.
# Hand-rolled to avoid the optional `crcmod` dep.
# ---------------------------------------------------------------------------
def _crc16_ccitt(data: bytes, *, init: int = 0x0000) -> int:
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# Compile step — shells out to bin/seed-json-to-proto.py so the MCP module
# doesn't have to duplicate the proto-encoding logic.
# ---------------------------------------------------------------------------
def _compile_proto(jsonl_path: pathlib.Path, out_path: pathlib.Path) -> None:
    if not _COMPILE_SCRIPT.is_file():
        raise FixtureError(f"compile script missing at {_COMPILE_SCRIPT}")
    cmd = [
        sys.executable,
        str(_COMPILE_SCRIPT),
        "--in",
        str(jsonl_path),
        "--out",
        str(out_path),
    ]
    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as exc:
        raise FixtureError(
            f"seed-json-to-proto.py failed (exit {exc.returncode}):\n"
            f"  stdout: {exc.stdout}\n  stderr: {exc.stderr}"
        ) from exc


def _resolve_seed_jsonl(size: int, custom: str | None) -> pathlib.Path:
    if custom is not None:
        p = pathlib.Path(custom).expanduser().resolve()
        if not p.is_file():
            raise FixtureError(f"custom_seed_jsonl not found: {p}")
        return p
    p = _SEED_DIR / f"seed_v25_{size:04d}.jsonl"
    if not p.is_file():
        raise FixtureError(
            f"missing committed seed at {p}. "
            f"Run `./bin/regen-fake-nodedbs.sh` to generate it."
        )
    return p


# ---------------------------------------------------------------------------
# Portduino push — file copy into ~/.portduino/<config>/prefs/
# ---------------------------------------------------------------------------
def _portduino_prefs_dir(config_name: str) -> pathlib.Path:
    home = pathlib.Path.home()
    return home / ".portduino" / config_name / "prefs"


def _push_portduino(
    size: int,
    jsonl: pathlib.Path,
    portduino_config: str,
    backup_existing: bool,
) -> dict[str, Any]:
    prefs = _portduino_prefs_dir(portduino_config)
    prefs.mkdir(parents=True, exist_ok=True)
    target = prefs / "nodes.proto"
    backed_up_to: str | None = None
    if backup_existing and target.is_file():
        ts = int(time.time())
        backup = prefs / f"nodes.proto.bak.{ts}"
        shutil.move(str(target), str(backup))
        backed_up_to = str(backup)
    _compile_proto(jsonl, target)
    raw = target.read_bytes()
    return {
        "transport": "portduino",
        "path": str(target),
        "bytes": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "jsonl_source": str(jsonl),
        "backed_up_to": backed_up_to,
    }


# ---------------------------------------------------------------------------
# Hardware push — XModem over BLE/serial via the meshtastic Python interface.
# ---------------------------------------------------------------------------
@dataclasses.dataclass
class _AckEvent:
    control: int
    seq: int


def _wait_for_response(q: "queue.Queue[_AckEvent]", timeout_s: float) -> _AckEvent:
    try:
        return q.get(timeout=timeout_s)
    except queue.Empty as exc:
        raise FixtureError(
            f"XModem response timeout after {timeout_s:.1f}s — device not responding"
        ) from exc


def _push_hardware(
    size: int,
    jsonl: pathlib.Path,
    port: str | None,
    reboot_after: bool,
) -> dict[str, Any]:
    # Lazy imports so the module loads even when the meshtastic deps aren't
    # available (e.g. CI in a Python env without the package installed).
    try:
        from meshtastic.protobuf import mesh_pb2, xmodem_pb2
        from pubsub import pub
    except ImportError as exc:  # pragma: no cover — dep missing
        raise FixtureError(
            f"hardware push requires the meshtastic + pypubsub packages: {exc}"
        ) from exc

    if is_tcp_port(port):
        raise FixtureError(
            "hardware push over TCP/portduino is not supported — use "
            "target='portduino' to drop the fixture directly into the prefs dir."
        )

    # Compile the fixture to a temp file with fresh timestamps.
    with tempfile.NamedTemporaryFile(suffix=".proto", delete=False) as tf:
        proto_path = pathlib.Path(tf.name)
    try:
        _compile_proto(jsonl, proto_path)
        payload = proto_path.read_bytes()
    finally:
        proto_path.unlink(missing_ok=True)

    sha256 = hashlib.sha256(payload).hexdigest()
    total_bytes = len(payload)

    # Subscribe to XModem responses BEFORE we open the interface, so we don't
    # race the first ACK that arrives during the SOH/seq=0 handshake.
    #
    # NB: the signature MUST declare every kwarg pypubsub will see for this
    # topic, or pubsub locks the topic spec to a smaller set (whichever
    # subscribe arrives first) and then *rejects* the meshtastic library's
    # publish call with `SenderUnknownMsgDataError: unknown ... interface`.
    # The meshtastic lib publishes both `packet=` and `interface=`
    # (mesh_interface.py:1389-1395), so both must appear here.
    response_q: "queue.Queue[_AckEvent]" = queue.Queue()

    def _on_xmodem(packet: Any = None, interface: Any = None, **_kw: Any) -> None:
        if packet is None:
            return
        response_q.put(_AckEvent(control=int(packet.control), seq=int(packet.seq)))

    pub.subscribe(_on_xmodem, "meshtastic.xmodempacket")

    chunks_sent = 0
    retried = 0
    rebooted = False

    XMC = xmodem_pb2.XModem.Control
    try:
        with connect(port=port) as iface:
            # 1) Send the filename (SOH, seq=0).
            init_pkt = xmodem_pb2.XModem(
                control=XMC.Value("SOH"),
                seq=0,
                buffer=_DEFAULT_NODES_FILENAME.encode("utf-8"),
            )
            iface._sendToRadio(mesh_pb2.ToRadio(xmodemPacket=init_pkt))
            ack = _wait_for_response(response_q, _ACK_TIMEOUT_INIT_S)
            if ack.control != XMC.Value("ACK"):
                raise FixtureError(
                    f"device refused filename {_DEFAULT_NODES_FILENAME!r} "
                    f"(got control={ack.control}, expected ACK). "
                    f"Filesystem full or permissions issue?"
                )

            # 2) Stream the payload in 128 B chunks.
            for offset in range(0, total_bytes, _XMODEM_CHUNK):
                chunk = payload[offset : offset + _XMODEM_CHUNK]
                if len(chunk) < _XMODEM_CHUNK:
                    # Pad final chunk to 128 B with SUB. The trailing 0x1A bytes
                    # become part of the file on-device, but nanopb ignores
                    # bytes past the end of the top-level message.
                    chunk = chunk + bytes([_XMODEM_SUB] * (_XMODEM_CHUNK - len(chunk)))
                seq = ((offset // _XMODEM_CHUNK) + 1) % 256
                # Retry loop on NAK / timeout.
                attempts = 0
                while True:
                    pkt = xmodem_pb2.XModem(
                        control=XMC.Value("SOH"),
                        seq=seq,
                        buffer=chunk,
                        crc16=_crc16_ccitt(chunk),
                    )
                    iface._sendToRadio(mesh_pb2.ToRadio(xmodemPacket=pkt))
                    ack = _wait_for_response(response_q, _ACK_TIMEOUT_CHUNK_S)
                    if ack.control == XMC.Value("ACK"):
                        chunks_sent += 1
                        break
                    if ack.control == XMC.Value("NAK"):
                        attempts += 1
                        retried += 1
                        if attempts >= _MAX_CHUNK_RETRIES:
                            # Abort: send CAN so the firmware removes the half-
                            # written file via FSCom.remove(filename).
                            iface._sendToRadio(
                                mesh_pb2.ToRadio(
                                    xmodemPacket=xmodem_pb2.XModem(
                                        control=XMC.Value("CAN")
                                    )
                                )
                            )
                            raise FixtureError(
                                f"chunk seq={seq} NAK'd {attempts} times; "
                                f"aborted transfer (file removed on-device)."
                            )
                        continue  # retry the same chunk
                    raise FixtureError(
                        f"unexpected XModem control={ack.control} on seq={seq}"
                    )

            # 3) Tell the device we're done.
            iface._sendToRadio(
                mesh_pb2.ToRadio(
                    xmodemPacket=xmodem_pb2.XModem(control=XMC.Value("EOT"))
                )
            )
            ack = _wait_for_response(response_q, _ACK_TIMEOUT_CHUNK_S)
            if ack.control != XMC.Value("ACK"):
                raise FixtureError(f"EOT not ACKed (got control={ack.control})")

            # 4) Reboot so loadFromDisk picks up the new file.
            if reboot_after:
                iface.localNode.reboot(secs=1)
                rebooted = True
    finally:
        try:
            pub.unsubscribe(_on_xmodem, "meshtastic.xmodempacket")
        except Exception:
            pass

    return {
        "transport": "hardware",
        "port": port,
        "filename_on_device": _DEFAULT_NODES_FILENAME,
        "bytes": total_bytes,
        "chunks_sent": chunks_sent,
        "retried": retried,
        "sha256": sha256,
        "jsonl_source": str(jsonl),
        "rebooted": rebooted,
    }


# ---------------------------------------------------------------------------
# Public entry point — registered as an MCP tool in server.py.
# ---------------------------------------------------------------------------
def push_fake_nodedb(
    size: int,
    target: Literal["portduino", "hardware"] = "portduino",
    *,
    port: str | None = None,
    portduino_config: str = "default",
    backup_existing: bool = True,
    confirm: bool = False,
    reboot_after: bool = True,
    custom_seed_jsonl: str | None = None,
) -> dict[str, Any]:
    """Compile a fresh-timestamp NodeDatabase fixture and push it to a device.

    Args:
      size: 250, 500, 1000, or 2000 — selects which committed seed JSONL to use.
      target: "portduino" (file copy to ~/.portduino/<config>/prefs/) or
              "hardware" (XModem upload to /prefs/nodes.proto + reboot).
      port: required for target="hardware". Serial path (e.g. /dev/cu.usbmodemXXXX)
            or BLE identifier. TCP endpoints are rejected — use target="portduino"
            instead.
      portduino_config: which Portduino instance dir under ~/.portduino/. Default "default".
      backup_existing: portduino only. Move nodes.proto -> nodes.proto.bak.<ts>
                        if present, so you can roll back.
      confirm: required True for target="hardware" (writes flash + reboots).
      reboot_after: hardware only. If True, send a 1-second reboot after the
                    final ACK so loadFromDisk picks up the new file at next boot.
      custom_seed_jsonl: override the committed JSONL. Use to push a hand-edited
                         test scenario.

    Returns:
        dict with transport, bytes, sha256, etc. — depends on target.

    """
    if size not in _VALID_SIZES:
        raise FixtureError(
            f"size must be one of {_VALID_SIZES}; got {size!r}. "
            f"Add a new committed seed if you need a different cardinality."
        )

    jsonl = _resolve_seed_jsonl(size, custom_seed_jsonl)

    if target == "portduino":
        return _push_portduino(size, jsonl, portduino_config, backup_existing)

    if target == "hardware":
        if not confirm:
            raise FixtureError(
                "hardware push writes flash and triggers a reboot — pass confirm=True."
            )
        if not port:
            raise FixtureError(
                "target='hardware' requires a port (e.g. /dev/cu.usbmodemXXXX)."
            )
        return _push_hardware(size, jsonl, port, reboot_after)

    raise FixtureError(f"unknown target {target!r}; expected 'portduino' or 'hardware'")
