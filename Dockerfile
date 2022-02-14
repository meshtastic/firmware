FROM ubuntu
MAINTAINER Kevin Hester <kevinh@geeksville.com>

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get -y install wget python3 g++ zip python3-venv git vim
RUN wget https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -O get-platformio.py; chmod +x get-platformio.py
RUN python3 get-platformio.py
RUN git clone https://github.com/meshtastic/Meshtastic-device.git
RUN cd Meshtastic-device; git submodule update --init --recursive
# only build the simulator
RUN sed -i 's/^BOARDS_ESP32.*/BOARDS_ESP32=""/' Meshtastic-device/bin/build-all.sh
RUN sed -i 's/^BOARDS_NRF52.*/BOARDS_NRF52=""/' Meshtastic-device/bin/build-all.sh
RUN sed -i 's/echo "Building Filesystem.*/exit/' Meshtastic-device/bin/build-all.sh
RUN . ~/.platformio/penv/bin/activate; cd Meshtastic-device; ./bin/build-all.sh

CMD ["/Meshtastic-device/release/latest/bins/universal/meshtasticd_linux_amd64"]
