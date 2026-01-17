#!/usr/bin/env python3
"""
Meshtastic Python Control Application

This application connects to a Meshtastic device, verifies/sets the private key,
and transmits position data on a private channel.
"""

import base64
import logging
import subprocess
import sys
import time
from pathlib import Path

import yaml

try:
    import meshtastic
    import meshtastic.serial_interface
    from pubsub import pub
except ImportError:
    print("Error: meshtastic package not installed.")
    print("Install with: pip3 install --upgrade 'meshtastic[cli]'")
    sys.exit(1)


class MeshtasticApp:
    """Main application for controlling Meshtastic device."""

    def __init__(self, config_path: str = "config.yaml"):
        self.config = self._load_config(config_path)
        self._setup_logging()
        self.interface = None
        self.running = False
        self.connected = False
        self.logger = logging.getLogger(__name__)

    def _load_config(self, config_path: str) -> dict:
        """Load configuration from YAML file."""
        path = Path(config_path)
        if not path.exists():
            raise FileNotFoundError(f"Config file not found: {config_path}")

        with open(path, "r") as f:
            return yaml.safe_load(f)

    def _setup_logging(self):
        """Configure logging based on config."""
        log_config = self.config.get("logging", {})
        level = getattr(logging, log_config.get("level", "INFO").upper())
        log_file = log_config.get("file", "meshtastic_app.log")

        logging.basicConfig(
            level=level,
            format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
            handlers=[
                logging.FileHandler(log_file),
                logging.StreamHandler(sys.stdout),
            ],
        )

    def _on_connection(self, interface, topic=pub.AUTO_TOPIC):
        """Handle connection established event."""
        self.connected = True
        self.logger.info("Connected to Meshtastic device")

        try:
            my_info = interface.getMyNodeInfo()
            user = my_info.get("user", {})
            self.logger.info(f"Device: {user.get('longName', 'Unknown')}")
            self.logger.info(f"Node ID: {user.get('id', 'Unknown')}")
        except Exception as e:
            self.logger.warning(f"Could not get node info: {e}")

    def _on_disconnect(self, interface, topic=pub.AUTO_TOPIC):
        """Handle connection lost event."""
        self.connected = False
        self.logger.warning("Disconnected from Meshtastic device")

    def _on_receive(self, packet, interface):
        """Handle incoming packets."""
        try:
            decoded = packet.get("decoded", {})
            from_id = packet.get("fromId", "unknown")
            portnum = decoded.get("portnum", "")

            if "text" in decoded:
                self.logger.info(f"[TEXT] From {from_id}: {decoded['text']}")
            elif portnum == "POSITION_APP":
                pos = decoded.get("position", {})
                self.logger.debug(
                    f"[POS] From {from_id}: "
                    f"{pos.get('latitude', 0)}, {pos.get('longitude', 0)}"
                )
        except Exception as e:
            self.logger.error(f"Error processing packet: {e}")

    def get_device_private_key(self) -> bytes:
        """
        Get the current private key from the device.

        Returns:
            bytes: The device's current private key, or empty bytes if not available.
        """
        if not self.interface:
            raise RuntimeError("Not connected to device")

        try:
            local_node = self.interface.getNode("^local")

            # Access security config from localConfig
            if hasattr(local_node, "localConfig") and local_node.localConfig:
                security = local_node.localConfig.security
                if hasattr(security, "private_key"):
                    return bytes(security.private_key)

            self.logger.warning("Could not access private key from device config")
            return b""

        except Exception as e:
            self.logger.error(f"Error getting device private key: {e}")
            return b""

    def get_device_public_key(self) -> bytes:
        """
        Get the current public key from the device.

        Returns:
            bytes: The device's current public key.
        """
        if not self.interface:
            raise RuntimeError("Not connected to device")

        try:
            # Try using the interface method first
            if hasattr(self.interface, "getPublicKey"):
                return self.interface.getPublicKey()

            # Fallback to localConfig
            local_node = self.interface.getNode("^local")
            if hasattr(local_node, "localConfig") and local_node.localConfig:
                security = local_node.localConfig.security
                if hasattr(security, "public_key"):
                    return bytes(security.public_key)

            return b""
        except Exception as e:
            self.logger.error(f"Error getting device public key: {e}")
            return b""

    def verify_private_key(self) -> bool:
        """
        Verify the device's private key matches the configured key.

        Returns:
            bool: True if keys match or no key configured, False otherwise.
        """
        configured_key = self.config.get("security", {}).get("private_key", "")

        if not configured_key:
            self.logger.info("No private key configured, skipping verification")
            return True

        try:
            # Decode configured key from base64
            expected_key = base64.b64decode(configured_key)
        except Exception as e:
            self.logger.error(f"Invalid base64 private key in config: {e}")
            return False

        device_key = self.get_device_private_key()

        if not device_key:
            self.logger.warning("Could not retrieve device private key for verification")
            return False

        if device_key == expected_key:
            self.logger.info("Private key verification: MATCH")
            return True
        else:
            self.logger.warning("Private key verification: MISMATCH")
            self.logger.debug(f"Expected: {base64.b64encode(expected_key).decode()}")
            self.logger.debug(f"Device:   {base64.b64encode(device_key).decode()}")
            return False

    def set_private_key_via_cli(self, private_key_b64: str) -> bool:
        """
        Set the device's private key using the meshtastic CLI.

        Args:
            private_key_b64: Base64 encoded private key.

        Returns:
            bool: True if successful, False otherwise.
        """
        self.logger.info("Setting private key via meshtastic CLI...")

        # Close current connection before CLI operation
        if self.interface:
            self.logger.info("Closing connection for CLI operation...")
            self.interface.close()
            self.interface = None
            self.connected = False
            time.sleep(2)  # Allow device to release

        try:
            # Build CLI command
            cmd = ["meshtastic", "--set", f"security.private_key", f"base64:{private_key_b64}"]

            # Add port if configured
            port = self.config.get("device", {}).get("port", "")
            if port:
                cmd.extend(["--port", port])

            self.logger.debug(f"Running command: {' '.join(cmd)}")

            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=60,
            )

            if result.returncode == 0:
                self.logger.info("Private key updated successfully via CLI")
                self.logger.info("Device will reboot. Waiting for restart...")
                time.sleep(10)  # Wait for device reboot
                return True
            else:
                self.logger.error(f"CLI command failed: {result.stderr}")
                return False

        except subprocess.TimeoutExpired:
            self.logger.error("CLI command timed out")
            return False
        except FileNotFoundError:
            self.logger.error("meshtastic CLI not found. Install with: pip3 install meshtastic")
            return False
        except Exception as e:
            self.logger.error(f"Error running CLI: {e}")
            return False

    def ensure_correct_private_key(self) -> bool:
        """
        Ensure the device has the correct private key.

        Returns:
            bool: True if key is correct (or was successfully updated), False otherwise.
        """
        if self.verify_private_key():
            return True

        auto_update = self.config.get("security", {}).get("auto_update_key", True)
        if not auto_update:
            self.logger.warning("Private key mismatch but auto_update_key is disabled")
            return False

        configured_key = self.config.get("security", {}).get("private_key", "")
        if not configured_key:
            self.logger.error("No private key configured for update")
            return False

        self.logger.info("Updating device private key to match configuration...")
        if self.set_private_key_via_cli(configured_key):
            # Reconnect after CLI operation
            self.connect()
            return self.verify_private_key()

        return False

    def send_position(
        self,
        latitude: float = None,
        longitude: float = None,
        altitude: int = None,
    ) -> bool:
        """
        Send position on the private channel.

        Args:
            latitude: GPS latitude (uses config/device GPS if None).
            longitude: GPS longitude (uses config/device GPS if None).
            altitude: Altitude in meters (uses config/device if None).

        Returns:
            bool: True if sent successfully, False otherwise.
        """
        if not self.interface or not self.connected:
            self.logger.error("Cannot send position: not connected")
            return False

        pos_config = self.config.get("position", {})
        channel_config = self.config.get("channel", {})
        channel_index = channel_config.get("private_channel_index", 1)

        # Use provided values, fall back to config, then None (use device GPS)
        lat = latitude or pos_config.get("fixed_latitude")
        lon = longitude or pos_config.get("fixed_longitude")
        alt = altitude or pos_config.get("fixed_altitude") or 0

        if lat is None or lon is None:
            self.logger.warning("No position available (no GPS or fixed position configured)")
            return False

        try:
            self.logger.info(
                f"Sending position ({lat}, {lon}, alt={alt}) on channel {channel_index}"
            )

            self.interface.sendPosition(
                latitude=lat,
                longitude=lon,
                altitude=alt,
                channelIndex=channel_index,
            )

            self.logger.info("Position sent successfully on private channel")
            return True

        except Exception as e:
            self.logger.error(f"Error sending position: {e}")
            return False

    def connect(self) -> bool:
        """
        Connect to the Meshtastic device.

        Returns:
            bool: True if connected successfully, False otherwise.
        """
        if self.interface:
            self.logger.warning("Already connected, disconnecting first...")
            self.interface.close()
            self.interface = None
            time.sleep(1)

        # Subscribe to events
        pub.subscribe(self._on_receive, "meshtastic.receive")
        pub.subscribe(self._on_connection, "meshtastic.connection.established")
        pub.subscribe(self._on_disconnect, "meshtastic.connection.lost")

        device_config = self.config.get("device", {})
        port = device_config.get("port", "")
        timeout = device_config.get("timeout", 30)

        try:
            self.logger.info(f"Connecting to Meshtastic device...")
            if port:
                self.logger.info(f"Using port: {port}")
                self.interface = meshtastic.serial_interface.SerialInterface(
                    devPath=port
                )
            else:
                self.logger.info("Auto-detecting device...")
                self.interface = meshtastic.serial_interface.SerialInterface()

            # Wait for connection
            start_time = time.time()
            while not self.connected and (time.time() - start_time) < timeout:
                time.sleep(0.5)

            if self.connected:
                self.logger.info("Connection established")
                return True
            else:
                self.logger.error("Connection timeout")
                return False

        except Exception as e:
            self.logger.error(f"Connection failed: {e}")
            return False

    def run_position_loop(self):
        """Run the main position transmission loop."""
        pos_config = self.config.get("position", {})

        if not pos_config.get("enabled", True):
            self.logger.info("Position transmission disabled in config")
            return

        interval = pos_config.get("interval_seconds", 300)
        self.running = True

        self.logger.info(f"Starting position transmission loop (interval: {interval}s)")

        while self.running and self.connected:
            self.send_position()

            # Sleep in small increments to allow for clean shutdown
            for _ in range(interval):
                if not self.running:
                    break
                time.sleep(1)

    def run(self):
        """Main application entry point."""
        self.logger.info("=" * 50)
        self.logger.info("Meshtastic Python App Starting")
        self.logger.info("=" * 50)

        # Connect to device
        if not self.connect():
            self.logger.error("Failed to connect to device")
            return False

        # Verify/update private key
        if not self.ensure_correct_private_key():
            self.logger.error("Failed to ensure correct private key")
            return False

        # Run position transmission loop
        try:
            self.run_position_loop()
        except KeyboardInterrupt:
            self.logger.info("Interrupted by user")
        finally:
            self.stop()

        return True

    def stop(self):
        """Stop the application and clean up."""
        self.logger.info("Stopping application...")
        self.running = False

        if self.interface:
            self.interface.close()
            self.interface = None

        self.logger.info("Application stopped")


def main():
    """CLI entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="Meshtastic Python Control App")
    parser.add_argument(
        "-c", "--config",
        default="config.yaml",
        help="Path to configuration file (default: config.yaml)",
    )
    parser.add_argument(
        "--verify-key-only",
        action="store_true",
        help="Only verify the private key, don't run position loop",
    )
    parser.add_argument(
        "--send-position-once",
        action="store_true",
        help="Send position once and exit",
    )
    parser.add_argument(
        "--show-device-info",
        action="store_true",
        help="Show device information and exit",
    )

    args = parser.parse_args()

    try:
        app = MeshtasticApp(config_path=args.config)

        if args.show_device_info:
            if app.connect():
                print("\nDevice Information:")
                print("-" * 30)
                my_info = app.interface.getMyNodeInfo()
                user = my_info.get("user", {})
                print(f"Long Name:  {user.get('longName', 'Unknown')}")
                print(f"Short Name: {user.get('shortName', 'Unknown')}")
                print(f"Node ID:    {user.get('id', 'Unknown')}")
                print(f"Hardware:   {user.get('hwModel', 'Unknown')}")

                pub_key = app.get_device_public_key()
                if pub_key:
                    print(f"Public Key: {base64.b64encode(pub_key).decode()}")

                print("\nKnown Nodes:")
                for node_id, node in app.interface.nodes.items():
                    node_user = node.get("user", {})
                    print(f"  - {node_user.get('longName', node_id)}")

                app.stop()
            return

        if args.verify_key_only:
            if app.connect():
                result = app.verify_private_key()
                app.stop()
                sys.exit(0 if result else 1)
            sys.exit(1)

        if args.send_position_once:
            if app.connect():
                if app.ensure_correct_private_key():
                    result = app.send_position()
                    app.stop()
                    sys.exit(0 if result else 1)
            sys.exit(1)

        # Normal run
        app.run()

    except FileNotFoundError as e:
        print(f"Error: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
