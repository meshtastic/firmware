#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_TLS_API) && defined(ARCH_RP2040)

#include <IPAddress.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

struct EthCertMaterial {
    std::vector<uint8_t> certDer; // self-signed X.509 cert in DER format
    std::vector<uint8_t> keyDer;  // ECDSA P-256 private key in DER format
};

// Ensure we have a valid self-signed cert + ECDSA P-256 key whose CN + SAN
// match the given IP. Loads from LittleFS if cached, otherwise generates
// fresh and persists. Cert validity 2024-01-01 to 2034-01-01.
//
// Returns true on success. On false: TLS server should refuse to start (we'd
// rather fail closed than serve no encryption).
bool ensureCertForIp(IPAddress ip, EthCertMaterial &out);

// Spawn the one-shot OSThread that drives cert gen off the Periodic stack.
// The Phase 2.1 inline call from reconnectETH() overflowed the Periodic
// thread stack on RP2350; the dedicated OSThread uses the mainController
// stack which is sized for protobuf work and comfortably handles ECDSA P-256
// keygen + x509 sign + LittleFS write. Idempotent.
void initEthCertThread();

// True once the thread has finished (success or definitive failure).
bool isEthCertReady();

// Snapshot of the generated material once isEthCertReady(). Empty otherwise.
const EthCertMaterial &getEthCert();

// Monotonic counter bumped each time the cert is (re)generated - e.g. when a DHCP
// lease change moves us to a new IP. The TLS server reloads when this changes.
uint32_t getEthCertGeneration();

#endif // HAS_ETHERNET && HAS_ETHERNET_TLS_API && ARCH_RP2040
