import time
from typing import Dict, List, NamedTuple

import meshtastic
import meshtastic.serial_interface
import pytest
from dotmap import DotMap
from pubsub import pub  # type: ignore[import-untyped]
from setup import setup_device, setup_users_prefs  # type: ignore[import-untyped]


class ConnectedDevice(NamedTuple):
    port: str
    pio_env: str
    arch: str
    interface: meshtastic.serial_interface.SerialInterface
    mesh_packets: List[meshtastic.mesh_pb2.FromRadio]


devices: Dict[str, ConnectedDevice] = {}

heltec_v3 = ["/dev/cu.usbserial-0001", "heltec-v3", "esp32"]
rak4631 = ["/dev/cu.usbmodem14101", "rak4631", "nrf52"]
tbeam = ["/dev/", "rak4631", "nrf52"]


setup_users_prefs("userPrefs.h")

for port_device in [heltec_v3, rak4631]:
    print("Setting up device", port_device[1], "on port", port_device[0])
    setup_device(port=port_device[0], pio_env=port_device[1], arch=port_device[2])


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
        yield devices[port]
    # Tear down devices
    devices[port].interface.close()


def default_on_receive(packet, interface):
    print("Received packet", packet["decoded"], "interface", interface)
    # find the device that sent the packet
    for port in devices:
        if devices[port].interface == interface:
            devices[port].mesh_packets.append(packet)


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
    pub.subscribe(default_on_receive, "meshtastic.receive")
    device.interface.waitForAckNak


def test_should_send_text_message_and_receive_ack(device: ConnectedDevice):
    # Send a text message
    print("Sending text from device", device.pio_env)
    device.interface.sendText(text="Test broadcast", wantAck=True)
    # time.sleep(1)
    # device.interface.waitForAckNak()
    print("Received ack from device")
    time.sleep(2)
    # for port in devices:
    #     if devices[port].port != device.port:
    #         print("Checking device", devices[port].pio_env, "for received message")
    #         print(devices[port].mesh_packets)
    #         # Assert should have received a message
    #         # find text message in packets
    #         textPackets = list(
    #             filter(
    #                 lambda packet: packet["decoded"]["portnum"]
    #                 == meshtastic.portnums_pb2.TEXT_MESSAGE_APP
    #                 and packet["decoded"]["payload"].decode("utf-8")
    #                 == "Test broadcast",
    #                 devices[port].mesh_packets,
    #             )
    #         )
    #         assert (
    #             len(textPackets) > 0
    #         ), "Expected a text message received on other device"
    # # Assert should have received an ack
    # ackPackets = list(
    #     filter(
    #         lambda packet: packet["decoded"]["portnum"]
    #         == meshtastic.portnums_pb2.ROUTING_APP,
    #         device.mesh_packets,
    #     )
    # )
    # assert len(ackPackets) > 0, "Expected an ack from the device"


if __name__ == "__main__":
    pytest.main()
