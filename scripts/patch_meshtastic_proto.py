#!/usr/bin/env python3
"""
Patches the installed meshtastic Python package to add output_gpio_enabled
to Config.DeviceConfig, matching the custom firmware field (tag 14).
"""
import sys
import re
import importlib.util

# --------------------------------------------------------------------------
# 1. Locate the installed meshtastic package
# --------------------------------------------------------------------------
spec = importlib.util.find_spec("meshtastic")
if spec is None:
    print("ERROR: meshtastic package not found. Run: pip install meshtastic")
    sys.exit(1)

import os
pkg_dir = os.path.dirname(spec.origin)
pb2_path = os.path.join(pkg_dir, "protobuf", "config_pb2.py")
pyi_path = os.path.join(pkg_dir, "protobuf", "config_pb2.pyi")

print(f"Found meshtastic at: {pkg_dir}")
print(f"Patching: {pb2_path}")

# --------------------------------------------------------------------------
# 2. Patch config_pb2.py — modify the serialized FileDescriptorProto
# --------------------------------------------------------------------------
from google.protobuf import descriptor_pb2

with open(pb2_path, "r", encoding="utf-8") as f:
    source = f.read()

# Extract the serialized bytes literal
match = re.search(r"AddSerializedFile\((b'.*?')\)", source, re.DOTALL)
if not match:
    print("ERROR: Could not find AddSerializedFile call in config_pb2.py")
    sys.exit(1)

old_bytes_repr = match.group(1)
serialized = eval(old_bytes_repr)  # safe: it's a bytes literal from a known package

# Parse the FileDescriptorProto
file_proto = descriptor_pb2.FileDescriptorProto()
file_proto.ParseFromString(serialized)

# Find Config -> DeviceConfig and add the field
FIELD_NAME = "output_gpio_enabled"
FIELD_NUMBER = 14
TYPE_BOOL = 8   # FieldDescriptorProto.TYPE_BOOL
LABEL_OPTIONAL = 1  # FieldDescriptorProto.LABEL_OPTIONAL

patched = False
for msg in file_proto.message_type:
    if msg.name == "Config":
        for nested in msg.nested_type:
            if nested.name == "DeviceConfig":
                existing_names = [f.name for f in nested.field]
                existing_numbers = [f.number for f in nested.field]
                if FIELD_NAME in existing_names:
                    print(f"Field '{FIELD_NAME}' already present — nothing to do.")
                    sys.exit(0)
                if FIELD_NUMBER in existing_numbers:
                    print(f"ERROR: Field number {FIELD_NUMBER} already in use by another field.")
                    sys.exit(1)
                field = nested.field.add()
                field.name = FIELD_NAME
                field.number = FIELD_NUMBER
                field.type = TYPE_BOOL
                field.label = LABEL_OPTIONAL
                patched = True
                print(f"Added field: {FIELD_NAME} = {FIELD_NUMBER} (bool)")
                break

if not patched:
    print("ERROR: Could not find Config.DeviceConfig in the descriptor.")
    sys.exit(1)

# Re-serialize
new_serialized = file_proto.SerializeToString()
new_bytes_repr = repr(new_serialized)

# Replace in source
new_source = source.replace(old_bytes_repr, new_bytes_repr, 1)

with open(pb2_path, "w", encoding="utf-8") as f:
    f.write(new_source)
print(f"config_pb2.py updated successfully.")

# --------------------------------------------------------------------------
# 3. Patch config_pb2.pyi — add the type stub entry
# --------------------------------------------------------------------------
if not os.path.exists(pyi_path):
    print("config_pb2.pyi not found, skipping stub update.")
    sys.exit(0)

with open(pyi_path, "r", encoding="utf-8") as f:
    pyi_source = f.read()

STUB_FIELD = "    output_gpio_enabled: builtins.bool\n"
STUB_MARKER = "    buzzer_mode: global___Config.DeviceConfig.BuzzerMode.ValueType\n"

if STUB_FIELD in pyi_source:
    print("config_pb2.pyi already has the field — skipping.")
else:
    if STUB_MARKER in pyi_source:
        pyi_source = pyi_source.replace(STUB_MARKER, STUB_MARKER + STUB_FIELD, 1)
        with open(pyi_path, "w", encoding="utf-8") as f:
            f.write(pyi_source)
        print("config_pb2.pyi updated successfully.")
    else:
        print("WARNING: Could not find anchor in config_pb2.pyi — stub not updated.")
        print("         Manual addition needed: add 'output_gpio_enabled: bool' to DeviceConfig.")

print("\nDone. Test with:")
print("  meshtastic --set device.output_gpio_enabled true")
