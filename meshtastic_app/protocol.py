#!/usr/bin/env python3
"""
Master-Slave Communication Protocol

This module defines the binary protocol for communication between the
master controller and slave firmware nodes.

Protocol Overview:
- All communication uses a private channel with a custom port number (485)
- Messages are binary encoded for efficiency
- Slaves send sensor data batches to master
- Master can send commands to slaves
- Standard Meshtastic telemetry is used for device metrics (battery, voltage)

Slave Discovery Flow:
    1. Slave starts without knowing master's node number
    2. Slave broadcasts DATA_BATCH on private channel (all nodes receive)
    3. Master receives broadcast, sends ACK back to slave
    4. Slave extracts master's node number from ACK packet's fromId field
    5. Slave now sends directly to master (more efficient, less channel usage)
    6. Slave also monitors master's position broadcasts to get/update node number
    7. If master node number changes, slave updates and continues direct sending

Message Format:
    [HEADER (4 bytes)] [PAYLOAD (variable)]

Header Format:
    Byte 0: Message Type (1 byte)
    Byte 1: Flags (1 byte)
    Byte 2-3: Payload Length (2 bytes, little-endian)

Message Types:
    0x01: TELEMETRY - Slave telemetry data (DEPRECATED - use standard Meshtastic)
    0x02: DATA_BATCH - Slave sensor data batch
    0x03: STATUS - Slave status report
    0x10: COMMAND - Master command to slave
    0x11: ACK - Acknowledgment (slave uses fromId to learn master node number)
    0x12: NACK - Negative acknowledgment
    0x20: REQUEST_STATUS - Master requests slave status
    0x21: REQUEST_DATA - Master requests slave data
    0x22: SLAVE_ACK - Slave acknowledgment
    0xFF: HEARTBEAT - Keep-alive

Flags:
    Bit 0: ACK_REQUESTED - Slave requests acknowledgment
    Bit 1: FRAGMENTED - Part of a larger message
    Bit 2: LAST_FRAGMENT - Last fragment of a fragmented message
    Bit 3-7: Reserved
"""

import struct
from dataclasses import dataclass, field
from enum import IntEnum
from typing import List, Optional, Tuple


# =============================================================================
# Constants
# =============================================================================

# Private application port number (256-511 are private app range)
PRIVATE_PORT_NUM = 485

# Protocol version
PROTOCOL_VERSION = 1

# Maximum payload size (Meshtastic limit ~230 bytes)
MAX_PAYLOAD_SIZE = 200

# Header size
HEADER_SIZE = 4


# =============================================================================
# Enums
# =============================================================================

class MessageType(IntEnum):
    """Protocol message types."""
    # Slave -> Master
    TELEMETRY = 0x01      # Telemetry data from slave
    DATA_BATCH = 0x02     # Batch of sensor/collected data
    STATUS = 0x03         # Slave status report
    SLAVE_ACK = 0x04      # Slave acknowledgment

    # Master -> Slave
    COMMAND = 0x10        # Command to slave
    CONFIG = 0x11         # Configuration update
    MASTER_ACK = 0x12     # Master acknowledgment
    REQUEST_STATUS = 0x13 # Request slave status
    REQUEST_DATA = 0x14   # Request data from slave

    # Bidirectional
    HEARTBEAT = 0xFF      # Keep-alive ping


class MessageFlags(IntEnum):
    """Protocol message flags."""
    NONE = 0x00
    ACK_REQUESTED = 0x01
    FRAGMENTED = 0x02
    LAST_FRAGMENT = 0x04


class CommandType(IntEnum):
    """Master command types (sent in COMMAND payload)."""
    REBOOT = 0x01
    SLEEP = 0x02
    WAKE = 0x03
    SET_INTERVAL = 0x04
    CLEAR_DATA = 0x05
    SEND_DATA = 0x06
    SET_MODE = 0x07


class SlaveStatus(IntEnum):
    """Slave status codes."""
    OK = 0x00
    LOW_BATTERY = 0x01
    SENSOR_ERROR = 0x02
    MEMORY_FULL = 0x03
    ERROR = 0xFF


# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class ProtocolMessage:
    """Represents a protocol message."""
    msg_type: MessageType
    flags: int = MessageFlags.NONE
    payload: bytes = b""
    sequence: int = 0  # For fragmented messages

    def encode(self) -> bytes:
        """Encode message to binary format."""
        payload_len = len(self.payload)
        if payload_len > MAX_PAYLOAD_SIZE:
            raise ValueError(f"Payload too large: {payload_len} > {MAX_PAYLOAD_SIZE}")

        header = struct.pack(
            "<BBH",  # Little-endian: byte, byte, unsigned short
            self.msg_type,
            self.flags,
            payload_len,
        )
        return header + self.payload

    @classmethod
    def decode(cls, data: bytes) -> Optional["ProtocolMessage"]:
        """Decode binary data to message."""
        if len(data) < HEADER_SIZE:
            return None

        try:
            msg_type, flags, payload_len = struct.unpack("<BBH", data[:HEADER_SIZE])
            payload = data[HEADER_SIZE : HEADER_SIZE + payload_len]

            if len(payload) < payload_len:
                return None  # Incomplete message

            return cls(
                msg_type=MessageType(msg_type),
                flags=flags,
                payload=payload,
            )
        except (struct.error, ValueError):
            return None


@dataclass
class TelemetryData:
    """
    DEPRECATED: Use standard Meshtastic telemetry (TELEMETRY_APP) instead.

    This class is kept for backwards compatibility but is no longer used
    by the master controller. Standard Meshtastic provides device telemetry
    (battery, voltage) and environment metrics (temperature, humidity, pressure)
    automatically via the TELEMETRY_APP.

    Legacy Binary Format (12 bytes):
        Bytes 0-3:   Timestamp (uint32, seconds since epoch or uptime)
        Bytes 4:     Battery (uint8, percentage 0-100)
        Bytes 5:     Status (uint8, SlaveStatus)
        Bytes 6-7:   Temperature (int16, Celsius * 100, e.g., 2550 = 25.50C)
        Bytes 8-9:   Voltage (uint16, millivolts, e.g., 3700 = 3.7V)
        Bytes 10-11: Reserved (for future use)
    """
    timestamp: int = 0
    battery_percent: int = 0
    status: SlaveStatus = SlaveStatus.OK
    temperature: float = 0.0  # Celsius
    voltage_mv: int = 0  # Millivolts

    STRUCT_FORMAT = "<IBBhHH"  # uint32, uint8, uint8, int16, uint16, uint16
    STRUCT_SIZE = 12

    def encode(self) -> bytes:
        """Encode telemetry to binary."""
        temp_encoded = int(self.temperature * 100)

        return struct.pack(
            self.STRUCT_FORMAT,
            self.timestamp,
            self.battery_percent,
            self.status,
            temp_encoded,
            self.voltage_mv,
            0,  # Reserved
        )

    @classmethod
    def decode(cls, data: bytes) -> Optional["TelemetryData"]:
        """Decode binary to telemetry."""
        if len(data) < cls.STRUCT_SIZE:
            return None

        try:
            (
                timestamp,
                battery,
                status,
                temp_encoded,
                voltage_mv,
                _reserved,
            ) = struct.unpack(cls.STRUCT_FORMAT, data[: cls.STRUCT_SIZE])

            return cls(
                timestamp=timestamp,
                battery_percent=battery,
                status=SlaveStatus(status),
                temperature=temp_encoded / 100.0,
                voltage_mv=voltage_mv,
            )
        except (struct.error, ValueError):
            return None


@dataclass
class DataBatch:
    """
    A batch of data records from a slave.

    Binary Format:
        Bytes 0-3:   Batch ID (uint32)
        Bytes 4:     Record Count (uint8)
        Bytes 5:     Record Size (uint8)
        Bytes 6-7:   Reserved
        Bytes 8+:    Records (variable)
    """
    batch_id: int = 0
    records: List[bytes] = field(default_factory=list)
    record_size: int = 0

    HEADER_FORMAT = "<IBBH"
    HEADER_SIZE = 8

    def encode(self) -> bytes:
        """Encode data batch to binary."""
        if not self.records:
            return struct.pack(self.HEADER_FORMAT, self.batch_id, 0, 0, 0)

        record_size = len(self.records[0])
        header = struct.pack(
            self.HEADER_FORMAT,
            self.batch_id,
            len(self.records),
            record_size,
            0,  # Reserved
        )
        return header + b"".join(self.records)

    @classmethod
    def decode(cls, data: bytes) -> Optional["DataBatch"]:
        """Decode binary to data batch."""
        if len(data) < cls.HEADER_SIZE:
            return None

        try:
            batch_id, record_count, record_size, _ = struct.unpack(
                cls.HEADER_FORMAT, data[: cls.HEADER_SIZE]
            )

            records = []
            offset = cls.HEADER_SIZE

            for _ in range(record_count):
                if offset + record_size > len(data):
                    break
                records.append(data[offset : offset + record_size])
                offset += record_size

            return cls(
                batch_id=batch_id,
                records=records,
                record_size=record_size,
            )
        except struct.error:
            return None


@dataclass
class SlaveStatusReport:
    """
    Status report from a slave.

    Binary Format (16 bytes):
        Bytes 0-3:   Uptime (uint32, seconds)
        Bytes 4:     Status code (uint8)
        Bytes 5:     Battery (uint8, percentage)
        Bytes 6-7:   Free memory (uint16, KB)
        Bytes 8-11:  Pending data size (uint32, bytes)
        Bytes 12-13: Error count (uint16)
        Bytes 14-15: Reserved
    """
    uptime: int = 0
    status: SlaveStatus = SlaveStatus.OK
    battery_percent: int = 0
    free_memory_kb: int = 0
    pending_data_bytes: int = 0
    error_count: int = 0

    STRUCT_FORMAT = "<IBBHIHH"
    STRUCT_SIZE = 16

    def encode(self) -> bytes:
        """Encode status report to binary."""
        return struct.pack(
            self.STRUCT_FORMAT,
            self.uptime,
            self.status,
            self.battery_percent,
            self.free_memory_kb,
            self.pending_data_bytes,
            self.error_count,
            0,  # Reserved
        )

    @classmethod
    def decode(cls, data: bytes) -> Optional["SlaveStatusReport"]:
        """Decode binary to status report."""
        if len(data) < cls.STRUCT_SIZE:
            return None

        try:
            (
                uptime,
                status,
                battery,
                free_mem,
                pending_data,
                errors,
                _reserved,
            ) = struct.unpack(cls.STRUCT_FORMAT, data[: cls.STRUCT_SIZE])

            return cls(
                uptime=uptime,
                status=SlaveStatus(status),
                battery_percent=battery,
                free_memory_kb=free_mem,
                pending_data_bytes=pending_data,
                error_count=errors,
            )
        except (struct.error, ValueError):
            return None


@dataclass
class MasterCommand:
    """
    Command from master to slave.

    Binary Format:
        Byte 0:    Command type (uint8)
        Byte 1:    Parameter count (uint8)
        Bytes 2+:  Parameters (variable, each prefixed with 1-byte length)
    """
    command: CommandType
    params: List[bytes] = field(default_factory=list)

    def encode(self) -> bytes:
        """Encode command to binary."""
        data = struct.pack("<BB", self.command, len(self.params))
        for param in self.params:
            data += struct.pack("<B", len(param)) + param
        return data

    @classmethod
    def decode(cls, data: bytes) -> Optional["MasterCommand"]:
        """Decode binary to command."""
        if len(data) < 2:
            return None

        try:
            cmd_type, param_count = struct.unpack("<BB", data[:2])
            params = []
            offset = 2

            for _ in range(param_count):
                if offset >= len(data):
                    break
                param_len = data[offset]
                offset += 1
                if offset + param_len > len(data):
                    break
                params.append(data[offset : offset + param_len])
                offset += param_len

            return cls(
                command=CommandType(cmd_type),
                params=params,
            )
        except (struct.error, ValueError):
            return None


# =============================================================================
# Helper Functions
# =============================================================================

def create_telemetry_message(telemetry: TelemetryData, ack_requested: bool = False) -> bytes:
    """
    DEPRECATED: Use standard Meshtastic telemetry instead.

    Standard Meshtastic provides device and environment telemetry automatically.
    This function is kept for backwards compatibility only.
    """
    import warnings
    warnings.warn(
        "create_telemetry_message is deprecated. Use standard Meshtastic telemetry.",
        DeprecationWarning,
        stacklevel=2
    )
    flags = MessageFlags.ACK_REQUESTED if ack_requested else MessageFlags.NONE
    msg = ProtocolMessage(
        msg_type=MessageType.TELEMETRY,
        flags=flags,
        payload=telemetry.encode(),
    )
    return msg.encode()


def create_data_batch_message(batch: DataBatch, ack_requested: bool = True) -> bytes:
    """Create a data batch protocol message."""
    flags = MessageFlags.ACK_REQUESTED if ack_requested else MessageFlags.NONE
    msg = ProtocolMessage(
        msg_type=MessageType.DATA_BATCH,
        flags=flags,
        payload=batch.encode(),
    )
    return msg.encode()


def create_status_message(status: SlaveStatusReport) -> bytes:
    """Create a status report protocol message."""
    msg = ProtocolMessage(
        msg_type=MessageType.STATUS,
        payload=status.encode(),
    )
    return msg.encode()


def create_command_message(command: MasterCommand) -> bytes:
    """Create a master command protocol message."""
    msg = ProtocolMessage(
        msg_type=MessageType.COMMAND,
        flags=MessageFlags.ACK_REQUESTED,
        payload=command.encode(),
    )
    return msg.encode()


def create_ack_message(for_msg_type: MessageType) -> bytes:
    """Create an acknowledgment message."""
    msg = ProtocolMessage(
        msg_type=MessageType.MASTER_ACK,
        payload=struct.pack("<B", for_msg_type),
    )
    return msg.encode()


def create_heartbeat_message() -> bytes:
    """Create a heartbeat message."""
    msg = ProtocolMessage(
        msg_type=MessageType.HEARTBEAT,
    )
    return msg.encode()


def parse_message(data: bytes) -> Tuple[Optional[ProtocolMessage], Optional[object]]:
    """
    Parse binary data and return the message and decoded payload.

    Returns:
        Tuple of (ProtocolMessage, decoded_payload) or (None, None) on error.
    """
    msg = ProtocolMessage.decode(data)
    if not msg:
        return None, None

    payload = None

    # Note: TELEMETRY is deprecated - use standard Meshtastic TELEMETRY_APP
    if msg.msg_type == MessageType.DATA_BATCH:
        payload = DataBatch.decode(msg.payload)
    elif msg.msg_type == MessageType.STATUS:
        payload = SlaveStatusReport.decode(msg.payload)
    elif msg.msg_type == MessageType.COMMAND:
        payload = MasterCommand.decode(msg.payload)

    return msg, payload
