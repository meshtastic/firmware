"""Fleet: different session seeds produce non-overlapping PSKs.

No hardware needed — this is a pure property check on the test profile
generator, elevated into the `fleet/` tier because it's the critical
invariant for running concurrent CI labs without cross-contamination.
"""

from __future__ import annotations

from meshtastic_mcp import userprefs


def test_psk_seed_isolates_runs() -> None:
    """Two labs running simultaneously with different seeds must end up with
    different PSKs — which means firmware baked in lab A cannot decode lab B's
    traffic, and vice versa.

    This is the formal statement of the isolation claim that
    `testing_profile` promises operators.
    """
    lab_a_morning = userprefs.build_testing_profile(psk_seed="lab-a-2026-04-16-morning")
    lab_a_evening = userprefs.build_testing_profile(psk_seed="lab-a-2026-04-16-evening")
    lab_b_morning = userprefs.build_testing_profile(psk_seed="lab-b-2026-04-16-morning")

    # Same lab, same date, different time-of-day → different PSKs
    assert (
        lab_a_morning["USERPREFS_CHANNEL_0_PSK"]
        != lab_a_evening["USERPREFS_CHANNEL_0_PSK"]
    )
    # Different labs, same time-of-day → different PSKs
    assert (
        lab_a_morning["USERPREFS_CHANNEL_0_PSK"]
        != lab_b_morning["USERPREFS_CHANNEL_0_PSK"]
    )

    # Re-deriving with the same seed yields the same PSK (reproducibility)
    lab_a_morning_again = userprefs.build_testing_profile(
        psk_seed="lab-a-2026-04-16-morning"
    )
    assert (
        lab_a_morning["USERPREFS_CHANNEL_0_PSK"]
        == lab_a_morning_again["USERPREFS_CHANNEL_0_PSK"]
    )
