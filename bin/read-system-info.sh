#!/usr/bin/env bash

esptool.py --baud 115200 read_flash 0x1000 0xf000 system-info.img
