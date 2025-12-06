# This container is used to build Meshtastic with the libraries required by the fuzzer.
# ClusterFuzzLite starts the container, runs the build.sh script, and then exits.

# As this is not a long running service, health-checks are not required. ClusterFuzzLite
# also only works if the user remains unchanged from the base image (it expects to run
# as root).
# trunk-ignore-all(trivy/DS026): No healthcheck is needed for this builder container
# trunk-ignore-all(checkov/CKV_DOCKER_2): No healthcheck is needed for this builder container
# trunk-ignore-all(checkov/CKV_DOCKER_3): We must run as root for this container
# trunk-ignore-all(trivy/DS002): We must run as root for this container
# trunk-ignore-all(checkov/CKV_DOCKER_8): We must run as root for this container
# trunk-ignore-all(hadolint/DL3002): We must run as root for this container

FROM gcr.io/oss-fuzz-base/base-builder:v1

ENV PIP_ROOT_USER_ACTION=ignore

# trunk-ignore(hadolint/DL3008): apt packages are not pinned.
# trunk-ignore(terrascan/AC_DOCKER_0002): apt packages are not pinned.
RUN apt-get update && apt-get install --no-install-recommends -y \
        cmake git zip libgpiod-dev libbluetooth-dev libi2c-dev \
        libunistring-dev libmicrohttpd-dev libgnutls28-dev libgcrypt20-dev \
        libusb-1.0-0-dev libssl-dev pkg-config && \
    apt-get clean && rm -rf /var/lib/apt/lists/* && \
    pip install --no-cache-dir -U \
        platformio==6.1.16 \
        grpcio-tools==1.68.1 \
        meshtastic==2.5.9

# Ugly hack to avoid clang detecting a conflict between the math "log" function and the "log" function in framework-portduino/cores/portduino/logging.h
RUN sed -i -e 's/__MATHCALL_VEC (log,, (_Mdouble_ __x));//' /usr/include/x86_64-linux-gnu/bits/mathcalls.h

# A few dependencies are too old on the base-builder image. More recent versions are built from source.
WORKDIR $SRC
RUN git config --global advice.detachedHead false && \
    git clone --depth 1 --branch 0.8.0 https://github.com/jbeder/yaml-cpp.git && \
    git clone --depth 1 --branch v2.3.3 https://github.com/babelouest/orcania.git && \
    git clone --depth 1 --branch v1.4.20 https://github.com/babelouest/yder.git && \
    git clone --depth 1 --branch v2.7.15 https://github.com/babelouest/ulfius.git

COPY ./.clusterfuzzlite/build.sh $SRC/

WORKDIR $SRC/firmware
COPY . $SRC/firmware/

# https://docs.platformio.org/en/latest/envvars.html
ENV PLATFORMIO_CORE_DIR=$SRC/pio/core \
    PLATFORMIO_LIBDEPS_DIR=$SRC/pio/libdeps \
    PLATFORMIO_PACKAGES_DIR=$SRC/pio/packages \
    PLATFORMIO_SETTING_ENABLE_CACHE=No \
    PIO_ENV=buildroot
RUN platformio pkg install --environment $PIO_ENV
