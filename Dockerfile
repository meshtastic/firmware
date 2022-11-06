FROM debian:bullseye-slim
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get -y install wget python3 g++ zip python3-venv git vim
RUN wget https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -O get-platformio.py; chmod +x get-platformio.py
RUN python3 get-platformio.py
RUN git clone https://github.com/meshtastic/firmware --recurse-submodules
RUN cd firmware
RUN chmod +x ./firmware/bin/build-native.sh
RUN . ~/.platformio/penv/bin/activate; cd firmware; sh ./bin/build-native.sh

CMD ["/firmware/release/meshtasticd_linux_amd64"]