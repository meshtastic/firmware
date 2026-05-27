// User-provided mbedTLS config — pulled in AFTER the default mbedtls_config.h
// via -DMBEDTLS_USER_CONFIG_FILE in the variant's platformio.ini.
//
// We compile mbedtls source files straight out of pico-sdk on bare metal, so
// every option that needs POSIX (time, sockets, filesystem) is disabled here.
// Without this, sources like net_sockets.c, timing.c, platform_util.c, etc.
// abort with #error or #include <sys/socket.h>.
//
// Code paths that touch these symbols are gated by the same MBEDTLS_* macros,
// so the linker simply drops the unreachable branches — no manual file
// exclusion in the build script.

#pragma once

// Entropy: entropy_poll.c does a hard `#error` on non-POSIX/non-Windows
// platforms. Tell it to skip the platform-specific entropy plumbing — our
// cert module passes a custom f_rng (picoRand → get_rand_64) directly into
// every mbedtls call that needs randomness, so we never invoke entropy_poll.
#define MBEDTLS_NO_PLATFORM_ENTROPY

// Time: pico-sdk mbedtls only knows clock_gettime() (POSIX) and GetTickCount64()
// (Win32). Neither exists here. We don't need calendar time on the server side
// (cert validity check at TLS init is the only user of MBEDTLS_HAVE_TIME_DATE
// and our self-signed cert is dated 2024-2034 so the client decides validity).
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_TIMING_C

// Networking: net_sockets.c uses POSIX sockets. We wrap EthernetClient
// ourselves with mbedtls_ssl_set_bio() callbacks.
#undef MBEDTLS_NET_C

// Filesystem: cert/key load happens via our own LittleFS code, not via
// mbedtls_x509_crt_parse_file()/fopen().
#undef MBEDTLS_FS_IO

// PSA persistent storage: requires POSIX fopen. Unused.
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
