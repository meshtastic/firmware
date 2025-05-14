# trunk-ignore-all(trivy/DS002): We must run as root for this container
# trunk-ignore-all(hadolint/DL3002): We must run as root for this container
# trunk-ignore-all(hadolint/DL3018): Do not pin apk package versions
# trunk-ignore-all(hadolint/DL3013): Do not pin pip package versions

FROM python:3.13-alpine3.21 AS builder
ARG PIO_ENV=native
ENV PIP_ROOT_USER_ACTION=ignore

RUN apk --no-cache add \
        bash g++ libstdc++-dev linux-headers zip git ca-certificates libgpiod-dev yaml-cpp-dev bluez-dev \
        libusb-dev i2c-tools-dev libuv-dev openssl-dev pkgconf argp-standalone \
        libx11-dev libinput-dev libxkbcommon-dev \
    && rm -rf /var/cache/apk/* \
    && pip install --no-cache-dir -U platformio \
    && mkdir /tmp/firmware

WORKDIR /tmp/firmware
COPY . /tmp/firmware

# Create small package (no debugging symbols)
# Add `argp` for musl
ENV PLATFORMIO_BUILD_FLAGS="-Os -ffunction-sections -fdata-sections -Wl,--gc-sections -largp"

RUN bash ./bin/build-native.sh "$PIO_ENV" && \
    cp "/tmp/firmware/release/meshtasticd_linux_$(uname -m)" "/tmp/firmware/release/meshtasticd"

# ##### PRODUCTION BUILD #############

FROM alpine:3.21

# nosemgrep: dockerfile.security.last-user-is-root.last-user-is-root
USER root

RUN apk --no-cache add \
        libstdc++ libgpiod yaml-cpp libusb i2c-tools libuv \
        libx11 libinput libxkbcommon \
    && rm -rf /var/cache/apk/* \
    && mkdir -p /var/lib/meshtasticd \
    && mkdir -p /etc/meshtasticd/config.d \
    && mkdir -p /etc/meshtasticd/ssl

# Fetch compiled binary from the builder
COPY --from=builder /tmp/firmware/release/meshtasticd /usr/sbin/
# Copy config templates
COPY ./bin/config.d /etc/meshtasticd/available.d

WORKDIR /var/lib/meshtasticd
VOLUME /var/lib/meshtasticd

EXPOSE 4403

CMD [ "sh",  "-cx", "meshtasticd --fsdir=/var/lib/meshtasticd" ]

HEALTHCHECK NONE