#!/usr/bin/env python3

import argparse
import struct
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
DEFAULT_HOP_LIMIT = 0
LOCAL_ESCAPE_BYTE = b"\x1d"  # Ctrl+]
REPLAY_REQUEST_SIZE = 4
HEARTBEAT_STATUS_SIZE = 8
MISSING_SEQ_RETRY_INTERVAL_SEC = 1.0
INPUT_BATCH_WINDOW_SEC = .5
INPUT_BATCH_MAX_BYTES = 64
HEARTBEAT_IDLE_DELAY_SEC = 5.0
HEARTBEAT_REPEAT_SEC = 15.0
HEARTBEAT_POLL_INTERVAL_SEC = 0.25


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
    parser.add_argument("--cols", type=int, default=None, help="initial terminal columns (default: detect local terminal)")
    parser.add_argument("--rows", type=int, default=None, help="initial terminal rows (default: detect local terminal)")
    parser.add_argument("--command", action="append", default=[], help="send a command line after opening")
    parser.add_argument("--close-after", type=float, default=2.0, help="seconds to wait before closing in command mode")
    parser.add_argument("--timeout", type=float, default=10.0, help="seconds to wait for API/session events")
    parser.add_argument("--verbose", action="store_true", help="print extra protocol events")
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


@dataclass
class SessionState:
    pb2: object  # ProtoModules with mesh and portnums attributes
    target: int
    channel: int
    verbose: bool
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
    requested_missing_seqs: set[int] = field(default_factory=set)
    replay_log_lock: threading.Lock = field(default_factory=threading.Lock)
    replay_log_file: Optional[TextIO] = None
    replay_log_path: Optional[Path] = None
    last_transport_activity_time: float = field(default_factory=time.monotonic)
    last_heartbeat_sent_time: float = 0.0

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

    def heartbeat_payload(self) -> bytes:
        with self.tx_lock:
            return encode_heartbeat_status(max(0, self.next_seq - 1), self.last_rx_seq)

    def note_outbound_packet(self, heartbeat: bool = False) -> None:
        with self.tx_lock:
            now = time.monotonic()
            if heartbeat:
                self.last_heartbeat_sent_time = now
            else:
                self.last_transport_activity_time = now

    def note_inbound_packet(self) -> None:
        with self.tx_lock:
            self.last_transport_activity_time = time.monotonic()

    def heartbeat_due(self) -> bool:
        with self.tx_lock:
            now = time.monotonic()
            if (now - self.last_transport_activity_time) < HEARTBEAT_IDLE_DELAY_SEC:
                return False
            if self.last_heartbeat_sent_time <= self.last_transport_activity_time:
                return True
            return (now - self.last_heartbeat_sent_time) >= HEARTBEAT_REPEAT_SEC

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
            if seq > self.highest_seen_rx_seq:
                self.highest_seen_rx_seq = seq
            if self.highest_seen_rx_seq < self.next_expected_rx_seq:
                self.highest_seen_rx_seq = 0
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
            if (
                self.last_requested_missing_seq == self.next_expected_rx_seq
                and (now - self.last_missing_request_time) < MISSING_SEQ_RETRY_INTERVAL_SEC
            ):
                return None
            self.last_requested_missing_seq = self.next_expected_rx_seq
            self.last_missing_request_time = now
            return self.last_requested_missing_seq

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
        if frame.seq == 0 or frame.op == self.pb2.mesh.DMShell.ACK:
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


def encode_replay_request(start_seq: int) -> bytes:
    return struct.pack(">I", start_seq)


def decode_replay_request(payload: bytes) -> Optional[int]:
    if len(payload) < REPLAY_REQUEST_SIZE:
        return None
    return struct.unpack(">I", payload[:REPLAY_REQUEST_SIZE])[0]


def encode_heartbeat_status(last_tx_seq: int, last_rx_seq: int) -> bytes:
    return struct.pack(">II", last_tx_seq, last_rx_seq)


def decode_heartbeat_status(payload: bytes) -> Optional[tuple[int, int]]:
    if len(payload) < HEARTBEAT_STATUS_SIZE:
        return None
    return struct.unpack(">II", payload[:HEARTBEAT_STATUS_SIZE])


def send_toradio(transport, toradio) -> None:
    send_stream_frame(transport, toradio.SerializeToString())


def make_toradio_packet(pb2, state: SessionState, shell_msg) -> object:
    packet = pb2.mesh.MeshPacket()
    packet.id = random.randint(1, 0x7FFFFFFF)
    packet.to = state.target
    # The 'from' field is a reserved keyword in Python, so use setattr
    setattr(packet, "from", 0)
    packet.channel = state.channel
    packet.hop_limit = DEFAULT_HOP_LIMIT
    packet.want_ack = False
    packet.decoded.portnum = pb2.portnums.DM_SHELL_APP
    packet.decoded.payload = shell_msg.SerializeToString()
    packet.decoded.want_response = False
    packet.decoded.dest = state.target
    packet.decoded.source = 0

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
    remember: bool = True,
    heartbeat: bool = False,
) -> int:
    if seq is None:
        seq = 0 if op == state.pb2.mesh.DMShell.ACK else state.alloc_seq()
    if ack_seq is None:
        ack_seq = state.current_ack_seq()
    if session_id is None:
        session_id = state.session_id

    shell = state.pb2.mesh.DMShell()
    shell.op = op
    shell.session_id = session_id
    shell.seq = seq
    shell.ack_seq = ack_seq
    shell.cols = cols
    shell.rows = rows
    shell.flags = flags
    if payload:
        shell.payload = payload
    with state.socket_lock:
        send_toradio(transport, make_toradio_packet(state.pb2, state, shell))
    if remember:
        state.remember_sent_frame(
            SentShellFrame(op=op, session_id=session_id, seq=seq, ack_seq=ack_seq, payload=payload, cols=cols, rows=rows, flags=flags)
        )
    state.note_outbound_packet(heartbeat=heartbeat)
    return seq


def send_ack_frame(transport, state: SessionState, replay_from: Optional[int] = None) -> None:
    payload = b"" if replay_from is None else encode_replay_request(replay_from)
    send_shell_frame(transport, state, state.pb2.mesh.DMShell.ACK, payload=payload, seq=0, remember=False)


def replay_frames_from(transport, state: SessionState, start_seq: int) -> None:
    frame = next((f for f in state.replay_frames_from(start_seq) if f.seq == start_seq), None)
    if frame is None:
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
        remember=False,
    )


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
    if packet.decoded.portnum != state.pb2.portnums.DM_SHELL_APP:
        return None
    shell = state.pb2.mesh.DMShell()
    shell.ParseFromString(packet.decoded.payload)
    return shell


def reader_loop(transport, state: SessionState) -> None:
    def handle_in_order_shell(shell) -> bool:
        state.note_replayed_seq_received(shell.seq)
        if shell.op == state.pb2.mesh.DMShell.OPEN_OK:
            state.session_id = shell.session_id
            state.open_replay_log(state.session_id)
            state.set_receive_cursor(shell.seq)
            state.active = True
            state.opened_event.set()
            pid = int.from_bytes(shell.payload, "big") if shell.payload else 0
            state.event_queue.put(
                f"opened session=0x{shell.session_id:08x} cols={shell.cols} rows={shell.rows} pid={pid}"
            )
            if state.replay_log_path is not None:
                state.event_queue.put(f"replay log: {state.replay_log_path}")
        elif shell.op == state.pb2.mesh.DMShell.OUTPUT:
            if shell.payload:
                sys.stdout.buffer.write(shell.payload)
                sys.stdout.buffer.flush()
        elif shell.op == state.pb2.mesh.DMShell.ERROR:
            message = shell.payload.decode("utf-8", errors="replace")
            state.event_queue.put(f"remote error: {message}")
        elif shell.op == state.pb2.mesh.DMShell.CLOSED:
            message = shell.payload.decode("utf-8", errors="replace")
            state.event_queue.put(f"session closed: {message}")
            state.closed_event.set()
            state.active = False
            return True
        elif shell.op == state.pb2.mesh.DMShell.PONG:
            heartbeat_status = decode_heartbeat_status(shell.payload)
            if heartbeat_status is not None:
                remote_last_tx_seq, remote_last_rx_seq = heartbeat_status
                local_latest_tx_seq = state.highest_sent_seq()
                if remote_last_rx_seq < local_latest_tx_seq:
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
            #state.prune_sent_frames(shell.ack_seq)
            if shell.op == state.pb2.mesh.DMShell.ACK:
                #state.event_queue.put("peer requested replay")
                replay_from = decode_replay_request(shell.payload)
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
        if state.heartbeat_due():
            try:
                send_shell_frame(
                    transport,
                    state,
                    state.pb2.mesh.DMShell.PING,
                    payload=state.heartbeat_payload(),
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
        send_shell_frame(transport, state, state.pb2.mesh.DMShell.INPUT, (command + "\n").encode("utf-8"))
    time.sleep(close_after)
    send_shell_frame(transport, state, state.pb2.mesh.DMShell.CLOSE)
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
            send_shell_frame(transport, state, state.pb2.mesh.DMShell.CLOSE)
            return False
        if cmd == "ping":
            send_shell_frame(transport, state, state.pb2.mesh.DMShell.PING)
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
            send_shell_frame(transport, state, state.pb2.mesh.DMShell.RESIZE, cols=cols, rows=rows)
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
                send_shell_frame(transport, state, state.pb2.mesh.DMShell.CLOSE)
                break
            send_shell_frame(transport, state, state.pb2.mesh.DMShell.INPUT, data)
        return

    fd = sys.stdin.fileno()
    old_attrs = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        while not state.closed_event.is_set():
            drain_events(state)
            ready, _, _ = select.select([sys.stdin], [], [], 0.05)
            if not ready:
                continue

            data = os.read(fd, 1)
            if not data:
                send_shell_frame(transport, state, state.pb2.mesh.DMShell.CLOSE)
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
                send_shell_frame(transport, state, state.pb2.mesh.DMShell.INPUT, bytes(batched))

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
    )

    cols, rows = resolve_initial_terminal_size(args.cols, args.rows)

    transport = open_transport(args)
    try:
        wait_for_config_complete(transport, pb2, args.timeout, args.verbose)

        reader = threading.Thread(target=reader_loop, args=(transport, state), daemon=True)
        reader.start()

        send_shell_frame(transport, state, pb2.mesh.DMShell.OPEN, cols=cols, rows=rows)
        if not state.opened_event.wait(timeout=args.timeout):
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