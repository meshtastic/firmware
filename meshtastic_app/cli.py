#!/usr/bin/env python3
"""
Command-Line Interface for Meshtastic Master Controller

Usage:
    python -m meshtastic_app                       # Run with default config
    python -m meshtastic_app -c config.yaml        # Run with custom config
    python -m meshtastic_app --api                 # Run with REST API server
    python -m meshtastic_app --info                # Show device info
    python -m meshtastic_app --configure           # Configure device via CLI
"""

import argparse
import sys

from .controller import MasterController
from .models import MasterConfig


def main():
    """Command-line entry point."""
    parser = argparse.ArgumentParser(
        description="Meshtastic Master Controller",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python -m meshtastic_app                         # Run with default config
  python -m meshtastic_app -c config.yaml          # Run with custom config
  python -m meshtastic_app --info                  # Show device info and exit
  python -m meshtastic_app --configure             # Configure device settings via CLI
  python -m meshtastic_app --api                   # Run with REST API server
  python -m meshtastic_app --api --api-port 8000   # Custom API port
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
    parser.add_argument(
        "--configure",
        action="store_true",
        help="Configure device settings (position precision, etc.) via CLI and exit",
    )
    parser.add_argument(
        "--api",
        action="store_true",
        help="Run with REST API server for web access",
    )
    parser.add_argument(
        "--api-host",
        default=None,
        help="API server host (default: from config or 0.0.0.0)",
    )
    parser.add_argument(
        "--api-port",
        type=int,
        default=None,
        help="API server port (default: from config or 8080)",
    )

    args = parser.parse_args()

    # Load config
    try:
        config = MasterConfig.from_yaml(args.config)
    except FileNotFoundError:
        print(f"Error: Config file not found: {args.config}")
        sys.exit(1)
    except Exception as e:
        print(f"Error loading config: {e}")
        sys.exit(1)

    # Create controller
    master = MasterController(config)

    # Handle --configure
    if args.configure:
        print("\n" + "=" * 50)
        print("CONFIGURING DEVICE")
        print("=" * 50)
        print(f"Public CH position precision:  {config.public_channel_precision}")
        print(f"Private CH position precision: {config.private_channel_precision}")
        print(f"Fixed position:                {config.use_fixed_position}")
        if config.use_fixed_position and config.fixed_latitude and config.fixed_longitude:
            print(f"  Latitude:  {config.fixed_latitude}")
            print(f"  Longitude: {config.fixed_longitude}")
        print("=" * 50)

        if master.configure_device():
            print("\nDevice configured successfully!")
            sys.exit(0)
        else:
            print("\nDevice configuration failed!")
            sys.exit(1)

    # Handle --info
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
            print("=" * 50)
            master.shutdown()
        sys.exit(0)

    # Handle --api or config.api.enabled
    if args.api or config.api.enabled:
        api_host = args.api_host or config.api.host
        api_port = args.api_port or config.api.port

        print("\n" + "=" * 50)
        print("MASTER CONTROLLER + REST API")
        print("=" * 50)
        print(f"API Host:        {api_host}")
        print(f"API Port:        {api_port}")
        print(f"Private Channel: {config.private_channel_index}")
        print(f"Private Port:    {config.private_port_num}")
        print("=" * 50)

        master.run_with_api(
            api_host=api_host,
            api_port=api_port,
        )
        sys.exit(0)

    # Run main loop (no API)
    master.run()


if __name__ == "__main__":
    main()
