#!/usr/bin/env python

"""Generate the CI matrix."""

import json
import sys
import random
import re
from platformio.project.config import ProjectConfig

options = sys.argv[1:]

outlist = []

if len(options) < 1:
  print(json.dumps(outlist))
  exit(1)

cfg = ProjectConfig.get_instance()
pio_envs = cfg.envs()

# Gather all PlatformIO environments for filtering later
all_envs = []
for pio_env in pio_envs:
  env_build_flags = cfg.get(f"env:{pio_env}", 'build_flags')
  env_platform = None
  for flag in env_build_flags:
    # Extract the platform from the build flags
    # Example flag: -I variants/esp32s3/heltec-v3
    match = re.search(r"-I\s?variants/([^/]+)", flag)
    if match:
      env_platform = match.group(1)
      break
  # Intentionally fail if platform cannot be determined
  if not env_platform:
    print(f"Error: Could not determine platform for environment '{pio_env}'")
    exit(1)
  # Store env details as a dictionary, and add to 'all_envs' list
  env = {
    'name': pio_env,
    'platform': env_platform,
    'board_level': cfg.get(f"env:{pio_env}", 'board_level', default=None),
    'board_check': bool(cfg.get(f"env:{pio_env}", 'board_check', default=False))
  }
  all_envs.append(env)

# Filter outputs based on options
# Check is currently mutually exclusive with other options
if "check" in options:
  for env in all_envs:
    if env['board_check']:
      outlist.append(env['name'])
# Filter (non-check) builds by platform
else:
  for env in all_envs:
    if options[0] == env['platform']:
      # If no board level is specified, always include it
      if not env['board_level']:
        outlist.append(env['name'])
      # Include `extra` boards when requested
      elif "extra" in options and env['board_level'] == "extra":
        outlist.append(env['name'])

# Return as a JSON list
if ("quick" in options) and (len(outlist) > 3):
    print(json.dumps(random.sample(outlist, 3)))
else:
    print(json.dumps(outlist))
