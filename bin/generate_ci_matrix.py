#!/usr/bin/env python3

"""Generate the CI matrix."""

import argparse
import json
import re
from platformio.project.config import ProjectConfig

parser = argparse.ArgumentParser(description="Generate the CI matrix")
parser.add_argument("platform", help="Platform to build for")
parser.add_argument(
  "--level",
  choices=["extra", "pr"],
  nargs="*",
  default=[],
  help="Board level to build for (omit for full release boards)",
)
args = parser.parse_args()

outlist = []

cfg = ProjectConfig.get_instance()
pio_envs = cfg.envs()

# Gather all PlatformIO environments for filtering later
all_envs = []
for pio_env in pio_envs:
  env_build_flags = cfg.get(f"env:{pio_env}", "build_flags")
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
    "ci": {"board": pio_env, "platform": env_platform},
    "board_level": cfg.get(f"env:{pio_env}", "board_level", default=None),
    "board_check": bool(cfg.get(f"env:{pio_env}", "board_check", default=False)),
  }
  all_envs.append(env)

# Filter outputs based on options
# Check is mutually exclusive with other options (except 'pr')
if "check" in args.platform:
  for env in all_envs:
    if env["board_check"]:
      if "pr" in args.level:
        if env["board_level"] == "pr":
          outlist.append(env["ci"])
      else:
        outlist.append(env["ci"])
# Filter (non-check) builds by platform
else:
  for env in all_envs:
    if args.platform == env["ci"]["platform"] or args.platform == "all":
      # Always include board_level = 'pr'
      if env["board_level"] == "pr":
        outlist.append(env["ci"])
      # Include board_level = 'extra' when requested
      elif "extra" in args.level and env["board_level"] == "extra":
        outlist.append(env["ci"])
      # If no board level is specified, include in release builds (not PR)
      elif "pr" not in args.level and not env["board_level"]:
        outlist.append(env["ci"])

# Return as a JSON list
print(json.dumps(outlist))
