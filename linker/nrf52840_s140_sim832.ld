/* Linker script to configure memory regions. 

geeksville: modified this to simulate a nrf52832 but with a sd140 soft device.  So I can 
see how the memory footprint works on this lower end CPU.  Note: to work with sd140 in my bootloader 
I need to start ram at 0x6000 (instead of the correct 0x3600 for sd132) - so I have less
RAM available than on a real 832.
*/

SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx)     : ORIGIN = 0x26000, LENGTH = 0x6D000 - 0x26000
  /* FLASH (rx)     : ORIGIN = 0x26000, LENGTH = 0xED000 - 0x26000 */

  /* SRAM required by S132 depend on
   * - Attribute Table Size
   * - Vendor UUID count
   * - Max ATT MTU
   * - Concurrent connection peripheral + central + secure links
   * - Event Len, HVN queue, Write CMD queue
   */ 
  /* RAM (rwx) :  ORIGIN = 0x20003600, LENGTH = 0x20010000 - 0x20003600 */
  RAM (rwx) :  ORIGIN = 0x20006000, LENGTH = 0x20010000 - 0x20006000
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
