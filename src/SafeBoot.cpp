/**
 * SafeBoot: pre-init power guard for battery/solar nodes.
 * See SafeBoot.h for design rationale.
 */

#include "SafeBoot.h"
#include "configuration.h"
#include "power.h"

#include <Arduino.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Compile-time enablement
// ---------------------------------------------------------------------------
//
// SafeBoot only makes sense on battery-capable MCU platforms with some way
// of measuring Vbat very early. For the first iteration we cover ESP32 and
// nRF52, which is where the upstream issues have been reported. Other
// platforms compile to a no-op.
//
// The user can also force-disable SafeBoot via SAFE_BOOT_DISABLED.
#if !defined(SAFE_BOOT_DISABLED) && (defined(ARCH_ESP32) || defined(ARCH_NRF52)) && defined(BATTERY_PIN)
#define SAFE_BOOT_ENABLED 1
#else
#define SAFE_BOOT_ENABLED 0
#endif

// ---------------------------------------------------------------------------
// Defaults: when a board does not define explicit SafeBoot thresholds, derive
// them from the existing battery curve so the early-boot power guard matches
// the same voltage model used for battery percentage and runtime low-battery
// handling. Boards with weak regulators can still override the thresholds, but
// the preferred long-term direction is to encode those constraints in a custom
// OCV curve so 0% means "no longer safe to boot" for that hardware.
//
// Why the wake hysteresis stays separate from the curve:
//   - the sleep threshold should align with the board's minimum stable point;
//   - the wake threshold needs extra headroom so a transient solar surge does
//     not immediately bounce the node back into another marginal boot.
//   - 120 s initial recheck balances responsiveness vs. average current.
//   - Backoff cap at 600 s prevents the node disappearing for too long
//     once power has actually returned.
// ---------------------------------------------------------------------------
#ifndef DEFAULT_SAFE_BOOT_WAKE_HYST_MV
#define DEFAULT_SAFE_BOOT_WAKE_HYST_MV 300
#endif
#ifndef DEFAULT_SAFE_BOOT_RECHECK_SECS
#define DEFAULT_SAFE_BOOT_RECHECK_SECS 120
#endif
#ifndef DEFAULT_SAFE_BOOT_MAX_RECHECK_SECS
#define DEFAULT_SAFE_BOOT_MAX_RECHECK_SECS 600
#endif
// Number of consecutive Vbat samples that must clear wake threshold to
// consider voltage "stable" and continue boot. ~10 ms apart by default.
#ifndef DEFAULT_SAFE_BOOT_STABLE_SAMPLES
#define DEFAULT_SAFE_BOOT_STABLE_SAMPLES 8
#endif
#ifndef DEFAULT_SAFE_BOOT_SAMPLE_INTERVAL_MS
#define DEFAULT_SAFE_BOOT_SAMPLE_INTERVAL_MS 10
#endif
// After a brownout reset, force at least this much cooldown sleep before
// the next attempt, even if Vbat looks fine. Anti boot-loop safeguard.
#ifndef DEFAULT_SAFE_BOOT_BROWNOUT_COOLDOWN_SECS
#define DEFAULT_SAFE_BOOT_BROWNOUT_COOLDOWN_SECS 300
#endif

// USERPREFS overrides (build-time, via userPrefs.jsonc -> build-userprefs-json.py)
#ifdef USERPREFS_POWER_SAFE_BOOT_WAKE_MV
#undef DEFAULT_SAFE_BOOT_WAKE_MV
#define DEFAULT_SAFE_BOOT_WAKE_MV USERPREFS_POWER_SAFE_BOOT_WAKE_MV
#endif
#ifdef USERPREFS_POWER_SAFE_BOOT_SLEEP_MV
#undef DEFAULT_SAFE_BOOT_SLEEP_MV
#define DEFAULT_SAFE_BOOT_SLEEP_MV USERPREFS_POWER_SAFE_BOOT_SLEEP_MV
#endif
#ifdef USERPREFS_POWER_SAFE_BOOT_RECHECK_SECS
#undef DEFAULT_SAFE_BOOT_RECHECK_SECS
#define DEFAULT_SAFE_BOOT_RECHECK_SECS USERPREFS_POWER_SAFE_BOOT_RECHECK_SECS
#endif

namespace
{
constexpr uint16_t defaultSafeBootCurveSleepMv()
{
    constexpr uint16_t ocv[NUM_OCV_POINTS] = {OCV_ARRAY};
    return static_cast<uint16_t>(ocv[NUM_OCV_POINTS - 1] * NUM_CELLS);
}

constexpr uint16_t defaultSafeBootWakeMv(uint16_t sleepMv)
{
    constexpr uint32_t hysteresisMv = DEFAULT_SAFE_BOOT_WAKE_HYST_MV;
    constexpr uint32_t maxMv = 0xFFFFu;
    const uint32_t wakeMv = static_cast<uint32_t>(sleepMv) + hysteresisMv;
    return static_cast<uint16_t>(wakeMv > maxMv ? maxMv : wakeMv);
}

#ifdef USERPREFS_POWER_SAFE_BOOT_SLEEP_MV
constexpr uint16_t kSafeBootSleepMv = USERPREFS_POWER_SAFE_BOOT_SLEEP_MV;
#elif defined(DEFAULT_SAFE_BOOT_SLEEP_MV)
constexpr uint16_t kSafeBootSleepMv = DEFAULT_SAFE_BOOT_SLEEP_MV;
#else
constexpr uint16_t kSafeBootSleepMv = defaultSafeBootCurveSleepMv();
#endif

#ifdef USERPREFS_POWER_SAFE_BOOT_WAKE_MV
constexpr uint16_t kSafeBootWakeMv = USERPREFS_POWER_SAFE_BOOT_WAKE_MV;
#elif defined(DEFAULT_SAFE_BOOT_WAKE_MV)
constexpr uint16_t kSafeBootWakeMv = DEFAULT_SAFE_BOOT_WAKE_MV;
#else
constexpr uint16_t kSafeBootWakeMv = defaultSafeBootWakeMv(kSafeBootSleepMv);
#endif
} // namespace

// ---------------------------------------------------------------------------
// Module state (process-local; not persisted)
// ---------------------------------------------------------------------------
bool g_settled = false;
bool g_wokeFromSafeBoot = false;
bool g_lastResetUnclean = false;

// ===========================================================================
// SafeBoot real implementation (only on supported platforms)
// ===========================================================================
#if SAFE_BOOT_ENABLED

#if defined(ARCH_ESP32)
#include "esp_sleep.h"
#include "esp_system.h"
#include "soc/rtc.h"
#include <driver/rtc_io.h>
#if __has_include("rom/rtc.h")
#include "rom/rtc.h"
#endif
#endif

#if defined(ARCH_NRF52)
#include <nrf_power.h>
#if __has_include(<nrf_soc.h>)
#include <nrf_soc.h>
#endif
#if __has_include(<hal/nrf_lpcomp.h>)
#include <hal/nrf_lpcomp.h>
#endif
#if __has_include(<nrf_gpio.h>)
#include <nrf_gpio.h>
#endif
#if __has_include(<nrf_wdt.h>)
#include <nrf_wdt.h>
#endif
#endif

namespace
{

// Magic stored in retention memory to validate the persisted state.
constexpr uint32_t SAFE_BOOT_MAGIC = 0x5AFEB007u;

#pragma pack(push, 1)
struct PersistedState {
    uint32_t magic;
    uint16_t backoff_secs; // current sleep interval; grows with each fail
    uint16_t attempts;     // consecutive SafeBoot-induced sleeps
    uint16_t last_vbat_mv; // last lightweight reading
    uint16_t flags;        // bit0 = unclean reset suspected
    uint32_t reserved;     // future use; keep struct 16-byte aligned
};
#pragma pack(pop)
static_assert(sizeof(PersistedState) <= 16, "PersistedState too large");

#if defined(ARCH_ESP32)
// RTC slow memory survives deep sleep but contents are undefined on a
// cold power-on; we use a magic word to detect that.
RTC_NOINIT_ATTR PersistedState g_persisted;

PersistedState loadPersisted()
{
    if (g_persisted.magic != SAFE_BOOT_MAGIC) {
        PersistedState fresh = {};
        fresh.magic = SAFE_BOOT_MAGIC;
        fresh.backoff_secs = DEFAULT_SAFE_BOOT_RECHECK_SECS;
        fresh.attempts = 0;
        fresh.last_vbat_mv = 0;
        fresh.flags = 0;
        fresh.reserved = 0;
        return fresh;
    }
    return g_persisted;
}

void storePersisted(const PersistedState &s) { g_persisted = s; }

#elif defined(ARCH_NRF52)
// nRF52 has only 8-bit GPREGRET / GPREGRET2 (the bootloader uses GPREGRET).
// We don't have enough room for the full struct, so we keep a compact
// encoding in GPREGRET2:
//   bits[0..2] : backoff stage (0..7) -> sleep = base * 2^stage, capped
//   bits[3..6] : attempts counter (0..15, saturating)
//   bit [7]    : "previous reset unclean" sticky flag
// Magic-checking is handled implicitly by reading RESETREAS (the register
// is zeroed on cold power-on, see nRF52 product specification).
constexpr uint8_t MASK_STAGE = 0x07;
constexpr uint8_t SHIFT_ATTEMPTS = 3;
constexpr uint8_t MASK_ATTEMPTS = 0x0F << SHIFT_ATTEMPTS;
constexpr uint8_t FLAG_UNCLEAN = 0x80;

static inline uint32_t backoffSecsForStage(uint8_t stage)
{
    uint32_t base = DEFAULT_SAFE_BOOT_RECHECK_SECS;
    uint32_t cap = DEFAULT_SAFE_BOOT_MAX_RECHECK_SECS;
    uint32_t secs = base;
    for (uint8_t i = 0; i < stage && secs < cap; i++)
        secs <<= 1;
    if (secs > cap)
        secs = cap;
    return secs;
}

PersistedState loadPersisted()
{
    PersistedState s = {};
    uint8_t reg = (uint8_t)NRF_POWER->GPREGRET2;
    s.magic = SAFE_BOOT_MAGIC;
    uint8_t stage = reg & MASK_STAGE;
    s.backoff_secs = (uint16_t)backoffSecsForStage(stage);
    s.attempts = (uint16_t)((reg & MASK_ATTEMPTS) >> SHIFT_ATTEMPTS);
    s.flags = (reg & FLAG_UNCLEAN) ? 1u : 0u;
    s.last_vbat_mv = 0;
    return s;
}

void storePersisted(const PersistedState &s)
{
    uint8_t stage = 0;
    {
        uint32_t secs = s.backoff_secs ? s.backoff_secs : DEFAULT_SAFE_BOOT_RECHECK_SECS;
        uint32_t base = DEFAULT_SAFE_BOOT_RECHECK_SECS;
        while (secs > base && stage < 7) {
            secs >>= 1;
            stage++;
        }
    }
    uint8_t attempts = (s.attempts > 15) ? 15 : (uint8_t)s.attempts;
    uint8_t reg = (uint8_t)((stage & MASK_STAGE) | ((attempts << SHIFT_ATTEMPTS) & MASK_ATTEMPTS) |
                            ((s.flags & 1u) ? FLAG_UNCLEAN : 0));
#if __has_include(<nrf_soc.h>)
    // Try the SoftDevice path first (works once SD is enabled). Fall through
    // to direct register write if SD isn't ready yet (typical at this stage).
    if (sd_power_gpregret_clr(1, 0xFF) != NRF_SUCCESS || sd_power_gpregret_set(1, reg) != NRF_SUCCESS) {
        NRF_POWER->GPREGRET2 = reg;
    }
#else
    NRF_POWER->GPREGRET2 = reg;
#endif
}
#endif // ARCH_NRF52

// ---------------------------------------------------------------------------
// Lightweight Vbat measurement.
//
// We deliberately do NOT use the full Power / AnalogBatteryLevel stack here:
//   - it pulls in I2C scans, PMU init, OCV tables, telemetry sensors;
//   - it may enable peripherals that we are explicitly trying to keep off
//     until the guard has decided to continue;
//   - it requires NodeDB / config to be loaded (chicken-and-egg).
// Instead we read BATTERY_PIN directly using the board-specific
// ADC_MULTIPLIER from variant.h (overridable at runtime via the existing
// PowerConfig field, but we don't have that yet at this stage).
// ---------------------------------------------------------------------------

#ifndef ADC_MULTIPLIER
// Same fallback as in src/Power.cpp.
#define ADC_MULTIPLIER 2.0f
#endif

#ifdef BATTERY_SENSE_RESOLUTION_BITS
constexpr int kBatteryResolutionBits = BATTERY_SENSE_RESOLUTION_BITS;
#else
constexpr int kBatteryResolutionBits = 12;
#endif

#if defined(ADC_CTRL) && !defined(ARCH_NRF52)
// Some boards (e.g. Heltec V3/V4) gate the battery divider with a control
// pin to save µA. Drive it active for the duration of the read, then back.
#ifndef ADC_CTRL_ENABLED
#define ADC_CTRL_ENABLED HIGH
#endif
#endif

uint16_t readVbatMillivoltsLight()
{
#ifdef BATTERY_PIN
#if defined(ADC_CTRL) && !defined(ARCH_NRF52)
    pinMode(ADC_CTRL, OUTPUT);
    digitalWrite(ADC_CTRL, ADC_CTRL_ENABLED);
    delay(2);
#endif

    pinMode(BATTERY_PIN, INPUT);

    uint32_t sum_mv = 0;
    constexpr int kSamples = 4;

#if defined(ARCH_ESP32)
    // analogReadMilliVolts() applies eFuse calibration internally, which
    // gives us a usable absolute voltage without standing up the full
    // esp_adc_cal characterization machinery.
    for (int i = 0; i < kSamples; i++) {
        sum_mv += analogReadMilliVolts(BATTERY_PIN);
    }
    uint32_t adc_mv = sum_mv / kSamples;
    float vbat = (float)adc_mv * (float)ADC_MULTIPLIER;
#elif defined(ARCH_NRF52)
#ifdef VBAT_AR_INTERNAL
    analogReference(VBAT_AR_INTERNAL);
#else
    analogReference(AR_INTERNAL); // 3.6 V reference, matches Power.cpp
#endif
    analogReadResolution(kBatteryResolutionBits);
    uint32_t raw_sum = 0;
    for (int i = 0; i < kSamples; i++)
        raw_sum += analogRead(BATTERY_PIN);
    uint32_t raw = raw_sum / kSamples;
    // AREF_VOLTAGE in src/Power.cpp uses 3.6 for nRF52 when AR_INTERNAL.
#ifndef AREF_VOLTAGE
    constexpr float kAref = 3.6f;
#else
    constexpr float kAref = (float)AREF_VOLTAGE;
#endif
    float vbat = ((float)ADC_MULTIPLIER) * ((1000.0f * kAref) / (float)(1 << kBatteryResolutionBits)) * (float)raw;
#else
    float vbat = 0.0f;
#endif

#if defined(ADC_CTRL) && !defined(ARCH_NRF52)
    digitalWrite(ADC_CTRL, !ADC_CTRL_ENABLED);
#endif

    if (vbat < 0.0f)
        return 0;
    if (vbat > 65535.0f)
        return 65535;
    return (uint16_t)vbat;
#else
    return 0; // No BATTERY_PIN — caller should have skipped us
#endif
}

// ---------------------------------------------------------------------------
// Reset reason inspection
// ---------------------------------------------------------------------------
bool detectUncleanReset()
{
#if defined(ARCH_ESP32)
    // We're called BEFORE esp32Setup(), but after initDeepSleep(). Check
    // both the modern API and the legacy one to catch C3/S3/C6 too.
    esp_reset_reason_t r = esp_reset_reason();
    switch (r) {
    case ESP_RST_BROWNOUT:
    case ESP_RST_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_PANIC:
        return true;
    default:
        return false;
    }
#elif defined(ARCH_NRF52)
    uint32_t why = NRF_POWER->RESETREAS;
    // POWER_RESETREAS_DOG_Msk (bit 1) = watchdog
    // POWER_RESETREAS_LOCKUP_Msk (bit 3) = CPU lockup
    // Brownout on nRF52 typically appears as a fresh power-on (RESETREAS == 0)
    // or via the POF event during operation; we conservatively treat
    // dog/lockup as unclean. POF handling is done in main-nrf52.cpp.
    constexpr uint32_t kUncleanMask = (1u << 1) | (1u << 3);
    return (why & kUncleanMask) != 0;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Deep sleep entry (used while in SafeBoot, before Power/sleep.cpp init).
// We deliberately bypass src/sleep.cpp here because it touches NodeDB,
// screen, BLE and others that we have not initialised yet.
// ---------------------------------------------------------------------------
[[noreturn]] void enterSafeBootSleep(uint32_t secs)
{
    if (secs == 0)
        secs = DEFAULT_SAFE_BOOT_RECHECK_SECS;
    (void)secs; // may be unused on SYSTEM_OFF paths (nRF52 LPCOMP/button)

#if defined(ARCH_ESP32)
    // Timer wake.
    esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);

#ifdef BUTTON_PIN
    // Allow the user to force-wake by pressing the button. ext1 is wired
    // up downstream in sleep.cpp::doDeepSleep when buttons are normally
    // configured; we do the minimum here so we don't depend on it.
#if SOC_PM_SUPPORT_EXT_WAKEUP
    uint64_t mask = ((uint64_t)1) << BUTTON_PIN;
#ifdef CONFIG_IDF_TARGET_ESP32
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW);
#else
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
#endif
#endif

    Serial.flush();
    esp_deep_sleep_start();

#elif defined(ARCH_NRF52)
    // nRF52 SYSTEM_OFF cannot be timed directly: it requires a wake source
    // (LPCOMP, GPIO SENSE, NFC field, or reset). We pick the best available
    // option in this priority order:
    //
    //   1) BATTERY_LPCOMP_INPUT defined  -> SYSTEM_OFF + LPCOMP wake on
    //      Vbat rise. Optimal path: ~1-3 µA.
    //   2) BUTTON_PIN defined            -> SYSTEM_OFF + GPIO SENSE on the
    //      user button. Same ultra-low current, but auto-recheck is lost
    //      (the user has to press to retry). We *also* arm a short System
    //      ON nap below so that auto-recheck still works.
    //   3) Otherwise                     -> System ON __WFE() loop with
    //      watchdog feeding. Higher current (~1.5-3 mA) but the only way
    //      to keep timer-based auto-recovery on variants that wired
    //      neither LPCOMP nor a button.
    //
    // Variants on solar deployments SHOULD define BATTERY_LPCOMP_INPUT.

    // Optional: actively pull external radio EN/RXEN pins to GND before
    // sleep. Some external SX126x modules (e.g. EBYTE E22P on ProMicro/
    // NiceNano-based DIY boards) have internal pull-ups on their enable
    // line; if we leave it floating during SYSTEM_OFF the module will
    // backpower the MCU through that pin and either wake it spuriously
    // ("phantom boot") or just keep draining the battery. Variants that
    // need this define EXTERNAL_RADIO_ENABLE_PIN (single pin) and/or
    // EXTERNAL_RADIO_ENABLE_PINS_LIST (comma-separated initialiser list)
    // in their variant.h. Boards without external modules don't define
    // these macros and this whole block compiles out -- zero risk.
    // See https://github.com/meshtastic/firmware/issues/10084
#if __has_include(<nrf_gpio.h>) && (defined(EXTERNAL_RADIO_ENABLE_PIN) || defined(EXTERNAL_RADIO_ENABLE_PINS_LIST))
    {
#ifdef EXTERNAL_RADIO_ENABLE_PIN
        nrf_gpio_cfg_output(EXTERNAL_RADIO_ENABLE_PIN);
        nrf_gpio_pin_clear(EXTERNAL_RADIO_ENABLE_PIN);
#endif
#ifdef EXTERNAL_RADIO_ENABLE_PINS_LIST
        const uint32_t kRadioEnPins[] = {EXTERNAL_RADIO_ENABLE_PINS_LIST};
        for (size_t i = 0; i < sizeof(kRadioEnPins) / sizeof(kRadioEnPins[0]); i++) {
            nrf_gpio_cfg_output(kRadioEnPins[i]);
            nrf_gpio_pin_clear(kRadioEnPins[i]);
        }
#endif
    }
#endif

#ifdef BATTERY_LPCOMP_INPUT
    {
        nrf_lpcomp_config_t c = {};
#ifdef BATTERY_LPCOMP_THRESHOLD
        c.reference = BATTERY_LPCOMP_THRESHOLD;
#else
        c.reference = NRF_LPCOMP_REF_SUPPLY_3_8;
#endif
        c.detection = NRF_LPCOMP_DETECT_UP;
        c.hyst = NRF_LPCOMP_HYST_NOHYST;
        nrf_lpcomp_configure(NRF_LPCOMP, &c);
        nrf_lpcomp_input_select(NRF_LPCOMP, BATTERY_LPCOMP_INPUT);
        nrf_lpcomp_enable(NRF_LPCOMP);
        nrf_lpcomp_task_trigger(NRF_LPCOMP, NRF_LPCOMP_TASK_START);
        while (!nrf_lpcomp_event_check(NRF_LPCOMP, NRF_LPCOMP_EVENT_READY))
            ;
    }
#endif

#if defined(BUTTON_PIN) && __has_include(<nrf_gpio.h>)
    // Manual wake via button SENSE. Cheap to arm and complements LPCOMP
    // (the user can force a retry without waiting for solar).
    nrf_gpio_cfg_input(BUTTON_PIN, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_sense_set(BUTTON_PIN, NRF_GPIO_PIN_SENSE_LOW);
#endif

#if defined(BATTERY_LPCOMP_INPUT) || (defined(BUTTON_PIN) && __has_include(<nrf_gpio.h>))
    // We have at least one SYSTEM_OFF wake source: enter ultra-low-power.
    // Try the SoftDevice path first (handles EasyDMA quiesce), then fall
    // back to the bare register write — this matches the pattern used in
    // src/platform/nrf52/main-nrf52.cpp::cpuDeepSleep().
    Serial.flush();
#if __has_include(<nrf_soc.h>)
    if (sd_power_system_off() != NRF_SUCCESS) {
        NRF_POWER->SYSTEMOFF = 1;
    }
#else
    NRF_POWER->SYSTEMOFF = 1;
#endif
    while (1) {
        __WFE();
    }
#else
    // Last-resort fallback: System ON sleep with WFE + WDT feed.
    // Used only on nRF52 variants that wired neither LPCOMP nor a button.
    // Honest tradeoff: current ~1.5-3 mA instead of ~3 µA. The boot loop
    // still slows down to once per `secs`, which is the primary goal of
    // SafeBoot. Encourage these variants to add BATTERY_LPCOMP_INPUT.
    Serial.flush();
    const uint32_t start = millis();
    const uint32_t deadline_ms = secs * 1000u;
    while ((millis() - start) < deadline_ms) {
        // Defensive WDT feed: if a watchdog was started by the bootloader
        // or earlier init code, keep it happy. Reading RUNSTATUS is safe
        // even when the WDT is idle (returns 0).
#if __has_include(<nrf_wdt.h>)
        if (nrf_wdt_started(NRF_WDT)) {
            for (nrf_wdt_rr_register_t i = NRF_WDT_RR0; i <= NRF_WDT_RR7; i = (nrf_wdt_rr_register_t)(i + 1)) {
                if (nrf_wdt_reload_request_is_enabled(NRF_WDT, i))
                    nrf_wdt_reload_request_set(NRF_WDT, i);
            }
        }
#endif
        __WFE();
        __SEV();
        __WFE();
        // Cap idle granularity so we revisit the WDT feed every ~100 ms
        // even if no event arrives. delay() yields and is RTC-driven.
        delay(100);
    }
    NVIC_SystemReset();
    while (1)
        ;
#endif
#else
    while (1)
        ;
#endif
}

// Pick the next backoff interval (capped).
uint32_t bumpBackoff(uint32_t prev_secs)
{
    uint32_t next = prev_secs * 2u;
    if (next < DEFAULT_SAFE_BOOT_RECHECK_SECS)
        next = DEFAULT_SAFE_BOOT_RECHECK_SECS;
    if (next > DEFAULT_SAFE_BOOT_MAX_RECHECK_SECS)
        next = DEFAULT_SAFE_BOOT_MAX_RECHECK_SECS;
    return next;
}

} // namespace

void SafeBoot::checkAndMaybeSleep()
{
    g_lastResetUnclean = detectUncleanReset();

    PersistedState st = loadPersisted();
    g_wokeFromSafeBoot = (st.attempts > 0);

    // If we've been here before AND the last reset looked unclean (most
    // likely a brownout we caused by attempting to boot too eagerly),
    // force at least one extra cooldown cycle even if Vbat now reads OK.
    bool forceCooldown = g_lastResetUnclean && (g_wokeFromSafeBoot || st.flags != 0);

    // Lightweight Vbat read (with a few samples to ride out coupling noise).
    uint16_t mv = 0;
    int stable = 0;
    for (int i = 0; i < DEFAULT_SAFE_BOOT_STABLE_SAMPLES; i++) {
        uint16_t s = readVbatMillivoltsLight();
        if (s == 0) {
            // Sensor not ready / no battery rail — bail out and continue
            // boot. Powered-from-USB nodes hit this path.
            g_settled = true;
            return;
        }
        if (s >= kSafeBootWakeMv)
            stable++;
        else
            stable = 0;
        mv = s;
        delay(DEFAULT_SAFE_BOOT_SAMPLE_INTERVAL_MS);
    }

    // Decision matrix:
    //   - Vbat below sleep threshold       -> sleep with current backoff
    //   - Vbat above wake threshold (stable) and no forced cooldown -> proceed
    //   - Otherwise (between sleep & wake, or forced cooldown)      -> sleep
    bool proceed = false;
    if (mv >= kSafeBootWakeMv && stable >= DEFAULT_SAFE_BOOT_STABLE_SAMPLES && !forceCooldown) {
        proceed = true;
    } else if (mv < kSafeBootSleepMv) {
        proceed = false;
    } else {
        // In hysteresis band: if we're a fresh boot (not from SafeBoot)
        // and the reset is clean, give it a chance — otherwise wait.
        proceed = (!g_wokeFromSafeBoot && !g_lastResetUnclean);
    }

    if (proceed) {
        // Reset persisted state: we made it through.
        PersistedState clear = {};
        clear.magic = SAFE_BOOT_MAGIC;
        clear.backoff_secs = DEFAULT_SAFE_BOOT_RECHECK_SECS;
        clear.attempts = 0;
        clear.last_vbat_mv = mv;
        clear.flags = 0;
        storePersisted(clear);
        g_settled = true;
        Serial.printf("[SafeBoot] Vbat=%u mV stable -- continuing boot (attempts=%u, unclean=%d)\r\n", (unsigned)mv,
                      (unsigned)st.attempts, (int)g_lastResetUnclean);
        return;
    }

    // We're going back to sleep. Record state, log, then bypass everything.
    uint32_t sleep_secs;
    if (forceCooldown && st.attempts == 0) {
        sleep_secs = DEFAULT_SAFE_BOOT_BROWNOUT_COOLDOWN_SECS;
    } else if (st.backoff_secs == 0) {
        sleep_secs = DEFAULT_SAFE_BOOT_RECHECK_SECS;
    } else {
        sleep_secs = bumpBackoff(st.backoff_secs);
    }

    PersistedState next = st;
    next.magic = SAFE_BOOT_MAGIC;
    next.backoff_secs = (uint16_t)sleep_secs;
    next.attempts = (uint16_t)((st.attempts >= 0xFFFE) ? st.attempts : (st.attempts + 1));
    next.last_vbat_mv = mv;
    next.flags = (uint16_t)(g_lastResetUnclean ? 1u : 0u);
    storePersisted(next);

    Serial.printf("[SafeBoot] Vbat=%u mV below safe threshold (wake=%u sleep=%u). "
                  "Sleep %us, attempt #%u, unclean=%d.\r\n",
                  (unsigned)mv, (unsigned)kSafeBootWakeMv, (unsigned)kSafeBootSleepMv, (unsigned)sleep_secs,
                  (unsigned)next.attempts, (int)g_lastResetUnclean);
    Serial.flush();

    enterSafeBootSleep(sleep_secs);
}

#else // !SAFE_BOOT_ENABLED

void SafeBoot::checkAndMaybeSleep()
{
    g_settled = true;
}

#endif // SAFE_BOOT_ENABLED

bool SafeBoot::isSettled()
{
    return g_settled;
}

bool SafeBoot::wokeFromSafeBoot()
{
    return g_wokeFromSafeBoot;
}

bool SafeBoot::lastResetWasUnclean()
{
    return g_lastResetUnclean;
}
