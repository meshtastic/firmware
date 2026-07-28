# Complete code
"""
Packet policy header file.
"""

from typing import Optional

class PacketPolicy:
    def __init__(self):
        self.licensed_mode = False

    def is_licensed_mode(self) -> bool:
        return self.licensed_mode

    def set_licensed_mode(self, licensed_mode: bool) -> None:
        self.licensed_mode = licensed_mode