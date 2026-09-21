/* Linker script to configure memory regions. */

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  /* App region ends at 0xEA000, not 0xED000: the 12 KB warm-node-store
   * record-ring (3 x 4 KB pages, src/mesh/WarmNodeStore.h) occupies
   * 0xEA000-0xED000, directly below the stock LittleFS partition. Boards on
   * the framework-default linker script are covered by the post-link guard
   * in extra_scripts/nrf52_warm_region.py instead.
   *
   * S140 v6.x app region starts at 0x26000 (152 KB SoftDevice); v7.x uses
   * 0x27000. All other boundaries are identical — see nrf52840_s140_v7.ld. */
  FLASH (rx)     : ORIGIN = 0x26000, LENGTH = 0xEA000 - 0x26000

  /* BLE-mesh variant of nrf52840_s140_v6.ld: same FLASH layout, higher RAM base.
   *
   * The stock 0x20004000 was measured for Bluefruit's default (1 peripheral link,
   * 0 central). BLE mesh scans, and scanning needs a central link, which raises
   * what sd_ble_enable() demands - past 0x20004000, so the SoftDevice rejects the
   * RAM base and the node comes up with no Bluetooth at all. Observed on a
   * RAK4631: it went silent on the serial API until reflashed.
   *
   * 0x20006000 is the origin this tree shipped before the 0x20004000 measurement,
   * i.e. a value known to satisfy a strictly more generous SoftDevice config than
   * 1-peripheral-plus-1-central. It costs 8 KB of RAM, which is why this is a
   * separate script selected only by BLE-mesh builds rather than a change to the
   * shared one.
   *
   * Re-measure before changing bandwidth/MTU/link settings again: build with
   * CFG_DEBUG=1 and read Bluefruit's "SoftDevice's RAM requires: 0x...".
   */
  RAM (rwx) :  ORIGIN = 0x20006000, LENGTH = 0x20040000 - 0x20006000
}

SECTIONS
{
  . = ALIGN(4);
  .svc_data :
  {
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM

  .fs_data :
  {
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;

INCLUDE "nrf52_common.ld"
