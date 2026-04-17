# post:extra_scripts/nrf54l15_linker.py
#
# Fix for Zephyr two-pass link on nRF54L15:
# platformio-build.py registers env.Depends("$PROG_PATH", final_ld_script) but
# the SCons dependency chain is broken (final_ld_script Command never runs).
# This script adds a PreAction on the final firmware binary that runs the gcc
# preprocessing command directly (extracted from build.ninja) to generate
# zephyr/linker.cmd before the link step.
#
# PlatformIO bundles an old Ninja that can't handle multi-output depslog rules,
# so we parse the COMMAND line from build.ninja and run just the gcc -E part,
# skipping the cmake_transform_depfile step (only needed for Ninja deps tracking).

Import("env")
import os
import re
import subprocess

if env.get("PIOENV") != "nrf54l15dk":
    pass  # Only for the nrf54l15dk environment
else:

    def _extract_gcc_command(ninja_build):
        """Parse build.ninja to find the gcc -E command that generates linker.cmd.

        The rule looks like:
          build zephyr/linker.cmd | ...: CUSTOM_COMMAND ...
            COMMAND = cmd.exe /C "cd /D ZEPHYR_DIR && arm-none-eabi-gcc.exe ... -o linker.cmd && cmake.exe -E cmake_transform_depfile ..."
            DESC = Generating linker.cmd

        Returns (gcc_cmd_string, cwd_path) or raises RuntimeError.
        """
        in_rule = False
        with open(ninja_build, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                # Detect start of the linker.cmd custom command rule
                if not in_rule:
                    if "build zephyr/linker.cmd" in line and "CUSTOM_COMMAND" in line:
                        in_rule = True
                    continue

                stripped = line.strip()
                if not stripped.startswith("COMMAND = "):
                    continue

                command_val = stripped[len("COMMAND = ") :]

                # The value is: C:\Windows\system32\cmd.exe /C "cd /D DIR && GCC_CMD && cmake ..."
                # Extract the content between the outermost double-quotes.
                m = re.search(r'/C\s+"(.*)"', command_val)
                if not m:
                    raise RuntimeError(
                        "nRF54L15 linker fix: unexpected COMMAND format in build.ninja:\n%s"
                        % command_val[:200]
                    )

                inner = m.group(1)  # "cd /D DIR && GCC_CMD && cmake ..."
                parts = inner.split(" && ")

                cwd = None
                gcc_cmd = None
                for part in parts:
                    part = part.strip()
                    if part.startswith("cd /D "):
                        cwd = part[len("cd /D ") :]
                    elif "arm-none-eabi-gcc" in part:
                        gcc_cmd = part

                if not gcc_cmd:
                    raise RuntimeError(
                        "nRF54L15 linker fix: arm-none-eabi-gcc command not found in:\n%s"
                        % inner[:400]
                    )

                return gcc_cmd, cwd

        raise RuntimeError(
            "nRF54L15 linker fix: 'build zephyr/linker.cmd' rule not found in build.ninja"
        )

    def _generate_linker_cmd(target, source, env):
        """Generate zephyr/linker.cmd via direct gcc invocation before the final link."""
        build_dir = env.subst("$BUILD_DIR")
        zephyr_dir = os.path.join(build_dir, "zephyr")
        linker_cmd = os.path.join(zephyr_dir, "linker.cmd")

        if os.path.exists(linker_cmd):
            return  # Already present — nothing to do

        ninja_build = os.path.join(build_dir, "build.ninja")
        if not os.path.exists(ninja_build):
            raise RuntimeError(
                "nRF54L15 linker fix: build.ninja not found at %s\n"
                "Run a full build first so CMake generates the Ninja files."
                % ninja_build
            )

        gcc_cmd, cwd = _extract_gcc_command(ninja_build)
        run_cwd = cwd if cwd else zephyr_dir

        print(
            "==> nRF54L15: Generating zephyr/linker.cmd (LINKER_ZEPHYR_FINAL) via GCC"
        )
        # gcc_cmd comes verbatim from our own build.ninja (never user input) and
        # contains Windows-style paths with spaces that cannot be safely argv-split
        # with shlex, so we run it via the platform shell. nosec/nosemgrep below
        # acknowledge this deliberate, scoped use of shell=True.
        result = subprocess.run(  # nosec B602
            gcc_cmd,
            shell=True,  # nosemgrep: python.lang.security.audit.subprocess-shell-true.subprocess-shell-true
            cwd=run_cwd,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print("GCC stdout:", result.stdout[:2000])
            print("GCC stderr:", result.stderr[:2000])
            raise RuntimeError(
                "nRF54L15 linker fix: GCC failed to generate linker.cmd (rc=%d)"
                % result.returncode
            )
        if not os.path.exists(linker_cmd):
            raise RuntimeError(
                "nRF54L15 linker fix: GCC returned 0 but linker.cmd was not created at %s"
                % linker_cmd
            )
        print("==> linker.cmd generated successfully")

    # Use PIOMAINPROG (set by ZephyrBuildProgram) to get the exact SCons node
    prog = env.get("PIOMAINPROG")
    if prog:
        env.AddPreAction(prog, _generate_linker_cmd)
    else:
        print(
            "[nrf54l15_linker] WARNING: PIOMAINPROG not set, falling back to $PROG_PATH"
        )
        env.AddPreAction(env.subst("$PROG_PATH"), _generate_linker_cmd)
