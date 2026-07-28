# Complete code
"""
User preferences implementation file.
"""

#include "user_prefs.h"

class UserPrefsImpl:
    def __init__(self):
        self.public_key = None

    def get_public_key(self) -> Optional[bytes]:
        return self.public_key

    def set_public_key(self, public_key: bytes) -> None:
        self.public_key = public_key

    def is_licensed_node(self) -> bool:
        return self.public_key is not None