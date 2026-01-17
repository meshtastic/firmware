"""
Meshtastic Master Controller Module

Control and monitor Meshtastic devices via USB/Serial connection.
This module acts as the master node, communicating with slave firmware
nodes over a private channel.

Architecture:
    Master (this module) <--USB--> Meshtastic Device <--RF--> Slave Nodes

Usage:
    from meshtastic_app import MasterController, MasterConfig

    config = MasterConfig.from_yaml("config.yaml")
    master = MasterController(config)

    # Register handlers
    master.on_message(lambda from_id, text, pkt: print(f"{from_id}: {text}"))

    # Initialize (connects + auto-verifies key)
    master.initialize()

    # Send commands to slaves
    master.send_text("STATUS", destination="!abcd1234")

    # Run main loop
    master.run()
"""

from .master import MasterController, MasterConfig, MasterState, SlaveNode

__version__ = "0.2.0"
__all__ = ["MasterController", "MasterConfig", "MasterState", "SlaveNode"]
