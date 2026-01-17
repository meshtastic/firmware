# Meshtastic Python Library Research

## Overview

The Meshtastic Python library provides an API for controlling and monitoring Meshtastic devices from a computer. Instead of embedding logic into firmware, you connect your device via USB/Serial, TCP/WiFi, or Bluetooth and run your application logic in Python.

**Installation:**
```bash
pip3 install --upgrade "meshtastic[cli]"
```

---

## Connection Interfaces

The library provides three main connection types:

### 1. SerialInterface (USB Connection)

```python
import meshtastic
import meshtastic.serial_interface

# Auto-detect connected device
interface = meshtastic.serial_interface.SerialInterface()

# Or specify device path explicitly
interface = meshtastic.serial_interface.SerialInterface(devPath='/dev/ttyUSB0')
# Windows: devPath='COM4'
# macOS: devPath='/dev/cu.usbmodem53230050571'
```

**Constructor Parameters:**
- `devPath` (Optional[str]): Device filepath (e.g., `/dev/ttyUSB0`)
- `debugOut` (stream): Receives debug serial output
- `noProto` (bool): Disable protocol (raw serial mode)
- `connectNow` (bool): Connect immediately (default: True)
- `noNodes` (bool): Skip node initialization

### 2. TCPInterface (WiFi Connection)

```python
import meshtastic.tcp_interface

# Connect via IP address (default port: 4403)
interface = meshtastic.tcp_interface.TCPInterface(hostname='192.168.1.100')
```

### 3. BLEInterface (Bluetooth)

```python
import meshtastic.ble_interface

# Scan for BLE devices first
devices = meshtastic.ble_interface.BLEInterface.scan()

# Connect by address
interface = meshtastic.ble_interface.BLEInterface(address='AA:BB:CC:DD:EE:FF')
```

---

## Event-Driven Architecture (Pub/Sub)

The library uses a publish-subscribe model for receiving messages and events:

```python
import meshtastic
import meshtastic.serial_interface
from pubsub import pub

def onReceive(packet, interface):
    """Called when any packet arrives"""
    print(f"Received: {packet}")

def onReceiveText(packet, interface):
    """Called specifically for text messages"""
    print(f"Text message: {packet['decoded']['text']}")

def onConnection(interface, topic=pub.AUTO_TOPIC):
    """Called when connected to the radio"""
    print("Connected to mesh!")
    interface.sendText("Hello mesh!")

def onDisconnect(interface, topic=pub.AUTO_TOPIC):
    """Called when connection is lost"""
    print("Disconnected from radio")

# Subscribe to events
pub.subscribe(onReceive, "meshtastic.receive")
pub.subscribe(onReceiveText, "meshtastic.receive.text")
pub.subscribe(onConnection, "meshtastic.connection.established")
pub.subscribe(onDisconnect, "meshtastic.connection.lost")

# Connect to device
interface = meshtastic.serial_interface.SerialInterface()
```

### Available Event Topics

| Topic | Description |
|-------|-------------|
| `meshtastic.connection.established` | Successfully connected and downloaded node DB |
| `meshtastic.connection.lost` | Link to radio lost |
| `meshtastic.receive` | Any packet received |
| `meshtastic.receive.text` | Text message received |
| `meshtastic.receive.position` | Position update received |
| `meshtastic.receive.user` | User info received |
| `meshtastic.receive.data.portnum` | Data packet on specific port |
| `meshtastic.node.updated` | Node database updated |
| `meshtastic.log.line` | Debug log line |

---

## Core Messaging Methods

### Sending Text Messages

```python
# Broadcast to all nodes
interface.sendText("Hello everyone!")

# Send to specific node (by ID)
interface.sendText("Hello!", destinationId="!abcd1234")

# Send with acknowledgment request
interface.sendText("Important message", wantAck=True)

# Send on specific channel
interface.sendText("Channel 2 message", channelIndex=2)
```

**sendText() Parameters:**
- `text` (str): Message content
- `destinationId` (Union[int, str]): Target node (default: broadcast)
- `wantAck` (bool): Request delivery confirmation
- `wantResponse` (bool): Request application-layer response
- `onResponse` (Callable): Response handler callback
- `channelIndex` (int): Channel to use (default: 0)

### Sending Binary Data

```python
# Send raw bytes
interface.sendData(
    data=b'\x01\x02\x03\x04',
    destinationId="!abcd1234",
    portNum=portnums_pb2.PortNum.PRIVATE_APP
)
```

**sendData() Parameters:**
- `data`: Payload (bytes or protobuf)
- `destinationId`: Target node
- `portNum`: Application port number
- `wantAck` (bool): Reliable delivery mode
- `hopLimit` (int): Maximum hop count
- `priority`: Message priority level

### Sending Position

```python
interface.sendPosition(
    latitude=37.7749,
    longitude=-122.4194,
    altitude=10
)
```

### Sending Telemetry

```python
interface.sendTelemetry(
    destinationId="!abcd1234",
    telemetryType="device_metrics"
)
```

### Trace Route

```python
interface.sendTraceRoute(
    dest="!abcd1234",
    hopLimit=7
)
```

---

## Node Information & Configuration

### Accessing Node Database

```python
# Get all known nodes
for node_id, node in interface.nodes.items():
    print(f"Node {node_id}: {node}")

# Access nodes by number
node = interface.nodesByNum[123456789]

# Get local node info
my_info = interface.getMyNodeInfo()
my_user = interface.getMyUser()
long_name = interface.getLongName()
short_name = interface.getShortName()
```

### Configuring the Local Node

```python
# Get local node object
ourNode = interface.getNode('^local')

# Read current configuration
print(f"Local config: {ourNode.localConfig}")
print(f"Module config: {ourNode.moduleConfig}")

# Modify configuration
ourNode.localConfig.position.gps_update_interval = 60
ourNode.localConfig.power.is_power_saving = True

# Write changes (specify config section)
ourNode.writeConfig("position")
ourNode.writeConfig("power")
```

### Channel Configuration

```python
# Get channel info
channels = ourNode.channels

# Modify channels
ourNode.channels[0].settings.name = "MyChannel"
ourNode.writeChannel(0)
```

---

## Interface Properties

| Property | Description |
|----------|-------------|
| `interface.nodes` | Read-only dict of mesh nodes with location/username |
| `interface.nodesByNum` | Node lookup by numerical ID |
| `interface.myInfo` | Local radio device information |
| `interface.metadata` | Device metadata (version, hardware) |
| `interface.localNode` | Reference to local device node object |

---

## Complete Example: Monitor & Control App

```python
#!/usr/bin/env python3
"""
Meshtastic Monitor & Control Application
"""

import time
import meshtastic
import meshtastic.serial_interface
from pubsub import pub


class MeshtasticApp:
    def __init__(self, device_path=None):
        self.interface = None
        self.device_path = device_path
        self.running = False

    def on_receive(self, packet, interface):
        """Handle incoming packets"""
        try:
            decoded = packet.get('decoded', {})
            from_id = packet.get('fromId', 'unknown')

            # Handle text messages
            if 'text' in decoded:
                print(f"[TEXT] From {from_id}: {decoded['text']}")

            # Handle position updates
            elif decoded.get('portnum') == 'POSITION_APP':
                pos = decoded.get('position', {})
                lat = pos.get('latitude', 0)
                lon = pos.get('longitude', 0)
                print(f"[POS] From {from_id}: {lat}, {lon}")

            # Handle telemetry
            elif decoded.get('portnum') == 'TELEMETRY_APP':
                print(f"[TELEMETRY] From {from_id}: {decoded}")

        except Exception as e:
            print(f"Error processing packet: {e}")

    def on_connection(self, interface, topic=pub.AUTO_TOPIC):
        """Called when connected to device"""
        print("Connected to Meshtastic device!")
        print(f"Device: {interface.getLongName()}")
        print(f"Node ID: {interface.getMyNodeInfo().get('user', {}).get('id', 'unknown')}")

        # List known nodes
        print("\nKnown nodes:")
        for node_id, node in interface.nodes.items():
            user = node.get('user', {})
            print(f"  - {user.get('longName', node_id)}")

    def on_disconnect(self, interface, topic=pub.AUTO_TOPIC):
        """Called when connection lost"""
        print("Disconnected from device!")
        self.running = False

    def connect(self):
        """Establish connection to device"""
        # Subscribe to events
        pub.subscribe(self.on_receive, "meshtastic.receive")
        pub.subscribe(self.on_connection, "meshtastic.connection.established")
        pub.subscribe(self.on_disconnect, "meshtastic.connection.lost")

        # Connect
        if self.device_path:
            self.interface = meshtastic.serial_interface.SerialInterface(
                devPath=self.device_path
            )
        else:
            self.interface = meshtastic.serial_interface.SerialInterface()

        self.running = True

    def send_message(self, text, destination=None):
        """Send a text message"""
        if destination:
            self.interface.sendText(text, destinationId=destination)
        else:
            self.interface.sendText(text)
        print(f"Sent: {text}")

    def send_position(self, lat, lon, alt=0):
        """Send position update"""
        self.interface.sendPosition(latitude=lat, longitude=lon, altitude=alt)
        print(f"Sent position: {lat}, {lon}")

    def get_node_info(self):
        """Get information about all nodes"""
        nodes = []
        for node_id, node in self.interface.nodes.items():
            user = node.get('user', {})
            position = node.get('position', {})
            nodes.append({
                'id': node_id,
                'long_name': user.get('longName', 'Unknown'),
                'short_name': user.get('shortName', '????'),
                'latitude': position.get('latitude'),
                'longitude': position.get('longitude'),
                'last_heard': node.get('lastHeard')
            })
        return nodes

    def configure_device(self, **settings):
        """Configure device settings"""
        local_node = self.interface.getNode('^local')

        if 'gps_update_interval' in settings:
            local_node.localConfig.position.gps_update_interval = settings['gps_update_interval']
            local_node.writeConfig("position")

        if 'is_power_saving' in settings:
            local_node.localConfig.power.is_power_saving = settings['is_power_saving']
            local_node.writeConfig("power")

    def run(self):
        """Main run loop"""
        try:
            while self.running:
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\nShutting down...")
        finally:
            self.close()

    def close(self):
        """Clean up connection"""
        if self.interface:
            self.interface.close()
            self.interface = None


# Usage
if __name__ == "__main__":
    app = MeshtasticApp()
    app.connect()

    # Example: Send a message
    # app.send_message("Hello from Python!")

    # Example: Get all nodes
    # nodes = app.get_node_info()
    # for node in nodes:
    #     print(node)

    # Run main loop (listens for messages)
    app.run()
```

---

## Security & PKI (Public Key Infrastructure)

Meshtastic v2.5+ uses X25519 elliptic curve cryptography for device-to-device encryption and authentication.

### Key Types

1. **Device PKI Keys** - X25519 public/private key pairs for secure direct messaging
2. **Channel PSK** - Pre-shared keys for channel encryption (AES128 or AES256)
3. **Admin Keys** - Keys for remote device administration

### Accessing Security Configuration

```python
# Get local node
local_node = interface.getNode('^local')

# Access security config
security = local_node.localConfig.security

# Read keys (as bytes)
private_key = bytes(security.private_key)
public_key = bytes(security.public_key)

# Or use interface method for public key
public_key = interface.getPublicKey()
```

### Setting Private Key via CLI

```bash
# Set private key (base64 encoded)
meshtastic --set security.private_key base64:YOUR_BASE64_KEY_HERE

# Set public key
meshtastic --set security.public_key base64:YOUR_BASE64_KEY_HERE

# Export full configuration (includes keys)
meshtastic --export-config > config_backup.yaml

# Restore configuration
meshtastic --configure config_backup.yaml
```

### Channel PSK Configuration

```bash
# Set encryption key on channel 0 (primary)
meshtastic --ch-set psk base64:puavdd7vtYJh8NUVWgxbsoG2u9Sdqc54YvMLs+KNcMA= --ch-index 0

# Set key in hex format
meshtastic --ch-set psk 0x1a1a1a1a2b2b2b2b1a1a1a1a2b2b2b2b1a1a1a1a2b2b2b2b1a1a1a1a2b2b2b2b --ch-index 0

# Generate random key
meshtastic --ch-set psk random --ch-index 1

# Disable encryption
meshtastic --ch-set psk none --ch-index 0
```

### Sending on Specific Channels

```python
# Send position on channel 1 (private channel)
interface.sendPosition(
    latitude=37.7749,
    longitude=-122.4194,
    altitude=10,
    channelIndex=1  # Use private channel
)

# Send text on channel 1
interface.sendText("Private message", channelIndex=1)

# Send data on channel 1
interface.sendData(
    data=my_bytes,
    channelIndex=1,
    portNum=portnums_pb2.PortNum.PRIVATE_APP
)
```

### Key Backup Warning

Public and private keys are regenerated on firmware erase/reinstall. Always backup:
```bash
meshtastic --export-config > my_device_backup.yaml
```

---

## Example Scripts Reference

The [Meshtastic-Python-Examples](https://github.com/pdxlocations/Meshtastic-Python-Examples) repository contains 39 example scripts:

### Messaging
- `send-and-receive.py` - Terminal-based bidirectional messaging
- `autoresponder.py` - Auto-reply to incoming messages
- `send-direct-message.py` - Direct messaging to specific nodes
- `print-messages.py` - Display received messages

### Data & Packets
- `print-packets.py` - Parse and display all packets
- `print-packets-json.py` - Output packets in JSON format
- `decode-protobuf.py` - Decode Protocol Buffer messages

### Telemetry & Position
- `send-position.py` - Transmit location data
- `request-position.py` - Query positions from nodes
- `send-telemetry.py` - Send telemetry data
- `send-device-metrics.py` - Share device metrics
- `send-environment-metrics.py` - Environmental sensor data

### Diagnostics
- `send-traceroute-simple.py` - Trace route to a node
- `print-traceroute.py` - Display all traceroutes
- `test-rssi-snr.py` - Signal strength testing

### Configuration
- `get-channels.py` - Retrieve channel configurations
- `save-channel.py` - Store channel configs
- `print-nodedb.py` - List nodes in database

### MQTT Integration
- `mqtt-client.py` - MQTT publishing
- `receive-mqtt-packets.py` - MQTT subscription
- `serial-mqtt.py` - Serial to MQTT bridge

---

## Sources

- [Meshtastic Python CLI Guide](https://meshtastic.org/docs/software/python/cli/)
- [Meshtastic Python Library Usage](https://meshtastic.org/docs/development/python/library/)
- [Meshtastic API Documentation](https://python.meshtastic.org/)
- [SerialInterface API](https://python.meshtastic.org/serial_interface.html)
- [MeshInterface API](https://python.meshtastic.org/mesh_interface.html)
- [TCPInterface API](https://python.meshtastic.org/tcp_interface.html)
- [BLEInterface API](https://python.meshtastic.org/ble_interface.html)
- [Node API](https://python.meshtastic.org/node.html)
- [Security Configuration](https://meshtastic.org/docs/configuration/radio/security/)
- [Channel Configuration](https://meshtastic.org/docs/configuration/radio/channels/)
- [Meshtastic Encryption](https://meshtastic.org/docs/overview/encryption/)
- [PKI Implementation](https://meshtastic.org/blog/introducing-new-public-key-cryptography-in-v2_5/)
- [GitHub Repository](https://github.com/meshtastic/python)
- [PyPI Package](https://pypi.org/project/meshtastic/)
- [Meshtastic Python Examples](https://github.com/pdxlocations/Meshtastic-Python-Examples)
