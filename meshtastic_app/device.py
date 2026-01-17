#!/usr/bin/env python3
"""
Device Operations for Meshtastic Master Controller

This module handles all device-related operations:
- Serial connection management
- Private key verification and update
- Device configuration via CLI
- Remote administration via admin module
"""

import base64
import logging
import subprocess
import time
from typing import List, Optional

from .models import MasterConfig, MasterState, CLI_TIMEOUT_SECONDS, DEVICE_REBOOT_WAIT


class DeviceManager:
    """
    Manages Meshtastic device operations.

    Handles connection, key management, device configuration,
    and remote administration.
    """

    def __init__(self, config: MasterConfig, logger: logging.Logger = None):
        """
        Initialize device manager.

        Args:
            config: Master configuration.
            logger: Logger instance (creates one if not provided).
        """
        self.config = config
        self.logger = logger or logging.getLogger("DeviceManager")
        self.interface = None
        self.my_node_id: str = ""
        self.my_node_info: dict = {}
        self._subscribed = False

    # -------------------------------------------------------------------------
    # Connection Management
    # -------------------------------------------------------------------------

    def connect(self, on_receive=None, on_connection=None, on_disconnect=None) -> bool:
        """
        Connect to the Meshtastic device.

        Args:
            on_receive: Callback for received packets.
            on_connection: Callback for connection established.
            on_disconnect: Callback for connection lost.

        Returns:
            True if connected successfully.
        """
        try:
            import meshtastic
            import meshtastic.serial_interface
            from pubsub import pub
        except ImportError:
            self.logger.error("meshtastic package not installed")
            return False

        if self.interface:
            self.interface.close()
            self.interface = None
            time.sleep(1)

        # Subscribe to events (only once to avoid duplicates)
        if not self._subscribed:
            if on_receive:
                pub.subscribe(on_receive, "meshtastic.receive")
            if on_connection:
                pub.subscribe(on_connection, "meshtastic.connection.established")
            if on_disconnect:
                pub.subscribe(on_disconnect, "meshtastic.connection.lost")
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
            return False

        except Exception as e:
            self.logger.error(f"Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from the device."""
        if self.interface:
            self.interface.close()
            self.interface = None

    def handle_connection(self, interface, topic=None):
        """
        Handle connection established event.

        Call this from your connection callback to update node info.
        """
        try:
            self.my_node_info = interface.getMyNodeInfo()
            user = self.my_node_info.get("user", {})
            self.my_node_id = user.get("id", "")

            self.logger.info(f"Device: {user.get('longName', 'Unknown')}")
            self.logger.info(f"Node ID: {self.my_node_id}")
            self.logger.info(f"Hardware: {user.get('hwModel', 'Unknown')}")
        except Exception as e:
            self.logger.warning(f"Could not get node info: {e}")

    # -------------------------------------------------------------------------
    # Private Key Management
    # -------------------------------------------------------------------------

    def get_device_private_key(self) -> bytes:
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

    def verify_private_key(self) -> bool:
        """
        Verify the device private key matches configuration.

        Returns:
            True if key matches or no key configured.
        """
        if not self.config.private_key:
            self.logger.info("No private key configured - skipping verification")
            return True

        try:
            expected_key = base64.b64decode(self.config.private_key)
        except Exception as e:
            self.logger.error(f"Invalid base64 private key in config: {e}")
            return False

        device_key = self.get_device_private_key()

        if not device_key:
            self.logger.warning("Could not retrieve device private key")
            return False

        if device_key == expected_key:
            self.logger.info("Private key verification: OK")
            return True

        self.logger.warning("Private key verification: MISMATCH")
        return False

    def update_private_key(self) -> bool:
        """
        Update device private key via CLI.

        Returns:
            True if update successful.
        """
        if not self.config.private_key:
            self.logger.error("No private key configured")
            return False

        self.logger.info("Updating device private key...")

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
                timeout=CLI_TIMEOUT_SECONDS,
            )

            if result.returncode == 0:
                self.logger.info("Private key updated - device rebooting...")
                time.sleep(DEVICE_REBOOT_WAIT)
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

    def ensure_private_key(self, reconnect_callback=None) -> bool:
        """
        Ensure correct private key - verify and update if needed.

        Args:
            reconnect_callback: Function to call to reconnect after key update.

        Returns:
            True if key is correct (verified or updated).
        """
        if self.verify_private_key():
            return True

        self.logger.info("Private key mismatch - updating automatically...")

        if not self.update_private_key():
            return False

        # Reconnect and verify
        if reconnect_callback:
            if not reconnect_callback():
                return False

        if not self.verify_private_key():
            self.logger.error("Key verification failed after update")
            return False

        return True

    # -------------------------------------------------------------------------
    # Device Configuration
    # -------------------------------------------------------------------------

    def configure_device(self) -> bool:
        """
        Configure the Meshtastic device settings via CLI.

        Sets up position precision on channels, position broadcasting settings,
        and fixed position if configured.

        Returns:
            True if configuration successful.
        """
        self.logger.info("Configuring device settings via CLI...")

        # Close connection for CLI operation
        if self.interface:
            self.interface.close()
            self.interface = None
            time.sleep(2)

        try:
            # Set position precision on public channel (channel 0)
            if not self._run_cli_command([
                "--ch-set", "module_settings.position_precision",
                str(self.config.public_channel_precision),
                "--ch-index", "0"
            ]):
                return False

            self.logger.info(
                f"Set public channel (0) position precision: {self.config.public_channel_precision}"
            )
            time.sleep(DEVICE_REBOOT_WAIT)

            # Set position precision on private channel
            if not self._run_cli_command([
                "--ch-set", "module_settings.position_precision",
                str(self.config.private_channel_precision),
                "--ch-index", str(self.config.private_channel_index)
            ]):
                return False

            self.logger.info(
                f"Set private channel ({self.config.private_channel_index}) "
                f"position precision: {self.config.private_channel_precision}"
            )
            time.sleep(DEVICE_REBOOT_WAIT)

            # Set position settings
            pos_args = []

            if self.config.disable_smart_position:
                pos_args.extend(["--set", "position.position_broadcast_smart_enabled", "false"])

            pos_args.extend([
                "--set", "position.gps_update_interval",
                str(self.config.gps_update_interval)
            ])

            if self.config.use_fixed_position:
                pos_args.extend(["--set", "position.fixed_position", "true"])

                if self.config.fixed_latitude is not None:
                    pos_args.extend(["--setlat", str(self.config.fixed_latitude)])

                if self.config.fixed_longitude is not None:
                    pos_args.extend(["--setlon", str(self.config.fixed_longitude)])

            if not self._run_cli_command(pos_args):
                return False

            self.logger.info("Position settings configured")
            time.sleep(DEVICE_REBOOT_WAIT)

            self.logger.info("Device configuration complete")
            return True

        except Exception as e:
            self.logger.error(f"Error configuring device: {e}")
            return False

    def _run_cli_command(self, args: List[str]) -> bool:
        """
        Run a meshtastic CLI command.

        Args:
            args: CLI arguments.

        Returns:
            True if command succeeded.
        """
        cmd = ["meshtastic"]

        if self.config.device_port:
            cmd.extend(["--port", self.config.device_port])

        cmd.extend(args)

        self.logger.debug(f"Running: {' '.join(cmd)}")

        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=CLI_TIMEOUT_SECONDS,
            )

            if result.returncode == 0:
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

    # -------------------------------------------------------------------------
    # Remote Administration (Admin Module)
    # -------------------------------------------------------------------------

    def get_public_key(self) -> Optional[str]:
        """
        Get the master device's public key (for slave admin_key setup).

        Returns:
            Base64 encoded public key, or None on error.
        """
        if not self.interface:
            self.logger.error("Cannot get public key: not connected")
            return None

        try:
            local_node = self.interface.getNode("^local")
            if not local_node or not hasattr(local_node, "localConfig"):
                self.logger.error("Cannot access local node config")
                return None

            security = local_node.localConfig.security
            if hasattr(security, "public_key") and security.public_key:
                public_key = base64.b64encode(bytes(security.public_key)).decode()
                self.logger.info(f"Master public key: {public_key}")
                return public_key

            self.logger.warning("No public key found on device")
            return None

        except Exception as e:
            self.logger.error(f"Error getting public key: {e}")
            return None

    def admin_get_setting(self, destination: str, setting: str) -> bool:
        """
        Get a setting from a remote slave via admin module.

        Args:
            destination: Target slave node ID (e.g., "!28979058").
            setting: Setting path to get.

        Returns:
            True if command sent.
        """
        assert destination and destination.startswith("!"), "Invalid node ID format"
        assert setting and isinstance(setting, str), "Invalid setting path"

        return self._run_admin_cli(["--get", setting], destination)

    def admin_set_setting(self, destination: str, setting: str, value: str) -> bool:
        """
        Set a setting on a remote slave via admin module.

        Args:
            destination: Target slave node ID.
            setting: Setting path to set.
            value: Value to set.

        Returns:
            True if command sent successfully.
        """
        assert destination and destination.startswith("!"), "Invalid node ID format"
        assert setting and isinstance(setting, str), "Invalid setting path"
        assert value is not None, "Value cannot be None"

        return self._run_admin_cli(["--set", setting, str(value)], destination)

    def setup_slave_admin_key(self, destination: str) -> bool:
        """
        Configure a slave's admin_key with master's public key.

        Args:
            destination: Target slave node ID.

        Returns:
            True if command sent successfully.
        """
        public_key = self.config.public_key or self.get_public_key()
        if not public_key:
            self.logger.error("Cannot setup admin: no public key available")
            return False

        assert destination and destination.startswith("!"), "Invalid node ID"

        self.logger.info(f"Setting admin_key on {destination} to master's public key")
        return self.admin_set_setting(
            destination,
            "security.admin_key",
            f"base64:{public_key}"
        )

    def _run_admin_cli(self, args: List[str], destination: str) -> bool:
        """
        Run a meshtastic CLI command for remote administration.

        Args:
            args: CLI arguments.
            destination: Target node ID.

        Returns:
            True if command executed successfully.
        """
        if not args or not destination:
            self.logger.error("Invalid admin CLI parameters")
            return False

        # Close interface for CLI
        interface_was_open = self.interface is not None
        if interface_was_open:
            self.interface.close()
            self.interface = None
            time.sleep(2)

        try:
            cmd = ["meshtastic"]

            if self.config.device_port:
                cmd.extend(["--port", self.config.device_port])

            cmd.extend(args)
            cmd.extend(["--dest", destination])

            self.logger.debug(f"Admin CLI: {' '.join(cmd)}")

            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=CLI_TIMEOUT_SECONDS,
            )

            if result.returncode == 0:
                self.logger.info(f"Admin command successful: {args[0]} {args[1] if len(args) > 1 else ''}")
                if result.stdout:
                    self.logger.debug(f"Output: {result.stdout}")
                return True
            else:
                self.logger.error(f"Admin command failed: {result.stderr}")
                return False

        except subprocess.TimeoutExpired:
            self.logger.error("Admin CLI command timed out")
            return False
        except FileNotFoundError:
            self.logger.error("meshtastic CLI not found")
            return False
        except Exception as e:
            self.logger.error(f"Admin CLI error: {e}")
            return False

    # -------------------------------------------------------------------------
    # Data Transmission
    # -------------------------------------------------------------------------

    def send_data(
        self,
        data: bytes,
        destination: str = None,
        port_num: int = None,
        channel_index: int = None,
    ) -> bool:
        """
        Send data through the Meshtastic device.

        Args:
            data: Binary data to send.
            destination: Target node ID (None for broadcast).
            port_num: Port number (uses config default if None).
            channel_index: Channel index (uses config default if None).

        Returns:
            True if sent successfully.
        """
        if not self.interface:
            self.logger.error("Cannot send: not connected")
            return False

        try:
            kwargs = {
                "data": data,
                "portNum": port_num or self.config.private_port_num,
                "channelIndex": channel_index if channel_index is not None else self.config.private_channel_index,
            }
            if destination:
                kwargs["destinationId"] = destination

            self.interface.sendData(**kwargs)
            return True

        except Exception as e:
            self.logger.error(f"Send failed: {e}")
            return False

    def send_position(
        self,
        latitude: float,
        longitude: float,
        altitude: int = 0,
        channel_index: int = None,
    ) -> bool:
        """
        Send position through the Meshtastic device.

        Args:
            latitude: GPS latitude.
            longitude: GPS longitude.
            altitude: Altitude in meters.
            channel_index: Channel index (uses config default if None).

        Returns:
            True if sent successfully.
        """
        if not self.interface:
            self.logger.error("Cannot send position: not connected")
            return False

        try:
            self.interface.sendPosition(
                latitude=latitude,
                longitude=longitude,
                altitude=altitude,
                channelIndex=channel_index if channel_index is not None else self.config.private_channel_index,
            )
            return True

        except Exception as e:
            self.logger.error(f"Send position failed: {e}")
            return False
