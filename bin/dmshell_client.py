#!/usr/bin/env python3

import argparse
import os
import queue
import random
import shutil
import socket
import select
import subprocess
import sys
import tempfile
import termios
import threading
import time
import tty
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, TextIO


START1 = 0x94
START2 = 0xC3
HEADER_LEN = 4
DEFAULT_API_PORT = 4403
# Zero, and this is about airtime rather than reachability. A non-zero hop limit arms two
# mechanisms in the firmware that DMShell wants nothing from. NextHopRouter::sendWithNextHop starts
# its own relay retransmissions for the packet - its gate is (hop_limit > 0 || want_ack) with a
# known next hop - and those repeats reuse the packet id, so the far end discards them in
# shouldFilterReceived as dupes after paying for them on the air. Then, to stop them,
# ReliableRouter::sniffReceived answers every single frame with a 0-hop routing ACK, which is an
# extra transmission from the peer in the same direction as its own shell output, on a half-duplex
# channel. Neither is visible to this protocol: DMShell carries its own sequence numbers and
# cursors, and sets want_ack = False for exactly that reason.
#
# Zero is also symmetric with the firmware, which already sends its side of the session with
# hop_limit = 0, so DMShell is a direct-neighbour protocol in both directions either way. It
# survives the receiver's pre-hop drop policy because hop_start == 0 is accepted when the Data
# bitfield is present (classifyHopStart in NodeDB.cpp) and perhapsEncode sets that on every
# locally-originated packet - which is what makes the firmware's own hop_limit = 0 frames arrive
# here. Raise it with --hop-limit only to restore the old behaviour for a comparison.
DEFAULT_HOP_LIMIT = 0
LOCAL_ESCAPE_BYTE = b"\x1d"  # Ctrl+]
# The floor, and the bootstrap before a single round trip has been observed. It is not the interval:
# a flat 1 s is shorter than one frame's time on air at LongFast (~2.2 s), so repeating on it queues
# a second copy of a frame that is still being transmitted, and a third behind that. The live
# interval is derived from the measured acknowledgement latency instead - see
# _retransmit_base_interval_locked.
MISSING_SEQ_RETRY_INTERVAL_SEC = 1.0
# A gap the peer cannot fill used to be retried at a flat 1/sec until the 5-minute idle timeout, so
# the retry needs *some* bound. But measured on hardware, every repeat within the first few attempts
# was a collision on a gap that did fill (8 of 8 recovered, worst case 5 attempts), and backing off
# there only delays a recovery that was going to work - visibly, in an interactive shell. So hold the
# base interval for the first few attempts and only grow once "unfillable" is the better hypothesis.
# The ceiling is low because the firmware now reports a genuinely evicted frame in one round trip;
# this only has to bound the residue (a peer without that fix, or a lost teardown).
MISSING_SEQ_RETRY_FLAT_ATTEMPTS = 4
MISSING_SEQ_RETRY_MAX_SEC = 8.0
# How much of a new acknowledgement-latency sample to believe. Low, because the quantity we want is
# the preset's frame time, which does not change within a session, while any single sample also
# carries that frame's queueing and its collisions.
ACK_LATENCY_SMOOTHING = 0.25
# The interval is this multiple of the estimate: one frame out and one acknowledgement back are
# already in the estimate, so the margin is for the peer's own queue.
RETRANSMIT_LATENCY_MULTIPLIER = 2.0
INPUT_BATCH_WINDOW_SEC = .5
INPUT_BATCH_MAX_BYTES = 64
HEARTBEAT_IDLE_DELAY_SEC = 5.0
HEARTBEAT_REPEAT_SEC = 15.0
HEARTBEAT_POLL_INTERVAL_SEC = 0.25
# The server bounds how much unacknowledged output it keeps in flight, so it needs our receive cursor
# to make progress. On a one-way stream we would otherwise transmit nothing at all - the heartbeat is
# suppressed while inbound traffic keeps arriving - so the stream would advance one window per
# heartbeat instead of continuously. Every frame we send already carries ack_seq, so this only has to
# fire when we have nothing else to say.
#
# Must not exceed the server's window (DMSHELL_TX_WINDOW, default 4), or it stalls: the server blocks
# with its window full while we are still waiting to accumulate frames, and only the heartbeat breaks
# the deadlock.


def _ack_after_frames() -> int:
    raw = os.environ.get("DMSHELL_ACK_EVERY", "")
    if not raw:
        return 2
    try:
        return max(1, int(raw))
    except ValueError:
        print(f"[dmshell] ignoring unparseable DMSHELL_ACK_EVERY={raw!r}", file=sys.stderr)
        return 2


ACK_AFTER_FRAMES = _ack_after_frames()


def _input_window_frames() -> int:
    raw = os.environ.get("DMSHELL_INPUT_WINDOW", "")
    if not raw:
        return 4
    try:
        value = int(raw)
    except ValueError:
        print(f"[dmshell] ignoring unparseable DMSHELL_INPUT_WINDOW={raw!r}", file=sys.stderr)
        return 4
    if value < 0 or value > 50:
        print(f"[dmshell] DMSHELL_INPUT_WINDOW={raw!r} outside 0-50, using 4", file=sys.stderr)
        return 4
    return value


# The mirror of the server's DMSHELL_TX_WINDOW, and the other half of the same failure. The server
# bounds its output; nothing bounded our input, so at LongFast - where we emit keystrokes far faster
# than the server drains them - our own 50-frame replay ring was outrun and the session died with
# replay_evicted, exactly as the server's did at ShortTurbo before it was bounded. 0 restores the
# unbounded behaviour for measuring one build both ways.
INPUT_WINDOW_FRAMES = _input_window_frames()
# Bytes of typed input held locally while the window is shut. The keystrokes are the user's, so
# dropping them is not an option; this is the backpressure, and the shape of the local terminal's own
# tty buffer. Generous, because the frames it holds are small and the alternative is losing input.
PENDING_INPUT_MAX_BYTES = 4096
# Consecutive retransmissions of one input frame before we give up on the server, matching the
# firmware's bound. The interval escalates the way a replay re-ask does, because unlike the firmware
# we cannot read the modem config: that puts the allowance at roughly 1 s x 4 then doubling to an 8 s
# ceiling, so 25 repeats span about two and a half minutes.
MAX_INPUT_RETRANSMITS = 25
# A lost OPEN or OPEN_OK costs a full frame airtime, which is ~2.2 s on LongFast against ~0.1 s on
# ShortTurbo, so the handshake needs far more headroom than the API socket does. Measured: a LongFast
# session needed 150 s to open.
DEFAULT_OPEN_TIMEOUT_SEC = 180.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Tiny DMShell client for Meshtastic native TCP API",
        epilog=(
            "Examples:\n"
            "  bin/dmshell_client.py --to !170896f7\n"
            "  bin/dmshell_client.py --to 0x170896f7 --command 'uname -a' --command 'id'"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--host", default="127.0.0.1", help="meshtasticd API host")
    parser.add_argument("--port", type=int, default=DEFAULT_API_PORT, help="meshtasticd API port")
    parser.add_argument(
        "--serial",
        nargs="?",
        const="auto",
        default=None,
        help="use USB serial transport (optionally provide device path, default: auto-detect)",
    )
    parser.add_argument("--baud", type=int, default=115200, help="serial baud rate when using --serial")
    parser.add_argument("--to", required=True, help="destination node number, e.g. !170896f7 or 0x170896f7")
    parser.add_argument("--channel", type=int, default=0, help="channel index to use")
    parser.add_argument(
        "--hop-limit",
        type=int,
        default=DEFAULT_HOP_LIMIT,
        help="hop limit (default %(default)s; non-zero arms the firmware's relay retransmissions "
        "and makes the peer routing-ACK every frame)",
    )
    parser.add_argument("--cols", type=int, default=None, help="initial terminal columns (default: detect local terminal)")
    parser.add_argument("--rows", type=int, default=None, help="initial terminal rows (default: detect local terminal)")
    parser.add_argument("--command", action="append", default=[], help="send a command line after opening")
    parser.add_argument("--close-after", type=float, default=2.0, help="seconds to wait before closing in command mode")
    parser.add_argument("--timeout", type=float, default=10.0, help="seconds to wait for API/session events")
    parser.add_argument(
        "--open-timeout",
        type=float,
        default=DEFAULT_OPEN_TIMEOUT_SEC,
        help="seconds to wait for OPEN_OK (needs to be generous on slow presets; default %(default)s)",
    )
    parser.add_argument("--verbose", action="store_true", help="print extra protocol events")
    parser.add_argument(
        "--legacy-recovery",
        action="store_true",
        help="retry a missing sequence number at a flat 1/sec instead of backing off "
        "(pair with DMSHELL_LEGACY_RECOVERY=1 on the node to measure the pre-fix behaviour)",
    )
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def load_proto_modules() -> object:
    try:
        import google.protobuf  # noqa: F401
    except ImportError as exc:
        raise SystemExit("python package 'protobuf' is required to run this client") from exc

    protoc = shutil.which("protoc")
    if not protoc:
        raise SystemExit("'protoc' is required to generate temporary Python protobuf bindings")

    out_dir = Path(tempfile.mkdtemp(prefix="meshtastic_dmshell_proto_"))
    proto_dir = repo_root() / "protobufs"
    
    # Compile all required protos for DMShell client (mesh and dependencies)
    # Excludes nanopb.proto and other complex build artifacts
    required_protos = [
        "mesh.proto",
        "channel.proto", 
        "config.proto",
        # config.proto imports this. protoc resolves it via -I for compilation but only emits a
        # _pb2 module for files named on the command line, so without it config_pb2 imports a
        # module that was never generated and the client cannot start.
        "field_metadata.proto",
        "device_ui.proto",
        "module_config.proto",
        "atak.proto",
        "portnums.proto",
        "telemetry.proto",
        "xmodem.proto",
    ]
    proto_files = [proto_dir / "meshtastic" / name for name in required_protos]
    for pf in proto_files:
        if not pf.exists():
            raise SystemExit(f"could not find required proto file: {pf}")

    # Create __init__.py to make meshtastic a package
    (out_dir / "meshtastic").mkdir(exist_ok=True)
    (out_dir / "meshtastic" / "__init__.py").touch()

    # Build protoc command with just the meshtastic proto directory as include path
    # protoc will use its built-in includes for standard google protobuf types
    cmd = [protoc, f"-I{proto_dir}", f"--python_out={out_dir}", *[str(path) for path in proto_files]]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"protoc stderr: {result.stderr}", file=sys.stderr)
        print(f"protoc stdout: {result.stdout}", file=sys.stderr)
        print(f"protoc command: {' '.join(cmd)}", file=sys.stderr)
        raise SystemExit(f"protoc failed with return code {result.returncode}")

    # Create _pb2_grpc module stub if not present (protoc 3.20+)
    mesh_pb2_file = out_dir / "meshtastic" / "mesh_pb2.py"
    if not mesh_pb2_file.exists():
        raise SystemExit(f"protoc did not generate mesh_pb2.py in {out_dir / 'meshtastic'}")

    sys.path.insert(0, str(out_dir))
    try:
        from meshtastic import mesh_pb2, portnums_pb2  # type: ignore
    except ImportError as exc:
        print(f"Failed to import protobuf modules. Output dir contents:", file=sys.stderr)
        for item in (out_dir / "meshtastic").iterdir():
            print(f"  {item.name}", file=sys.stderr)
        raise SystemExit(f"could not import meshtastic proto modules: {exc}") from exc

    # Return an object that has both modules accessible
    class ProtoModules:
        pass
    
    pb2 = ProtoModules()
    pb2.mesh = mesh_pb2
    pb2.portnums = portnums_pb2
    return pb2


def parse_node_num(raw: str) -> int:
    value = raw.strip()
    if value.startswith("!"):
        value = value[1:]
    if value.lower().startswith("0x"):
        return int(value, 16)
    if any(ch in "abcdefABCDEF" for ch in value):
        return int(value, 16)
    return int(value, 10)


class SerialTransport:
    def __init__(self, serial_obj):
        self._serial = serial_obj

    def recv(self, length: int) -> bytes:
        return self._serial.read(length)

    def sendall(self, data: bytes) -> None:
        self._serial.write(data)
        self._serial.flush()

    def close(self) -> None:
        self._serial.close()


def detect_meshtastic_serial_port() -> str:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit("python package 'pyserial' is required for --serial mode") from exc

    ports = list(list_ports.comports())
    if not ports:
        raise SystemExit("no serial ports found for --serial mode")

    scored: list[tuple[int, str]] = []
    for port in ports:
        text = " ".join(
            filter(
                None,
                [port.device, port.description, port.manufacturer, port.product, port.hwid],
            )
        ).lower()
        score = 0
        if "meshtastic" in text:
            score += 100
        if "lora" in text or "mesh" in text:
            score += 10
        if "ttyacm" in (port.device or "").lower() or "ttyusb" in (port.device or "").lower():
            score += 1
        scored.append((score, port.device))

    scored.sort(reverse=True)
    best_score, best_device = scored[0]
    if best_score <= 0 and len(scored) > 1:
        raise SystemExit(
            "could not confidently auto-detect a Meshtastic serial port; pass --serial /dev/ttyXXX explicitly"
        )
    return best_device


def open_transport(args: argparse.Namespace):
    if args.serial is None:
        sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
        sock.settimeout(None)
        return sock

    serial_path = args.serial
    if serial_path == "auto":
        serial_path = detect_meshtastic_serial_port()
        print(f"[dmshell] using serial port {serial_path}", file=sys.stderr)

    try:
        import serial
    except ImportError as exc:
        raise SystemExit("python package 'pyserial' is required for --serial mode") from exc

    try:
        serial_obj = serial.Serial(serial_path, baudrate=args.baud, timeout=None, write_timeout=2)
    except Exception as exc:
        raise SystemExit(f"failed to open serial device {serial_path}: {exc}") from exc

    return SerialTransport(serial_obj)


def recv_exact(transport, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        piece = transport.recv(length - len(chunks))
        if not piece:
            raise ConnectionError("connection closed by transport")
        chunks.extend(piece)
    return bytes(chunks)


def detect_local_terminal_size() -> tuple[int, int]:
    size = shutil.get_terminal_size(fallback=(100, 40))
    cols = max(1, int(size.columns))
    rows = max(1, int(size.lines))
    return cols, rows


def resolve_initial_terminal_size(cols_override: Optional[int], rows_override: Optional[int]) -> tuple[int, int]:
    detected_cols, detected_rows = detect_local_terminal_size()
    cols = detected_cols if cols_override is None else max(1, cols_override)
    rows = detected_rows if rows_override is None else max(1, rows_override)
    return cols, rows


def recv_stream_frame(transport) -> bytes:
    while True:
        start = recv_exact(transport, 1)[0]
        if start != START1:
            continue
        if recv_exact(transport, 1)[0] != START2:
            continue
        header = recv_exact(transport, 2)
        length = (header[0] << 8) | header[1]
        return recv_exact(transport, length)


def send_stream_frame(transport, payload: bytes) -> None:
    if len(payload) > 0xFFFF:
        raise ValueError("payload too large for stream API")
    header = bytes((START1, START2, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF))
    transport.sendall(header + payload)


@dataclass
class SentShellFrame:
    op: int
    session_id: int
    seq: int
    ack_seq: int
    payload: bytes = b""
    cols: int = 0
    rows: int = 0
    flags: int = 0
    last_tx_seq: int = 0
    last_rx_seq: int = 0
    # When this frame was last handed to the API, retransmissions included. Read for two things: no
    # frame is repeated before it has been outstanding for a full interval, and the interval itself
    # is measured from how long acknowledgement actually takes on this link.
    sent_time: float = 0.0


@dataclass
class SessionState:
    pb2: object  # ProtoModules with mesh and portnums attributes
    target: int
    channel: int
    verbose: bool
    hop_limit: int = DEFAULT_HOP_LIMIT
    session_id: int = field(default_factory=lambda: random.randint(1, 0x7FFFFFFF))
    next_seq: int = 1
    last_rx_seq: int = 0
    next_expected_rx_seq: int = 1
    highest_seen_rx_seq: int = 0
    active: bool = False
    stopped: bool = False
    opened_event: threading.Event = field(default_factory=threading.Event)
    closed_event: threading.Event = field(default_factory=threading.Event)
    event_queue: "queue.Queue[str]" = field(default_factory=queue.Queue)
    tx_lock: threading.Lock = field(default_factory=threading.Lock)
    socket_lock: threading.Lock = field(default_factory=threading.Lock)
    tx_history: deque[SentShellFrame] = field(default_factory=lambda: deque(maxlen=50))
    pending_rx_frames: dict[int, object] = field(default_factory=dict)
    last_requested_missing_seq: int = 0
    last_missing_request_time: float = 0.0
    missing_request_interval: float = MISSING_SEQ_RETRY_INTERVAL_SEC
    missing_request_attempts: int = 0
    frames_since_outbound: int = 0
    legacy_recovery: bool = False
    requested_missing_seqs: set[int] = field(default_factory=set)
    replay_log_lock: threading.Lock = field(default_factory=threading.Lock)
    replay_log_file: Optional[TextIO] = None
    replay_log_path: Optional[Path] = None
    last_transport_activity_time: float = field(default_factory=time.monotonic)
    # Inbound only. The heartbeat has to key off the peer's silence, not off any traffic: our own
    # keystrokes used to suppress it (see heartbeat_due).
    last_inbound_time: float = field(default_factory=time.monotonic)
    last_heartbeat_sent_time: float = 0.0
    # Outbound flow control, mirroring the firmware's txWindow. peer_acked_tx_seq is the server's
    # cumulative receive cursor over our frames; see note_peer_receive_cursor.
    input_window_frames: int = INPUT_WINDOW_FRAMES
    peer_acked_tx_seq: int = 0
    input_window_blocked: bool = False
    # Held across taking bytes off pending_input and transmitting them. The input thread and the
    # heartbeat thread both flush, and two takes that interleave with two sends would put the user's
    # keystrokes on the wire out of order.
    input_lock: threading.Lock = field(default_factory=threading.Lock)
    pending_input: bytearray = field(default_factory=bytearray)
    pending_input_dropped: int = 0
    next_input_retransmit_time: float = 0.0
    input_retransmit_interval: float = MISSING_SEQ_RETRY_INTERVAL_SEC
    input_retransmit_seq: int = 0
    input_retransmits: int = 0
    # Smoothed time from sending a frame to seeing the peer's cursor pass it. None until the first
    # sample, which the OPEN/OPEN_OK exchange supplies before any input is sent.
    ack_latency: Optional[float] = None

    def alloc_seq(self) -> int:
        with self.tx_lock:
            value = self.next_seq
            self.next_seq += 1
            return value

    def current_ack_seq(self) -> int:
        with self.tx_lock:
            return self.last_rx_seq

    def highest_sent_seq(self) -> int:
        with self.tx_lock:
            return max(0, self.next_seq - 1)

    def note_outbound_packet(self, heartbeat: bool = False) -> None:
        with self.tx_lock:
            now = time.monotonic()
            # Anything we send carries our receive cursor in ack_seq, so it settles the flow-control
            # debt whatever its opcode.
            self.frames_since_outbound = 0
            if heartbeat:
                self.last_heartbeat_sent_time = now
            else:
                self.last_transport_activity_time = now

    def note_inbound_packet(self) -> None:
        with self.tx_lock:
            now = time.monotonic()
            self.last_transport_activity_time = now
            self.last_inbound_time = now

    def heartbeat_due(self) -> bool:
        """Whether to probe a peer that has gone quiet.

        Keyed on inbound silence alone. It used to key on traffic in either direction, so our own
        keystrokes reset it - and typing faster than HEARTBEAT_IDLE_DELAY_SEC suppressed the heartbeat
        entirely. That mattered because the server's PING handler is what retransmits a frame we are
        missing, so an interactive user typing into a stalled session silenced their own recovery. A
        healthy stream still sends no heartbeats, because inbound traffic keeps arriving.
        """
        with self.tx_lock:
            now = time.monotonic()
            if (now - self.last_inbound_time) < HEARTBEAT_IDLE_DELAY_SEC:
                return False
            if self.last_heartbeat_sent_time <= self.last_inbound_time:
                return True
            return (now - self.last_heartbeat_sent_time) >= HEARTBEAT_REPEAT_SEC

    def note_peer_receive_cursor(self, ack_seq: int, last_rx_seq: int) -> None:
        """Take the server's cumulative cursor over our frames from any inbound frame.

        Both fields mean "the highest sequence number I have in order": the server sets ack_seq on
        every frame it originates and last_rx_seq explicitly when it asks for a replay, so the larger
        is the cursor. Clamped to what we have actually sent, so a confused peer cannot grant credit
        past our own progress, and monotone, so a replay carrying a stale cursor cannot close the
        window again. The mirror of the firmware's notePeerReceiveCursor().
        """
        cursor = max(ack_seq, last_rx_seq)
        with self.tx_lock:
            highest_sent = max(0, self.next_seq - 1)
            cursor = min(cursor, highest_sent)
            if cursor <= self.peer_acked_tx_seq:
                return
            self._sample_ack_latency_locked(cursor)
            self.peer_acked_tx_seq = cursor
            # Progress proves the server is alive, so the run that would eventually declare it gone
            # starts over, and the next stall gets a fresh interval. Clearing the deadline does not
            # make the next frame instantly repeatable: input_retransmit_due re-arms it from that
            # frame's own send time.
            self.next_input_retransmit_time = 0.0
            self.input_retransmit_interval = self._retransmit_base_interval_locked()
            self.input_retransmit_seq = 0
            self.input_retransmits = 0

    def _sent_time_locked(self, seq: int) -> Optional[float]:
        """When we last transmitted seq, or None if the ring no longer holds it. Caller holds tx_lock."""
        for frame in reversed(self.tx_history):
            if frame.seq == seq:
                return frame.sent_time or None
        return None

    def _sample_ack_latency_locked(self, cursor: int) -> None:
        """Fold the round trip for the frame the cursor just reached into the estimate.

        The sample is the whole path we have to wait out before repeating anything: our frame's time
        on air, the peer's processing, and its reply's time on air. Measuring it beats deriving it,
        because this side cannot see the modem preset and the derivation would have to guess at
        queueing. The first sample comes from OPEN/OPEN_OK, which is why an estimate exists before
        any keystroke is sent. Caller holds tx_lock.
        """
        sent_at = self._sent_time_locked(cursor)
        if sent_at is None:
            return
        sample = time.monotonic() - sent_at
        if sample <= 0:
            return
        if self.ack_latency is None:
            self.ack_latency = sample
        else:
            self.ack_latency += ACK_LATENCY_SMOOTHING * (sample - self.ack_latency)

    def _retransmit_base_interval_locked(self) -> float:
        """Give the first interval to wait before repeating anything. Caller holds tx_lock."""
        if self.ack_latency is None:
            return MISSING_SEQ_RETRY_INTERVAL_SEC
        scaled = RETRANSMIT_LATENCY_MULTIPLIER * self.ack_latency
        return min(max(scaled, MISSING_SEQ_RETRY_INTERVAL_SEC), MISSING_SEQ_RETRY_MAX_SEC)

    def note_frame_resent(self, seq: int) -> None:
        """Restamp a frame we have just put back on the wire, so the next wait is measured from it."""
        now = time.monotonic()
        with self.tx_lock:
            for frame in reversed(self.tx_history):
                if frame.seq == seq:
                    frame.sent_time = now
                    return

    def outstanding_input(self) -> int:
        with self.tx_lock:
            return max(0, self.next_seq - 1) - self.peer_acked_tx_seq

    def input_window_open(self) -> bool:
        with self.tx_lock:
            if self.input_window_frames == 0:
                return True
            return (max(0, self.next_seq - 1) - self.peer_acked_tx_seq) < self.input_window_frames

    def queue_input(self, data: bytes) -> int:
        """Hold typed input until the window reopens. Returns how many bytes had to be dropped."""
        with self.tx_lock:
            room = PENDING_INPUT_MAX_BYTES - len(self.pending_input)
            if room <= 0:
                self.pending_input_dropped += len(data)
                return len(data)
            kept = data[:room]
            dropped = len(data) - len(kept)
            self.pending_input.extend(kept)
            self.pending_input_dropped += dropped
            return dropped

    def take_queued_input(self, limit: int) -> bytes:
        with self.tx_lock:
            if not self.pending_input:
                return b""
            chunk = bytes(self.pending_input[:limit])
            del self.pending_input[:len(chunk)]
            return chunk

    def has_queued_input(self) -> bool:
        with self.tx_lock:
            return bool(self.pending_input)

    def input_retransmit_due(self) -> tuple[Optional[int], bool]:
        """The input frame to repeat while the window is shut, and whether the allowance is spent.

        Symmetric to the firmware's retransmitOldestUnacked, and needed for the same reason: with our
        window full the server never sees a sequence number above the gap, so it never asks for the
        replay that would reopen us. Its only trigger is an arriving frame, and we have stopped
        sending. Without this the session latches silently.
        """
        with self.tx_lock:
            if self.input_window_frames == 0 or self.legacy_recovery:
                return (None, False)
            highest_sent = max(0, self.next_seq - 1)
            missing = self.peer_acked_tx_seq + 1
            if missing > highest_sent:
                return (None, False)
            now = time.monotonic()
            if now < self.next_input_retransmit_time:
                return (None, False)
            if missing != self.input_retransmit_seq:
                self.input_retransmit_seq = missing
                self.input_retransmits = 0
                self.input_retransmit_interval = self._retransmit_base_interval_locked()
                # The deadline for a sequence number we have not repeated yet belongs to the frame,
                # not to the tick that noticed it. Without this, the cursor advancing clears the
                # deadline and the very next frame - which may still be on the air - is repeated on
                # the following poll, which is how one keystroke turned into several copies.
                sent_at = self._sent_time_locked(missing)
                if sent_at is not None and now < sent_at + self.input_retransmit_interval:
                    self.next_input_retransmit_time = sent_at + self.input_retransmit_interval
                    return (None, False)
            if self.input_retransmits >= MAX_INPUT_RETRANSMITS:
                return (missing, True)
            self.input_retransmits += 1
            if self.input_retransmits > MISSING_SEQ_RETRY_FLAT_ATTEMPTS:
                self.input_retransmit_interval = min(self.input_retransmit_interval * 2, MISSING_SEQ_RETRY_MAX_SEC)
            self.next_input_retransmit_time = now + self.input_retransmit_interval
            return (missing, False)

    def note_peer_reported_tx_seq(self, seq: int) -> None:
        with self.tx_lock:
            if seq > self.highest_seen_rx_seq:
                self.highest_seen_rx_seq = seq

    def note_received_seq(self, seq: int) -> tuple[str, Optional[int]]:
        with self.tx_lock:
            if seq == 0:
                return ("process", None)
            if seq < self.next_expected_rx_seq:
                if self.highest_seen_rx_seq >= self.next_expected_rx_seq:
                    return ("gap", self.next_expected_rx_seq)
                return ("duplicate", None)
            if seq > self.next_expected_rx_seq:
                if seq > self.highest_seen_rx_seq:
                    self.highest_seen_rx_seq = seq
                return ("gap", self.next_expected_rx_seq)
            self.last_rx_seq = seq
            self.next_expected_rx_seq = seq + 1
            if self.last_requested_missing_seq != 0 and self.next_expected_rx_seq > self.last_requested_missing_seq:
                self.last_requested_missing_seq = 0
                self.missing_request_interval = self._retransmit_base_interval_locked()
                self.missing_request_attempts = 0
            if seq > self.highest_seen_rx_seq:
                self.highest_seen_rx_seq = seq
            if self.highest_seen_rx_seq < self.next_expected_rx_seq:
                self.highest_seen_rx_seq = 0
            # Only an in-order frame moves the cursor the server is waiting on, so only this case
            # creates a flow-control debt.
            self.frames_since_outbound += 1
            return ("process", None)

    def remember_out_of_order_frame(self, shell) -> None:
        with self.tx_lock:
            if shell.seq <= self.next_expected_rx_seq:
                return
            if shell.seq not in self.pending_rx_frames:
                self.pending_rx_frames[shell.seq] = shell
            if shell.seq > self.highest_seen_rx_seq:
                self.highest_seen_rx_seq = shell.seq

    def pop_next_buffered_frame(self):
        with self.tx_lock:
            return self.pending_rx_frames.pop(self.next_expected_rx_seq, None)

    def pending_missing_seq(self) -> Optional[int]:
        with self.tx_lock:
            if self.highest_seen_rx_seq >= self.next_expected_rx_seq:
                return self.next_expected_rx_seq
            return None

    def request_missing_seq_once(self) -> Optional[int]:
        with self.tx_lock:
            if self.highest_seen_rx_seq < self.next_expected_rx_seq:
                return None
            now = time.monotonic()
            same_seq = self.last_requested_missing_seq == self.next_expected_rx_seq
            if same_seq and (now - self.last_missing_request_time) < self.missing_request_interval:
                return None
            if same_seq and not self.legacy_recovery:
                self.missing_request_attempts += 1
                if self.missing_request_attempts > MISSING_SEQ_RETRY_FLAT_ATTEMPTS:
                    self.missing_request_interval = min(self.missing_request_interval * 2, MISSING_SEQ_RETRY_MAX_SEC)
            else:
                self.missing_request_attempts = 1
                self.missing_request_interval = self._retransmit_base_interval_locked()
            self.last_requested_missing_seq = self.next_expected_rx_seq
            self.last_missing_request_time = now
            return self.last_requested_missing_seq

    def flow_control_ack_due(self) -> bool:
        """Whether the server is likely waiting on our receive cursor to reopen its send window."""
        with self.tx_lock:
            return self.frames_since_outbound >= ACK_AFTER_FRAMES

    def set_receive_cursor(self, seq: int) -> None:
        with self.tx_lock:
            self.last_rx_seq = seq
            self.next_expected_rx_seq = seq + 1
            self.highest_seen_rx_seq = seq

    def open_replay_log(self, session_id: int) -> None:
        with self.replay_log_lock:
            if self.replay_log_file is not None:
                return
            path = Path.cwd() / f"{session_id:08x}.log"
            self.replay_log_file = path.open("a", encoding="utf-8")
            self.replay_log_path = path
            self.replay_log_file.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} session_open session=0x{session_id:08x}\n")
            self.replay_log_file.flush()

    def log_replay_event(self, event: str, seq: int, detail: str = "") -> None:
        with self.replay_log_lock:
            if self.replay_log_file is None:
                return
            extra = f" {detail}" if detail else ""
            self.replay_log_file.write(
                f"{time.strftime('%Y-%m-%d %H:%M:%S')} {event} seq={seq}{extra}\n"
            )
            self.replay_log_file.flush()

    def note_missing_seq_requested(self, seq: int, reason: str) -> None:
        with self.tx_lock:
            self.requested_missing_seqs.add(seq)
        self.log_replay_event("missing_requested", seq, f"reason={reason}")

    def note_replayed_seq_received(self, seq: int) -> None:
        with self.tx_lock:
            was_requested = seq in self.requested_missing_seqs
            if was_requested:
                self.requested_missing_seqs.remove(seq)
        if was_requested:
            self.log_replay_event("replay_received", seq)

    def close_replay_log(self) -> None:
        with self.replay_log_lock:
            if self.replay_log_file is None:
                return
            self.replay_log_file.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} session_close\n")
            self.replay_log_file.flush()
            self.replay_log_file.close()
            self.replay_log_file = None

    def remember_sent_frame(self, frame: SentShellFrame) -> None:
        if frame.seq == 0 or frame.op == self.pb2.mesh.RemoteShell.ACK:
            return
        with self.tx_lock:
            self.tx_history.append(frame)

    def prune_sent_frames(self, ack_seq: int) -> None:
        if ack_seq <= 0:
            return
        with self.tx_lock:
            self.tx_history = deque((frame for frame in self.tx_history if frame.seq > ack_seq), maxlen=50)

    def replay_frames_from(self, start_seq: int) -> list[SentShellFrame]:
        with self.tx_lock:
            return [frame for frame in self.tx_history if frame.seq >= start_seq]

    def oldest_retained_tx_seq(self) -> Optional[int]:
        """Lowest sequence number the replay ring still holds, or None while it is empty.

        tx_history is appended in send order and sequence numbers are allocated monotonically, so
        the leftmost entry is the oldest. Note prune_sent_frames() has no live call site, which
        makes this a flat last-50-sent ring - the same shape as the firmware's txHistory, and
        outrun the same way.
        """
        with self.tx_lock:
            for frame in self.tx_history:
                return frame.seq
            return None


def send_toradio(transport, toradio) -> None:
    send_stream_frame(transport, toradio.SerializeToString())


def make_toradio_packet(pb2, state: SessionState, shell_msg) -> object:
    packet = pb2.mesh.MeshPacket()
    packet.id = random.randint(1, 0x7FFFFFFF)
    packet.to = state.target
    # The 'from' field is a reserved keyword in Python, so use setattr
    setattr(packet, "from", 0)
    packet.channel = state.channel
    packet.hop_limit = state.hop_limit
    packet.want_ack = False
    packet.decoded.portnum = pb2.portnums.REMOTE_SHELL_APP
    packet.decoded.payload = shell_msg.SerializeToString()
    packet.decoded.want_response = False
    packet.decoded.dest = state.target
    packet.decoded.source = 0
    # Mark has_bitfield (proto3 optional -> assigning even 0 sets presence) and leave OK_TO_MQTT clear,
    # which is right for a private shell. Note: this is NOT what prevents the far-end pre-hop drop --
    # that check runs before decryption and can't read the (encrypted) bitfield; a non-zero hop_limit
    # is what keeps us alive there. The firmware also sets this for locally-originated packets, so this
    # is mostly belt-and-suspenders for the post-decryption consumers (e.g. getHopsAway).
    packet.decoded.bitfield = 0

    toradio = pb2.mesh.ToRadio()
    toradio.packet.CopyFrom(packet)
    return toradio


def send_shell_frame(
    transport,
    state: SessionState,
    op: int,
    payload: bytes = b"",
    cols: int = 0,
    rows: int = 0,
    session_id: Optional[int] = None,
    ack_seq: Optional[int] = None,
    seq: Optional[int] = None,
    flags: int = 0,
    last_tx_seq: int = 0,
    last_rx_seq: int = 0,
    remember: bool = True,
    heartbeat: bool = False,
) -> int:
    # Held across allocation and transmission both: the input, heartbeat and reader threads all send,
    # and a sequence number that reaches the radio out of order reads as a gap at the far end.
    with state.socket_lock:
        return _send_shell_frame_locked(
            transport, state, op, payload, cols, rows, session_id, ack_seq, seq, flags, last_tx_seq, last_rx_seq,
            remember, heartbeat,
        )


def _send_shell_frame_locked(
    transport,
    state: SessionState,
    op: int,
    payload: bytes,
    cols: int,
    rows: int,
    session_id: Optional[int],
    ack_seq: Optional[int],
    seq: Optional[int],
    flags: int,
    last_tx_seq: int,
    last_rx_seq: int,
    remember: bool,
    heartbeat: bool,
) -> int:
    if seq is None:
        seq = 0 if op == state.pb2.mesh.RemoteShell.ACK else state.alloc_seq()
    if ack_seq is None:
        ack_seq = state.current_ack_seq()
    if session_id is None:
        session_id = state.session_id

    shell = state.pb2.mesh.RemoteShell()
    shell.op = op
    shell.session_id = session_id
    shell.seq = seq
    shell.ack_seq = ack_seq
    shell.cols = cols
    shell.rows = rows
    shell.flags = flags
    shell.last_tx_seq = last_tx_seq
    shell.last_rx_seq = last_rx_seq
    if payload:
        shell.payload = payload
    send_toradio(transport, make_toradio_packet(state.pb2, state, shell))
    if remember:
        state.remember_sent_frame(
            SentShellFrame(
                op=op,
                session_id=session_id,
                seq=seq,
                ack_seq=ack_seq,
                payload=payload,
                cols=cols,
                rows=rows,
                flags=flags,
                last_tx_seq=last_tx_seq,
                last_rx_seq=last_rx_seq,
                sent_time=time.monotonic(),
            )
        )
    state.note_outbound_packet(heartbeat=heartbeat)
    return seq


def send_ack_frame(transport, state: SessionState, replay_from: Optional[int] = None) -> None:
    send_shell_frame(
        transport,
        state,
        state.pb2.mesh.RemoteShell.ACK,
        seq=0,
        last_rx_seq=0 if replay_from is None else replay_from - 1,
        remember=False,
    )


def replay_frames_from(transport, state: SessionState, start_seq: int) -> None:
    frame = next((f for f in state.replay_frames_from(start_seq) if f.seq == start_seq), None)
    if frame is None:
        oldest = state.oldest_retained_tx_seq()
        if not state.legacy_recovery and oldest is not None and 0 < start_seq < oldest:
            # The mirror of the firmware's eviction path. The peer is asking for a frame our own
            # ring has already dropped, so it can never be answered, and the peer will not advance
            # past the hole: it would keep asking until the idle timeout. End the session instead,
            # and say why, so the run's log distinguishes this from a lossy link.
            state.log_replay_event("replay_evicted", start_seq, f"oldest_retained={oldest}")
            state.event_queue.put(
                f"peer asked to replay seq={start_seq}, which has aged out of our {state.tx_history.maxlen}-frame history; closing session"
            )
            send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.CLOSE, remember=False)
            state.active = False
            state.closed_event.set()
            return
        #state.event_queue.put(f"replay unavailable from seq={start_seq}")
        state.log_replay_event("replay_unavailable", start_seq)
        return
    state.log_replay_event("replay_sent", start_seq)
    #state.event_queue.put(f"replay frame seq={start_seq}")
    send_shell_frame(
        transport,
        state,
        frame.op,
        payload=frame.payload,
        cols=frame.cols,
        rows=frame.rows,
        session_id=frame.session_id,
        ack_seq=frame.ack_seq,
        seq=frame.seq,
        flags=frame.flags,
        last_tx_seq=frame.last_tx_seq,
        last_rx_seq=frame.last_rx_seq,
        remember=False,
    )
    state.note_frame_resent(frame.seq)


def log_input_window_transition(state: SessionState) -> None:
    """Record the window opening and closing, independently of whether input is waiting.

    This used to live inside the flush loop, which only runs while bytes are queued - so a window
    that reopened with an empty queue logged nothing, and the run showed a closed with no matching
    reopen. That reads as a latch and is not one. Called on every service tick instead, so the
    closed/reopened counts describe the window rather than the typing.
    """
    if state.input_window_frames == 0:
        return
    closed = not state.input_window_open()
    if closed == state.input_window_blocked:
        return
    state.input_window_blocked = closed
    if closed:
        state.log_replay_event("input_window_closed", state.peer_acked_tx_seq,
                               f"outstanding={state.outstanding_input()}")
    else:
        state.log_replay_event("input_window_reopened", state.peer_acked_tx_seq)


def flush_pending_input(transport, state: SessionState) -> None:
    """Send as much held input as the window allows, oldest bytes first."""
    with state.input_lock:
        _flush_pending_input_locked(transport, state)


def _flush_pending_input_locked(transport, state: SessionState) -> None:
    while state.active and not state.closed_event.is_set() and state.has_queued_input():
        log_input_window_transition(state)
        if not state.input_window_open():
            return
        chunk = state.take_queued_input(INPUT_BATCH_MAX_BYTES)
        if not chunk:
            return
        send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.INPUT, chunk)


def send_input(transport, state: SessionState, data: bytes) -> None:
    """Queue typed input and send what the window allows.

    Everything goes through the queue even when the window is open, so bytes can only ever leave in
    the order they were typed.
    """
    dropped = state.queue_input(data)
    if dropped:
        state.event_queue.put(f"input buffer full, dropped {dropped} byte(s); the remote is not keeping up")
    flush_pending_input(transport, state)


def service_input_window(transport, state: SessionState) -> None:
    """Keep a blocked input stream moving. Called from the heartbeat thread, which already polls."""
    flush_pending_input(transport, state)
    if not state.active or state.closed_event.is_set():
        return
    log_input_window_transition(state)
    if state.input_window_open():
        return
    missing, exhausted = state.input_retransmit_due()
    if missing is None:
        return
    if exhausted:
        state.log_replay_event("peer_unresponsive", missing, f"retransmits={MAX_INPUT_RETRANSMITS}")
        state.event_queue.put(
            f"remote has not acknowledged input seq={missing} after {MAX_INPUT_RETRANSMITS} retransmissions; closing session"
        )
        send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.CLOSE, remember=False)
        state.active = False
        state.closed_event.set()
        return
    # The attempt number is what distinguishes a link that is recovering slowly from one that is not
    # recovering at all: a healthy gap clears in a handful, and an attempt count climbing toward
    # MAX_INPUT_RETRANSMITS on one sequence number is a direction of the link that is not passing
    # traffic, not a flow-control problem.
    state.log_replay_event("input_retransmit", missing,
                           f"attempt={state.input_retransmits} of {MAX_INPUT_RETRANSMITS} "
                           f"interval={state.input_retransmit_interval:g}s")
    replay_frames_from(transport, state, missing)


def wait_for_config_complete(transport, pb2, timeout: float, verbose: bool) -> None:
    nonce = random.randint(1, 0x7FFFFFFF)
    toradio = pb2.mesh.ToRadio()
    toradio.want_config_id = nonce
    send_toradio(transport, toradio)

    deadline = time.time() + timeout
    while time.time() < deadline:
        fromradio = pb2.mesh.FromRadio()
        fromradio.ParseFromString(recv_stream_frame(transport))
        variant = fromradio.WhichOneof("payload_variant")
        if verbose and variant:
            print(f"[api] fromradio {variant}", file=sys.stderr)
        if variant == "config_complete_id" and fromradio.config_complete_id == nonce:
            return
    raise TimeoutError("timed out waiting for config handshake to complete")


def decode_shell_packet(state: SessionState, packet) -> Optional[object]:
    if packet.WhichOneof("payload_variant") != "decoded":
        return None
    if packet.decoded.portnum != state.pb2.portnums.REMOTE_SHELL_APP:
        return None
    shell = state.pb2.mesh.RemoteShell()
    shell.ParseFromString(packet.decoded.payload)
    return shell


def reader_loop(transport, state: SessionState) -> None:
    def handle_in_order_shell(shell) -> bool:
        state.note_replayed_seq_received(shell.seq)
        if shell.op == state.pb2.mesh.RemoteShell.OPEN_OK:
            state.session_id = shell.session_id
            state.open_replay_log(state.session_id)
            state.set_receive_cursor(shell.seq)
            state.active = True
            state.opened_event.set()
            state.event_queue.put(
                f"opened session=0x{shell.session_id:08x} cols={shell.cols} rows={shell.rows}"
            )
            if state.replay_log_path is not None:
                state.event_queue.put(f"replay log: {state.replay_log_path}")
        elif shell.op == state.pb2.mesh.RemoteShell.OUTPUT:
            if shell.payload:
                sys.stdout.buffer.write(shell.payload)
                sys.stdout.buffer.flush()
        elif shell.op == state.pb2.mesh.RemoteShell.ERROR:
            message = shell.payload.decode("utf-8", errors="replace")
            if state.replay_log_file is None:
                state.open_replay_log(shell.session_id or state.session_id)
            sanitized = message.replace("\n", "\\n")
            state.log_replay_event("error_received", shell.seq, f"message={sanitized}")
            state.event_queue.put(f"remote error: {message}")
        elif shell.op == state.pb2.mesh.RemoteShell.PONG:
            remote_last_tx_seq = shell.last_tx_seq
            remote_last_rx_seq = shell.last_rx_seq
            local_latest_tx_seq = state.highest_sent_seq()
            if remote_last_rx_seq != 0 and remote_last_rx_seq < local_latest_tx_seq:
                replay_frames_from(transport, state, remote_last_rx_seq + 1)
            if remote_last_tx_seq > state.current_ack_seq():
                state.note_peer_reported_tx_seq(remote_last_tx_seq)
                req = state.request_missing_seq_once()
                if req is not None:
                    state.note_missing_seq_requested(req, "heartbeat_status")
                    send_ack_frame(transport, state, replay_from=req)
            #state.event_queue.put("pong")
        return False

    while not state.stopped:
        try:
            fromradio = state.pb2.mesh.FromRadio()
            fromradio.ParseFromString(recv_stream_frame(transport))
        except Exception as exc:
            if not state.stopped:
                state.event_queue.put(f"connection error: {exc}")
                state.closed_event.set()
            return

        variant = fromradio.WhichOneof("payload_variant")
        if variant == "packet":
            shell = decode_shell_packet(state, fromradio.packet)
            if not shell:
                continue
            state.note_inbound_packet()
            # Before every opcode branch, including the ones that continue: an out-of-order frame is
            # still proof the peer is alive and still carries a valid cursor, and during a gap it may
            # be the only kind arriving. Mirrors where the firmware calls notePeerReceiveCursor().
            state.note_peer_receive_cursor(shell.ack_seq, shell.last_rx_seq)
            #state.prune_sent_frames(shell.ack_seq)
            if shell.op == state.pb2.mesh.RemoteShell.CLOSED:
                # Terminal, so act on it regardless of sequence order. Buffering a CLOSED behind a
                # gap the peer has just told us it cannot fill left us retrying until the idle
                # timeout. ERROR deliberately stays on the ordered path below: it is not always
                # terminal, and skipping its sequence number would open a gap of its own.
                message = shell.payload.decode("utf-8", errors="replace")
                state.event_queue.put(f"session closed: {message}")
                state.closed_event.set()
                state.active = False
                return

            if shell.op == state.pb2.mesh.RemoteShell.ACK:
                #state.event_queue.put("peer requested replay")
                replay_from = shell.last_rx_seq + 1 if shell.last_rx_seq > 0 else None
                if replay_from is not None:
                    #state.event_queue.put(f"peer requested replay from seq={replay_from}")
                    replay_frames_from(transport, state, replay_from)
                continue

            action, missing_from = state.note_received_seq(shell.seq)
            if action == "duplicate":
                req = state.request_missing_seq_once()
                if req is not None:
                    state.note_missing_seq_requested(req, "duplicate")
                    send_ack_frame(transport, state, replay_from=req)
                else:
                    # We already have this in order, so the peer has not seen our cursor - which is
                    # what a sender whose window is shut looks like. Answer with the cursor rather
                    # than nothing, or it retransmits until its own bound closes the session. The
                    # firmware answers our duplicates the same way.
                    send_ack_frame(transport, state)
                continue
            if action == "gap":
                state.remember_out_of_order_frame(shell)
                req = state.request_missing_seq_once()
                if req is not None:
                    state.note_missing_seq_requested(req, "gap")
                    send_ack_frame(transport, state, replay_from=req)
                continue

            if handle_in_order_shell(shell):
                return

            while True:
                buffered_shell = state.pop_next_buffered_frame()
                if buffered_shell is None:
                    break
                buffered_action, _ = state.note_received_seq(buffered_shell.seq)
                if buffered_action != "process":
                    state.remember_out_of_order_frame(buffered_shell)
                    break
                if handle_in_order_shell(buffered_shell):
                    return

            req = state.request_missing_seq_once()
            if req is not None:
                state.note_missing_seq_requested(req, "post_process_gap")
                send_ack_frame(transport, state, replay_from=req)
            elif state.flow_control_ack_due():
                # Bare ack: replay_from is None so last_rx_seq stays 0 and the peer does not read it
                # as a replay request, but ack_seq still carries the cursor that reopens its window.
                send_ack_frame(transport, state)
        elif state.verbose and variant:
            state.event_queue.put(f"fromradio {variant}")


def drain_events(state: SessionState) -> None:
    while True:
        try:
            event = state.event_queue.get_nowait()
        except queue.Empty:
            return
        print(f"[dmshell] {event}", file=sys.stderr)


def heartbeat_loop(transport, state: SessionState) -> None:
    while not state.stopped and not state.closed_event.is_set():
        if not state.active:
            time.sleep(HEARTBEAT_POLL_INTERVAL_SEC)
            continue
        try:
            service_input_window(transport, state)
        except Exception as exc:
            if not state.stopped:
                state.event_queue.put(f"input window error: {exc}")
                state.closed_event.set()
                return
        if state.heartbeat_due():
            try:
                send_shell_frame(
                    transport,
                    state,
                    state.pb2.mesh.RemoteShell.PING,
                    last_tx_seq=state.highest_sent_seq(),
                    last_rx_seq=state.current_ack_seq(),
                    remember=True,
                    heartbeat=True,
                )
            except Exception as exc:
                if not state.stopped:
                    state.event_queue.put(f"heartbeat error: {exc}")
                    state.closed_event.set()
                    return
        time.sleep(HEARTBEAT_POLL_INTERVAL_SEC)


def run_command_mode(transport, state: SessionState, commands: list[str], close_after: float) -> None:
    for command in commands:
        send_input(transport, state, (command + "\n").encode("utf-8"))
    # The window may still be holding part of a command, so give it the same grace the output gets
    # before tearing the session down.
    deadline = time.monotonic() + close_after
    while state.has_queued_input() and not state.closed_event.is_set() and time.monotonic() < deadline:
        time.sleep(HEARTBEAT_POLL_INTERVAL_SEC)
    remaining = deadline - time.monotonic()
    if remaining > 0:
        time.sleep(remaining)
    send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.CLOSE)
    state.closed_event.wait(timeout=close_after + 5.0)


def run_interactive_mode(transport, state: SessionState) -> None:
    def read_local_command() -> str:
        prompt = "\r\n[dmshell] local command (resume|close|ping|resize C R): "
        sys.stderr.write(prompt)
        sys.stderr.flush()
        buf = bytearray()

        while True:
            ch = os.read(sys.stdin.fileno(), 1)
            if not ch:
                sys.stderr.write("\r\n")
                sys.stderr.flush()
                return "close"

            b = ch[0]
            if b in (10, 13):
                sys.stderr.write("\r\n")
                sys.stderr.flush()
                return buf.decode("utf-8", errors="replace").strip()

            if b in (8, 127):
                if buf:
                    buf.pop()
                    sys.stderr.write("\b \b")
                    sys.stderr.flush()
                continue

            if b < 32:
                continue

            buf.append(b)
            sys.stderr.write(chr(b))
            sys.stderr.flush()

    def handle_local_command(cmd: str) -> bool:
        if cmd in ("", "resume"):
            return True
        if cmd == "close":
            send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.CLOSE)
            return False
        if cmd == "ping":
            send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.PING)
            return True
        if cmd.startswith("resize "):
            parts = cmd.split()
            if len(parts) != 3:
                state.event_queue.put("usage: resize COLS ROWS")
                return True
            try:
                cols = int(parts[1])
                rows = int(parts[2])
            except ValueError:
                state.event_queue.put("usage: resize COLS ROWS")
                return True
            send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.RESIZE, cols=cols, rows=rows)
            return True

        state.event_queue.put(f"unknown local command: {cmd}")
        return True

    print(
        "Raw input mode active. All keys (including Ctrl+C/Ctrl+X) are sent to remote. Ctrl+] for local commands.",
        file=sys.stderr,
    )

    if not sys.stdin.isatty():
        # Fallback for non-TTY stdin: still send input as it arrives.
        while not state.closed_event.is_set():
            drain_events(state)
            data = sys.stdin.buffer.read(INPUT_BATCH_MAX_BYTES)
            if not data:
                send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.CLOSE)
                break
            send_input(transport, state, data)
        return

    fd = sys.stdin.fileno()
    old_attrs = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        while not state.closed_event.is_set():
            drain_events(state)
            ready, _, _ = select.select([sys.stdin], [], [], 0.05)
            if not ready:
                flush_pending_input(transport, state)
                continue

            data = os.read(fd, 1)
            if not data:
                send_shell_frame(transport, state, state.pb2.mesh.RemoteShell.CLOSE)
                break

            if data == LOCAL_ESCAPE_BYTE:
                keep_running = handle_local_command(read_local_command())
                if not keep_running:
                    break
                continue

            # Coalesce a short burst of bytes to reduce packet overhead for fast typing.
            batched = bytearray(data)
            enter_local_command = False
            deadline = time.monotonic() + INPUT_BATCH_WINDOW_SEC
            while len(batched) < INPUT_BATCH_MAX_BYTES:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                more_ready, _, _ = select.select([sys.stdin], [], [], remaining)
                if not more_ready:
                    break
                next_byte = os.read(fd, 1)
                if not next_byte:
                    break
                if next_byte == LOCAL_ESCAPE_BYTE:
                    enter_local_command = True
                    break
                batched.extend(next_byte)
                if next_byte == b'\r' or next_byte == b'\t':
                    break
                deadline = time.monotonic() + INPUT_BATCH_WINDOW_SEC

            if batched:
                send_input(transport, state, bytes(batched))

            if enter_local_command:
                keep_running = handle_local_command(read_local_command())
                if not keep_running:
                    break
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_attrs)


def main() -> int:
    args = parse_args()
    pb2 = load_proto_modules()

    state = SessionState(
            pb2=pb2,
        target=parse_node_num(args.to),
        channel=args.channel,
        verbose=args.verbose,
        hop_limit=args.hop_limit,
        legacy_recovery=args.legacy_recovery,
        input_window_frames=0 if args.legacy_recovery else INPUT_WINDOW_FRAMES,
    )
    if state.input_window_frames == 0:
        print("[dmshell] input window disabled, typed input may run away from a gap", file=sys.stderr)
    else:
        print(f"[dmshell] bounding unacknowledged input to {state.input_window_frames} frames", file=sys.stderr)

    cols, rows = resolve_initial_terminal_size(args.cols, args.rows)

    transport = open_transport(args)
    try:
        wait_for_config_complete(transport, pb2, args.timeout, args.verbose)

        reader = threading.Thread(target=reader_loop, args=(transport, state), daemon=True)
        reader.start()

        send_shell_frame(transport, state, pb2.mesh.RemoteShell.OPEN, cols=cols, rows=rows)
        if not state.opened_event.wait(timeout=max(args.open_timeout, args.timeout)):
            raise SystemExit("timed out waiting for OPEN_OK from remote DMShell")

        heartbeat = threading.Thread(target=heartbeat_loop, args=(transport, state), daemon=True)
        heartbeat.start()

        drain_events(state)
        if args.command:
            run_command_mode(transport, state, args.command, args.close_after)
        else:
            run_interactive_mode(transport, state)

        state.stopped = True
        drain_events(state)
        reader.join(timeout=1.0)
        heartbeat.join(timeout=1.0)
        state.close_replay_log()
    finally:
        transport.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())