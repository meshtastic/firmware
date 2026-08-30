# Git Policy

This repository has a known-good Meshtastic firmware baseline for `heltec-v4-r8-tft`.

Known-good firmware baseline:

- Firmware commit: `7239fe886a30fa13cd35946fa5ae1a46a2807eeb`
- Firmware tag at that commit: `v2.8.0.7239fe8`
- Target: `heltec-v4-r8-tft`
- Flash mode: `--flash-mode dio`
- Device UI dependency: `https://github.com/meshtastic/device-ui/archive/9d9b9df81fcde646811a10942d00d5f45f72af7b.zip`

## Rules

1. Never develop directly on the known-good baseline branch.

2. Never permanently edit `.pio/libdeps/`, because it is generated PlatformIO dependency state.

3. All Device UI source changes must live in a proper `meshtastic/device-ui` working repository, fork, branch, submodule, local dependency, or pinned source checkout.

4. External dependencies used for reproducible builds must be pinned to exact commits.

5. Do not use moving branch names such as `develop`, `master`, or `main` as the final production dependency reference.

6. Before every source modification, check:

```powershell
git status
git branch --show-current
git rev-parse HEAD
```

7. Before every build intended for flashing, record:

- firmware HEAD
- device-ui HEAD
- target
- build identifier
- flash mode

8. Never allow Codex to commit or push automatically.

9. Commits are only made after:

- build succeeds
- diff is reviewed
- physical behavior is tested when relevant
- user explicitly approves commit

10. Keep logical changes separated. Examples: dependency setup, Quick Message UI, message send helper, incoming-message UI, and BLE/MUI experiment should not all be mixed into one commit.

11. Never rewrite or force-push shared history.

12. Known-good firmware and flash artifacts must remain recoverable.

13. Working clean-flash artifacts must not be overwritten silently.

14. The known working esptool setting `--flash-mode dio` must be preserved unless physical testing proves otherwise.

15. Generated UI files must not be treated as authoritative source without first identifying the generator/source files.

16. Before updating upstream Meshtastic or device-ui, create a separate integration/update branch.

17. Any experimental branch must be trivially removable without damaging the known-good baseline.

