#!/usr/bin/env python3
"""
Meshtastic Master Controller Module

This module acts as the master controller for a Meshtastic mesh network.
It connects to a local device via USB/Serial and communicates with slave
nodes over the private channel.

Features:
- Automatic private key verification and update on startup
- Position transmission on private channel
- Framework for commanding slave nodes
"""

import base64
import logging
import subprocess
import sys
import time
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Callable, Dict, List, Optional

import yaml

try:
    import meshtastic
    import meshtastic.serial_interface
    from pubsub import pub
except ImportError:
    print("Error: meshtastic package not installed.")
    print("Install with: pip3 install --upgrade 'meshtastic[cli]'")
    sys.exit(1)


class MasterState(Enum):
    """Master controller states."""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    VERIFYING_KEY = "verifying_key"
    UPDATING_KEY = "updating_key"
    READY = "ready"
    ERROR = "error"


@dataclass
class SlaveNode:
    """Represents a slave node in the mesh network."""
    node_id: str
    long_name: str = ""
    short_name: str = ""
    last_seen: float = 0.0
    last_position: Optional[Dict] = None
    is_online: bool = False


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

    # Position settings
    position_enabled: bool = True
    position_interval: int = 300
    fixed_latitude: Optional[float] = None
    fixed_longitude: Optional[float] = None
    fixed_altitude: Optional[int] = None

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
            position_enabled=data.get("position", {}).get("enabled", True),
            position_interval=data.get("position", {}).get("interval_seconds", 300),
            fixed_latitude=data.get("position", {}).get("fixed_latitude"),
            fixed_longitude=data.get("position", {}).get("fixed_longitude"),
            fixed_altitude=data.get("position", {}).get("fixed_altitude"),
            log_level=data.get("logging", {}).get("level", "INFO"),
            log_file=data.get("logging", {}).get("file", "master.log"),
        )


class MasterController:
    """
    Master controller for Meshtastic mesh network.

    This class manages the connection to the local Meshtastic device,
    handles automatic private key verification/update, and provides
    methods for communicating with slave nodes on the private channel.
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
        self.slave_nodes: Dict[str, SlaveNode] = {}

        # Callbacks
        self._message_handlers: List[Callable] = []
        self._position_handlers: List[Callable] = []
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
            to_id = packet.get("toId", "")
            channel = packet.get("channel", 0)
            portnum = decoded.get("portnum", "")

            # Only process packets on our private channel
            if channel != self.config.private_channel_index:
                return

            # Update slave node tracking
            self._update_slave_node(from_id, packet)

            # Handle text messages
            if "text" in decoded:
                text = decoded["text"]
                self.logger.info(f"[CH{channel}] {from_id}: {text}")
                self._dispatch_message(from_id, text, packet)

            # Handle position updates
            elif portnum == "POSITION_APP":
                pos = decoded.get("position", {})
                self.logger.debug(
                    f"[POS] {from_id}: {pos.get('latitude')}, {pos.get('longitude')}"
                )
                self._dispatch_position(from_id, pos, packet)

            # Handle telemetry
            elif portnum == "TELEMETRY_APP":
                self.logger.debug(f"[TELEMETRY] {from_id}: {decoded}")

        except Exception as e:
            self.logger.error(f"Error processing packet: {e}")

    def _update_slave_node(self, node_id: str, packet: Dict):
        """Update slave node tracking info."""
        if node_id == self.my_node_id:
            return

        if node_id not in self.slave_nodes:
            self.slave_nodes[node_id] = SlaveNode(node_id=node_id)
            self.logger.info(f"New slave node discovered: {node_id}")

        node = self.slave_nodes[node_id]
        node.last_seen = time.time()
        node.is_online = True

        # Update position if available
        decoded = packet.get("decoded", {})
        if decoded.get("portnum") == "POSITION_APP":
            node.last_position = decoded.get("position")

    def _dispatch_message(self, from_id: str, text: str, packet: Dict):
        """Dispatch text message to handlers."""
        for handler in self._message_handlers:
            try:
                handler(from_id, text, packet)
            except Exception as e:
                self.logger.error(f"Message handler error: {e}")

    def _dispatch_position(self, from_id: str, position: Dict, packet: Dict):
        """Dispatch position update to handlers."""
        for handler in self._position_handlers:
            try:
                handler(from_id, position, packet)
            except Exception as e:
                self.logger.error(f"Position handler error: {e}")

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

        # Subscribe to events
        pub.subscribe(self._on_receive, "meshtastic.receive")
        pub.subscribe(self._on_connection, "meshtastic.connection.established")
        pub.subscribe(self._on_disconnect, "meshtastic.connection.lost")

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
        self.logger.info("=" * 60)

        # Step 1: Connect
        if not self._connect():
            return False

        # Step 2: Verify/update private key (AUTOMATIC)
        if not self._ensure_private_key():
            return False

        # Ready
        self._set_state(MasterState.READY)
        self.logger.info("Master controller ready")
        return True

    # -------------------------------------------------------------------------
    # Public API - Communication
    # -------------------------------------------------------------------------

    def send_text(self, text: str, destination: str = None) -> bool:
        """
        Send text message on private channel.

        Args:
            text: Message text.
            destination: Target node ID (None for broadcast).

        Returns:
            True if sent successfully.
        """
        if self.state != MasterState.READY:
            self.logger.error(f"Cannot send: state is {self.state.value}")
            return False

        try:
            kwargs = {
                "text": text,
                "channelIndex": self.config.private_channel_index,
            }
            if destination:
                kwargs["destinationId"] = destination

            self.interface.sendText(**kwargs)
            self.logger.info(f"Sent on CH{self.config.private_channel_index}: {text}")
            return True

        except Exception as e:
            self.logger.error(f"Send failed: {e}")
            return False

    def send_position(
        self,
        latitude: float = None,
        longitude: float = None,
        altitude: int = None,
    ) -> bool:
        """
        Send position on private channel.

        Args:
            latitude: GPS latitude (uses config if None).
            longitude: GPS longitude (uses config if None).
            altitude: Altitude in meters (uses config if None).

        Returns:
            True if sent successfully.
        """
        if self.state != MasterState.READY:
            self.logger.error(f"Cannot send: state is {self.state.value}")
            return False

        lat = latitude if latitude is not None else self.config.fixed_latitude
        lon = longitude if longitude is not None else self.config.fixed_longitude
        alt = altitude if altitude is not None else (self.config.fixed_altitude or 0)

        if lat is None or lon is None:
            self.logger.warning("No position available")
            return False

        try:
            self.interface.sendPosition(
                latitude=lat,
                longitude=lon,
                altitude=alt,
                channelIndex=self.config.private_channel_index,
            )
            self.logger.info(
                f"Position sent on CH{self.config.private_channel_index}: "
                f"({lat}, {lon}, alt={alt})"
            )
            return True

        except Exception as e:
            self.logger.error(f"Send position failed: {e}")
            return False

    def send_data(self, data: bytes, destination: str = None, port_num: int = 256) -> bool:
        """
        Send binary data on private channel.

        Args:
            data: Binary payload.
            destination: Target node ID (None for broadcast).
            port_num: Application port number (default: PRIVATE_APP = 256).

        Returns:
            True if sent successfully.
        """
        if self.state != MasterState.READY:
            self.logger.error(f"Cannot send: state is {self.state.value}")
            return False

        try:
            kwargs = {
                "data": data,
                "portNum": port_num,
                "channelIndex": self.config.private_channel_index,
            }
            if destination:
                kwargs["destinationId"] = destination

            self.interface.sendData(**kwargs)
            self.logger.debug(f"Data sent: {len(data)} bytes")
            return True

        except Exception as e:
            self.logger.error(f"Send data failed: {e}")
            return False

    # -------------------------------------------------------------------------
    # Public API - Handlers
    # -------------------------------------------------------------------------

    def on_message(self, handler: Callable[[str, str, Dict], None]):
        """
        Register a message handler.

        Handler signature: handler(from_id: str, text: str, packet: dict)
        """
        self._message_handlers.append(handler)

    def on_position(self, handler: Callable[[str, Dict, Dict], None]):
        """
        Register a position handler.

        Handler signature: handler(from_id: str, position: dict, packet: dict)
        """
        self._position_handlers.append(handler)

    def on_state_change(self, handler: Callable[[MasterState, MasterState], None]):
        """
        Register a state change handler.

        Handler signature: handler(old_state: MasterState, new_state: MasterState)
        """
        self._state_handlers.append(handler)

    # -------------------------------------------------------------------------
    # Public API - Slave Management
    # -------------------------------------------------------------------------

    def get_slave_nodes(self) -> List[SlaveNode]:
        """Get list of known slave nodes."""
        return list(self.slave_nodes.values())

    def get_online_slaves(self, timeout: float = 300.0) -> List[SlaveNode]:
        """Get list of recently seen slave nodes."""
        now = time.time()
        return [
            node for node in self.slave_nodes.values()
            if (now - node.last_seen) < timeout
        ]

    # -------------------------------------------------------------------------
    # Public API - Run Loop
    # -------------------------------------------------------------------------

    def run(self):
        """
        Run the master controller main loop.

        This will:
        1. Initialize (connect + verify key)
        2. Periodically send position on private channel
        3. Process incoming messages
        """
        if not self.initialize():
            self.logger.error("Initialization failed")
            return False

        self.running = True
        last_position_time = 0

        try:
            while self.running and self.state == MasterState.READY:
                now = time.time()

                # Send position at configured interval
                if self.config.position_enabled:
                    if (now - last_position_time) >= self.config.position_interval:
                        self.send_position()
                        last_position_time = now

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
  python master.py                    # Run with default config
  python master.py -c myconfig.yaml   # Run with custom config
  python master.py --info             # Show device info and exit
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
            print("\n" + "=" * 40)
            print("DEVICE INFO")
            print("=" * 40)
            print(f"Node ID:    {master.my_node_id}")
            user = master.my_node_info.get("user", {})
            print(f"Long Name:  {user.get('longName', 'Unknown')}")
            print(f"Short Name: {user.get('shortName', 'Unknown')}")
            print(f"Hardware:   {user.get('hwModel', 'Unknown')}")
            print(f"State:      {master.state.value}")
            master.shutdown()
        sys.exit(0)

    # Run main loop
    master.run()


if __name__ == "__main__":
    main()
