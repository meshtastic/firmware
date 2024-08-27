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

for port in [heltec_v3, rak4631]:
    print("Flashing device", port)
    flash.flash_esp32(pio_env=port[1], port=port[0])


class ConnectedDevice(NamedTuple):
    port: str
    pio_env: str
    arch: str
    interface: meshtastic.serial_interface.SerialInterface
    mesh_packets: List[meshtastic.mesh_pb2.FromRadio]


devices: Dict[str, ConnectedDevice] = {}
# Set up testnet channel and lora config for test harness
# device.interface.localNode.beginSettingsTransaction()
# time.sleep(1)
# device.interface.localNode.setURL(
#     "https://meshtastic.org/e/#CisSIMqU8uiTvxZmoXhh1eOgay0QoT8c5-cwr-XozNr40ZUrGgdUZXN0TmV0EhEIATgBQAJIAVABWB9oAcAGAQ"
# )
# # time.sleep(1)
# # device_config = device.interface.localNode.localConfig.device
# # device_config.debug_log_enabled = True
# # device.interface.localNode.writeConfig(device_config)
# # todo security debug_log_enabled
# device.interface.localNode.commitSettingsTransaction()
# time.sleep(1)


@pytest.fixture(scope="module", params=[rak4631, heltec_v3])
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
    device.interface.localNode.setURL(
        "https://meshtastic.org/e/#CisSIMqU8uiTvxZmoXhh1eOgay0QoT8c5-cwr-XozNr40ZUrGgdUZXN0TmV0EhEIATgBQAJIAVABWB9oAcAGAQ"
    )
    time.sleep(1)
    pub.subscribe(default_on_receive, "meshtastic.receive")
    # pub.subscribe(
    #     lambda packet: {
    #         print(device.pio_env, "Received packet", packet),
    #         device.interface.mesh_packets.append(packet),
    #     },
    #     "meshtastic.receive",
    # )


def default_on_receive(packet, interface):
    print("Received packet", packet["decoded"], "interface", interface)
    # find the device that sent the packet
    for port in devices:
        if devices[port].interface == interface:
            devices[port].mesh_packets.append(packet)


def test_should_send_text_message_and_receive_ack(device: ConnectedDevice):
    time.sleep(2)
    # Send a text message
    print("Sending text from device", device.pio_env)
    device.interface.sendText(text="Test broadcast", wantAck=True)
    time.sleep(2)
    for port in devices:
        if devices[port].port != device.port:
            print("Checking device", devices[port].pio_env, "for received message")
            print(devices[port].mesh_packets)
            # Assert should have received a message
            # find text message in packets
            textPackets = list(
                filter(
                    lambda packet: packet["decoded"]["portnum"]
                    == meshtastic.portnums_pb2.TEXT_MESSAGE_APP
                    and packet["decoded"]["payload"].decode("utf-8")
                    == "Test broadcast",
                    devices[port].mesh_packets,
                )
            )
            assert (
                len(textPackets) > 0
            ), "Expected a text message received on other device"
    # Assert should have received an ack
    # ackPackets = list(filter(
    #     lambda packet: packet["decoded"]["portnum"] == meshtastic.portnums_pb2.ROUTING_APP, device.mesh_packets
    # ))
    # assert len(ackPackets) > 0, "Expected an ack from the device"


if __name__ == "__main__":
    pytest.main()
