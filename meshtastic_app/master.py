#!/usr/bin/env python3
"""
Meshtastic Master Controller Module

This module acts as the master controller for a Meshtastic mesh network.
It connects to a local device via USB/Serial and communicates with slave
firmware nodes over a private channel using a custom binary protocol.

Architecture:
    Master (this Python app)
        │
        ├── USB/Serial connection
        ▼
    Meshtastic Device (connected to PC)
        │
        ├── Private Channel (RF)
        ▼
    Slave Nodes (custom firmware)
        - Send telemetry data
        - Send data batches
        - Receive commands

Features:
- Automatic private key verification and update on startup
- Custom binary protocol over private port app
- Slave node tracking with telemetry history
- Binary data batch collection
"""

import base64
import json
import logging
import struct
import subprocess
import sys
import time
from collections import deque
from dataclasses import dataclass, field, asdict
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Deque, Dict, List, Optional

import yaml

try:
    import meshtastic
    import meshtastic.serial_interface
    from pubsub import pub
except ImportError:
    print("Error: meshtastic package not installed.")
    print("Install with: pip3 install --upgrade 'meshtastic[cli]'")
    sys.exit(1)

from .protocol import (
    PRIVATE_PORT_NUM,
    CommandType,
    DataBatch,
    MasterCommand,
    MessageFlags,
    MessageType,
    ProtocolMessage,
    SlaveStatus,
    SlaveStatusReport,
    create_ack_message,
    create_command_message,
    create_heartbeat_message,
    parse_message,
)


# =============================================================================
# Constants
# =============================================================================

# Maximum telemetry history per slave
MAX_TELEMETRY_HISTORY = 100

# Maximum data batches to store per slave
MAX_DATA_BATCHES = 50

# Slave offline timeout (seconds)
SLAVE_OFFLINE_TIMEOUT = 600


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
class MasterConfig:
    """Configuration for the master controller."""
    # Device settings
    device_port: str = ""
    device_timeout: int = 30

    # Security settings
    private_key: str = ""  # Base64 encoded

    # Channel settings
    private_channel_index: int = 1

    # Protocol settings
    private_port_num: int = PRIVATE_PORT_NUM
    send_ack: bool = True  # Auto-send ACK for received data

    # Position settings (master can broadcast its position)
    position_enabled: bool = False
    position_interval: int = 300
    fixed_latitude: Optional[float] = None
    fixed_longitude: Optional[float] = None
    fixed_altitude: Optional[int] = None

    # Slave management
    slave_timeout: float = SLAVE_OFFLINE_TIMEOUT

    # Logging
    log_level: str = "INFO"
    log_file: str = "master.log"

    @classmethod
    def from_yaml(cls, path: str) -> "MasterConfig":
        """Load configuration from YAML file."""
        with open(path, "r") as f:
            data = yaml.safe_load(f)

        return cls(
            device_port=data.get("device", {}).get("port", ""),
            device_timeout=data.get("device", {}).get("timeout", 30),
            private_key=data.get("security", {}).get("private_key", ""),
            private_channel_index=data.get("channel", {}).get("private_channel_index", 1),
            private_port_num=data.get("protocol", {}).get("port_num", PRIVATE_PORT_NUM),
            send_ack=data.get("protocol", {}).get("send_ack", True),
            position_enabled=data.get("position", {}).get("enabled", False),
            position_interval=data.get("position", {}).get("interval_seconds", 300),
            fixed_latitude=data.get("position", {}).get("fixed_latitude"),
            fixed_longitude=data.get("position", {}).get("fixed_longitude"),
            fixed_altitude=data.get("position", {}).get("fixed_altitude"),
            slave_timeout=data.get("slaves", {}).get("offline_timeout", SLAVE_OFFLINE_TIMEOUT),
            log_level=data.get("logging", {}).get("level", "INFO"),
            log_file=data.get("logging", {}).get("file", "master.log"),
        )


# =============================================================================
# Master Controller
# =============================================================================

class MasterController:
    """
    Master controller for Meshtastic mesh network.

    This class manages the connection to the local Meshtastic device,
    handles automatic private key verification/update, and provides
    methods for communicating with slave nodes using a binary protocol
    over a private channel and port.
    """

    def __init__(self, config: MasterConfig):
        """
        Initialize the master controller.

        Args:
            config: Master configuration object.
        """
        self.config = config
        self._setup_logging()

        self.logger = logging.getLogger("MasterController")
        self.interface: Optional[meshtastic.serial_interface.SerialInterface] = None
        self.state = MasterState.DISCONNECTED
        self.running = False

        # Node tracking
        self.my_node_id: str = ""
        self.my_node_info: Dict = {}
        self.slaves: Dict[str, SlaveNode] = {}

        # Callbacks for slave data
        self._telemetry_handlers: List[Callable] = []
        self._data_batch_handlers: List[Callable] = []
        self._status_handlers: List[Callable] = []
        self._state_handlers: List[Callable] = []

        # Internal state
        self._subscribed = False  # Track pub/sub subscription state

    def _setup_logging(self):
        """Configure logging."""
        level = getattr(logging, self.config.log_level.upper())

        logging.basicConfig(
            level=level,
            format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
            handlers=[
                logging.FileHandler(self.config.log_file),
                logging.StreamHandler(sys.stdout),
            ],
        )

    def _set_state(self, state: MasterState):
        """Update controller state and notify handlers."""
        old_state = self.state
        self.state = state
        self.logger.info(f"State: {old_state.value} -> {state.value}")

        for handler in self._state_handlers:
            try:
                handler(old_state, state)
            except Exception as e:
                self.logger.error(f"State handler error: {e}")

    # -------------------------------------------------------------------------
    # Event Handlers
    # -------------------------------------------------------------------------

    def _on_connection(self, interface, topic=pub.AUTO_TOPIC):
        """Handle connection established."""
        self.logger.info("Connected to Meshtastic device")

        try:
            self.my_node_info = interface.getMyNodeInfo()
            user = self.my_node_info.get("user", {})
            self.my_node_id = user.get("id", "")

            self.logger.info(f"Device: {user.get('longName', 'Unknown')}")
            self.logger.info(f"Node ID: {self.my_node_id}")
            self.logger.info(f"Hardware: {user.get('hwModel', 'Unknown')}")
        except Exception as e:
            self.logger.warning(f"Could not get node info: {e}")

    def _on_disconnect(self, interface, topic=pub.AUTO_TOPIC):
        """Handle connection lost."""
        self.logger.warning("Disconnected from Meshtastic device")
        self._set_state(MasterState.DISCONNECTED)

    def _on_receive(self, packet, interface):
        """Handle incoming packets."""
        try:
            decoded = packet.get("decoded", {})
            from_id = packet.get("fromId", "unknown")
            channel = packet.get("channel", 0)
            portnum = decoded.get("portnum", "")

            # Only process packets on our private channel
            if channel != self.config.private_channel_index:
                return

            # Check if this is our private port app
            # portnum can be string name or int value
            port_value = self._get_port_value(portnum)

            if port_value == self.config.private_port_num:
                # This is our protocol - handle binary data
                payload = decoded.get("payload", b"")
                if isinstance(payload, str):
                    payload = payload.encode("latin-1")
                self._handle_protocol_message(from_id, payload)
            else:
                # Other traffic on private channel (position, text, etc.)
                self._handle_other_traffic(from_id, decoded, packet)

        except Exception as e:
            self.logger.error(f"Error processing packet: {e}")

    def _get_port_value(self, portnum) -> int:
        """Convert portnum to integer value."""
        if isinstance(portnum, int):
            return portnum
        if isinstance(portnum, str):
            # Try to parse as "PRIVATE_APP" or similar
            if portnum.startswith("PRIVATE_APP"):
                return 256
            try:
                return int(portnum)
            except ValueError:
                pass
        return 0

    def _handle_protocol_message(self, from_id: str, data: bytes):
        """Handle incoming protocol message from a slave."""
        if not data:
            return

        msg, payload = parse_message(data)
        if not msg:
            self.logger.warning(f"Invalid protocol message from {from_id}")
            self._get_or_create_slave(from_id).error_count += 1
            return

        self.logger.debug(
            f"Protocol message from {from_id}: type={msg.msg_type.name}, "
            f"flags={msg.flags:#x}, payload_len={len(msg.payload)}"
        )

        # Update slave tracking
        slave = self._get_or_create_slave(from_id)
        slave.last_seen = time.time()
        slave.is_online = True

        # Handle by message type (custom protocol)
        # Note: Standard Meshtastic telemetry is handled in _handle_other_traffic()
        if msg.msg_type == MessageType.DATA_BATCH:
            self._handle_data_batch(slave, payload, msg)

        elif msg.msg_type == MessageType.STATUS:
            self._handle_status(slave, payload, msg)

        elif msg.msg_type == MessageType.HEARTBEAT:
            self.logger.debug(f"Heartbeat from {from_id}")

        elif msg.msg_type == MessageType.SLAVE_ACK:
            self.logger.debug(f"ACK from {from_id}")

        # Send ACK if requested
        if (msg.flags & MessageFlags.ACK_REQUESTED) and self.config.send_ack:
            self._send_ack(from_id, msg.msg_type)

    def _handle_data_batch(self, slave: SlaveNode, batch: DataBatch, msg: ProtocolMessage):
        """Handle incoming data batch."""
        if not batch:
            self.logger.warning(f"Invalid data batch from {slave.node_id}")
            slave.error_count += 1
            return

        slave.data_batches.appendleft(batch)
        slave.batch_count += 1

        self.logger.info(
            f"[DATA_BATCH] {slave.node_id}: "
            f"batch_id={batch.batch_id}, "
            f"records={len(batch.records)}, "
            f"record_size={batch.record_size}"
        )

        # Dispatch to handlers
        for handler in self._data_batch_handlers:
            try:
                handler(slave, batch)
            except Exception as e:
                self.logger.error(f"Data batch handler error: {e}")

    def _handle_status(self, slave: SlaveNode, status: SlaveStatusReport, msg: ProtocolMessage):
        """Handle incoming status report."""
        if not status:
            self.logger.warning(f"Invalid status from {slave.node_id}")
            slave.error_count += 1
            return

        slave.last_status = status

        self.logger.info(
            f"[STATUS] {slave.node_id}: "
            f"uptime={status.uptime}s, "
            f"bat={status.battery_percent}%, "
            f"mem={status.free_memory_kb}KB, "
            f"pending={status.pending_data_bytes}B, "
            f"errors={status.error_count}, "
            f"status={status.status.name}"
        )

        # Dispatch to handlers
        for handler in self._status_handlers:
            try:
                handler(slave, status)
            except Exception as e:
                self.logger.error(f"Status handler error: {e}")

    def _handle_other_traffic(self, from_id: str, decoded: Dict, packet: Dict):
        """
        Handle non-protocol traffic on private channel (standard Meshtastic).

        This handles:
        - TELEMETRY_APP: Device metrics (battery, voltage) and environment metrics
        - TEXT_MESSAGE_APP: Text messages
        """
        portnum = decoded.get("portnum", "")

        # Ignore our own messages
        if from_id == self.my_node_id:
            return

        # Update slave tracking for any traffic
        slave = self._get_or_create_slave(from_id)
        slave.last_seen = time.time()
        slave.is_online = True

        if "text" in decoded:
            self.logger.info(f"[TEXT] {from_id}: {decoded['text']}")

        elif portnum == "TELEMETRY_APP":
            self._handle_standard_telemetry(slave, decoded)

    def _handle_standard_telemetry(self, slave: SlaveNode, decoded: Dict):
        """
        Handle standard Meshtastic telemetry (device and environment metrics).

        This is the built-in Meshtastic telemetry, not our custom protocol.
        """
        telemetry = decoded.get("telemetry", {})

        # Device metrics (battery, voltage, channel utilization)
        device_metrics = telemetry.get("deviceMetrics", {})
        if device_metrics:
            if "batteryLevel" in device_metrics:
                slave.battery_level = device_metrics["batteryLevel"]
            if "voltage" in device_metrics:
                slave.voltage = device_metrics["voltage"]
            if "channelUtilization" in device_metrics:
                slave.channel_utilization = device_metrics["channelUtilization"]
            if "airUtilTx" in device_metrics:
                slave.air_util_tx = device_metrics["airUtilTx"]

            self.logger.info(
                f"[TELEMETRY] {slave.node_id}: "
                f"bat={slave.battery_level}%, "
                f"voltage={slave.voltage:.2f}V, "
                f"ch_util={slave.channel_utilization:.1f}%"
            )

        # Environment metrics (temperature, humidity, pressure from sensors)
        env_metrics = telemetry.get("environmentMetrics", {})
        if env_metrics:
            if "temperature" in env_metrics:
                slave.temperature = env_metrics["temperature"]
            if "relativeHumidity" in env_metrics:
                slave.humidity = env_metrics["relativeHumidity"]
            if "barometricPressure" in env_metrics:
                slave.pressure = env_metrics["barometricPressure"]

            env_str = []
            if slave.temperature is not None:
                env_str.append(f"temp={slave.temperature:.1f}C")
            if slave.humidity is not None:
                env_str.append(f"humidity={slave.humidity:.1f}%")
            if slave.pressure is not None:
                env_str.append(f"pressure={slave.pressure:.1f}hPa")

            if env_str:
                self.logger.info(f"[ENVIRONMENT] {slave.node_id}: {', '.join(env_str)}")

        slave.last_telemetry_time = time.time()
        slave.telemetry_count += 1

        # Dispatch to telemetry handlers
        for handler in self._telemetry_handlers:
            try:
                handler(slave, telemetry)
            except Exception as e:
                self.logger.error(f"Telemetry handler error: {e}")

    def _get_or_create_slave(self, node_id: str) -> SlaveNode:
        """Get existing slave or create new one."""
        if node_id not in self.slaves:
            now = time.time()
            self.slaves[node_id] = SlaveNode(
                node_id=node_id,
                first_seen=now,
                last_seen=now,
            )
            self.logger.info(f"New slave discovered: {node_id}")

        return self.slaves[node_id]

    def _send_ack(self, destination: str, for_msg_type: MessageType):
        """Send acknowledgment to a slave."""
        try:
            ack_data = create_ack_message(for_msg_type)
            self.interface.sendData(
                data=ack_data,
                destinationId=destination,
                portNum=self.config.private_port_num,
                channelIndex=self.config.private_channel_index,
            )
            self.logger.debug(f"ACK sent to {destination} for {for_msg_type.name}")
        except Exception as e:
            self.logger.error(f"Failed to send ACK: {e}")

    # -------------------------------------------------------------------------
    # Private Key Management
    # -------------------------------------------------------------------------

    def _get_device_private_key(self) -> bytes:
        """Get the device's current private key."""
        if not self.interface:
            return b""

        try:
            local_node = self.interface.getNode("^local")
            if hasattr(local_node, "localConfig") and local_node.localConfig:
                security = local_node.localConfig.security
                if hasattr(security, "private_key"):
                    return bytes(security.private_key)
            return b""
        except Exception as e:
            self.logger.error(f"Error getting device private key: {e}")
            return b""

    def _verify_private_key(self) -> bool:
        """Verify the device private key matches configuration."""
        if not self.config.private_key:
            self.logger.info("No private key configured - skipping verification")
            return True

        try:
            expected_key = base64.b64decode(self.config.private_key)
        except Exception as e:
            self.logger.error(f"Invalid base64 private key in config: {e}")
            return False

        device_key = self._get_device_private_key()

        if not device_key:
            self.logger.warning("Could not retrieve device private key")
            return False

        if device_key == expected_key:
            self.logger.info("Private key verification: OK")
            return True

        self.logger.warning("Private key verification: MISMATCH")
        return False

    def _update_private_key(self) -> bool:
        """Update device private key via CLI."""
        if not self.config.private_key:
            self.logger.error("No private key configured")
            return False

        self.logger.info("Updating device private key...")
        self._set_state(MasterState.UPDATING_KEY)

        # Close connection for CLI operation
        if self.interface:
            self.interface.close()
            self.interface = None
            time.sleep(2)

        try:
            cmd = [
                "meshtastic",
                "--set", "security.private_key", f"base64:{self.config.private_key}"
            ]

            if self.config.device_port:
                cmd.extend(["--port", self.config.device_port])

            self.logger.debug(f"Running: {' '.join(cmd)}")

            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=60,
            )

            if result.returncode == 0:
                self.logger.info("Private key updated - device rebooting...")
                time.sleep(10)  # Wait for reboot
                return True
            else:
                self.logger.error(f"CLI failed: {result.stderr}")
                return False

        except subprocess.TimeoutExpired:
            self.logger.error("CLI command timed out")
            return False
        except FileNotFoundError:
            self.logger.error("meshtastic CLI not found")
            return False
        except Exception as e:
            self.logger.error(f"Error updating key: {e}")
            return False

    def _ensure_private_key(self) -> bool:
        """Ensure correct private key - verify and update if needed."""
        self._set_state(MasterState.VERIFYING_KEY)

        if self._verify_private_key():
            return True

        self.logger.info("Private key mismatch - updating automatically...")

        if not self._update_private_key():
            self._set_state(MasterState.ERROR)
            return False

        # Reconnect and verify
        if not self._connect():
            return False

        if not self._verify_private_key():
            self.logger.error("Key verification failed after update")
            self._set_state(MasterState.ERROR)
            return False

        return True

    # -------------------------------------------------------------------------
    # Connection Management
    # -------------------------------------------------------------------------

    def _connect(self) -> bool:
        """Connect to the Meshtastic device."""
        self._set_state(MasterState.CONNECTING)

        if self.interface:
            self.interface.close()
            self.interface = None
            time.sleep(1)

        # Subscribe to events (only once to avoid duplicates)
        if not self._subscribed:
            pub.subscribe(self._on_receive, "meshtastic.receive")
            pub.subscribe(self._on_connection, "meshtastic.connection.established")
            pub.subscribe(self._on_disconnect, "meshtastic.connection.lost")
            self._subscribed = True

        try:
            if self.config.device_port:
                self.logger.info(f"Connecting to {self.config.device_port}...")
                self.interface = meshtastic.serial_interface.SerialInterface(
                    devPath=self.config.device_port
                )
            else:
                self.logger.info("Auto-detecting device...")
                self.interface = meshtastic.serial_interface.SerialInterface()

            # Wait for connection
            start = time.time()
            while not self.my_node_id and (time.time() - start) < self.config.device_timeout:
                time.sleep(0.5)

            if self.my_node_id:
                return True

            self.logger.error("Connection timeout")
            self._set_state(MasterState.ERROR)
            return False

        except Exception as e:
            self.logger.error(f"Connection failed: {e}")
            self._set_state(MasterState.ERROR)
            return False

    # -------------------------------------------------------------------------
    # Public API - Initialization
    # -------------------------------------------------------------------------

    def initialize(self) -> bool:
        """
        Initialize the master controller.

        This method:
        1. Connects to the device
        2. Verifies the private key (updates automatically if mismatch)
        3. Sets state to READY

        Returns:
            True if initialization successful, False otherwise.
        """
        self.logger.info("=" * 60)
        self.logger.info("MASTER CONTROLLER INITIALIZING")
        self.logger.info(f"Private Channel: {self.config.private_channel_index}")
        self.logger.info(f"Private Port: {self.config.private_port_num}")
        self.logger.info("=" * 60)

        # Step 1: Connect
        if not self._connect():
            return False

        # Step 2: Verify/update private key (AUTOMATIC)
        if not self._ensure_private_key():
            return False

        # Ready
        self._set_state(MasterState.READY)
        self.logger.info("Master controller ready - listening for slaves")
        return True

    # -------------------------------------------------------------------------
    # Public API - Commands to Slaves
    # -------------------------------------------------------------------------

    def send_command(
        self,
        destination: str,
        command: CommandType,
        params: List[bytes] = None,
    ) -> bool:
        """
        Send a command to a slave node.

        Args:
            destination: Target slave node ID.
            command: Command type to send.
            params: Optional command parameters.

        Returns:
            True if sent successfully.
        """
        if self.state != MasterState.READY:
            self.logger.error(f"Cannot send: state is {self.state.value}")
            return False

        try:
            cmd = MasterCommand(command=command, params=params or [])
            data = create_command_message(cmd)

            self.interface.sendData(
                data=data,
                destinationId=destination,
                portNum=self.config.private_port_num,
                channelIndex=self.config.private_channel_index,
            )

            self.logger.info(f"Command {command.name} sent to {destination}")
            return True

        except Exception as e:
            self.logger.error(f"Send command failed: {e}")
            return False

    def request_status(self, destination: str) -> bool:
        """
        Request status report from a slave.

        Sends a REQUEST_STATUS message to the slave.
        Slave should respond with a STATUS message.
        """
        if self.state != MasterState.READY:
            return False

        try:
            msg = ProtocolMessage(
                msg_type=MessageType.REQUEST_STATUS,
                flags=MessageFlags.ACK_REQUESTED,
            )
            self.interface.sendData(
                data=msg.encode(),
                destinationId=destination,
                portNum=self.config.private_port_num,
                channelIndex=self.config.private_channel_index,
            )
            self.logger.info(f"Status request sent to {destination}")
            return True
        except Exception as e:
            self.logger.error(f"Request status failed: {e}")
            return False

    def request_data(self, destination: str) -> bool:
        """
        Request slave to send its pending data batches.

        Sends a REQUEST_DATA message to the slave.
        Slave should respond with DATA_BATCH message(s).
        """
        if self.state != MasterState.READY:
            return False

        try:
            msg = ProtocolMessage(
                msg_type=MessageType.REQUEST_DATA,
                flags=MessageFlags.ACK_REQUESTED,
            )
            self.interface.sendData(
                data=msg.encode(),
                destinationId=destination,
                portNum=self.config.private_port_num,
                channelIndex=self.config.private_channel_index,
            )
            self.logger.info(f"Data request sent to {destination}")
            return True
        except Exception as e:
            self.logger.error(f"Request data failed: {e}")
            return False

    def send_heartbeat(self, destination: str = None) -> bool:
        """Send heartbeat to slave(s)."""
        if self.state != MasterState.READY:
            return False

        try:
            data = create_heartbeat_message()
            kwargs = {
                "data": data,
                "portNum": self.config.private_port_num,
                "channelIndex": self.config.private_channel_index,
            }
            if destination:
                kwargs["destinationId"] = destination

            self.interface.sendData(**kwargs)
            return True

        except Exception as e:
            self.logger.error(f"Send heartbeat failed: {e}")
            return False

    def broadcast_command(self, command: CommandType, params: List[bytes] = None) -> bool:
        """Broadcast a command to all slaves."""
        if self.state != MasterState.READY:
            return False

        try:
            cmd = MasterCommand(command=command, params=params or [])
            data = create_command_message(cmd)

            self.interface.sendData(
                data=data,
                portNum=self.config.private_port_num,
                channelIndex=self.config.private_channel_index,
            )

            self.logger.info(f"Broadcast command: {command.name}")
            return True

        except Exception as e:
            self.logger.error(f"Broadcast failed: {e}")
            return False

    # -------------------------------------------------------------------------
    # Public API - Position Broadcasting (Standard Meshtastic)
    # -------------------------------------------------------------------------

    def send_position(
        self,
        latitude: float = None,
        longitude: float = None,
        altitude: int = None,
    ) -> bool:
        """
        Broadcast position on the private channel using standard Meshtastic.

        This uses the standard Meshtastic POSITION_APP, NOT our custom protocol.
        Position is sent ONLY on the private channel, not the public channel.

        Args:
            latitude: GPS latitude (uses config if None).
            longitude: GPS longitude (uses config if None).
            altitude: Altitude in meters (uses config if None).

        Returns:
            True if sent successfully.
        """
        if self.state != MasterState.READY:
            self.logger.error(f"Cannot send position: state is {self.state.value}")
            return False

        lat = latitude if latitude is not None else self.config.fixed_latitude
        lon = longitude if longitude is not None else self.config.fixed_longitude
        alt = altitude if altitude is not None else (self.config.fixed_altitude or 0)

        if lat is None or lon is None:
            self.logger.warning("No position available (set fixed_latitude/longitude in config)")
            return False

        try:
            # Use standard Meshtastic sendPosition on PRIVATE channel only
            self.interface.sendPosition(
                latitude=lat,
                longitude=lon,
                altitude=alt,
                channelIndex=self.config.private_channel_index,
            )

            self.logger.info(
                f"Position broadcast on private CH{self.config.private_channel_index}: "
                f"({lat:.6f}, {lon:.6f}, alt={alt}m)"
            )
            return True

        except Exception as e:
            self.logger.error(f"Send position failed: {e}")
            return False

    # -------------------------------------------------------------------------
    # Public API - Handlers
    # -------------------------------------------------------------------------

    def on_telemetry(self, handler: Callable[[SlaveNode, Dict], None]):
        """
        Register a handler for standard Meshtastic telemetry.

        Handler signature: handler(slave: SlaveNode, telemetry: dict)

        The telemetry dict contains the raw Meshtastic telemetry with:
        - deviceMetrics: batteryLevel, voltage, channelUtilization, airUtilTx
        - environmentMetrics: temperature, relativeHumidity, barometricPressure
        """
        self._telemetry_handlers.append(handler)

    def on_data_batch(self, handler: Callable[[SlaveNode, DataBatch], None]):
        """
        Register a data batch handler.

        Handler signature: handler(slave: SlaveNode, batch: DataBatch)
        """
        self._data_batch_handlers.append(handler)

    def on_status(self, handler: Callable[[SlaveNode, SlaveStatusReport], None]):
        """
        Register a status handler.

        Handler signature: handler(slave: SlaveNode, status: SlaveStatusReport)
        """
        self._status_handlers.append(handler)

    def on_state_change(self, handler: Callable[[MasterState, MasterState], None]):
        """
        Register a state change handler.

        Handler signature: handler(old_state: MasterState, new_state: MasterState)
        """
        self._state_handlers.append(handler)

    # -------------------------------------------------------------------------
    # Public API - Slave Management
    # -------------------------------------------------------------------------

    def get_slaves(self) -> List[SlaveNode]:
        """Get list of all known slave nodes."""
        return list(self.slaves.values())

    def get_slave(self, node_id: str) -> Optional[SlaveNode]:
        """Get a specific slave by node ID."""
        return self.slaves.get(node_id)

    def get_online_slaves(self) -> List[SlaveNode]:
        """Get list of online slave nodes."""
        now = time.time()
        return [
            slave for slave in self.slaves.values()
            if (now - slave.last_seen) < self.config.slave_timeout
        ]

    def get_slave_telemetry(self, node_id: str) -> Optional[Dict]:
        """
        Get current telemetry for a slave.

        Returns:
            Dictionary with current telemetry values, or None if slave not found.
            Keys: battery_level, voltage, channel_utilization, air_util_tx,
                  temperature, humidity, pressure, last_telemetry_time
        """
        slave = self.slaves.get(node_id)
        if not slave:
            return None
        return {
            "battery_level": slave.battery_level,
            "voltage": slave.voltage,
            "channel_utilization": slave.channel_utilization,
            "air_util_tx": slave.air_util_tx,
            "temperature": slave.temperature,
            "humidity": slave.humidity,
            "pressure": slave.pressure,
            "last_telemetry_time": slave.last_telemetry_time,
        }

    def get_slave_batches(self, node_id: str, limit: int = 10) -> List[DataBatch]:
        """Get data batches for a slave."""
        slave = self.slaves.get(node_id)
        if not slave:
            return []
        return list(slave.data_batches)[:limit]

    def update_slave_status(self):
        """Update online/offline status of all slaves."""
        now = time.time()
        for slave in self.slaves.values():
            was_online = slave.is_online
            slave.is_online = (now - slave.last_seen) < self.config.slave_timeout

            if was_online and not slave.is_online:
                self.logger.warning(f"Slave {slave.node_id} went offline")

    # -------------------------------------------------------------------------
    # Public API - Data Export
    # -------------------------------------------------------------------------

    def export_data(self, filepath: str = None) -> str:
        """
        Export all slave data to a JSON file.

        Args:
            filepath: Output file path. Auto-generated if None.

        Returns:
            Path to the exported file.
        """
        if filepath is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filepath = f"master_export_{timestamp}.json"

        export_data = {
            "export_time": datetime.now().isoformat(),
            "master_node_id": self.my_node_id,
            "slaves": {}
        }

        for node_id, slave in self.slaves.items():
            slave_data = {
                "node_id": slave.node_id,
                "long_name": slave.long_name,
                "short_name": slave.short_name,
                "first_seen": slave.first_seen,
                "last_seen": slave.last_seen,
                "is_online": slave.is_online,
                "telemetry_count": slave.telemetry_count,
                "batch_count": slave.batch_count,
                "error_count": slave.error_count,
                "device_telemetry": {
                    "battery_level": slave.battery_level,
                    "voltage": slave.voltage,
                    "channel_utilization": slave.channel_utilization,
                    "air_util_tx": slave.air_util_tx,
                    "temperature": slave.temperature,
                    "humidity": slave.humidity,
                    "pressure": slave.pressure,
                    "last_telemetry_time": slave.last_telemetry_time,
                },
                "data_batches": [
                    {
                        "batch_id": b.batch_id,
                        "record_count": len(b.records),
                        "record_size": b.record_size,
                        "records_hex": [r.hex() for r in b.records],
                    }
                    for b in slave.data_batches
                ],
                "last_status": None,
            }

            if slave.last_status:
                slave_data["last_status"] = {
                    "uptime": slave.last_status.uptime,
                    "status": slave.last_status.status.name,
                    "battery_percent": slave.last_status.battery_percent,
                    "free_memory_kb": slave.last_status.free_memory_kb,
                    "pending_data_bytes": slave.last_status.pending_data_bytes,
                    "error_count": slave.last_status.error_count,
                }

            export_data["slaves"][node_id] = slave_data

        with open(filepath, "w") as f:
            json.dump(export_data, f, indent=2)

        self.logger.info(f"Data exported to {filepath}")
        return filepath

    def export_telemetry_csv(self, filepath: str = None) -> str:
        """
        Export current telemetry state for all slaves to a CSV file.

        Note: This exports the latest telemetry values, not historical data.
        Standard Meshtastic telemetry doesn't provide history - use export_data()
        for data batches which are stored.

        Args:
            filepath: Output file path. Auto-generated if None.

        Returns:
            Path to the exported file.
        """
        if filepath is None:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filepath = f"telemetry_{timestamp}.csv"

        with open(filepath, "w") as f:
            # Header
            f.write("node_id,battery_level,voltage,channel_util,air_util_tx,"
                    "temperature,humidity,pressure,last_seen\n")

            # Data (current state of each slave)
            for node_id, slave in self.slaves.items():
                f.write(
                    f"{node_id},{slave.battery_level},{slave.voltage:.2f},"
                    f"{slave.channel_utilization:.1f},{slave.air_util_tx:.1f},"
                    f"{slave.temperature or ''},{slave.humidity or ''},"
                    f"{slave.pressure or ''},{slave.last_telemetry_time}\n"
                )

        self.logger.info(f"Telemetry exported to {filepath}")
        return filepath

    def get_stats(self) -> Dict[str, Any]:
        """
        Get statistics about the master and all slaves.

        Returns:
            Dictionary with statistics.
        """
        total_telemetry = sum(s.telemetry_count for s in self.slaves.values())
        total_batches = sum(s.batch_count for s in self.slaves.values())
        total_errors = sum(s.error_count for s in self.slaves.values())
        online_count = len(self.get_online_slaves())

        return {
            "master_node_id": self.my_node_id,
            "state": self.state.value,
            "total_slaves": len(self.slaves),
            "online_slaves": online_count,
            "offline_slaves": len(self.slaves) - online_count,
            "total_telemetry_received": total_telemetry,
            "total_batches_received": total_batches,
            "total_errors": total_errors,
        }

    # -------------------------------------------------------------------------
    # Public API - Run Loop
    # -------------------------------------------------------------------------

    def run(self, auto_reconnect: bool = True, max_reconnect_attempts: int = 5):
        """
        Run the master controller main loop.

        This will:
        1. Initialize (connect + verify key)
        2. Periodically broadcast position on private channel (if enabled)
        3. Listen for slave telemetry and data
        4. Periodically update slave status
        5. Auto-reconnect on connection loss (if enabled)

        Args:
            auto_reconnect: Enable automatic reconnection on disconnect.
            max_reconnect_attempts: Maximum consecutive reconnect attempts.
        """
        if not self.initialize():
            self.logger.error("Initialization failed")
            return False

        self.running = True
        last_status_check = 0
        last_position_broadcast = 0
        reconnect_attempts = 0
        STATUS_CHECK_INTERVAL = 60
        RECONNECT_DELAY = 5

        # Log position broadcast settings
        if self.config.position_enabled:
            self.logger.info(
                f"Position broadcasting enabled: every {self.config.position_interval}s "
                f"on private CH{self.config.private_channel_index}"
            )
        else:
            self.logger.info("Position broadcasting disabled")

        try:
            while self.running:
                # Check for disconnection and attempt reconnect
                if self.state == MasterState.DISCONNECTED:
                    if auto_reconnect and reconnect_attempts < max_reconnect_attempts:
                        reconnect_attempts += 1
                        self.logger.info(
                            f"Attempting reconnect ({reconnect_attempts}/{max_reconnect_attempts})..."
                        )
                        time.sleep(RECONNECT_DELAY)

                        if self._connect():
                            self._set_state(MasterState.READY)
                            reconnect_attempts = 0
                            self.logger.info("Reconnected successfully")
                        else:
                            self.logger.warning("Reconnect failed")
                    else:
                        if reconnect_attempts >= max_reconnect_attempts:
                            self.logger.error("Max reconnect attempts reached")
                        break

                # Normal operation when READY
                if self.state == MasterState.READY:
                    reconnect_attempts = 0  # Reset on successful operation
                    now = time.time()

                    # Periodically broadcast position on private channel
                    if self.config.position_enabled:
                        if (now - last_position_broadcast) >= self.config.position_interval:
                            self.send_position()
                            last_position_broadcast = now

                    # Periodically check slave status
                    if (now - last_status_check) >= STATUS_CHECK_INTERVAL:
                        self.update_slave_status()
                        last_status_check = now

                time.sleep(1)

        except KeyboardInterrupt:
            self.logger.info("Interrupted by user")
        finally:
            self.shutdown()

        return True

    def shutdown(self):
        """Shutdown the master controller."""
        self.logger.info("Shutting down master controller...")
        self.running = False

        if self.interface:
            self.interface.close()
            self.interface = None

        self._set_state(MasterState.DISCONNECTED)
        self.logger.info("Master controller stopped")


# =============================================================================
# CLI Entry Point
# =============================================================================

def main():
    """Command-line entry point."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Meshtastic Master Controller",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python -m meshtastic_app.master                # Run with default config
  python -m meshtastic_app.master -c config.yaml # Run with custom config
  python -m meshtastic_app.master --info         # Show device info and exit
        """
    )
    parser.add_argument(
        "-c", "--config",
        default="config.yaml",
        help="Path to config file (default: config.yaml)",
    )
    parser.add_argument(
        "--info",
        action="store_true",
        help="Show device info and exit",
    )

    args = parser.parse_args()

    try:
        config = MasterConfig.from_yaml(args.config)
    except FileNotFoundError:
        print(f"Error: Config file not found: {args.config}")
        sys.exit(1)
    except Exception as e:
        print(f"Error loading config: {e}")
        sys.exit(1)

    master = MasterController(config)

    if args.info:
        if master.initialize():
            print("\n" + "=" * 50)
            print("MASTER DEVICE INFO")
            print("=" * 50)
            print(f"Node ID:         {master.my_node_id}")
            user = master.my_node_info.get("user", {})
            print(f"Long Name:       {user.get('longName', 'Unknown')}")
            print(f"Short Name:      {user.get('shortName', 'Unknown')}")
            print(f"Hardware:        {user.get('hwModel', 'Unknown')}")
            print(f"State:           {master.state.value}")
            print(f"Private Channel: {master.config.private_channel_index}")
            print(f"Private Port:    {master.config.private_port_num}")
            master.shutdown()
        sys.exit(0)

    # Run main loop
    master.run()


if __name__ == "__main__":
    main()
