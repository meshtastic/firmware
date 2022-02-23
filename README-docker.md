## What is Docker used for

Developers can simulate Device hardware by compiling and running
a linux native binary application. If you do not own a Linux
machine, or you just want to separate things, you might want
to run simulator inside a docker container

## The Image
To build docker image, type

  `docker build -t meshtastic/device .`

## Usage

To run a container, type

  `docker run --rm -p 4403:4403 meshtastic/device`

or, to get an interactive shell on the docker created container:

  `docker run -it -p 4403:4403 meshtastic/device bash`

You might want to mount your local development folder:

  `docker run -it --mount type=bind,source=/PathToMyProjects/Meshtastic/Meshtastic-device-mybranch,target=/Meshtastic-device-mybranch -p 4403:4403 meshtastic/device bash`

## Build the native application

Linux native application should be built inside the container.
For this you must run container with interactive console
"-it", as seen above.

First, some environment variables need to be set up with command:

  `. ~/.platformio/penv/bin/activate`

You also want to make some adjustments in the bin/build-all.sh to conform the amd64 build:

```
  sed -i 's/^BOARDS_ESP32.*/BOARDS_ESP32=""/' bin/build-all.sh
  sed -i 's/^BOARDS_NRF52.*/BOARDS_NRF52=""/' bin/build-all.sh
  sed -i 's/echo "Building SPIFFS.*/exit/' bin/build-all.sh
```

You can build amd64 image with command

`bin/build-all.sh`

## Executing the application interactively

The built binary file should be found under name
`release/latest/bins/universal/meshtastic_linux_amd64`.
If this is not the case, you can also use direct program name:
`.pio/build/native/program`

To use python cli against exposed port 4403,
type this in the host machine:

`meshtastic --info --host localhost`

## Stop the container

Run this to get the ID:

`docker ps`

Stop the container with command:

`docker kill <id>`

> Tip: you can just use the first few characters of the ID in docker commands

