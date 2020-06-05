
# Setup Monitor Mode Debugging
# Per .platformio/packages/framework-arduinoadafruitnrf52-old/cores/nRF5/linker/nrf52840_s140_v6.ld
# our appload starts at 0x26000
# Disable for now because our version on board doesn't support monitor mode debugging
# mon exec SetMonModeDebug=1
# mon exec SetMonModeVTableAddr=0x26000

# echo setting RTTAddr
# eval "monitor exec SetRTTAddr %p", &_SEGGER_RTT

# the jlink debugger seems to want a pause after reset before we tell it to start running
define restart
  echo Restarting
  monitor reset 
  shell sleep 1
  cont 
end

