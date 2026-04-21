"""Recovery tier — exercises `uhubctl` power control end-to-end.

Requires `uhubctl` installed AND at least one connected device on a
PPPS-capable hub. The whole tier skips cleanly via
`tests/recovery/conftest.py::_recovery_tier_guard` when either is missing.
"""
