# You probably don't need this - it is a basic test of the serial flash on the TTGO eink board

nrfjprog --qspiini nrf52/ttgo_eink_qpsi.ini --qspieraseall
nrfjprog --qspiini nrf52/ttgo_eink_qpsi.ini --memwr 0x12000000 --val 0xdeadbeef --verify
nrfjprog --qspiini nrf52/ttgo_eink_qpsi.ini --readqspi spi.hex
objdump -s spi.hex | less
