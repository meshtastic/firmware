import usb.core
import subprocess

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
    # Flash the ESP32 target
    subprocess.run(["platformio", "run", "-e", pio_env, "-t", "upload", "-p", port])

def flash_nrf52(pio_env, port):
    # Flash the nrf52 target
    subprocess.run(["platformio", "run", "-e", pio_env, "-t", "upload", "-p", port])