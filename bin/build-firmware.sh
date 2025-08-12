#!/usr/bin/env bash

sed -i 's/#-DBUILD_EPOCH=$UNIX_TIME/-DBUILD_EPOCH=$UNIX_TIME/' platformio.ini

export PIP_BREAK_SYSTEM_PACKAGES=1

if (echo $2 | grep -q "esp32"); then
  bin/build-esp32.sh $1
elif (echo $2 | grep -q "nrf52"); then
  bin/build-nrf52.sh $1
elif (echo $2 | grep -q "stm32"); then
  bin/build-stm32.sh $1
elif (echo $2 | grep -q "rpi2040"); then
  bin/build-rp2xx0.sh $1
else
  echo "Unknown target $2"
  exit 1
fi