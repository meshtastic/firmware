#!/usr/bin/env bash

cp "release/meshtasticd_linux_$(uname -m)" /usr/bin/meshtasticd
cp "bin/meshtasticd-start.sh" /usr/bin/meshtasticd-start.sh
mkdir -p /etc/meshtasticd
if [[ -f "/etc/meshtasticd/config.yaml" ]]; then
	cp bin/config-dist.yaml /etc/meshtasticd/config-upgrade.yaml
else
	cp bin/config-dist.yaml /etc/meshtasticd/config.yaml
fi
cp bin/meshtasticd.service /usr/lib/systemd/system/meshtasticd.service
