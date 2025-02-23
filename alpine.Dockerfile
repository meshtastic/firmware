# trunk-ignore-all(trivy/DS002): We must run as root for this container
# trunk-ignore-all(checkov/CKV_DOCKER_8): We must run as root for this container
# trunk-ignore-all(hadolint/DL3002): We must run as root for this container

FROM python:3.12-alpine3.21 AS builder

ENV PIP_ROOT_USER_ACTION=ignore
RUN apk add bash g++ libstdc++-dev linux-headers zip git ca-certificates libgpiod-dev yaml-cpp-dev bluez-dev \
        libusb-dev i2c-tools-dev openssl-dev pkgconf argp-standalone && \
    pip install --no-cache-dir -U platformio==6.1.16 && \
    mkdir /tmp/firmware

WORKDIR /tmp/firmware
COPY . /tmp/firmware

# Create small package (no debugging symbols)
# Add `argp` for musl
ENV PLATFORMIO_BUILD_FLAGS="-Os -ffunction-sections -fdata-sections -Wl,--gc-sections -largp"

RUN bash ./bin/build-native.sh && \
    cp "/tmp/firmware/release/meshtasticd_linux_$(uname -m)" "/tmp/firmware/release/meshtasticd"

# ##### PRODUCTION BUILD #############

FROM alpine:3.21

# nosemgrep: dockerfile.security.last-user-is-root.last-user-is-root
USER root

RUN apk add libstdc++ libgpiod yaml-cpp libusb i2c-tools \
    && mkdir -p /var/lib/meshtasticd \
    && mkdir -p /etc/meshtasticd/config.d
COPY --from=builder /tmp/firmware/release/meshtasticd /usr/sbin/

WORKDIR /var/lib/meshtasticd
VOLUME /var/lib/meshtasticd

EXPOSE 4403

CMD [ "sh",  "-cx", "meshtasticd --fsdir=/var/lib/meshtasticd" ]

HEALTHCHECK NONE