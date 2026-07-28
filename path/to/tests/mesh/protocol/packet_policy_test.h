# Complete code
"""
Packet policy test header file.
"""

#include "gtest/gtest.h"

class PacketPolicyTest : public ::testing::Test {
 protected:
  PacketPolicyTest() {}
  ~PacketPolicyTest() override {}

  PacketPolicyImpl packet_policy_;
};