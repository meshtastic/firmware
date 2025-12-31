/* Arduino fix: catch esp_event's orphaned .text.handler_execute section and align to 4 bytes */
SECTIONS
{
  .text.handler_execute :
  {
    KEEP(*(.text.handler_execute))
    KEEP(*(.text.handler_execute.*))
    . = ALIGN(4);
  }
}
INSERT AFTER .flash.text;