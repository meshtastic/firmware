"""Coordinate scrubber for outbound telemetry/logs.

Three modes:
  off    — pass through unchanged
  coarse — round decimal lat/lon to 1 dp, truncate 1e-7 integer coords to the
           nearest million, drop NMEA sentence bodies
  redact — replace any coordinate value with ``<scrubbed>``

Only coordinate-named keys are touched, so a decimal like ``latency=12.5`` is
left alone.
"""

from __future__ import annotations

import re

# Decimal coordinate: lat/lon/latitude/longitude = <signed decimal>. Longest
# names first so "latitude" isn't half-matched as "lat".
_DEC = re.compile(r"\b(latitude|longitude|lat|lon)=(-?\d+\.\d+)")
# Protobuf 1e-7 integer coordinate: same names with an _i suffix = <signed int>.
_INT = re.compile(r"\b(latitude_i|longitude_i|lat_i|lon_i)=(-?\d+)")
# NMEA sentence ("$GPGGA,...") — drop everything after the sentence type.
_NMEA = re.compile(r"(\$G[A-Z]{4}),\S*")


class Scrubber:
    def __init__(self, mode: str = "coarse") -> None:
        self.mode = (mode or "off").lower()

    def scrub(self, text: str) -> str:
        if self.mode == "off" or not text:
            return text
        if self.mode == "redact":
            text = _DEC.sub(lambda m: f"{m.group(1)}=<scrubbed>", text)
            text = _INT.sub(lambda m: f"{m.group(1)}=<scrubbed>", text)
            text = _NMEA.sub(r"\1,<scrubbed>", text)
            return text
        # coarse
        text = _DEC.sub(lambda m: f"{m.group(1)}={round(float(m.group(2)), 1)}", text)
        text = _INT.sub(
            lambda m: f"{m.group(1)}={int(float(m.group(2)) / 1e6) * 1_000_000}", text
        )
        text = _NMEA.sub(r"\1,<scrubbed>", text)
        return text
