#!/usr/bin/env bash

cp "release/meshtasticd_linux_$(arch)" /usr/sbin/meshtasticd
mkdir /etc/meshtasticd
if [[ -f "/etc/meshtasticd/config.yaml" ]]; then
	cp bin/config-dist.yaml /etc/meshtasticd/config-upgrade.yaml
else
	cp bin/config-dist.yaml /etc/meshtasticd/config.yaml
fi
cp bin/meshtasticd.service /usr/lib/systemd/system/meshtasticd.service
