# trunk-ignore-all(trivy/DS002)
# trunk-ignore-all(trivy/DS026)
# trunk-ignore-all(checkov/CKV_DOCKER_2)
# trunk-ignore-all(checkov/CKV_DOCKER_3)
FROM mcr.microsoft.com/devcontainers/cpp:1-debian-12

# [Optional] Uncomment this section to install additional packages.
# trunk-ignore(terrascan/AC_DOCKER_0002)
# trunk-ignore(hadolint/DL3008)
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