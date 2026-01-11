/**
 * Unit tests for Syslog::isConnected() functionality
 * Tests the connection status reporting for UDP syslog
 */

#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

#ifdef ARCH_PORTDUINO

// Mock UDP client for testing
class MockUDP : public UDP {
public:
  uint8_t begin(uint16_t port) override { return 1; }
  void stop() override {}
  int beginPacket(IPAddress ip, uint16_t port) override { return 1; }
  int beginPacket(const char *host, uint16_t port) override { return 1; }
  int endPacket() override { return 1; }
  size_t write(uint8_t byte) override { return 1; }
  size_t write(const uint8_t *buffer, size_t size) override { return size; }
  int parsePacket() override { return 0; }
  int available() override { return 0; }
  int read() override { return -1; }
  int read(unsigned char *buffer, size_t len) override { return 0; }
  int read(char *buffer, size_t len) override { return 0; }
  int peek() override { return -1; }
  void flush() override {}
  IPAddress remoteIP() override { return IPAddress(); }
  uint16_t remotePort() override { return 0; }
};

MockUDP testUdpClient;
Syslog testSyslog(testUdpClient);

void setUp(void) {
  // Reset syslog state before each test
  testSyslog.disable();
}

void tearDown(void) { testSyslog.disable(); }

/**
 * Test: Initial state should report not connected
 */
void test_syslog_initial_state_not_connected(void) {
  MockUDP freshUdp;
  Syslog freshSyslog(freshUdp);

  TEST_ASSERT_FALSE(freshSyslog.isConnected());
  TEST_ASSERT_FALSE(freshSyslog.isEnabled());
}

/**
 * Test: After server() but before enable(), should report not connected
 */
void test_syslog_configured_but_not_enabled(void) {
  testSyslog.server("192.168.1.100", 514);

  TEST_ASSERT_FALSE(testSyslog.isConnected());
  TEST_ASSERT_FALSE(testSyslog.isEnabled());
}

/**
 * Test: After server() and enable(), should report connected
 */
void test_syslog_configured_and_enabled(void) {
  testSyslog.server("192.168.1.100", 514);
  testSyslog.enable();

  TEST_ASSERT_TRUE(testSyslog.isConnected());
  TEST_ASSERT_TRUE(testSyslog.isEnabled());
}

/**
 * Test: After disable(), should report not connected
 */
void test_syslog_disabled_after_enabled(void) {
  testSyslog.server("192.168.1.100", 514);
  testSyslog.enable();
  TEST_ASSERT_TRUE(testSyslog.isConnected());

  testSyslog.disable();
  TEST_ASSERT_FALSE(testSyslog.isConnected());
  TEST_ASSERT_FALSE(testSyslog.isEnabled());
}

/**
 * Test: Using IP address instead of hostname
 */
void test_syslog_with_ip_address(void) {
  IPAddress ip(192, 168, 1, 100);
  testSyslog.server(ip, 514);
  testSyslog.enable();

  TEST_ASSERT_TRUE(testSyslog.isConnected());
}

/**
 * Test: Port 0 should report not connected even if enabled
 */
void test_syslog_port_zero_not_connected(void) {
  testSyslog.server("192.168.1.100", 0);
  testSyslog.enable();

  // Even with enable(), port 0 means not properly configured
  TEST_ASSERT_FALSE(testSyslog.isConnected());
}

/**
 * Test: Custom port should work
 */
void test_syslog_custom_port(void) {
  testSyslog.server("syslog.example.com", 1514);
  testSyslog.enable();

  TEST_ASSERT_TRUE(testSyslog.isConnected());
}

void setup() {
  delay(2000); // Wait for board initialization

  initializeTestEnvironment();
  UNITY_BEGIN();

  RUN_TEST(test_syslog_initial_state_not_connected);
  RUN_TEST(test_syslog_configured_but_not_enabled);
  RUN_TEST(test_syslog_configured_and_enabled);
  RUN_TEST(test_syslog_disabled_after_enabled);
  RUN_TEST(test_syslog_with_ip_address);
  RUN_TEST(test_syslog_port_zero_not_connected);
  RUN_TEST(test_syslog_custom_port);

  exit(UNITY_END());
}

void loop() {}

#else
// Stub for non-Portduino builds
void setup() { exit(0); }
void loop() {}
#endif
