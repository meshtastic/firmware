# Complete code
"""
Node info header file.
"""

from typing import Optional

class NodeInfo:
    def __init__(self):
        self.public_key = None

    def get_public_key(self) -> Optional[bytes]:
        return self.public_key

    def set_public_key(self, public_key: bytes) -> None:
        self.public_key = public_key

    def get_node_num(self) -> int:
        if self.public_key is not None:
            return crc32(self.public_key)
        else:
            return 0