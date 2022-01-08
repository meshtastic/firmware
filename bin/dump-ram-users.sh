#!/usr/bin/env bash

arm-none-eabi-readelf -s -e .pio/build/nrf52dk/firmware.elf | head -80

nm -CSr --size-sort .pio/build/nrf52dk/firmware.elf  | grep '^200'
