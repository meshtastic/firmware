"""
Meshtastic Master Controller Module

Control and monitor Meshtastic slave firmware nodes via USB/Serial connection.
This module acts as the master node, collecting telemetry and sensor data from
slaves over a private channel.

Architecture:
    Master (this module)
        │
        ├── USB/Serial
        ▼
    Meshtastic Device (connected to PC)
        │
        ├── Private Channel (RF)
        ▼
    Slave Nodes (custom firmware, NO GPS)
        - Send device telemetry via STANDARD Meshtastic (battery, voltage, temp)
        - Send sensor data batches via CUSTOM binary protocol (port 485)
        - Receive master position via standard Meshtastic
        - Receive commands via custom protocol

Data Flow:
    Standard Meshtastic (built-in):
        - Master → Slaves: Position broadcasts
        - Slaves → Master: Device telemetry (battery, voltage, channel util)
        - Slaves → Master: Environment metrics (temp, humidity, pressure)

    Custom Binary Protocol (port 485):
        - Slaves → Master: Sensor data batches (DATA_BATCH)
        - Master → Slaves: ACK with batch_id (slave can delete batch from memory)
        - Master → Slaves: Commands (COMMAND, REQUEST_DATA, etc.)

ACK Flow (batch memory management):
    1. Slave sends DATA_BATCH with batch_id, keeps batch in memory
    2. Master processes batch, sends ACK with batch_id
    3. Slave receives ACK, extracts batch_id using parse_ack_message()
    4. Slave deletes acknowledged batch from memory

Usage:
    from meshtastic_app import MasterController, MasterConfig

    config = MasterConfig.from_yaml("config.yaml")
    master = MasterController(config)

    # Register handlers
    master.on_telemetry(lambda slave, telem: print(f"{slave.node_id}: bat={slave.battery_level}%"))
    master.on_data_batch(lambda slave, batch: print(f"Batch: {batch.batch_id}"))

    # Initialize (connects + auto-verifies key)
    master.initialize()

    # Get slave info
    for slave in master.get_slaves():
        print(f"{slave.node_id}: bat={slave.battery_level}%, batches={slave.batch_count}")

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
    parse_ack_message,
)

__version__ = "0.8.0"
__all__ = [
    # Master
    "MasterController",
    "MasterConfig",
    "MasterState",
    "SlaveNode",
    # Protocol (for custom sensor data batches)
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
]
