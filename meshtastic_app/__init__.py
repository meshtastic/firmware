"""
Meshtastic Master Controller Module

Control and monitor Meshtastic slave firmware nodes via USB/Serial connection.
This module acts as the master node, receiving telemetry and data batches from
slaves over a private channel using a custom binary protocol.

Architecture:
    Master (this module)
        │
        ├── USB/Serial
        ▼
    Meshtastic Device (connected to PC)
        │
        ├── Private Channel + Private Port (RF)
        ▼
    Slave Nodes (custom firmware)
        - Send telemetry (position, battery, temp, status)
        - Send data batches (binary records)
        - Receive commands from master

Protocol:
    - Private channel (index 1 by default)
    - Private port app (257 by default)
    - Binary message format with header + payload
    - Message types: TELEMETRY, DATA_BATCH, STATUS, COMMAND, ACK, HEARTBEAT

Usage:
    from meshtastic_app import MasterController, MasterConfig

    config = MasterConfig.from_yaml("config.yaml")
    master = MasterController(config)

    # Register handlers for slave data
    master.on_telemetry(lambda slave, data: print(f"{slave.node_id}: {data}"))
    master.on_data_batch(lambda slave, batch: print(f"Batch: {batch.batch_id}"))

    # Initialize (connects + auto-verifies key)
    master.initialize()

    # Send command to a slave
    master.send_command("!abcd1234", CommandType.SEND_DATA)

    # Get all slaves
    for slave in master.get_slaves():
        print(f"{slave.node_id}: {slave.telemetry_count} telemetry records")

    # Run main loop
    master.run()
"""

from .master import MasterController, MasterConfig, MasterState, SlaveNode
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
    TelemetryData,
)

__version__ = "0.3.0"
__all__ = [
    # Master
    "MasterController",
    "MasterConfig",
    "MasterState",
    "SlaveNode",
    # Protocol
    "PRIVATE_PORT_NUM",
    "MessageType",
    "MessageFlags",
    "CommandType",
    "SlaveStatus",
    "ProtocolMessage",
    "TelemetryData",
    "DataBatch",
    "SlaveStatusReport",
    "MasterCommand",
]
