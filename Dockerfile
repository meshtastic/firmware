# trunk-ignore-all(terrascan/AC_DOCKER_0002): Known terrascan issue
# trunk-ignore-all(hadolint/DL3008): Use latest version of apt packages for buildchain
# trunk-ignore-all(trivy/DS002): We must run as root for this container
# trunk-ignore-all(checkov/CKV_DOCKER_8): We must run as root for this container
# trunk-ignore-all(hadolint/DL3002): We must run as root for this container

FROM python:3.12-bookworm AS builder
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# Install Dependencies
ENV PIP_ROOT_USER_ACTION=ignore
RUN apt-get update && apt-get install --no-install-recommends -y wget g++ zip git ca-certificates \
        libgpiod-dev libyaml-cpp-dev libbluetooth-dev libi2c-dev \
        libusb-1.0-0-dev libulfius-dev liborcania-dev libssl-dev pkg-config && \
    apt-get clean && rm -rf /var/lib/apt/lists/* && \
    pip install --no-cache-dir -U platformio==6.1.16 && \
    mkdir /tmp/firmware

# Copy source code
WORKDIR /tmp/firmware
COPY . /tmp/firmware

# Build
RUN bash ./bin/build-native.sh && \
    cp "/tmp/firmware/release/meshtasticd_linux_$(uname -m)" "/tmp/firmware/release/meshtasticd"


##### PRODUCTION BUILD #############

FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# nosemgrep: dockerfile.security.last-user-is-root.last-user-is-root
USER root

RUN apt-get update && apt-get --no-install-recommends -y install libc-bin libc6 libgpiod2 libyaml-cpp0.7 libi2c0 libulfius2.7 libusb-1.0-0-dev liborcania2.3 libssl3 && \
    apt-get clean && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /var/lib/meshtasticd \
    && mkdir -p /etc/meshtasticd/config.d

# Fetch compiled binary from the builder
COPY --from=builder /tmp/firmware/release/meshtasticd /usr/sbin/
# Copy config templates
COPY ./bin/config.d /etc/meshtasticd/available.d

WORKDIR /var/lib/meshtasticd
VOLUME /var/lib/meshtasticd

# Expose Meshtastic TCP API port from the host
EXPOSE 4403

CMD [ "sh", "-cx", "meshtasticd -d /var/lib/meshtasticd" ]

HEALTHCHECK NONE