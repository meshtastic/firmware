# Git Hygiene Review

## 1. Executive summary

Firmware source is still at the known-good upstream commit `7239fe886a30fa13cd35946fa5ae1a46a2807eeb`, with tag `v2.8.0.7239fe8` pointing at `HEAD`.

There are no modified tracked firmware files. The dirty state is untracked local output and documentation:

- generated/local PlatformIO state: `.pio-core/`
- logs: `build-heltec-v4-r8-tft.log`, `build-heltec-v4-r8-tft-baseline-verify-20260830.log`
- known-good artifacts: `dist/`
- project documentation/scripts created in Phase 0: `GIT-POLICY.md`, `docs/`, `tools/git-preflight.ps1`

No baseline branch or tag should be created yet. The official Meshtastic tag already identifies the source baseline, and the preparation documentation should not be confused with that upstream baseline.

Recommended next step after user approval: preserve/copy known-good artifacts outside the Git working tree, add only repository-preparation docs and the preflight script, optionally add narrow ignore rules for local generated output/logs, then create a preparation commit on a feature/prep branch.

## 2. Firmware Git state

Repository path:

`C:\Users\JPJYR\Documents\meshtastic`

Current branch:

`develop`

Current `HEAD`:

`7239fe886a30fa13cd35946fa5ae1a46a2807eeb`

Tag pointing at `HEAD`:

`v2.8.0.7239fe8`

Remote:

`origin https://github.com/meshtastic/firmware.git`

Tracked modifications:

None found.

Untracked top-level items:

- `.pio-core/`
- `GIT-POLICY.md`
- `build-heltec-v4-r8-tft-baseline-verify-20260830.log`
- `build-heltec-v4-r8-tft.log`
- `dist/`
- `docs/`
- `tools/git-preflight.ps1`

## 3. Device UI Git state

Repository path:

`C:\Users\JPJYR\Documents\device-ui`

Current branch:

`feature/heltec-r8-quick-messages`

Current `HEAD`:

`9d9b9df81fcde646811a10942d00d5f45f72af7b`

Remote:

`origin https://github.com/meshtastic/device-ui.git`

Status:

Clean. No source modifications were found.

## 4. Complete dirty/untracked file inventory

Untracked files outside large generated directories:

```text
GIT-POLICY.md
build-heltec-v4-r8-tft.log
build-heltec-v4-r8-tft-baseline-verify-20260830.log
docs/development-baseline.md
docs/device-ui-development-workflow.md
docs/heltec-v4-r8-tft-programming-mode-investigation.md
docs/heltec-v4-r8-tft-quick-message-ui-investigation.md
docs/git-hygiene-review.md
tools/git-preflight.ps1
```

Untracked build/release artifact files:

```text
dist/heltec-v4-r8-tft/README-FLASHING.md
dist/heltec-v4-r8-tft/boot_app0.bin
dist/heltec-v4-r8-tft/bootloader.bin
dist/heltec-v4-r8-tft/firmware-heltec-v4-r8-tft-2.8.0.7239fe8.bin
dist/heltec-v4-r8-tft/firmware-heltec-v4-r8-tft-2.8.0.7239fe8.factory.bin
dist/heltec-v4-r8-tft/firmware-heltec-v4-r8-tft-2.8.0.7239fe8.mt.json
dist/heltec-v4-r8-tft/flash-full-linux.sh
dist/heltec-v4-r8-tft/flash-full-windows.bat
dist/heltec-v4-r8-tft/flash-map.json
dist/heltec-v4-r8-tft/flash-update-linux.sh
dist/heltec-v4-r8-tft/flash-update-windows.bat
dist/heltec-v4-r8-tft/littlefs-heltec-v4-r8-tft-2.8.0.7239fe8.bin
dist/heltec-v4-r8-tft/partitions.bin
dist/meshtastic-heltec-v4-r8-tft-esptool-package.zip
```

Large untracked/generated directory:

```text
.pio-core/  files=8857  bytes=574665419
```

Tracked modified files:

None.

Ignored relevant directories:

- `.pio/` is ignored by `.gitignore`.
- `.venv/` is ignored by `.gitignore`.

## 5. Classification table

| Path | Category | Tracked? | Ignored? | Recommended action | Reason |
|---|---|---:|---:|---|---|
| `GIT-POLICY.md` | A. PROJECT SOURCE / DOCUMENTATION | No | No | Include in preparation commit | Defines safety rules before firmware work. |
| `docs/development-baseline.md` | A. PROJECT SOURCE / DOCUMENTATION | No | No | Include in preparation commit | Records known-good firmware/device-ui/build baseline. |
| `docs/device-ui-development-workflow.md` | A. PROJECT SOURCE / DOCUMENTATION | No | No | Include in preparation commit | Documents safe Device UI editing workflow. |
| `docs/heltec-v4-r8-tft-programming-mode-investigation.md` | A. PROJECT SOURCE / DOCUMENTATION | No | No | Include only if user wants investigation reports tracked | Useful project research, but not strictly required for prep policy. |
| `docs/heltec-v4-r8-tft-quick-message-ui-investigation.md` | A. PROJECT SOURCE / DOCUMENTATION | No | No | Include only if user wants investigation reports tracked | Useful design research, but not strictly required for prep policy. |
| `docs/git-hygiene-review.md` | A. PROJECT SOURCE / DOCUMENTATION | No | No | Include in preparation commit | This review explains what to track and what not to track. |
| `tools/git-preflight.ps1` | A. PROJECT SOURCE / DOCUMENTATION | No | No | Include in preparation commit | Read-only safety helper for future Codex/source work. |
| `.pio-core/` | B. GENERATED / TEMPORARY | No | No | Do not commit; add narrow ignore rule after approval | Local PlatformIO core/packages/cache, about 575 MB. |
| `.pio/` | B. GENERATED / TEMPORARY | No | Yes | Do not commit | Already ignored PlatformIO build/dependency output. |
| `.venv/` | B. GENERATED / TEMPORARY | No | Yes | Do not commit | Already ignored local Python virtualenv. |
| `build-heltec-v4-r8-tft.log` | D. LOGS / REPORTS | No | No | Do not commit by default; optionally archive locally | Large old build log, useful only as temporary diagnostic context. |
| `build-heltec-v4-r8-tft-baseline-verify-20260830.log` | D. LOGS / REPORTS | No | No | Do not commit by default; optionally archive locally | Verification result is already summarized in docs; full log is reproducible. |
| `dist/heltec-v4-r8-tft/*.bin` | C. BUILD / RELEASE ARTIFACTS | No | No | Preserve, but do not commit now | Known-good physical rollback artifacts; binaries inflate repo. |
| `dist/heltec-v4-r8-tft/*.sh` | C. BUILD / RELEASE ARTIFACTS | No | No | Preserve with artifacts, not prep commit | Flash scripts matter for rollback and preserve `--flash-mode dio`. |
| `dist/heltec-v4-r8-tft/*.bat` | C. BUILD / RELEASE ARTIFACTS | No | No | Preserve with artifacts, not prep commit | Windows flash scripts matter for rollback and preserve `--flash-mode dio`. |
| `dist/heltec-v4-r8-tft/*.json` | C. BUILD / RELEASE ARTIFACTS | No | No | Preserve with artifacts, not prep commit | Manifests/map belong with binary artifact package. |
| `dist/heltec-v4-r8-tft/README-FLASHING.md` | C. BUILD / RELEASE ARTIFACTS | No | No | Preserve with artifacts; optionally copy selected facts into docs | Good hardware rollback instructions, already summarized in `development-baseline.md`. |
| `dist/meshtastic-heltec-v4-r8-tft-esptool-package.zip` | C. BUILD / RELEASE ARTIFACTS | No | No | Preserve, but do not commit now | Useful complete rollback package; binary archive is better outside Git. |

## 6. Current .gitignore analysis

The current `.gitignore` already covers:

- `.pio`
- `pio`
- `.pio-docker`
- `.platformio`
- `.cache`
- `.venv/`
- `venv/`
- `__pycache__`
- `release/`
- `/compile_commands.json`
- many build and editor artifacts

It does not currently cover:

- `.pio-core/`
- root build logs such as `build-heltec-v4-r8-tft.log`
- `dist/`

This is mostly reasonable for an upstream firmware repository. The local `.pio-core/` path is special to this workstation because `PLATFORMIO_CORE_DIR` was set to a project-local directory during setup. It should not be committed.

## 7. Proposed .gitignore changes

No `.gitignore` changes were made in this task.

Recommended narrow additions after user approval:

```gitignore
# Local PlatformIO core/cache used by this workstation.
.pio-core/

# Local build verification logs.
build-*.log
```

Do not automatically ignore `dist/` yet. It contains known-good rollback artifacts. Decide artifact preservation first.

An alternative to editing repository `.gitignore` is to put purely local rules in `.git/info/exclude`:

```gitignore
.pio-core/
build-*.log
```

That keeps upstream-visible files cleaner but does not help future clones.

## 8. Known-good artifact preservation strategy

Evaluated options:

### Option A: Keep `dist/` untracked but preserved locally

Good short-term option. It preserves the exact files without inflating Git. Risk: a future cleanup could delete them accidentally because they are untracked.

### Option B: Store known-good artifacts outside the Git working tree

Recommended current option.

Suggested location:

```text
C:\Users\JPJYR\Documents\meshtastic-artifacts\known-good\2.8.0.7239fe8\
```

This avoids accidental inclusion in source commits, reduces cleanup risk inside the repo, and keeps the rollback package clearly tied to the tested firmware.

### Option C: Track selected release artifacts in Git

Not recommended now. The binaries and ZIP are large, not source, and would mix release payload with firmware development history.

### Option D: Use GitHub Releases later

Good later option after a fork/release process exists. Do not upload anything during hygiene preparation.

Recommendation: use Option B after user approval, while keeping hashes and flash map in `docs/development-baseline.md`.

## 9. Exact proposed preparation commit contents

Proposed commit contents:

```text
GIT-POLICY.md
docs/development-baseline.md
docs/device-ui-development-workflow.md
docs/git-hygiene-review.md
tools/git-preflight.ps1
```

Reasons:

- `GIT-POLICY.md`: central safety policy.
- `docs/development-baseline.md`: source/build/artifact baseline facts.
- `docs/device-ui-development-workflow.md`: avoids `.pio/libdeps` edits and documents dependency pinning.
- `docs/git-hygiene-review.md`: records this classification and next-step plan.
- `tools/git-preflight.ps1`: read-only local safety check before any later source modifications.

Optional commit contents if the user wants research docs tracked too:

```text
docs/heltec-v4-r8-tft-programming-mode-investigation.md
docs/heltec-v4-r8-tft-quick-message-ui-investigation.md
```

Optional `.gitignore` additions after approval:

```text
.gitignore
```

Only include `.gitignore` if the user approves ignoring `.pio-core/` and local build logs.

## 10. Files explicitly NOT to commit

Do not include:

```text
.pio/
.pio-core/
build-heltec-v4-r8-tft.log
build-heltec-v4-r8-tft-baseline-verify-20260830.log
dist/
```

Do not include any generated Device UI files from:

```text
.pio/libdeps/
```

Do not include Device UI source changes in the firmware preparation commit.

## 11. Baseline branch/tag recommendation

The official upstream tag `v2.8.0.7239fe8` already points to the exact known-good firmware source commit. A new local baseline tag is therefore optional, not required.

Recommended approach:

1. Treat `v2.8.0.7239fe8` and commit `7239fe886a30fa13cd35946fa5ae1a46a2807eeb` as the immutable source baseline.
2. Do not create a local baseline branch from a dirty working tree.
3. After the preparation commit is reviewed, create future work from the known-good commit or from a dedicated prep branch, depending on whether the documentation should travel with the feature branch.

Most precise strategy:

- Baseline source identity: official tag `v2.8.0.7239fe8`.
- Local preparation branch: `prep/heltec-r8-git-hygiene`.
- Future firmware feature branch: `feature/heltec-r8-quick-messages`.

Avoid making the documentation commit look like a firmware baseline.

## 12. Future feature branch workflow

Matching firmware and Device UI branch names are sensible:

- Firmware: `feature/heltec-r8-quick-messages`
- Device UI: `feature/heltec-r8-quick-messages`

The Device UI branch already exists and is clean at commit `9d9b9df81fcde646811a10942d00d5f45f72af7b`.

Recommended sequence after approval:

1. Preserve known-good artifacts outside the Git working tree.
2. Apply approved ignore rules.
3. Add preparation docs/script.
4. Create a repository-preparation commit.
5. Create firmware feature branch for quick messages.
6. Temporarily point firmware to local `../device-ui` only on the feature branch.
7. Verify baseline build still succeeds with the intended dependency mode.
8. Begin Quick Message implementation in Device UI.

## 13. Exact commands that would be run AFTER user approval

These are proposed commands only. They were not executed during this review.

Preserve artifacts:

```powershell
New-Item -ItemType Directory -Force C:\Users\JPJYR\Documents\meshtastic-artifacts\known-good\2.8.0.7239fe8
Copy-Item dist\heltec-v4-r8-tft\* C:\Users\JPJYR\Documents\meshtastic-artifacts\known-good\2.8.0.7239fe8\ -Force
Copy-Item dist\meshtastic-heltec-v4-r8-tft-esptool-package.zip C:\Users\JPJYR\Documents\meshtastic-artifacts\known-good\2.8.0.7239fe8\ -Force
```

Add ignore rules if approved:

```powershell
# Edit .gitignore to add:
# .pio-core/
# build-*.log
```

Create prep branch and commit if approved:

```powershell
git switch -c prep/heltec-r8-git-hygiene
git add GIT-POLICY.md docs/development-baseline.md docs/device-ui-development-workflow.md docs/git-hygiene-review.md tools/git-preflight.ps1
git add .gitignore
git commit -m "Document Heltec V4 R8 TFT development baseline"
```

Create future feature branch if approved:

```powershell
git switch -c feature/heltec-r8-quick-messages
```

Check Device UI before editing:

```powershell
git -C ..\device-ui status
git -C ..\device-ui rev-parse --abbrev-ref HEAD
git -C ..\device-ui rev-parse HEAD
```

## 14. Rollback implications

Rollback source identity is already strong because the firmware source baseline is the official tag `v2.8.0.7239fe8` and exact commit `7239fe886a30fa13cd35946fa5ae1a46a2807eeb`.

Rollback artifact identity is strong if the `dist/` artifacts remain available and the hashes in `docs/development-baseline.md` match:

- factory image
- app image
- LittleFS image
- esptool package ZIP

Moving artifacts outside the Git working tree improves safety by separating source hygiene from hardware rollback payloads.

The essential hardware rollback facts are:

```text
Target: heltec-v4-r8-tft
Firmware: 7239fe886a30fa13cd35946fa5ae1a46a2807eeb
Device UI: 9d9b9df81fcde646811a10942d00d5f45f72af7b
Flash mode: dio
LittleFS offset: 0xc90000
```

## 15. Unresolved questions requiring user decision

1. Should known-good artifacts be copied to `C:\Users\JPJYR\Documents\meshtastic-artifacts\known-good\2.8.0.7239fe8\`?
2. Should `.gitignore` be changed to ignore `.pio-core/` and `build-*.log`, or should those rules stay local in `.git/info/exclude`?
3. Should the two investigation reports be included in the preparation commit, or only the policy/baseline/workflow/hygiene docs?
4. Should the preparation commit live on a new branch named `prep/heltec-r8-git-hygiene`?
5. Is the official tag `v2.8.0.7239fe8` enough as the source baseline, or do you also want a local baseline tag later?

