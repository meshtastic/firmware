# ZmodemModule Firmware Integration Guide

This guide explains how to integrate the ZmodemModule into your Meshtastic firmware build.

## Prerequisites

1. **Meshtastic Firmware**: You must have the Meshtastic firmware source code
2. **AkitaMeshZmodem Library**: The ZModem protocol library must be available
3. **Build Environment**: PlatformIO or Arduino IDE configured for your target platform

## Integration Steps

### 1. Copy Module Files

Copy the ZmodemModule files to your firmware's modules directory:

```bash
# From this directory
cp modules/ZmodemModule.h /path/to/firmware/src/modules/
cp modules/ZmodemModule.cpp /path/to/firmware/src/modules/
cp AkitaMeshZmodemConfig.h /path/to/firmware/src/
```

### 2. Add to Build System

#### For PlatformIO (platformio.ini)

Add the AkitaMeshZmodem library dependency:

```ini
[common]
lib_deps =
    ... existing dependencies ...
    AkitaMeshZmodem  # Or path to library if local
```

#### For Arduino IDE

Ensure the AkitaMeshZmodem library is installed in your Arduino libraries folder.

### 3. Register Module in Firmware

Edit `src/modules/Modules.cpp` to register the ZmodemModule:

```cpp
// Add include near the top
#include "modules/ZmodemModule.h"

// In the module list or initialization function, add:
ZmodemModule *zmodemModule;

void setupModules()
{
    // ... existing module setup ...

    // Add ZmodemModule
    zmodemModule = new ZmodemModule();

    // ... rest of setup ...
}
```

### 4. Port Configuration

Verify that ports 250 and 251 are not used by other modules:

**AkitaMeshZmodemConfig.h** defines:
- `AKZ_ZMODEM_COMMAND_PORTNUM` = 250 (command channel)
- `AKZ_ZMODEM_DATA_PORTNUM` = 251 (data channel)

If these ports conflict with other modules, change them to available ports in the `200-255` range (private application range).

### 5. Compile and Flash

Build the firmware for your target platform:

```bash
# PlatformIO
pio run -t upload

# Or for specific environment
pio run -e tbeam -t upload
```

## Verification

After flashing, verify the module loaded correctly:

### Serial Console

Connect to the device's serial console and look for initialization messages:

```
Initializing ZmodemModule v2.0.0...
  Max concurrent transfers: 5
  Command port: 250
  Data port: 251
  Session timeout: 60000 ms
ZmodemModule initialized successfully.
```

### Test Commands

#### Basic Test - Send a File

From another node, send a test command:

```
SEND:!<destination_node_id>:/path/to/file.txt
```

Expected response:
```
OK: Started SEND of /path/to/file.txt to !<destination_node_id>
```

#### Basic Test - Receive a File

```
RECV:/path/to/save.txt
```

Expected response:
```
OK: Started RECV to /path/to/save.txt. Waiting for sender...
```

## Configuration Options

### Maximum Concurrent Transfers

Default: 5 transfers

To change, define before including the header:

```cpp
#define MAX_CONCURRENT_TRANSFERS 10
#include "ZmodemModule.h"
```

Or modify in `ZmodemModule.h`:

```cpp
#ifndef MAX_CONCURRENT_TRANSFERS
#define MAX_CONCURRENT_TRANSFERS 10  // Change here
#endif
```

### Session Timeout

Default: 60000 ms (60 seconds)

To change:

```cpp
#ifndef TRANSFER_SESSION_TIMEOUT_MS
#define TRANSFER_SESSION_TIMEOUT_MS 120000  // 2 minutes
#endif
```

### Port Numbers

If ports 250/251 conflict, change in `AkitaMeshZmodemConfig.h`:

```cpp
#define AKZ_ZMODEM_COMMAND_PORTNUM 252
#define AKZ_ZMODEM_DATA_PORTNUM 253
```

## Troubleshooting

### Module Not Loading

**Symptom**: No initialization messages in serial console

**Solutions**:
1. Verify `ZmodemModule.cpp` is compiled (check build output)
2. Check that module registration code was added to `Modules.cpp`
3. Ensure `MeshModule` base class is available

### Filesystem Errors

**Symptom**: "Filesystem not available" error

**Solutions**:
1. Ensure your device has a filesystem (LittleFS, SPIFFS, SD card)
2. Verify filesystem is initialized before modules in firmware boot sequence
3. Check `FSCom` or equivalent is defined for your platform

### Packet Not Received

**Symptom**: Commands sent but no response

**Solutions**:
1. Verify ports 250/251 are not blocked by firmware
2. Check that other nodes can reach your device (test with text messages)
3. Verify command format: `SEND:!<hex_node_id>:/path` or `RECV:/path`
4. Enable DEBUG logging to see packet reception

### Transfer Hangs

**Symptom**: Transfer starts but doesn't complete

**Solutions**:
1. Check AkitaMeshZmodem library is properly initialized
2. Verify `loop()` is being called regularly (every ~100ms or faster)
3. Check for memory issues (insufficient RAM for buffers)
4. Review timeout settings - may need longer timeout for large files

### Multiple Transfers Fail

**Symptom**: First transfer works, subsequent ones fail

**Solutions**:
1. Verify session cleanup is working (check `logSessionStats()` output)
2. Check memory leaks in AkitaMeshZmodem instances
3. Increase `MAX_CONCURRENT_TRANSFERS` if hitting limit
4. Ensure sessions timeout and cleanup properly

## Debug Logging

To enable detailed logging, add debug flags to your build:

```ini
[env:your_board]
build_flags =
    -DDEBUG_PORT=Serial
    -DLOG_LEVEL=LOG_LEVEL_DEBUG
```

This will show:
- Packet reception (command and data)
- Session creation/removal
- Transfer progress
- Timeout events
- Error conditions

## Memory Considerations

Each active transfer session uses approximately:
- **Base overhead**: ~200 bytes (TransferSession struct)
- **AkitaMeshZmodem instance**: ~500-1000 bytes (depends on library buffers)
- **Total per session**: ~1-1.5 KB

With 5 concurrent transfers: ~5-7.5 KB memory usage

**For devices with limited RAM (<256KB)**:
- Reduce `MAX_CONCURRENT_TRANSFERS` to 2-3
- Monitor free heap during transfers
- Consider disabling other modules if needed

## Network Considerations

### Bandwidth Usage

Each file transfer generates:
- Command packets: ~100 bytes each
- Data packets: ~230 bytes each (configured MTU)
- ACK packets: variable based on protocol

**Recommendations**:
- Limit concurrent transfers on busy networks
- Use smaller packet sizes for congested networks
- Schedule large transfers during low-traffic periods

### Reliability

The module includes:
- ✅ Per-packet ACK (Meshtastic layer)
- ✅ ZModem protocol ACK (application layer)
- ✅ Session timeout and cleanup
- ✅ Automatic retry (via ZModem protocol)

For best reliability:
- Use slower LoRa spreading factors (SF9-SF12)
- Increase timeout values for long-range links
- Monitor signal strength and retry failed transfers

## Coordinator Integration (Future)

The module is designed to be called by a main coordinator system. The coordinator should:

1. **Discovery**: Query available nodes for file transfer capability
2. **Orchestration**: Send commands to appropriate nodes
3. **Monitoring**: Track transfer progress across network
4. **Error Handling**: Retry failed transfers
5. **UI**: Provide user interface for file management

### Coordinator Commands

Commands sent to port 250:

```
SEND:!<target_node_id>:/path/to/source/file.txt
RECV:/path/to/destination/file.txt
```

### Status Monitoring

The coordinator can monitor:
- Module status logging (poll via serial or mesh messages)
- Transfer completion notifications
- Error conditions and retries

## API Reference

### Command Format

#### SEND Command
```
SEND:!<hex_node_id>:/absolute/path/to/file.txt
```

Example:
```
SEND:!a1b2c3d4:/data/sensor.log
```

#### RECV Command
```
RECV:/absolute/path/to/save.txt
```

Example:
```
RECV:/data/received.log
```

### Response Messages

#### Success Responses
```
OK: Started SEND of <filename> to <node>
OK: Started RECV to <filename>. Waiting for sender...
```

#### Error Responses
```
ERROR: Unknown command: <command>
ERROR: Maximum concurrent transfers reached. Try again later.
ERROR: Invalid SEND format. Use SEND:!NodeID:/path/file.txt
ERROR: Invalid RECV format. Use RECV:/path/to/save.txt
ERROR: Invalid destination NodeID: <node>
ERROR: Transfer already in progress with your node
ERROR: Failed to create transfer session
ERROR: Failed to start SEND of <filename>
ERROR: Failed to start RECV to <filename>
```

## Module Status

Check module status via serial log (printed every 30 seconds if transfers active):

```
=== ZmodemModule Status ===
Active sessions: 2 / 5
  Session 1: SEND | SENDING | Node 0xa1b2c3d4 | /data/file1.txt | 45000/100000 bytes (45.0%) | Idle: 250 ms
  Session 2: RECV | RECEIVING | Node 0x12345678 | /data/file2.txt | 12000/50000 bytes (24.0%) | Idle: 180 ms
===========================
```

## Best Practices

### For Development
1. Start with single transfers before testing concurrent
2. Use small test files (<1KB) initially
3. Enable DEBUG logging during development
4. Monitor memory usage with different loads
5. Test timeout and error recovery paths

### For Production
1. Set appropriate timeout values for your network
2. Limit concurrent transfers based on available bandwidth
3. Implement coordinator-level retry logic
4. Monitor transfer success rates
5. Plan for firmware updates and compatibility

### For Network Health
1. Don't monopolize the mesh with large transfers
2. Implement rate limiting if needed
3. Use off-peak times for bulk transfers
4. Monitor network congestion
5. Respect other modules' bandwidth needs

## Support and Further Development

### Known Limitations

1. **AkitaMeshZmodem Integration**: Some TODOs remain for library initialization
   - `begin()` method signature needs verification
   - `processDataPacket()` may need type conversion

2. **NodeID Parsing**: Uses simple hex parsing, may need platform-specific method

3. **ACK Handling**: Relies on AkitaMeshZmodem library implementation

### Future Enhancements

- [ ] Progress reporting API for coordinators
- [ ] Transfer pause/resume capability
- [ ] Bandwidth throttling
- [ ] Transfer priority levels
- [ ] Filesystem quota management
- [ ] Transfer scheduling
- [ ] Encryption support

## Examples

See the `examples/` directory (if available) for:
- Basic send/receive test
- Coordinator integration example
- Multi-node transfer scenario
- Error handling patterns

## License

Copyright (c) 2025 Akita Engineering

(Add appropriate license information)

---

**Version**: 2.0.0
**Last Updated**: 2025-12-01
**Firmware Compatibility**: Meshtastic v2.x+
