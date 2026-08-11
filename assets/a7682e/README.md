# A7682E MP3 assets

Use these exact file names on the A7682E file system:

```text
mesh_tx.mp3
mesh_rx.mp3
```

The firmware does not copy these files into the ESP32 LittleFS image. The A7682E has
its own file system, so upload each file once during device provisioning to:

```text
C:/mesh_tx.mp3
C:/mesh_rx.mp3
```

For A7682E firmware with FTP(S) support, download each remote file to local
storage with `AT+CFTPSGETFILE` after starting the FTP service and logging in:

```text
AT+CFTPSGETFILE="/audio/mesh_tx.mp3",1
AT+CFTPSGETFILE="/audio/mesh_rx.mp3",1
```

Wait for `+CFTPSGETFILE:0` before playing. Verify playback with:

```text
AT+CCMXPLAY="C:/mesh_tx.mp3",0,0
AT+CCMXPLAY="C:/mesh_rx.mp3",0,0
```

Use a short MP3 accepted by the A7682E Audio Application Note. The final `0` is
the repeat count; output gain is configured separately by `AT+COUTGAIN=0..7`.
