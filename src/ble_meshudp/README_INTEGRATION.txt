README - How to add the BLE MeshUDP prototype into your fork

1) Recommended branch
   - Create a fresh branch from your fork's master that tracks upstream: e.g.
     git checkout -b feature/ble-meshudp-based-on-master origin/master

2) Copy files
   - Copy the files from this archive into your repo under:
       src/ble_meshudp/ble_meshudp.h
       src/ble_meshudp/ble_meshudp.c
       src/ble_meshudp/transport_ble_datagram.cpp

3) platformio.ini
   - PlatformIO will normally include everything under src/ automatically. No edits should be required
     unless your repo uses file lists. If platformio.ini explicitly lists source files, add the new files.

4) Boot-time wiring (quick test)
   - For fast testing without touching Meshtastic transport registry, add calls in your board's main.cpp
     or init path:
       // On the node that should be CENTRAL:
       extern void transport_ble_datagram_init(bool start_as_central);
       transport_ble_datagram_init(true);
       // On the node that should be PERIPHERAL:
       transport_ble_datagram_init(false);
   - This will start advertising on peripheral and start scanning on central.

5) Proper integration
   - Replace the extern meshtastic_receive_datagram_from_transport with the correct Meshtastic transport
     callback (see other transports for exact function names and expected datagram format).
   - Add config knobs in NodeConfig.proto if you want role config persistent across boots.
   - Implement fragmentation/reassembly if you plan to send > MTU bytes (recommended).

6) Build/Flash
   - Build:
       pio run -e nrf52_promicro_diy
   - Flash:
       pio run -e nrf52_promicro_diy -t upload

7) Testing
   - Use nRF Connect to verify advertising (peripheral) and characteristic UUIDs.
   - Confirm central connects and notifications/ writes succeed.
   - Observe serial logs for incoming datagrams being forwarded into Meshtastic code path.

If you want I can generate a git patch and exact commit sequence — but I need write-access to your fork (or you can apply the files and push the branch, then I can open a PR from that branch to your master and iterate).