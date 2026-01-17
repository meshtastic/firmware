#!/usr/bin/env python3
"""
Data Models for Meshtastic Master Controller

This module contains all data classes and enums used by the master controller.
"""

import yaml
from collections import deque
from dataclasses import dataclass, field
from enum import Enum
from typing import Deque, Dict, List, Optional

from .protocol import PRIVATE_PORT_NUM, DataBatch, SlaveStatusReport


# =============================================================================
# Constants (NASA Rule 2: Fixed bounds for all limits)
# =============================================================================

# Maximum telemetry history per slave
MAX_TELEMETRY_HISTORY = 100

# Maximum data batches to store per slave
MAX_DATA_BATCHES = 50

# Slave offline timeout (seconds)
DEFAULT_SLAVE_OFFLINE_TIMEOUT = 600

# Maximum number of slaves to track (NASA Rule 2: bounded collections)
MAX_SLAVES = 256

# Maximum retry attempts for operations (NASA Rule 2: bounded loops)
MAX_RETRY_ATTEMPTS = 10

# Maximum handlers per event type (NASA Rule 2: bounded collections)
MAX_EVENT_HANDLERS = 32

# CLI command timeout (seconds)
CLI_TIMEOUT_SECONDS = 60

# Device reboot wait time (seconds)
DEVICE_REBOOT_WAIT = 10


# =============================================================================
# Enums
# =============================================================================

class MasterState(Enum):
    """Master controller states."""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    VERIFYING_KEY = "verifying_key"
    UPDATING_KEY = "updating_key"
    READY = "ready"
    ERROR = "error"


# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class SlaveNode:
    """
    Represents a slave node in the mesh network.

    Tracks device telemetry (from standard Meshtastic), data batches, and status.
    """
    node_id: str
    long_name: str = ""
    short_name: str = ""

    # Timing
    first_seen: float = 0.0
    last_seen: float = 0.0

    # Status
    is_online: bool = False
    last_status: Optional[SlaveStatusReport] = None

    # Device telemetry (from standard Meshtastic TELEMETRY_APP)
    battery_level: int = 0  # Percentage 0-100
    voltage: float = 0.0  # Volts
    channel_utilization: float = 0.0  # Percentage
    air_util_tx: float = 0.0  # Percentage
    temperature: Optional[float] = None  # Celsius (from environment metrics)
    humidity: Optional[float] = None  # Percentage
    pressure: Optional[float] = None  # hPa
    last_telemetry_time: float = 0.0

    # Data batches received (newest first) - from our custom protocol
    data_batches: Deque[DataBatch] = field(
        default_factory=lambda: deque(maxlen=MAX_DATA_BATCHES)
    )

    # Statistics
    telemetry_count: int = 0  # Standard Meshtastic telemetry received
    batch_count: int = 0  # Custom protocol batches received
    error_count: int = 0

    @property
    def latest_batch(self) -> Optional[DataBatch]:
        """Get the most recent data batch."""
        return self.data_batches[0] if self.data_batches else None


@dataclass
class ApiConfig:
    """API server configuration."""
    enabled: bool = False
    host: str = "0.0.0.0"
    port: int = 8080


@dataclass
class MasterConfig:
    """Configuration for the master controller."""
    # Device settings
    device_port: str = ""
    device_timeout: int = 30

    # Security settings
    private_key: str = ""  # Base64 encoded private key
    public_key: str = ""   # Base64 encoded public key (for slave admin_key setup)

    # Channel settings
    private_channel_index: int = 1

    # Protocol settings
    private_port_num: int = PRIVATE_PORT_NUM
    send_ack: bool = True  # Auto-send ACK for received data

    # Position settings
    public_channel_precision: int = 0  # Disable position on public channel
    private_channel_precision: int = 0  # Disable automatic position on private
    position_broadcast_enabled: bool = False  # Python app position broadcasting
    position_broadcast_interval: int = 300
    fixed_latitude: Optional[float] = None
    fixed_longitude: Optional[float] = None
    fixed_altitude: Optional[int] = None
    use_fixed_position: bool = False
    disable_smart_position: bool = True
    gps_update_interval: int = 0

    # Slave management
    slave_timeout: float = DEFAULT_SLAVE_OFFLINE_TIMEOUT

    # API settings
    api: ApiConfig = field(default_factory=ApiConfig)

    # Logging
    log_level: str = "INFO"
    log_file: str = "master.log"

    @classmethod
    def from_yaml(cls, path: str) -> "MasterConfig":
        """Load configuration from YAML file."""
        with open(path, "r") as f:
            data = yaml.safe_load(f)

        pos = data.get("position", {})
        api_data = data.get("api", {})

        return cls(
            device_port=data.get("device", {}).get("port", ""),
            device_timeout=data.get("device", {}).get("timeout", 30),
            private_key=data.get("security", {}).get("private_key", ""),
            public_key=data.get("security", {}).get("public_key", ""),
            private_channel_index=data.get("channel", {}).get("private_channel_index", 1),
            private_port_num=data.get("protocol", {}).get("port_num", PRIVATE_PORT_NUM),
            send_ack=data.get("protocol", {}).get("send_ack", True),
            public_channel_precision=pos.get("public_channel_precision", 0),
            private_channel_precision=pos.get("private_channel_precision", 0),
            position_broadcast_enabled=pos.get("broadcast_enabled", False),
            position_broadcast_interval=pos.get("broadcast_interval_seconds", 300),
            fixed_latitude=pos.get("fixed_latitude"),
            fixed_longitude=pos.get("fixed_longitude"),
            fixed_altitude=pos.get("fixed_altitude"),
            use_fixed_position=pos.get("use_fixed_position", False),
            disable_smart_position=pos.get("disable_smart_position", True),
            gps_update_interval=pos.get("gps_update_interval", 0),
            slave_timeout=data.get("slaves", {}).get("offline_timeout", DEFAULT_SLAVE_OFFLINE_TIMEOUT),
            api=ApiConfig(
                enabled=api_data.get("enabled", False),
                host=api_data.get("host", "0.0.0.0"),
                port=api_data.get("port", 8080),
            ),
            log_level=data.get("logging", {}).get("level", "INFO"),
            log_file=data.get("logging", {}).get("file", "master.log"),
        )

    def to_dict(self) -> Dict:
        """Convert config to dictionary."""
        return {
            "device": {
                "port": self.device_port,
                "timeout": self.device_timeout,
            },
            "security": {
                "private_key": self.private_key,
                "public_key": self.public_key,
            },
            "channel": {
                "private_channel_index": self.private_channel_index,
            },
            "protocol": {
                "port_num": self.private_port_num,
                "send_ack": self.send_ack,
            },
            "position": {
                "public_channel_precision": self.public_channel_precision,
                "private_channel_precision": self.private_channel_precision,
                "broadcast_enabled": self.position_broadcast_enabled,
                "broadcast_interval_seconds": self.position_broadcast_interval,
                "fixed_latitude": self.fixed_latitude,
                "fixed_longitude": self.fixed_longitude,
                "fixed_altitude": self.fixed_altitude,
                "use_fixed_position": self.use_fixed_position,
                "disable_smart_position": self.disable_smart_position,
                "gps_update_interval": self.gps_update_interval,
            },
            "slaves": {
                "offline_timeout": self.slave_timeout,
            },
            "api": {
                "enabled": self.api.enabled,
                "host": self.api.host,
                "port": self.api.port,
            },
            "logging": {
                "level": self.log_level,
                "file": self.log_file,
            },
        }
