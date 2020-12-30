# Remote Hardware Service

FIXME - the following are a collection of notes moved from elsewhere.  We need to refactor these notes into actual documentation on the remote-hardware/gpio service.

### 1.7.2. New 'no-code-IOT' mini-app

Add a new 'remote GPIO/serial port/SPI/I2C access' mini-app. This new standard app would use the MQTT messaging layer to let users (developers that don't need to write device code) do basic (potentially dangerous) operations remotely.

#### 1.7.2.1. Supported operations in the initial release

Initially supported features for no-code-IOT.

- Set any GPIO
- Read any GPIO

#### 1.7.2.2. Supported operations eventually

General ideas for no-code IOT.

- Subscribe for notification of GPIO input status change (i.e. when pin goes low, send my app a message)
- Write/read N bytes over I2C/SPI bus Y (as one atomic I2C/SPI transaction)
- Send N bytes out serial port Z
- Subscribe for notification for when regex X matches the bytes that were received on serial port Z