#!/usr/bin/env python3
"""
Meshtastic Master Controller Package

A Python application for controlling and monitoring a Meshtastic mesh network.
Connects to a local device via USB and communicates with slave firmware nodes
over a private channel using a custom binary protocol.

Module Structure:
    controller.py  - MasterController class (main controller logic)
    models.py      - Data classes (MasterConfig, SlaveNode, MasterState)
    device.py      - DeviceManager (connection, key mgmt, CLI operations)
    protocol.py    - Binary protocol definitions
    api.py         - REST API (optional, requires fastapi/uvicorn)
    cli.py         - Command-line interface

Usage:
    # CLI
    python -m meshtastic_app                  # Run with default config
    python -m meshtastic_app --api            # Run with REST API
    python -m meshtastic_app --info           # Show device info

    # Programmatic
    from meshtastic_app import MasterController, MasterConfig

    config = MasterConfig.from_yaml("config.yaml")
    master = MasterController(config)
    master.run()
"""

# Controller
from .controller import MasterController

# Models
from .models import (
    MasterConfig,
    MasterState,
    SlaveNode,
    ApiConfig,
    MAX_SLAVES,
    MAX_EVENT_HANDLERS,
    API_DEFAULT_HOST,
    API_DEFAULT_PORT,
)

# Device Manager
from .device import DeviceManager

# Protocol
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
    parse_ack_message,
)

# CLI
from .cli import main

# API module (optional - requires fastapi, uvicorn)
try:
    from .api import create_api, run_api_server, run_api_server_async
    _HAS_API = True
except ImportError:
    _HAS_API = False

    def create_api(*args, **kwargs):
        raise ImportError(
            "API module requires fastapi and uvicorn. "
            "Install with: pip3 install fastapi uvicorn"
        )

    def run_api_server(*args, **kwargs):
        raise ImportError(
            "API module requires fastapi and uvicorn. "
            "Install with: pip3 install fastapi uvicorn"
        )

    def run_api_server_async(*args, **kwargs):
        raise ImportError(
            "API module requires fastapi and uvicorn. "
            "Install with: pip3 install fastapi uvicorn"
        )

__version__ = "1.0.0"
__all__ = [
    # Controller
    "MasterController",
    # Models
    "MasterConfig",
    "MasterState",
    "SlaveNode",
    "ApiConfig",
    "MAX_SLAVES",
    "MAX_EVENT_HANDLERS",
    "API_DEFAULT_HOST",
    "API_DEFAULT_PORT",
    # Device
    "DeviceManager",
    # Protocol
    "PRIVATE_PORT_NUM",
    "MessageType",
    "MessageFlags",
    "CommandType",
    "SlaveStatus",
    "ProtocolMessage",
    "DataBatch",
    "SlaveStatusReport",
    "MasterCommand",
    "parse_ack_message",
    # API
    "create_api",
    "run_api_server",
    "run_api_server_async",
    # CLI
    "main",
]
