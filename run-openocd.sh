

# /home/kevinh/.platformio/packages/tool-openocd-esp32/bin/openocd -s /home/kevinh/.platformio/packages/tool-openocd-esp32 -c gdb_port pipe; tcl_port disabled; telnet_port disabled -s /home/kevinh/.platformio/packages/tool-openocd-esp32/share/openocd/scripts -f interface/jlink.cfg -f board/esp-wroom-32.cfg
/home/kevinh/.platformio/packages/tool-openocd-esp32/bin/openocd -s /home/kevinh/.platformio/packages/tool-openocd-esp32 -s /home/kevinh/.platformio/packages/tool-openocd-esp32/share/openocd/scripts -f interface/jlink.cfg -f ./lora32-openocd.cfg

