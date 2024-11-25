FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

USER root

# trunk-ignore(terrascan/AC_DOCKER_0002): Known terrascan issue
# trunk-ignore(hadolint/DL3008): Use latest version of packages
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
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN pipx install platformio==6.1.15

COPY 99-platformio-udev.rules /etc/udev/rules.d/99-platformio-udev.rules

USER vscode

HEALTHCHECK NONE