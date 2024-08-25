import sys

import pytest
import meshtastic
import meshtastic.serial_interface
from datetime import datetime
import flash

heltec_v3 = ["COM17", "heltec-v3", "esp32"]
tbeam = ["COM18", "tbeam", "esp32"]
rak4631 = ["COM19", "rak4631", "nrf52"]

@pytest.fixture(scope="module", params=[heltec_v3])
def device(request):
    port = request.param[0]
    pio_env = request.param[1]
    arch = request.param[2]
    # Set up device
    if arch == "esp32":
        flash.flash_esp32(pio_env=pio_env, port=port)
    elif arch == "nrf52":
        flash.flash_nrf52(pio_env=pio_env, port=port)
        # factory reset
    yield meshtastic.serial_interface.SerialInterface(port)
    # Tear down device

# Test want_config responses from device
def test_get_info(device):
    assert device is not None, "Expected port to be set"
    assert len(device.nodes) > 0, "Expected at least one node in the device NodeDB"
    assert device.localNode.localConfig is not None, "Expected LocalConfig to be set"
    assert device.localNode.moduleConfig is not None, "Expected ModuleConfig to be set"
    assert len(device.localNode.channels) > 0, "Expected at least one channel in the device"

if __name__ == "__main__":
    pytest.main()