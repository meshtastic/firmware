#!/usr/bin/env python3
"""
Meshtastic Master Controller

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
- REST API for web access
"""

import json
import logging
import sys
import time
from datetime import datetime
from typing import Any, Callable, Dict, List, Optional

from .device import DeviceManager
from .models import (
    MasterConfig,
    MasterState,
    SlaveNode,
    MAX_SLAVES,
    MAX_EVENT_HANDLERS,
)
from .protocol import (
    CommandType,
    DataBatch,
    MasterCommand,
    MessageFlags,
    MessageType,
    ProtocolMessage,
    SlaveStatusReport,
    TimestampedBatch,
    create_ack_message,
    create_command_message,
    create_heartbeat_message,
    parse_message,
)
from .storage import DataStorage


class MasterController:
    """
    Master controller for Meshtastic mesh network.

    This class manages communication with slave nodes using a binary protocol
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
        self.state = MasterState.DISCONNECTED
        self.running = False

        # Device manager for connection and hardware operations
        self.device = DeviceManager(config, self.logger)

        # Data storage for timestamped batches
        self.storage = DataStorage()

        # Node tracking
        self.slaves: Dict[str, SlaveNode] = {}

        # Event handlers
        self._telemetry_handlers: List[Callable] = []
        self._data_batch_handlers: List[Callable] = []
        self._status_handlers: List[Callable] = []
        self._state_handlers: List[Callable] = []

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
    # Properties (delegate to device manager)
    # -------------------------------------------------------------------------

    @property
    def my_node_id(self) -> str:
        """Get this device's node ID."""
        return self.device.my_node_id

    @property
    def my_node_info(self) -> dict:
        """Get this device's node info."""
        return self.device.my_node_info

    @property
    def interface(self):
        """Get the Meshtastic interface (for compatibility)."""
        return self.device.interface

    # -------------------------------------------------------------------------
    # Event Handlers
    # -------------------------------------------------------------------------

    def _on_connection(self, interface, topic=None):
        """Handle connection established."""
        self.logger.info("Connected to Meshtastic device")
        self.device.handle_connection(interface, topic)

    def _on_disconnect(self, interface, topic=None):
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
            slave = self._get_or_create_slave(from_id)
            if slave:
                slave.error_count += 1
            return

        self.logger.debug(
            f"Protocol message from {from_id}: type={msg.msg_type.name}, "
            f"flags={msg.flags:#x}, payload_len={len(msg.payload)}"
        )

        slave = self._get_or_create_slave(from_id)
        if not slave:
            self.logger.warning(f"Cannot track slave {from_id}")
            return

        slave.last_seen = time.time()
        slave.is_online = True

        batch_id = None

        if msg.msg_type == MessageType.DATA_BATCH:
            self._handle_data_batch(slave, payload, msg)
            if payload:
                batch_id = payload.batch_id

        elif msg.msg_type == MessageType.TIMESTAMPED_BATCH:
            self._handle_timestamped_batch(slave, payload, msg)
            if payload:
                batch_id = payload.batch_id

        elif msg.msg_type == MessageType.STATUS:
            self._handle_status(slave, payload, msg)

        elif msg.msg_type == MessageType.HEARTBEAT:
            self.logger.debug(f"Heartbeat from {from_id}")

        elif msg.msg_type == MessageType.SLAVE_ACK:
            self.logger.debug(f"ACK from {from_id}")

        # Send ACK if requested
        if (msg.flags & MessageFlags.ACK_REQUESTED) and self.config.send_ack:
            self._send_ack(from_id, msg.msg_type, batch_id)

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

        for handler in self._data_batch_handlers:
            try:
                handler(slave, batch)
            except Exception as e:
                self.logger.error(f"Data batch handler error: {e}")

    def _handle_timestamped_batch(self, slave: SlaveNode, batch: TimestampedBatch, msg: ProtocolMessage):
        """Handle incoming timestamped batch - store to database."""
        if not batch:
            self.logger.warning(f"Invalid timestamped batch from {slave.node_id}")
            slave.error_count += 1
            return

        # Store to SQLite database
        stored = self.storage.store_batch(
            slave_id=slave.node_id,
            batch_id=batch.batch_id,
            batch_timestamp=batch.batch_timestamp,
            records=batch.to_storage_format(),
        )

        slave.batch_count += 1

        self.logger.info(
            f"[TIMESTAMPED_BATCH] {slave.node_id}: "
            f"batch_id={batch.batch_id}, "
            f"timestamp={batch.batch_timestamp}, "
            f"records={len(batch.records)}, "
            f"stored={stored}"
        )

        # Also call data batch handlers (for backwards compatibility)
        for handler in self._data_batch_handlers:
            try:
                handler(slave, batch)
            except Exception as e:
                self.logger.error(f"Data batch handler error: {e}")

    def _handle_status(self, slave: SlaveNode, status: SlaveStatusReport, msg: ProtocolMessage):
        """Handle incoming extended status report."""
        if not status:
            self.logger.warning(f"Invalid status from {slave.node_id}")
            slave.error_count += 1
            return

        slave.last_status = status
        slave.battery_level = status.battery_percent
        if status.voltage_mv > 0:
            slave.voltage = status.voltage_mv / 1000.0

        self.logger.info(
            f"[STATUS] {slave.node_id}: "
            f"uptime={status.uptime}s, "
            f"bat={status.battery_percent}%, "
            f"voltage={status.voltage_mv}mV, "
            f"status={status.status.name}"
        )

        mem_parts = [f"heap={status.free_heap_kb}KB"]
        if status.total_fram_kb > 0:
            mem_parts.append(f"fram={status.free_fram_kb}/{status.total_fram_kb}KB")
        if status.total_flash_kb > 0:
            mem_parts.append(f"flash={status.free_flash_kb}/{status.total_flash_kb}KB")
        mem_parts.append(f"pending={status.pending_data_bytes}B")
        mem_parts.append(f"errors={status.error_count}")

        self.logger.info(f"[MEMORY] {slave.node_id}: {', '.join(mem_parts)}")

        for handler in self._status_handlers:
            try:
                handler(slave, status)
            except Exception as e:
                self.logger.error(f"Status handler error: {e}")

    def _handle_other_traffic(self, from_id: str, decoded: Dict, packet: Dict):
        """Handle non-protocol traffic on private channel."""
        portnum = decoded.get("portnum", "")

        if from_id == self.my_node_id:
            return

        slave = self._get_or_create_slave(from_id)
        if not slave:
            return

        slave.last_seen = time.time()
        slave.is_online = True

        if "text" in decoded:
            self.logger.info(f"[TEXT] {from_id}: {decoded['text']}")

        elif portnum == "TELEMETRY_APP":
            self._handle_standard_telemetry(slave, decoded)

    def _handle_standard_telemetry(self, slave: SlaveNode, decoded: Dict):
        """Handle standard Meshtastic telemetry."""
        telemetry = decoded.get("telemetry", {})

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

        for handler in self._telemetry_handlers:
            try:
                handler(slave, telemetry)
            except Exception as e:
                self.logger.error(f"Telemetry handler error: {e}")

    def _get_or_create_slave(self, node_id: str) -> Optional[SlaveNode]:
        """Get existing slave or create new one."""
        if not node_id or not isinstance(node_id, str):
            self.logger.error("Invalid node_id for slave tracking")
            return None

        if node_id not in self.slaves:
            if len(self.slaves) >= MAX_SLAVES:
                self.logger.warning(f"Max slaves ({MAX_SLAVES}) reached, cannot track {node_id}")
                return None

            now = time.time()
            self.slaves[node_id] = SlaveNode(
                node_id=node_id,
                first_seen=now,
                last_seen=now,
            )
            self.logger.info(f"New slave discovered: {node_id} (total: {len(self.slaves)})")

        return self.slaves[node_id]

    def _send_ack(self, destination: str, for_msg_type: MessageType, batch_id: int = None):
        """Send acknowledgment to a slave."""
        try:
            ack_data = create_ack_message(for_msg_type, batch_id)
            self.device.send_data(ack_data, destination)
            if batch_id is not None:
                self.logger.debug(f"ACK sent to {destination} for {for_msg_type.name} batch_id={batch_id}")
            else:
                self.logger.debug(f"ACK sent to {destination} for {for_msg_type.name}")
        except Exception as e:
            self.logger.error(f"Failed to send ACK: {e}")

    # -------------------------------------------------------------------------
    # Connection Management
    # -------------------------------------------------------------------------

    def _connect(self) -> bool:
        """Connect to the Meshtastic device."""
        self._set_state(MasterState.CONNECTING)
        return self.device.connect(
            on_receive=self._on_receive,
            on_connection=self._on_connection,
            on_disconnect=self._on_disconnect,
        )

    # -------------------------------------------------------------------------
    # Public API - Initialization
    # -------------------------------------------------------------------------

    def initialize(self) -> bool:
        """
        Initialize the master controller.

        Returns:
            True if initialization successful.
        """
        self.logger.info("=" * 60)
        self.logger.info("MASTER CONTROLLER INITIALIZING")
        self.logger.info(f"Private Channel: {self.config.private_channel_index}")
        self.logger.info(f"Private Port: {self.config.private_port_num}")
        self.logger.info("=" * 60)

        if not self._connect():
            self._set_state(MasterState.ERROR)
            return False

        self._set_state(MasterState.VERIFYING_KEY)
        if not self.device.ensure_private_key(reconnect_callback=self._connect):
            self._set_state(MasterState.ERROR)
            return False

        self._set_state(MasterState.READY)
        self.logger.info("Master controller ready - listening for slaves")
        return True

    def configure_device(self) -> bool:
        """Configure device settings via CLI."""
        return self.device.configure_device()

    # -------------------------------------------------------------------------
    # Public API - Commands to Slaves
    # -------------------------------------------------------------------------

    def send_command(
        self,
        destination: str,
        command: CommandType,
        params: List[bytes] = None,
    ) -> bool:
        """Send a command to a slave node."""
        if self.state != MasterState.READY:
            self.logger.error(f"Cannot send: state is {self.state.value}")
            return False

        try:
            cmd = MasterCommand(command=command, params=params or [])
            data = create_command_message(cmd)
            self.device.send_data(data, destination)
            self.logger.info(f"Command {command.name} sent to {destination}")
            return True

        except Exception as e:
            self.logger.error(f"Send command failed: {e}")
            return False

    def request_status(self, destination: str) -> bool:
        """Request status report from a slave."""
        if self.state != MasterState.READY:
            return False

        try:
            msg = ProtocolMessage(
                msg_type=MessageType.REQUEST_STATUS,
                flags=MessageFlags.ACK_REQUESTED,
            )
            self.device.send_data(msg.encode(), destination)
            self.logger.info(f"Status request sent to {destination}")
            return True
        except Exception as e:
            self.logger.error(f"Request status failed: {e}")
            return False

    def request_data(self, destination: str) -> bool:
        """Request slave to send its pending data batches."""
        if self.state != MasterState.READY:
            return False

        try:
            msg = ProtocolMessage(
                msg_type=MessageType.REQUEST_DATA,
                flags=MessageFlags.ACK_REQUESTED,
            )
            self.device.send_data(msg.encode(), destination)
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
            self.device.send_data(data, destination)
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
            self.device.send_data(data)
            self.logger.info(f"Broadcast command: {command.name}")
            return True
        except Exception as e:
            self.logger.error(f"Broadcast failed: {e}")
            return False

    # -------------------------------------------------------------------------
    # Public API - Remote Administration
    # -------------------------------------------------------------------------

    def get_public_key(self) -> Optional[str]:
        """Get the master device's public key."""
        return self.device.get_public_key()

    def admin_get_setting(self, destination: str, setting: str) -> bool:
        """Get a setting from a remote slave via admin module."""
        return self.device.admin_get_setting(destination, setting)

    def admin_set_setting(self, destination: str, setting: str, value: str) -> bool:
        """Set a setting on a remote slave via admin module."""
        return self.device.admin_set_setting(destination, setting, value)

    def setup_slave_admin_key(self, destination: str) -> bool:
        """Configure a slave's admin_key with master's public key."""
        return self.device.setup_slave_admin_key(destination)

    # -------------------------------------------------------------------------
    # Public API - Position Broadcasting
    # -------------------------------------------------------------------------

    def send_position(
        self,
        latitude: float = None,
        longitude: float = None,
        altitude: int = None,
    ) -> bool:
        """Broadcast position on the private channel."""
        if self.state != MasterState.READY:
            self.logger.error(f"Cannot send position: state is {self.state.value}")
            return False

        lat = latitude if latitude is not None else self.config.fixed_latitude
        lon = longitude if longitude is not None else self.config.fixed_longitude
        alt = altitude if altitude is not None else (self.config.fixed_altitude or 0)

        if lat is None or lon is None:
            self.logger.warning("No position available")
            return False

        if self.device.send_position(lat, lon, alt):
            self.logger.info(
                f"Position broadcast on private CH{self.config.private_channel_index}: "
                f"({lat:.6f}, {lon:.6f}, alt={alt}m)"
            )
            return True
        return False

    # -------------------------------------------------------------------------
    # Public API - Event Handlers
    # -------------------------------------------------------------------------

    def on_telemetry(self, handler: Callable[[SlaveNode, Dict], None]) -> bool:
        """Register a handler for standard Meshtastic telemetry."""
        if len(self._telemetry_handlers) >= MAX_EVENT_HANDLERS:
            self.logger.warning("Max telemetry handlers reached")
            return False
        self._telemetry_handlers.append(handler)
        return True

    def on_data_batch(self, handler: Callable[[SlaveNode, DataBatch], None]) -> bool:
        """Register a data batch handler."""
        if len(self._data_batch_handlers) >= MAX_EVENT_HANDLERS:
            self.logger.warning("Max data batch handlers reached")
            return False
        self._data_batch_handlers.append(handler)
        return True

    def on_status(self, handler: Callable[[SlaveNode, SlaveStatusReport], None]) -> bool:
        """Register a status handler."""
        if len(self._status_handlers) >= MAX_EVENT_HANDLERS:
            self.logger.warning("Max status handlers reached")
            return False
        self._status_handlers.append(handler)
        return True

    def on_state_change(self, handler: Callable[[MasterState, MasterState], None]) -> bool:
        """Register a state change handler."""
        if len(self._state_handlers) >= MAX_EVENT_HANDLERS:
            self.logger.warning("Max state handlers reached")
            return False
        self._state_handlers.append(handler)
        return True

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
        """Get current telemetry for a slave."""
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
        """Export all slave data to a JSON file."""
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
                    "voltage_mv": slave.last_status.voltage_mv,
                    "free_heap_kb": slave.last_status.free_heap_kb,
                    "pending_data_bytes": slave.last_status.pending_data_bytes,
                    "error_count": slave.last_status.error_count,
                }

            export_data["slaves"][node_id] = slave_data

        with open(filepath, "w") as f:
            json.dump(export_data, f, indent=2)

        self.logger.info(f"Data exported to {filepath}")
        return filepath

    def get_stats(self) -> Dict[str, Any]:
        """Get statistics about the master and all slaves."""
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

        Args:
            auto_reconnect: Enable automatic reconnection on disconnect.
            max_reconnect_attempts: Maximum consecutive reconnect attempts.
        """
        if not self.initialize():
            self.logger.error("Initialization failed")
            return False

        self.running = True

        if self.config.position_broadcast_enabled:
            self.logger.info(
                f"Position broadcasting enabled: every {self.config.position_broadcast_interval}s "
                f"on private CH{self.config.private_channel_index}"
            )
        else:
            self.logger.info("Position broadcasting disabled")

        try:
            self._run_loop(auto_reconnect, max_reconnect_attempts)
        except KeyboardInterrupt:
            self.logger.info("Interrupted by user")
        finally:
            self.shutdown()

        return True

    def _run_loop(self, auto_reconnect: bool, max_reconnect_attempts: int):
        """Internal run loop."""
        last_status_check = 0
        last_position_broadcast = 0
        reconnect_attempts = 0
        STATUS_CHECK_INTERVAL = 60
        RECONNECT_DELAY = 5

        while self.running:
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

            if self.state == MasterState.READY:
                reconnect_attempts = 0
                now = time.time()

                if self.config.position_broadcast_enabled:
                    if (now - last_position_broadcast) >= self.config.position_broadcast_interval:
                        self.send_position()
                        last_position_broadcast = now

                if (now - last_status_check) >= STATUS_CHECK_INTERVAL:
                    self.update_slave_status()
                    last_status_check = now

            time.sleep(1)

    def run_with_api(
        self,
        api_host: str = None,
        api_port: int = None,
        auto_reconnect: bool = True,
        max_reconnect_attempts: int = 5,
    ):
        """
        Run the master controller with the REST API server.

        Args:
            api_host: Host to bind API server to (uses config if None).
            api_port: Port for API server (uses config if None).
            auto_reconnect: Enable automatic reconnection.
            max_reconnect_attempts: Maximum reconnect attempts.
        """
        try:
            from .api import create_api
        except ImportError:
            self.logger.error("API module requires fastapi and uvicorn")
            self.logger.error("Install with: pip3 install fastapi uvicorn")
            return False

        import threading

        host = api_host or self.config.api.host
        port = api_port or self.config.api.port

        if not self.initialize():
            self.logger.error("Initialization failed")
            return False

        app = create_api(self)

        def master_loop():
            self._run_loop(auto_reconnect, max_reconnect_attempts)

        self.running = True
        master_thread = threading.Thread(target=master_loop, daemon=True)
        master_thread.start()

        self.logger.info(f"API server starting on http://{host}:{port}")
        self.logger.info(f"API docs available at http://{host}:{port}/api/docs")

        try:
            import uvicorn
            uvicorn.run(app, host=host, port=port, log_level="info")
        except KeyboardInterrupt:
            self.logger.info("Interrupted by user")
        finally:
            self.shutdown()

        return True

    def shutdown(self):
        """Shutdown the master controller."""
        self.logger.info("Shutting down master controller...")
        self.running = False
        self.device.disconnect()
        self._set_state(MasterState.DISCONNECTED)
        self.logger.info("Master controller stopped")
