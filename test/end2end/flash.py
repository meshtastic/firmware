# trunk-ignore-all(bandit/B404)
import subprocess

import usb.core


def find_usb_device(vendor_id, product_id):
    # Find USB devices
    dev = usb.core.find(find_all=True)
    # Loop through devices, printing vendor and product ids in decimal and hex
    for cfg in dev:
        if cfg.idVendor == vendor_id and cfg.idProduct == product_id:
            return cfg
    return None


# Flash esp32 target
def flash_esp32(pio_env, port):
    # trunk-ignore(bandit/B603)
    # trunk-ignore(bandit/B607)
    subprocess.run(
        ["platformio", "run", "-e", pio_env, "-t", "upload", "--upload-port", port]
    )


def flash_nrf52(pio_env, port):
    # trunk-ignore(bandit/B603)
    # trunk-ignore(bandit/B607)
    subprocess.run(
        ["platformio", "run", "-e", pio_env, "-t", "upload", "--upload-port", port]
    )


def find_usb_device(vendor_id, product_id):
    # Find USB devices
    dev = usb.core.find(find_all=True)
    # Loop through devices, printing vendor and product ids in decimal and hex
    for cfg in dev:
        if cfg.idVendor == vendor_id and cfg.idProduct == product_id:
            return cfg
    return None


# Flash esp32 target
def flash_esp32(pio_env, port):
    # trunk-ignore(bandit/B603)
    # trunk-ignore(bandit/B607)
    subprocess.run(
        ["platformio", "run", "-e", pio_env, "-t", "upload", "--upload-port", port]
    )


def flash_nrf52(pio_env, port):
    # trunk-ignore(bandit/B603)
    # trunk-ignore(bandit/B607)
    subprocess.run(
        ["platformio", "run", "-e", pio_env, "-t", "upload", "--upload-port", port]
    )
