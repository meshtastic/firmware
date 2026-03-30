# meshtasticd configuration files

This directory contains YAML configuration files for meshtasticd. Each file describes a specific hardware configuration, including the LoRa module and pin assignments. These configurations are used by meshtasticd to correctly interface with the hardware.

## Metadata structure

Each configuration file includes a `Meta` section that provides information about the configuration.
This configuration is consumed by configuration-selection tools.

```yaml
Meta:
  name: MeshAdv-Pi E22-900M30S # A unique identifier for this configuration.
  support: community # community, official, or deprecated; determined by Meshtastic Leads.
  compatible: # A list of compatible products or platforms.
    - raspberry-pi
```
`name`: A unique identifier for the configuration, typically reflecting the hardware it supports.

`support`: Indicates the level of support for this configuration. It can be one of the following:

- `community`: Supported by the Meshtastic community. Meshtastic Members may not possess, or have not tested this configuration.
- `official`: Fully supported by Meshtastic. Meshtastic Members have tested and verified this configuration.
- `deprecated`: No longer recommended for deployment by Meshtastic.

`compatible`: A list of compatible products or platforms that can use this configuration.
This will vary depending on the intended use case / platform.
Multiple compatible entries can be included. E.g. Armbian `BOARD` value or OpenWrt `TARGET` value.
These tags can be consumed by different configuration-selection tools, filtering based upon their platform/etc.
