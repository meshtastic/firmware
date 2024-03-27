FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# http://bugs.python.org/issue19846
# > At the moment, setting "LANG=C" on a Linux system *fundamentally breaks Python 3*, and that's not OK.
ENV LANG C.UTF-8

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Install build deps
USER root
RUN apt-get update && \
	apt-get -y install wget python3 g++ zip python3-venv git vim ca-certificates libgpiod-dev libyaml-cpp-dev libbluetooth-dev

# create a non-priveleged user & group
RUN groupadd -g 1000 mesh && useradd -ml -u 1000 -g 1000 mesh

USER mesh
RUN wget https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -qO /tmp/get-platformio.py && \
	chmod +x /tmp/get-platformio.py && \
	python3 /tmp/get-platformio.py && \
	git clone https://github.com/meshtastic/firmware --recurse-submodules /tmp/firmware && \
	cd /tmp/firmware && \
	chmod +x /tmp/firmware/bin/build-native.sh && \
	source ~/.platformio/penv/bin/activate && \
	./bin/build-native.sh

RUN cp /tmp/firmware/release/meshtasticd_linux_$(uname -m) /tmp/firmware/release/meshtasticd

##### PRODUCTION BUILD #############

FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

RUN apt-get update && \
    apt-get -y install libc-bin libc6 libgpiod2 libyaml-cpp0.7
RUN groupadd -g 1000 mesh && useradd -ml -u 1000 -g 1000 mesh

USER mesh
WORKDIR /home/mesh
COPY --from=builder /tmp/firmware/release/meshtasticd /home/mesh/

CMD sh -cx "./meshtasticd -d /home/mesh/data --hwid '${HWID:-$RANDOM}'"


HEALTHCHECK NONE
