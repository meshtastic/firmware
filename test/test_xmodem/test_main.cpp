// Tests for XModemAdapter::isValidFilename - the path-traversal guard on the XModem file-transfer
// handler (src/xmodem.cpp). The filename in a SOH/STX control frame is attacker-controlled and
// drives FSCom open/remove; on the Portduino daemon FSCom is the host filesystem, so a ".."
// component could escape the mountpoint. Absolute/subdirectory paths must still be accepted.
#include "TestUtil.h"
#include "xmodem.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

#ifdef FSCom

void test_xmodem_rejects_dotdot_traversal(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename(".."));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("../secret"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("/prefs/../../etc/passwd"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("a/../b"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir/.."));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("/.."));
}

void test_xmodem_rejects_backslash_traversal(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("..\\secret"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("..\\..\\Windows\\System32\\drivers\\etc\\hosts"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir\\..\\..\\x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir/..\\x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("dir\\.."));
}

void test_xmodem_rejects_drive_qualified(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("C:\\Windows\\System32\\x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("C:/Windows/System32/x"));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename("c:relative.txt"));
}

void test_xmodem_rejects_empty(void)
{
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename(""));
    TEST_ASSERT_FALSE(XModemAdapter::isValidFilename(nullptr));
}

void test_xmodem_allows_legit_paths(void)
{
    // The file manager transfers absolute, subdirectoried paths from the manifest.
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("/prefs/config.proto"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("firmware.bin"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("dir/sub/file.txt"));
    // ".." only inside a name (not a whole component) is a valid filename, not traversal.
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("my..file"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("..."));
    // A colon that cannot form a drive qualifier is a legal filename on the POSIX daemon.
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("1:30pm.txt"));
    TEST_ASSERT_TRUE(XModemAdapter::isValidFilename("dir/1:30pm.txt"));
}

#endif // FSCom

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
#ifdef FSCom
    RUN_TEST(test_xmodem_rejects_dotdot_traversal);
    RUN_TEST(test_xmodem_rejects_backslash_traversal);
    RUN_TEST(test_xmodem_rejects_drive_qualified);
    RUN_TEST(test_xmodem_rejects_empty);
    RUN_TEST(test_xmodem_allows_legit_paths);
#endif
    exit(UNITY_END());
}

void loop() {}
