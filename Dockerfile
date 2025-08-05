# trunk-ignore-all(trivy/DS002): We must run as root for this container
# trunk-ignore-all(hadolint/DL3002): We must run as root for this container
# trunk-ignore-all(hadolint/DL3008): Do not pin apt package versions
# trunk-ignore-all(hadolint/DL3013): Do not pin pip package versions

FROM python:3.13-bookworm AS builder
ARG PIO_ENV=native
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# Install Dependencies
ENV PIP_ROOT_USER_ACTION=ignore
RUN apt-get update && apt-get install --no-install-recommends -y \
        curl wget g++ zip git ca-certificates pkg-config \
        libgpiod-dev libyaml-cpp-dev libbluetooth-dev libi2c-dev libuv1-dev \
        libusb-1.0-0-dev libulfius-dev liborcania-dev libssl-dev \
        libx11-dev libinput-dev libxkbcommon-x11-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir -U platformio \
    && mkdir /tmp/firmware

# Copy source code
WORKDIR /tmp/firmware
COPY . /tmp/firmware

# Build
RUN bash ./bin/build-native.sh "$PIO_ENV" && \
    cp "/tmp/firmware/release/meshtasticd_linux_$(uname -m)" "/tmp/firmware/release/meshtasticd"

# Fetch web assets
RUN curl -L "https://github.com/meshtastic/web/releases/download/v$(cat /tmp/firmware/bin/web.version)/build.tar" -o /tmp/web.tar \
    && mkdir -p /tmp/web \
    && tar -xf /tmp/web.tar -C /tmp/web/ \
    && gzip -dr /tmp/web \
    && rm /tmp/web.tar

##### PRODUCTION BUILD #############

FROM debian:bookworm-slim
LABEL org.opencontainers.image.title="Meshtastic" \
      org.opencontainers.image.description="Debian Meshtastic daemon and web interface" \
      org.opencontainers.image.url="https://meshtastic.org" \
      org.opencontainers.image.documentation="https://meshtastic.org/docs/" \
      org.opencontainers.image.authors="Meshtastic" \
      org.opencontainers.image.licenses="GPL-3.0-or-later" \
      org.opencontainers.image.source="https://github.com/meshtastic/firmware/"
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# nosemgrep: dockerfile.security.last-user-is-root.last-user-is-root
USER root

RUN apt-get update && apt-get --no-install-recommends -y install \
        libc-bin libc6 libgpiod2 libyaml-cpp0.7 libi2c0 libuv1 libusb-1.0-0-dev \
        liborcania2.3 libulfius2.7 libssl3 \
        libx11-6 libinput10 libxkbcommon-x11-0 \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /var/lib/meshtasticd \
    && mkdir -p /etc/meshtasticd/config.d \
    && mkdir -p /etc/meshtasticd/ssl

# Fetch compiled binary from the builder
COPY --from=builder /tmp/firmware/release/meshtasticd /usr/bin/
COPY --from=builder /tmp/web /usr/share/meshtasticd/
# Copy config templates
COPY ./bin/config.d /etc/meshtasticd/available.d

WORKDIR /var/lib/meshtasticd
VOLUME /var/lib/meshtasticd

# Expose Meshtastic TCP API port from the host
EXPOSE 4403
# Expose Meshtastic Web UI port from the host
EXPOSE 9443

CMD [ "sh", "-cx", "meshtasticd --fsdir=/var/lib/meshtasticd" ]

HEALTHCHECK NONE
