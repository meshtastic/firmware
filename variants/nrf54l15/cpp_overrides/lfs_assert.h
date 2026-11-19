#pragma once
// Force-included so littlefs v2 (nRF54L core) routes assertions and logs through Meshtastic
// (lfs_assert() in main-nrf52.cpp formats the filesystem on corruption).
#ifdef __cplusplus
extern "C" {
#endif
void lfs_assert(const char *reason);
void logLegacy(const char *level, const char *fmt, ...);
#ifdef __cplusplus
}
#endif
#define LFS_ASSERT(test)                                                                                                         \
    if (!(test))                                                                                                                 \
    lfs_assert(#test)
#define LFS_LOG_(level, fmt, ...) logLegacy(level, "lfs:%d: " fmt "%s\n", __LINE__, __VA_ARGS__)
#define LFS_DEBUG(...) LFS_LOG_("DEBUG", __VA_ARGS__, "")
#define LFS_WARN(...) LFS_LOG_("WARN", __VA_ARGS__, "")
#define LFS_ERROR(...) LFS_LOG_("ERROR", __VA_ARGS__, "")
