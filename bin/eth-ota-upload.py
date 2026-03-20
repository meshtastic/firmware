#!/usr/bin/env python3
"""
Meshtastic Ethernet OTA Upload Tool

Uploads firmware to RP2350-based Meshtastic devices via Ethernet (W5500).
Compresses firmware with GZIP and sends it over TCP using the MOTA protocol.
Authenticates using SHA256 challenge-response with a pre-shared key (PSK).

Usage:
    python bin/eth-ota-upload.py --host 192.168.1.100 firmware.bin
    python bin/eth-ota-upload.py --host 192.168.1.100 --psk mySecretKey firmware.bin
    python bin/eth-ota-upload.py --host 192.168.1.100 --psk-hex 6d65736874... firmware.bin
"""

import argparse
import gzip
import hashlib
import socket
import struct
import sys
import time

# Default PSK matching the firmware default: "meshtastic_ota_default_psk_v1!!!"
DEFAULT_PSK = b"meshtastic_ota_default_psk_v1!!!"


def crc32(data: bytes) -> int:
    """Compute CRC32 matching ErriezCRC32 (standard CRC32 with final XOR)."""
    import binascii

    return binascii.crc32(data) & 0xFFFFFFFF


def load_firmware(path: str) -> bytes:
    """Load firmware file, compressing with GZIP if not already compressed."""
    with open(path, "rb") as f:
        data = f.read()

    # Check if already GZIP compressed (magic bytes 1f 8b)
    if data[:2] == b"\x1f\x8b":
        print(f"Firmware already GZIP compressed: {len(data):,} bytes")
        return data

    print(f"Firmware raw size: {len(data):,} bytes")
    compressed = gzip.compress(data, compresslevel=9)
    ratio = len(compressed) / len(data) * 100
    print(f"GZIP compressed: {len(compressed):,} bytes ({ratio:.1f}%)")
    return compressed


def authenticate(sock: socket.socket, psk: bytes) -> bool:
    """Perform SHA256 challenge-response authentication with the device."""
    # Receive 32-byte nonce from server
    nonce = b""
    while len(nonce) < 32:
        chunk = sock.recv(32 - len(nonce))
        if not chunk:
            print("ERROR: Connection closed during authentication")
            return False
        nonce += chunk

    # Compute SHA256(nonce || PSK)
    h = hashlib.sha256()
    h.update(nonce)
    h.update(psk)
    response = h.digest()

    # Send 32-byte response
    sock.sendall(response)

    # Wait for auth result (1 byte)
    result = sock.recv(1)
    if not result:
        print("ERROR: No authentication response")
        return False

    if result[0] == 0x06:  # ACK
        print("Authentication successful.")
        return True
    elif result[0] == 0x07:  # OTA_ERR_AUTH
        print("ERROR: Authentication failed — wrong PSK")
        return False
    else:
        print(f"ERROR: Unexpected auth response 0x{result[0]:02X}")
        return False


def upload_firmware(host: str, port: int, firmware: bytes, psk: bytes, timeout: float) -> bool:
    """Upload firmware over TCP using the MOTA protocol with PSK authentication."""
    fw_crc = crc32(firmware)
    fw_size = len(firmware)

    print(f"Connecting to {host}:{port}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)

    try:
        sock.connect((host, port))
        print("Connected.")

        # Step 1: Authenticate
        print("Authenticating...")
        if not authenticate(sock, psk):
            return False

        # Step 2: Send 12-byte MOTA header: magic(4) + size(4) + crc32(4)
        header = struct.pack("<4sII", b"MOTA", fw_size, fw_crc)
        sock.sendall(header)
        print(f"Header sent: size={fw_size:,}, CRC32=0x{fw_crc:08X}")

        # Wait for ACK (1 byte)
        ack = sock.recv(1)
        if not ack or ack[0] != 0x06:
            error_codes = {
                0x02: "Size error",
                0x04: "Invalid magic",
                0x05: "Update.begin() failed",
            }
            code = ack[0] if ack else 0xFF
            msg = error_codes.get(code, f"Unknown error 0x{code:02X}")
            print(f"ERROR: Server rejected header: {msg}")
            return False

        print("Header accepted. Uploading firmware...")

        # Send firmware in 1KB chunks
        chunk_size = 1024
        sent = 0
        start_time = time.time()

        while sent < fw_size:
            end = min(sent + chunk_size, fw_size)
            chunk = firmware[sent:end]
            sock.sendall(chunk)
            sent = end

            # Progress bar
            pct = sent * 100 // fw_size
            bar_len = 40
            filled = bar_len * sent // fw_size
            bar = "█" * filled + "░" * (bar_len - filled)
            elapsed = time.time() - start_time
            speed = sent / elapsed if elapsed > 0 else 0
            sys.stdout.write(f"\r  [{bar}] {pct:3d}% {sent:,}/{fw_size:,} ({speed/1024:.1f} KB/s)")
            sys.stdout.flush()

        elapsed = time.time() - start_time
        print(f"\n  Transfer complete in {elapsed:.1f}s")

        # Wait for final result (1 byte)
        print("Waiting for verification...")
        result = sock.recv(1)
        if not result:
            print("ERROR: No response from device")
            return False

        result_codes = {
            0x00: "OK — Update staged, device rebooting",
            0x01: "CRC mismatch",
            0x02: "Size error",
            0x03: "Write error",
            0x06: "Timeout",
        }
        code = result[0]
        msg = result_codes.get(code, f"Unknown result 0x{code:02X}")

        if code == 0x00:
            print(f"SUCCESS: {msg}")
            return True
        else:
            print(f"ERROR: {msg}")
            return False

    except socket.timeout:
        print("ERROR: Connection timed out")
        return False
    except ConnectionRefusedError:
        print(f"ERROR: Connection refused by {host}:{port}")
        return False
    except OSError as e:
        print(f"ERROR: {e}")
        return False
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(
        description="Upload firmware to Meshtastic RP2350 devices via Ethernet OTA"
    )
    parser.add_argument("firmware", help="Path to firmware .bin or .bin.gz file")
    parser.add_argument("--host", required=True, help="Device IP address")
    parser.add_argument(
        "--port", type=int, default=4243, help="OTA port (default: 4243)"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="Socket timeout in seconds (default: 60)",
    )
    psk_group = parser.add_mutually_exclusive_group()
    psk_group.add_argument(
        "--psk",
        type=str,
        help="Pre-shared key as UTF-8 string (default: meshtastic_ota_default_psk_v1!!!)",
    )
    psk_group.add_argument(
        "--psk-hex",
        type=str,
        help="Pre-shared key as hex string (e.g., 6d65736874...)",
    )
    args = parser.parse_args()

    # Resolve PSK
    if args.psk:
        psk = args.psk.encode("utf-8")
    elif args.psk_hex:
        try:
            psk = bytes.fromhex(args.psk_hex)
        except ValueError:
            print("ERROR: Invalid hex string for --psk-hex")
            sys.exit(1)
    else:
        psk = DEFAULT_PSK

    print("Meshtastic Ethernet OTA Upload")
    print("=" * 40)

    firmware = load_firmware(args.firmware)

    if upload_firmware(args.host, args.port, firmware, psk, args.timeout):
        print("\nDevice is rebooting with new firmware.")
        sys.exit(0)
    else:
        print("\nUpload failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
