// WASM-side portduino glue (ARCH_PORTDUINO_WASM). Replaces the Linux YAML/
// filesystem config path with a CH341 setup (a MeshToad by default, or whatever
// the JS host pre-sets via the wasm_set_lora_* setters), mounts a persistent FS
// (IDBFS in the browser / NODEFS headless), resolves a per-node MAC, and bridges
// the firmware's PhoneAPI to JS (wasm_api_*). Also supplies
// delay()/yield()->emscripten_sleep and the other framework symbols normally
// provided by the excluded linux/LinuxCommon.cpp.
//
// Wiring in the firmware tree (already in place, all #ifdef ARCH_PORTDUINO_WASM):
//   - PortduinoGlue.cpp portduinoSetup(): calls wasm_config_apply() and
//     constructs the WebUSB-backed Ch341Hal, then returns before the YAML path.
//   - exec() short-circuits to "" (no popen/shell in the browser).
// Downstream is unchanged: Ch341Hal -> libpinedio_webusb.c -> WebUSB.

#include "CryptoEngine.h"   // crypto->ensurePkiKeys()
#include "MeshRadio.h"      // initRegion()
#include "MeshService.h"    // service->reloadConfig()
#include "NodeDB.h"         // config, owner globals + SEGMENT_CONFIG
#include "PhoneAPI.h"       // the transport-agnostic client API seam
#include "PortduinoFS.h"    // portduinoVFS
#include "PortduinoGlue.h"  // declares `portduino_config` + Ch341Hal
#include "RadioInterface.h" // RadioInterface::validateConfig*, instance
#include <cstdio>
#include <cstring>
#include <emscripten.h>
#include <string>
#include <sys/stat.h>

// Ask the JS host to persist the emscripten FS to its backing store. In the
// browser the /meshdata mount is IDBFS, so this flushes MEMFS->IndexedDB
// (async; fire-and-forget). Under headless node /meshdata is NODEFS (writes are
// already synchronous on the host fs) so syncfs is a harmless no-op there.
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_fs_sync()
{
    // Coalesce: IDBFS syncfs is async, and overlapping syncs warn "2 FS.syncfs
    // operations in flight". Never run two at once - if one is in flight, mark a
    // pending re-sync and let the running one chain it when it finishes.
    // NOTE: loose != / == and string ops only - clang-format mangles !== and
    // /regex/ literals inside EM_ASM JS (splitting them into invalid tokens).
    EM_ASM({
        try {
            if (typeof FS == "undefined" || !FS.syncfs)
                return;
            if (Module.__fsSyncing) {
                Module.__fsSyncPending = true;
                return;
            }
            var run = function()
            {
                Module.__fsSyncing = true;
                Module.__fsSyncPending = false;
                FS.syncfs(
                    false, function(err) {
                        Module.__fsSyncing = false;
                        if (err)
                            console.warn("syncfs:", err);
                        if (Module.__fsSyncPending)
                            run();
                    });
            };
            run();
        } catch (e) {
            console.warn("syncfs threw:", e);
        }
    });
}

// Point the portduino VFS at /meshdata so NodeDB/config saves succeed (the
// framework main.cpp we replaced normally does this; without it every save fails
// with "File system is not mounted"). The JS host has already FS.mount'ed an
// IDBFS (browser) or NODEFS (headless) backend at /meshdata for real persistence
// (see web/fs-setup.js); here we just ensure the subtree exists and set the root.
void wasm_fs_mount()
{
    mkdir("/meshdata", 0777);
    mkdir("/meshdata/prefs", 0777);
    mkdir("/meshdata/oem", 0777);
    portduinoVFS->mountpoint("/meshdata");
}

// Resolve a per-instance MAC address (12 uppercase hex chars, no colons - the
// MAC_from_string format getMacAddr() expects). The lower 4 bytes become the
// 32-bit NodeNum (pickNewNodeNum: mac[2..5]), so this is the node's identity on
// the mesh - every browser node MUST get a distinct one or they collide on the
// same NodeNum. Priority:
//   1. MESH_MAC env (headless determinism / parity tests, e.g. DEAD00C0FFEE),
//   2. a value persisted in the /meshdata tree (survives reload/restart),
//   3. a freshly generated locally-administered random MAC, which we persist.
// Runs inside wasm_config_apply() (called by portduinoSetup, before setup()'s
// pickNewNodeNum), and /meshdata is already JS-mounted + populated by then.
static std::string wasm_resolve_mac()
{
    // 1) explicit override (works in node via process.env; ignored in browser).
    char env[32] = {0};
    EM_ASM(
        {
            try {
                var m = (typeof process != "undefined" && process.env && process.env.MESH_MAC) || "";
                stringToUTF8(String(m).split(":").join(""), $0, 32);
            } catch (e) {
            }
        },
        env);
    if (strlen(env) >= 12)
        return std::string(env).substr(0, 12);

    // 2) persisted identity (raw POSIX path, independent of portduinoVFS mountpoint).
    if (FILE *f = fopen("/meshdata/oem/mac", "rb")) {
        char buf[13] = {0};
        size_t n = fread(buf, 1, 12, f);
        fclose(f);
        if (n == 12)
            return std::string(buf, 12);
    }

    // 3) generate a locally-administered, unicast MAC and persist it.
    unsigned char m[6] = {0};
    EM_ASM(
        {
            try {
                var c = (typeof crypto != "undefined" && crypto.getRandomValues) ? crypto : null;
                if (c)
                    c.getRandomValues(HEAPU8.subarray($0, $0 + 6));
                else
                    for (var i = 0; i < 6; i++)
                        HEAPU8[$0 + i] = (Math.random() * 256) | 0;
            } catch (e) {
                for (var j = 0; j < 6; j++)
                    HEAPU8[$0 + j] = (Math.random() * 256) | 0;
            }
        },
        m);
    m[0] = (m[0] | 0x02) & 0xFE; // locally administered (bit1=1), unicast (bit0=0)
    char hex[13];
    snprintf(hex, sizeof(hex), "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    if (FILE *f = fopen("/meshdata/oem/mac", "wb")) {
        fwrite(hex, 1, 12, f);
        fclose(f);
        wasm_fs_sync(); // browser: push the new identity to IndexedDB
    }
    return std::string(hex, 12);
}

// ---- Per-adapter config setters --------------------------------------------
// JS may call these BEFORE wasm_setup() to drive a non-MeshToad CH341 LoRa
// adapter. wasm_set_lora_module is the trigger: left unset (use_simradio, the
// struct default), wasm_config_apply() falls back to the MeshToad defaults.
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_set_lora_module(int module_enum)
{
    portduino_config.lora_module = (lora_module_enum)module_enum;
}
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_set_lora_usb_ids(int vid, int pid)
{
    portduino_config.lora_usb_vid = vid;
    portduino_config.lora_usb_pid = pid;
}
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_set_lora_usb_serial(const char *serial)
{
    portduino_config.lora_usb_serial_num = serial ? std::string(serial) : "";
}
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_set_lora_dio_config(int dio2_as_rf_switch, int dio3_tcxo_mv)
{
    portduino_config.dio2_as_rf_switch = (dio2_as_rf_switch != 0);
    portduino_config.dio3_tcxo_voltage = dio3_tcxo_mv;
}
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_set_lora_spi_speed(int hz)
{
    portduino_config.spiSpeed = hz;
}
// Pin name -> the matching portduino_config field. Sets .pin + .enabled to match
// the default path (the CH341 backend addresses by D-line number = .pin).
extern "C" EMSCRIPTEN_KEEPALIVE void wasm_set_lora_pin(const char *name, int pin)
{
    if (!name)
        return;
    std::string n(name);
    pinMapping *t = nullptr;
    if (n == "CS")
        t = &portduino_config.lora_cs_pin;
    else if (n == "IRQ")
        t = &portduino_config.lora_irq_pin;
    else if (n == "BUSY")
        t = &portduino_config.lora_busy_pin;
    else if (n == "RESET")
        t = &portduino_config.lora_reset_pin;
    else if (n == "RXEN")
        t = &portduino_config.lora_rxen_pin;
    else if (n == "TXEN")
        t = &portduino_config.lora_txen_pin;
    else if (n == "ANT_SW")
        t = &portduino_config.lora_sx126x_ant_sw_pin;
    if (t) {
        t->pin = pin;
        t->enabled = (pin != (int)RADIOLIB_NC);
    }
}

// LoRa adapter config. A MeshToad (E22/SX1262 over CH341) by default, or whatever
// the JS host pre-set via the wasm_set_lora_* setters before wasm_setup(). The
// browser/headless invariants (CH341 SPI dev, no screen/GPS, MAC) always apply.
// C++ linkage to match the `extern void wasm_config_apply();` decl in PortduinoGlue.cpp.
void wasm_config_apply()
{
    if (portduino_config.lora_module == use_simradio) {
        // Nothing pre-set by JS -> MeshToad E22/SX1262 defaults.
        portduino_config.lora_module = use_sx1262;
        portduino_config.lora_usb_vid = 0x1A86;
        portduino_config.lora_usb_pid = 0x5512;
        portduino_config.lora_usb_serial_num = ""; // first matching device
        portduino_config.dio2_as_rf_switch = true; // E22 uses DIO2 as the TX/RX switch
        portduino_config.dio3_tcxo_voltage = 1800; // 1.8 V TCXO
        portduino_config.spiSpeed = 2000000;
        // MeshToad CH341 D-line pin map (bin/config.d/lora-usb-meshtoad-e22.yaml).
        auto setPin = [](pinMapping &p, int n) {
            p.pin = n;
            p.enabled = true;
        };
        setPin(portduino_config.lora_cs_pin, 0);    // CS  = D0
        setPin(portduino_config.lora_irq_pin, 6);   // IRQ = D6
        setPin(portduino_config.lora_busy_pin, 4);  // BUSY = D4
        setPin(portduino_config.lora_reset_pin, 2); // RESET = D2
        setPin(portduino_config.lora_rxen_pin, 1);  // RXen = D1
    }
    portduino_config.lora_spi_dev = "ch341"; // every adapter here is WebUSB/CH341
    portduino_config.displayPanel = no_screen;
    portduino_config.has_gps = false;
    portduino_config.MaxNodes = 80; // small DB for the browser
    // Per-instance unique MAC (persisted in /meshdata, env-overridable). The lower
    // 4 bytes become this node's NodeNum; also short-circuits getMacAddr()'s popen.
    portduino_config.mac_address = wasm_resolve_mac();
}

// Set the LoRa region at runtime from the UI (browser <select>) or headless
// (MESH_REGION env, applied by run-node.mjs). Mirrors AdminModule's set_config
// (lora) region path exactly (AdminModule.cpp:904-937): validate -> first-region
// keygen + enable tx -> initRegion() -> reloadConfig (resetRadioConfig +
// reconfigure observer recomputes the carrier freq + saveToDisk). A region change
// is reconfigure-only - no reboot. Returns 0 on success, -1 if validation fails.
// `region` is a meshtastic_Config_LoRaConfig_RegionCode enum value.
// Re-entrancy guard. The node is single-threaded + Asyncify: while setup()/loop()
// is suspended inside a WebUSB transfer, the JS event loop is free, so a stray DOM
// or timer callback could re-enter a wasm_* entry point - starting a second
// Asyncify unwind ("async operation already in flight" abort) or clobbering shared
// PhoneAPI state. The host MUST call these only BETWEEN wasm_loop_once() ticks; the
// flag (set around setup()/loop() in portduino_main_wasm.cpp) lets the entry points
// reject a mid-tick call rather than corrupt/abort. The JS-side queue is the real
// fix - this is just the safety net the design otherwise lacked.
extern "C" volatile bool g_wasm_in_firmware = false;

extern "C" EMSCRIPTEN_KEEPALIVE int wasm_set_region(int region)
{
    if (g_wasm_in_firmware)
        return -2; // busy: re-entered mid-tick; call between wasm_loop_once() ticks
    auto newRegion = (meshtastic_Config_LoRaConfig_RegionCode)region;
    if (config.lora.region == newRegion)
        return 0; // no-op

    meshtastic_Config_LoRaConfig validated = config.lora;
    validated.region = newRegion;
    if (!(RadioInterface::validateConfigRegion(validated) && RadioInterface::validateConfigLora(validated)))
        return -1;

    bool wasUnset = (config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_UNSET);
    if (wasUnset && newRegion > meshtastic_Config_LoRaConfig_RegionCode_UNSET) {
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
        if (crypto)
            crypto->ensurePkiKeys(config.security, owner); // first real region -> generate keys
#endif
        validated.tx_enabled = true;
    }
    if (!wasUnset && newRegion == meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        validated.tx_enabled = false;

    config.lora = validated;
    initRegion(); // repoint myRegion at the new region table
    if (service)
        service->reloadConfig(SEGMENT_CONFIG); // reconfigure radio (new freq) + persist
    wasm_fs_sync();                            // browser: flush config.proto to IndexedDB
    return 0;
}

// ---------------------------------------------------------------------------
// Client/phone API seam. The firmware's PhoneAPI is the transport-agnostic
// request/response state machine that the TCP/serial/BLE/HTTP servers all wrap;
// those servers are excluded here, so we expose PhoneAPI directly to JS instead.
// A JS-side transport (browser in-process, or a headless HTTP/TCP bridge) feeds
// ToRadio protobufs in via wasm_api_to_radio() and drains FromRadio protobufs
// out via wasm_api_from_radio() - exactly the unframed contract the device HTTP
// API uses. PhoneAPI is abstract on checkIsConnected(); in-process we're always
// connected. We poll available() rather than push from onNowHasData(), to keep
// all API calls OUT of the loop's Asyncify suspend (the JS pump drains only
// between wasm_loop_once() ticks - never re-entering wasm mid-SPI).
class WasmPhoneAPI : public PhoneAPI
{
  public:
    virtual bool checkIsConnected() override { return true; }
};

static WasmPhoneAPI *g_wasmPhone = nullptr;

// Lazily construct after MeshService exists (PhoneAPI's ctor observes
// service->fromNumChanged). The first API call happens post-setup(), so service
// is live by then - no wasm_setup() ordering change needed.
static WasmPhoneAPI *phone()
{
    if (!g_wasmPhone && service)
        g_wasmPhone = new WasmPhoneAPI();
    return g_wasmPhone;
}

// JS -> device: hand one serialized ToRadio protobuf (UNFRAMED) to the node.
// The SDK's want_config_id, text messages, admin, etc. all arrive here. Returns
// 1 if accepted, 0 if rejected (e.g. per-portnum throttle - not a transport error).
extern "C" EMSCRIPTEN_KEEPALIVE int wasm_api_to_radio(const uint8_t *buf, size_t len)
{
    if (g_wasm_in_firmware)
        return 0; // busy: re-entered mid-tick; the host must drain between ticks
    WasmPhoneAPI *p = phone();
    return (p && p->handleToRadio((uint8_t *)buf, len)) ? 1 : 0;
}

// device -> JS: write ONE serialized FromRadio into out[0..max). Returns byte
// count, or 0 when nothing is ready this call. out must be >= 512.
extern "C" EMSCRIPTEN_KEEPALIVE int wasm_api_from_radio(uint8_t *out, size_t max)
{
    if (g_wasm_in_firmware)
        return 0; // busy: re-entered mid-tick; drain between ticks
    WasmPhoneAPI *p = phone();
    if (!p || max < 512 || !p->available())
        return 0;
    return (int)p->getFromRadio(out);
}

// Cheap readiness check (no 512B buffer needed) for the JS pump.
extern "C" EMSCRIPTEN_KEEPALIVE int wasm_api_available()
{
    if (g_wasm_in_firmware)
        return 0; // busy: re-entered mid-tick
    WasmPhoneAPI *p = phone();
    return (p && p->available()) ? 1 : 0;
}

// True once a config handshake is in progress / complete (state != NOTHING).
extern "C" EMSCRIPTEN_KEEPALIVE int wasm_api_is_connected()
{
    WasmPhoneAPI *p = phone();
    return (p && p->isConnected()) ? 1 : 0;
}

// delay()/yield() normally come from framework linux/LinuxCommon.cpp (excluded:
// it's POSIX/threads). We provide them via Asyncify's emscripten_sleep so
// init-time blocking yields to the browser event loop instead of freezing the
// tab - exactly the behavior we want at runtime. C++ linkage to match Arduino.h.
void delay(unsigned long ms)
{
    emscripten_sleep(ms);
}
void yield(void)
{
    emscripten_sleep(0);
}

// --- other framework symbols normally from linux/LinuxCommon.cpp (excluded) ---
#include <cstdlib>
long random(long howbig)
{
    return howbig > 0 ? (::rand() % howbig) : 0;
}
long random(long howsmall, long howbig)
{
    return howsmall >= howbig ? howsmall : howsmall + random(howbig - howsmall);
}
void randomSeed(unsigned long seed)
{
    ::srand((unsigned)seed);
}
// realHardware is defined by the framework's Linux hardware shims (now compiled in).
// Restart the node. There is no in-process restart in wasm, so we hand off to
// the JS host: a browser reloads the tab (NodeDB state survives via IDBFS, so it
// comes back as the same node); a headless host calls Module.onReboot() if it
// provided one (re-instantiate or exit as it sees fit), otherwise we just log -
// the caller (Power::reboot) already let modules persist via the reboot
// observers. NOTE: loose !=/== and double-quoted strings only - clang-format
// mangles !== and /regex/ literals inside EM_ASM JS into invalid runtime tokens.
void reboot()
{
    EM_ASM({
        try {
            if (typeof Module != "undefined" && typeof Module.onReboot == "function") {
                Module.onReboot();
                return;
            }
            if (typeof location != "undefined" && location.reload) {
                location.reload();
                return;
            }
            if (typeof console != "undefined")
                console.warn("[wasm] reboot requested but no Module.onReboot hook; node left running");
        } catch (e) {
            if (typeof console != "undefined")
                console.warn("reboot hook threw:", e);
        }
    });
}

// tone()/noTone() - no buzzer in the browser (from linux/LinuxCommon.cpp, excluded).
void tone(unsigned char, unsigned int, unsigned long) {}
void noTone(unsigned char) {}
void delayMicroseconds(unsigned int us)
{
    // No sub-ms sleep in the browser; yield for short waits, round up longer ones.
    if (us >= 1000)
        emscripten_sleep(us / 1000);
    else
        emscripten_sleep(0);
}

// graphics/Screen.cpp is excluded; TextMessageModule references this. No screen → never wake.
bool shouldWakeOnReceivedMessage()
{
    return false;
}

// TCP phone/API server is excluded in the wasm build - no-op stubs so main.cpp links.
void initApiServer(int) {}
void deInitApiServer() {}
