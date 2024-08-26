import time
from typing import Dict, List, NamedTuple

import flash
import meshtastic
import meshtastic.serial_interface
import pytest
from pubsub import pub  # type: ignore[import-untyped]
from readprops import readProps

version = readProps("version.properties")["long"]

heltec_v3 = ["/dev/cu.usbserial-0001", "heltec-v3", "esp32"]
rak4631 = ["/dev/cu.usbmodem14201", "rak4631", "nrf52"]
tbeam = ["COM18", "tbeam", "esp32"]


class ConnectedDevice(NamedTuple):
    port: str
    pio_env: str
    arch: str
    interface: meshtastic.serial_interface.SerialInterface
    mesh_packets: List[meshtastic.mesh_pb2.FromRadio]


devices: Dict[str, ConnectedDevice] = {}


@pytest.fixture(scope="module", params=[heltec_v3, rak4631])
def device(request):
    port = request.param[0]
    pio_env = request.param[1]
    arch = request.param[2]

    if devices.get(port) is not None and devices[port].interface.isConnected:
        yield devices[port]
    else:
        time.sleep(1)
        devices[port] = ConnectedDevice(
            port=port,
            pio_env=pio_env,
            arch=arch,
            interface=meshtastic.serial_interface.SerialInterface(port),
            mesh_packets=[],
        )
        # pub.subscribe(onReceive, "meshtastic.receive")
        devices[port].interface.waitForConfig()
        if devices[port].interface.metadata.firmware_version == version:
            print("Already at local ref version", version)
        else:
            print(
                "Device has version",
                devices[port].interface.metadata.firmware_version,
                " updating to",
                version,
            )
            devices[port].interface.close()
            # Set up device
            if arch == "esp32":
                flash.flash_esp32(pio_env=pio_env, port=port)
            elif arch == "nrf52":
                flash.flash_nrf52(pio_env=pio_env, port=port)
                # factory reset
            devices[port] = ConnectedDevice(
                port=port,
                pio_env=pio_env,
                arch=arch,
                interface=meshtastic.serial_interface.SerialInterface(port),
                mesh_packets=[],
            )
        yield devices[port]
    # Tear down devices
    devices[port].interface.close()


# Test want_config responses from device
def test_should_get_and_set_config(device: ConnectedDevice):
    assert device is not None, "Expected connected device"
    assert (
        len(device.interface.nodes) > 0
    ), "Expected at least one node in the device NodeDB"
    assert (
        device.interface.localNode.localConfig is not None
    ), "Expected LocalConfig to be set"
    assert (
        device.interface.localNode.moduleConfig is not None
    ), "Expected ModuleConfig to be set"
    assert (
        len(device.interface.localNode.channels) > 0
    ), "Expected at least one channel in the device"
    # Set up testnet channel and lora config for test harness
    device.interface.localNode.beginSettingsTransaction()
    time.sleep(1)
    device.interface.localNode.setURL(
        "https://meshtastic.org/e/#CisSIMqU8uiTvxZmoXhh1eOgay0QoT8c5-cwr-XozNr40ZUrGgdUZXN0TmV0EhEIATgBQAJIAVABWB9oAcAGAQ"
    )
    # time.sleep(1)
    # device_config = device.interface.localNode.localConfig.device
    # device_config.debug_log_enabled = True
    # device.interface.localNode.writeConfig(device_config)
    # todo security debug_log_enabled
    device.interface.localNode.commitSettingsTransaction()
    time.sleep(1)
    pub.subscribe(
        lambda packet: {
            print("Received packet", packet),
            # device.mesh_packets.append(packet),
        },
        "meshtastic.receive",
    )


def test_should_send_text_message_and_receive_ack(device: ConnectedDevice):
    device.interface.sendText(text="Test broadcast", wantAck=True)
    time.sleep(5)
    # for port in devices:
    #     if devices[port].interface != device.interface:
    #         # assert len(devices[port].mesh_packets) > 0
    #         # Assert should have received a message
    #         assert any(
    #             packet["decoded"]["payload"].decode("utf-8") == "Test broadcast"
    #             for packet in devices[port].mesh_packets
    #         )


if __name__ == "__main__":
    pytest.main()
