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
from typing import Optional


START1 = 0x94
START2 = 0xC3
HEADER_LEN = 4
DEFAULT_API_PORT = 4403
DEFAULT_HOP_LIMIT = 3
LOCAL_ESCAPE_BYTE = b"\x1d"  # Ctrl+]
REPLAY_REQUEST_SIZE = 4


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


def recv_exact(sock: socket.socket, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        piece = sock.recv(length - len(chunks))
        if not piece:
            raise ConnectionError("connection closed by server")
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


def recv_stream_frame(sock: socket.socket) -> bytes:
    while True:
        start = recv_exact(sock, 1)[0]
        if start != START1:
            continue
        if recv_exact(sock, 1)[0] != START2:
            continue
        header = recv_exact(sock, 2)
        length = (header[0] << 8) | header[1]
        return recv_exact(sock, length)


def send_stream_frame(sock: socket.socket, payload: bytes) -> None:
    if len(payload) > 0xFFFF:
        raise ValueError("payload too large for stream API")
    header = bytes((START1, START2, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF))
    sock.sendall(header + payload)


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
    active: bool = False
    stopped: bool = False
    opened_event: threading.Event = field(default_factory=threading.Event)
    closed_event: threading.Event = field(default_factory=threading.Event)
    event_queue: "queue.Queue[str]" = field(default_factory=queue.Queue)
    tx_lock: threading.Lock = field(default_factory=threading.Lock)
    tx_history: deque[SentShellFrame] = field(default_factory=lambda: deque(maxlen=10))

    def alloc_seq(self) -> int:
        with self.tx_lock:
            value = self.next_seq
            self.next_seq += 1
            return value

    def current_ack_seq(self) -> int:
        with self.tx_lock:
            return self.last_rx_seq

    def note_received_seq(self, seq: int) -> tuple[str, Optional[int]]:
        with self.tx_lock:
            if seq == 0:
                return ("process", None)
            if seq < self.next_expected_rx_seq:
                return ("duplicate", None)
            if seq > self.next_expected_rx_seq:
                return ("gap", self.next_expected_rx_seq)
            self.last_rx_seq = seq
            self.next_expected_rx_seq = seq + 1
            return ("process", None)

    def set_receive_cursor(self, seq: int) -> None:
        with self.tx_lock:
            self.last_rx_seq = seq
            self.next_expected_rx_seq = seq + 1

    def remember_sent_frame(self, frame: SentShellFrame) -> None:
        if frame.seq == 0 or frame.op == self.pb2.mesh.DMShell.ACK:
            return
        with self.tx_lock:
            self.tx_history.append(frame)

    def prune_sent_frames(self, ack_seq: int) -> None:
        if ack_seq <= 0:
            return
        with self.tx_lock:
            self.tx_history = deque((frame for frame in self.tx_history if frame.seq > ack_seq), maxlen=10)

    def replay_frames_from(self, start_seq: int) -> list[SentShellFrame]:
        with self.tx_lock:
            return [frame for frame in self.tx_history if frame.seq >= start_seq]


def encode_replay_request(start_seq: int) -> bytes:
    return struct.pack(">I", start_seq)


def decode_replay_request(payload: bytes) -> Optional[int]:
    if len(payload) < REPLAY_REQUEST_SIZE:
        return None
    return struct.unpack(">I", payload[:REPLAY_REQUEST_SIZE])[0]


def send_toradio(sock: socket.socket, toradio) -> None:
    send_stream_frame(sock, toradio.SerializeToString())


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
    sock: socket.socket,
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
    send_toradio(sock, make_toradio_packet(state.pb2, state, shell))
    if remember:
        state.remember_sent_frame(
            SentShellFrame(op=op, session_id=session_id, seq=seq, ack_seq=ack_seq, payload=payload, cols=cols, rows=rows, flags=flags)
        )
    return seq


def send_ack_frame(sock: socket.socket, state: SessionState, replay_from: Optional[int] = None) -> None:
    payload = b"" if replay_from is None else encode_replay_request(replay_from)
    send_shell_frame(sock, state, state.pb2.mesh.DMShell.ACK, payload=payload, seq=0, remember=False)


def replay_frames_from(sock: socket.socket, state: SessionState, start_seq: int) -> None:
    frames = state.replay_frames_from(start_seq)
    if not frames:
        state.event_queue.put(f"replay unavailable from seq={start_seq}")
        return
    for frame in frames:
        send_shell_frame(
            sock,
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


def wait_for_config_complete(sock: socket.socket, pb2, timeout: float, verbose: bool) -> None:
    nonce = random.randint(1, 0x7FFFFFFF)
    toradio = pb2.mesh.ToRadio()
    toradio.want_config_id = nonce
    send_toradio(sock, toradio)

    deadline = time.time() + timeout
    while time.time() < deadline:
        fromradio = pb2.mesh.FromRadio()
        fromradio.ParseFromString(recv_stream_frame(sock))
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


def reader_loop(sock: socket.socket, state: SessionState) -> None:
    while not state.stopped:
        try:
            fromradio = state.pb2.mesh.FromRadio()
            fromradio.ParseFromString(recv_stream_frame(sock))
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
            state.prune_sent_frames(shell.ack_seq)
            if shell.op == state.pb2.mesh.DMShell.ACK:
                replay_from = decode_replay_request(shell.payload)
                if replay_from is not None:
                    state.event_queue.put(f"peer requested replay from seq={replay_from}")
                    replay_frames_from(sock, state, replay_from)
                continue

            action, missing_from = state.note_received_seq(shell.seq)
            if action == "duplicate":
                send_ack_frame(sock, state)
                continue
            if action == "gap":
                state.event_queue.put(f"missing frame before seq={shell.seq}, requesting replay from seq={missing_from}")
                send_ack_frame(sock, state, replay_from=missing_from)
                continue

            if shell.op == state.pb2.mesh.DMShell.OPEN_OK:
                state.session_id = shell.session_id
                state.set_receive_cursor(shell.seq)
                state.active = True
                state.opened_event.set()
                pid = int.from_bytes(shell.payload, "big") if shell.payload else 0
                state.event_queue.put(
                    f"opened session=0x{shell.session_id:08x} cols={shell.cols} rows={shell.rows} pid={pid}"
                )
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
                return
            elif shell.op == state.pb2.mesh.DMShell.PONG:
                state.event_queue.put("pong")
        elif state.verbose and variant:
            state.event_queue.put(f"fromradio {variant}")


def drain_events(state: SessionState) -> None:
    while True:
        try:
            event = state.event_queue.get_nowait()
        except queue.Empty:
            return
        print(f"[dmshell] {event}", file=sys.stderr)


def run_command_mode(sock: socket.socket, state: SessionState, commands: list[str], close_after: float) -> None:
    for command in commands:
        send_shell_frame(sock, state, state.pb2.mesh.DMShell.INPUT, (command + "\n").encode("utf-8"))
    time.sleep(close_after)
    send_shell_frame(sock, state, state.pb2.mesh.DMShell.CLOSE)
    state.closed_event.wait(timeout=close_after + 5.0)


def run_interactive_mode(sock: socket.socket, state: SessionState) -> None:
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
            send_shell_frame(sock, state, state.pb2.mesh.DMShell.CLOSE)
            return False
        if cmd == "ping":
            send_shell_frame(sock, state, state.pb2.mesh.DMShell.PING)
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
            send_shell_frame(sock, state, state.pb2.mesh.DMShell.RESIZE, cols=cols, rows=rows)
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
            data = sys.stdin.buffer.read(1)
            if not data:
                send_shell_frame(sock, state, state.pb2.mesh.DMShell.CLOSE)
                break
            send_shell_frame(sock, state, state.pb2.mesh.DMShell.INPUT, data)
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
                send_shell_frame(sock, state, state.pb2.mesh.DMShell.CLOSE)
                break

            if data == LOCAL_ESCAPE_BYTE:
                keep_running = handle_local_command(read_local_command())
                if not keep_running:
                    break
                continue

            send_shell_frame(sock, state, state.pb2.mesh.DMShell.INPUT, data)
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

    with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
        sock.settimeout(None)
        wait_for_config_complete(sock, pb2, args.timeout, args.verbose)

        reader = threading.Thread(target=reader_loop, args=(sock, state), daemon=True)
        reader.start()

        send_shell_frame(sock, state, pb2.mesh.DMShell.OPEN, cols=cols, rows=rows)
        if not state.opened_event.wait(timeout=args.timeout):
            raise SystemExit("timed out waiting for OPEN_OK from remote DMShell")

        drain_events(state)
        if args.command:
            run_command_mode(sock, state, args.command, args.close_after)
        else:
            run_interactive_mode(sock, state)

        state.stopped = True
        drain_events(state)
        reader.join(timeout=1.0)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())