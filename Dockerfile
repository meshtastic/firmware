FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# http://bugs.python.org/issue19846
# > At the moment, setting "LANG=C" on a Linux system *fundamentally breaks Python 3*, and that's not OK.
ENV LANG C.UTF-8

# Install build deps
USER root

# trunk-ignore(terrascan/AC_DOCKER_0002): Known terrascan issue
# trunk-ignore(hadolint/DL3008): Use latest version of packages for buildchain
RUN apt-get update && apt-get install --no-install-recommends -y wget python3 python3-pip python3-wheel python3-venv g++ zip git \
                           ca-certificates libgpiod-dev libyaml-cpp-dev libbluetooth-dev \
                           libulfius-dev liborcania-dev libssl-dev pkg-config && \
    apt-get clean && rm -rf /var/lib/apt/lists/* && mkdir /tmp/firmware

RUN groupadd -g 1000 mesh && useradd -ml -u 1000 -g 1000 mesh && chown mesh:mesh /tmp/firmware
USER mesh

WORKDIR /tmp/firmware
RUN python3 -m venv /tmp/firmware 
RUN bash -o pipefail -c "source bin/activate; pip3 install --no-cache-dir -U platformio==6.1.15"
# trunk-ignore(terrascan/AC_DOCKER_00024): We would actually like these files to be owned by mesh tyvm
COPY --chown=mesh:mesh . /tmp/firmware
RUN bash -o pipefail -c "source ./bin/activate && bash ./bin/build-native.sh"
RUN cp "/tmp/firmware/release/meshtasticd_linux_$(uname -m)" "/tmp/firmware/release/meshtasticd"


##### PRODUCTION BUILD #############

FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# trunk-ignore(terrascan/AC_DOCKER_0002): Known terrascan issue
# trunk-ignore(hadolint/DL3008): Use latest version of packages for buildchain
RUN apt-get update && apt-get --no-install-recommends -y install libc-bin libc6 libgpiod2 libyaml-cpp0.7 libulfius2.7 liborcania2.3 libssl3 && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN groupadd -g 1000 mesh && useradd -ml -u 1000 -g 1000 mesh
USER mesh

WORKDIR /home/mesh
COPY --from=builder /tmp/firmware/release/meshtasticd /home/mesh/

RUN mkdir data
VOLUME /home/mesh/data

CMD [ "sh",  "-cx", "./meshtasticd -d /home/mesh/data --hwid=${HWID:-$RANDOM}" ]

HEALTHCHECK NONE
