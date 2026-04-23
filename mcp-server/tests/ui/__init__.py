"""UI tier — input-broker-driven screen navigation tests.

Only runs when a screen-bearing role (esp32s3/heltec-v3) is present on the
hub AND the firmware was baked with `enable_ui_log=True` (so the
`Screen: frame N/M name=... reason=...` log lines are emitted). The
`tests/ui/conftest.py` fixture forces that bake stamp.
"""
