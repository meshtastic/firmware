# Branch Strategy - Local Fork Setup

**Repository:** Local Meshtastic firmware fork with USB Capture Module
**Last Updated:** 2025-12-06

---

## Branch Structure

```
upstream/master (Meshtastic official)
       ↓
    master (tracks upstream/master)
       ↓
 dev/usb-capture (your development branch)
```

---

## Branches

### `master` - Upstream Tracking Branch
**Purpose:** Stays in sync with official Meshtastic firmware

**Configuration:**
- Tracks: `upstream/master`
- Remote: https://github.com/meshtastic/firmware.git
- Policy: **Never commit directly to master**
- Update: `git checkout master && git pull`

**Current HEAD:**
```
eeaafda62 Update protobufs (#8871)
Latest: v2.7.16
```

---

### `dev/usb-capture` - USB Capture Development Branch
**Purpose:** Your USB Capture Module implementation and enhancements

**Base:** upstream/master (rebased on 2025-12-06)

**Your Commits (10):**
```
6a6db35b3 fix: Remove Core1 direct logging to prevent crashes + comprehensive docs
563357c6e feat: Implement Core1 complete processing with PSRAM storage
c523ffd6b Architecture: Design PSRAM buffer for Core1 complete processing
17e9b7da0 Handoff: Next session - Core1 formatting implementation
9b86a013e Plan: Core1 formatting and FRAM/PSRAM architecture
c8fedd211 Analysis: Core distribution already optimal
c0f44d127 Add CPU ID to logging for core usage visibility
48680e96f Final: Baseline restored and verified
83ef0e196 Document: Complete lessons learned from PIO unstuffing attempts
33fe560eb Initial commit: USB Capture Module baseline
```

**Files Modified/Added:**
- New: `psram_buffer.h/cpp`, `formatted_event_queue.h/cpp`
- Modified: `USBCaptureModule.cpp/h`, `keyboard_decoder_core1.cpp/h`
- Docs: `USBCaptureModule_Documentation.md`, `PSRAM_BUFFER_ARCHITECTURE.md`
- Planning: `NEXT_SESSION_HANDOFF.md`, `CORE1_OPTIMIZATION_PLAN.md`

---

### `feature/core1-formatting` - Feature Branch (Old)
**Status:** Merged into dev/usb-capture
**Can be deleted:** Yes (work is on dev/usb-capture now)

---

### `feature/pico-pio-usb-rx` - Experimental Branch
**Purpose:** Pico-PIO-USB integration experiments
**Status:** Research/POC branch
**Keep:** Yes (for reference)

---

### `feature/pio-dual-unstuffing` - Experimental Branch
**Purpose:** PIO dual-core unstuffing experiments
**Status:** Research branch
**Keep:** Yes (for reference)

---

## Workflow

### Daily Development
```bash
# Work on your USB Capture module
git checkout dev/usb-capture

# Make changes, test, commit
git add <files>
git commit -m "feat: ..."

# Build and test
cd firmware && pio run -e xiao-rp2350-sx1262
```

### Sync with Upstream (Weekly/Monthly)
```bash
# Update master from upstream
git checkout master
git pull  # pulls from upstream/master

# Rebase your dev branch on latest upstream
git checkout dev/usb-capture
git rebase master

# Resolve any conflicts if needed
# Then continue development
```

### Create Feature Branches
```bash
# Branch from dev/usb-capture for specific features
git checkout dev/usb-capture
git checkout -b feature/new-feature-name

# Work on feature
# When done, merge back to dev/usb-capture
git checkout dev/usb-capture
git merge feature/new-feature-name
```

---

## Remotes

### `upstream` - Official Meshtastic
- URL: https://github.com/meshtastic/firmware.git
- Purpose: Pull latest changes
- Access: Read-only (public repo)

### `origin` - Your Fork (Future)
- URL: TBD (when you create GitHub fork)
- Purpose: Push your changes, create PRs
- Access: Read/Write

**To add origin when fork is created:**
```bash
git remote set-url origin https://github.com/YOUR_USERNAME/firmware.git
git push origin dev/usb-capture
```

---

## Contributing Back to Meshtastic

When your USB Capture module is ready for contribution:

1. **Create GitHub fork** (if not already done)
2. **Push your branch:**
   ```bash
   git push origin dev/usb-capture
   ```

3. **Create Pull Request on GitHub:**
   - Base: `meshtastic/firmware:master`
   - Compare: `YOUR_USERNAME/firmware:dev/usb-capture`
   - Title: "feat: USB Capture Module for RP2350"
   - Description: Reference your documentation

4. **Address review feedback:**
   ```bash
   git checkout dev/usb-capture
   # Make changes
   git commit -m "review: Address feedback"
   git push origin dev/usb-capture  # Updates PR automatically
   ```

---

## Current State Summary

**Repository:** `/Users/rstown/Desktop/ste`

**Branches:**
- ✅ `master` → tracks `upstream/master` (latest Meshtastic)
- ✅ `dev/usb-capture` → your development branch (10 commits ahead)
- 🗑️ `feature/core1-formatting` → can be deleted (merged to dev)
- 📚 `feature/pico-pio-usb-rx` → research branch (keep)
- 📚 `feature/pio-dual-unstuffing` → research branch (keep)

**Remotes:**
- ✅ `upstream` → https://github.com/meshtastic/firmware.git
- ⏳ `origin` → Not configured yet (waiting for GitHub fork)

**Clean State:** ✅ All work preserved on `dev/usb-capture`

---

## Quick Reference

```bash
# Switch to development
git checkout dev/usb-capture

# Update from upstream
git checkout master && git pull
git checkout dev/usb-capture && git rebase master

# Build firmware
cd firmware && pio run -e xiao-rp2350-sx1262

# Show branch status
git branch -vv

# Show your commits
git log upstream/master..dev/usb-capture --oneline
```

---

**Status:** Local fork structure complete! You can now work on `dev/usb-capture` and sync with upstream anytime.
