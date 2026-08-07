#!/usr/bin/env python3

"""Generate the CI matrix."""

import argparse
import json
import re

from platformio.project.config import ProjectConfig

# Every variant env must declare one of these as its 'board_level':
#   pr      - smallest subset, built on every PR (and in every larger matrix)
#   release - the full release matrix, built on push / schedule / workflow_dispatch
#   extra   - opt-in only, built when explicitly requested via '--level extra'
BOARD_LEVELS = ("pr", "release", "extra")

parser = argparse.ArgumentParser(description="Generate the CI matrix")
parser.add_argument("platform", help="Platform to build for")
parser.add_argument(
  "--level",
  choices=["extra", "pr"],
  nargs="*",
  default=[],
  help="Board level to build for (omit for the 'pr' + 'release' matrix)",
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
  board_level = cfg.get(f"env:{pio_env}", "board_level", default=None)
  # Intentionally fail on a missing or unrecognized level: every env must opt in
  # explicitly, so a new variant can't silently drop out of (or into) the matrix.
  if board_level not in BOARD_LEVELS:
    found = f"'{board_level}'" if board_level else "no value"
    print(
      f"Error: environment '{pio_env}' has {found} for 'board_level'; "
      f"expected one of: {', '.join(BOARD_LEVELS)}"
    )
    exit(1)
  # Store env details as a dictionary, and add to 'all_envs' list
  env = {
    "ci": {"board": pio_env, "platform": env_platform},
    "board_level": board_level,
  }
  all_envs.append(env)

# Filter builds by platform
for env in all_envs:
  if args.platform == env["ci"]["platform"] or args.platform == "all":
    # Always include board_level = 'pr'
    if env["board_level"] == "pr":
      outlist.append(env["ci"])
    # Include board_level = 'extra' when requested
    elif "extra" in args.level and env["board_level"] == "extra":
      outlist.append(env["ci"])
    # Include board_level = 'release' unless narrowed to the PR subset
    elif "pr" not in args.level and env["board_level"] == "release":
      outlist.append(env["ci"])

# Return as a JSON list
print(json.dumps(outlist))
