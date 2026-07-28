# Complete code
"""
Packet policy test implementation file.
"""

#include "packet_policy_test.h"

class PacketPolicyTest:
    def __init__(self):
        self.packet_policy = PacketPolicyImpl()

    def test_is_licensed_mode(self) -> None:
        self.packet_policy.set_licensed_mode(True)
        self.assertTrue(self.packet_policy.is_licensed_mode())

    def test_verify_licensed_traffic(self) -> None:
        packet = b"licensed traffic"
        self.assertTrue(self.packet_policy.verify_licensed_traffic(packet))