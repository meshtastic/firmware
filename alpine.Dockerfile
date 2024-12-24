FROM python:3.12-alpine3.21 AS builder

ENV PIP_ROOT_USER_ACTION=ignore
RUN apk add bash g++ libstdc++-dev linux-headers zip git ca-certificates libgpiod-dev yaml-cpp-dev bluez-dev \
        libusb-dev i2c-tools-dev openssl-dev pkgconf argp-standalone && \
    pip install --no-cache-dir -U platformio==6.1.16 && \
    mkdir /tmp/firmware

WORKDIR /tmp/firmware
COPY . /tmp/firmware

# For musl
ENV PLATFORMIO_BUILD_FLAGS="-largp"

RUN bash ./bin/build-native.sh && \
    cp "/tmp/firmware/release/meshtasticd_linux_$(uname -m)" "/tmp/firmware/release/meshtasticd"

# ##### PRODUCTION BUILD #############

FROM alpine:3.21

RUN apk add libstdc++ libgpiod yaml-cpp libusb i2c-tools gdb \
    && mkdir -p /var/lib/meshtasticd \
    && mkdir -p /etc/meshtasticd/config.d
COPY --from=builder /tmp/firmware/release/meshtasticd /usr/sbin/

WORKDIR /var/lib/meshtasticd
VOLUME /var/lib/meshtasticd

EXPOSE 4403

CMD [ "sh",  "-cx", "meshtasticd --fsdir=/var/lib/meshtasticd" ]

HEALTHCHECK NONE