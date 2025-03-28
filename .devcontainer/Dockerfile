# trunk-ignore-all(terrascan/AC_DOCKER_0002): Known terrascan issue
# trunk-ignore-all(hadolint/DL3008): Do not pin apt package versions
# trunk-ignore-all(hadolint/DL3013): Do not pin pip package versions
FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

USER root

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
    ca-certificates \
    g++ \
    git \
    libbluetooth-dev \
    libgpiod-dev \
    liborcania-dev \
    libssl-dev \
    libulfius-dev \
    libyaml-cpp-dev \
    pipx \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    python3-wheel \
    wget \
    zip \
    usbutils \
    hwdata \
    gpg \
    gnupg2 \
    libusb-1.0-0-dev \
    libuv1-dev \
    libi2c-dev \
    libxcb-xkb-dev \
    libxkbcommon-dev \
    libinput-dev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN pipx install platformio

COPY 99-platformio-udev.rules /etc/udev/rules.d/99-platformio-udev.rules

USER vscode

HEALTHCHECK NONE