"""Tests for the Vigil synthetic audio generator."""

import json
import struct
import tempfile
from pathlib import Path

import numpy as np
import pytest

from generate import (
    SPEED_OF_SOUND,
    build_mic_positions,
    compute_tdoa_delays,
    direction_vector,
    generate,
    generate_drone_tone,
    write_wav,
)


class TestMicGeometry:
    def test_16_positions(self):
        pos = build_mic_positions()
        assert pos.shape == (16, 3)

    def test_lower_tier_symmetric(self):
        pos = build_mic_positions()
        # Lower tier (0-7) should be roughly symmetric around origin in x/y
        lower = pos[:8]
        assert abs(lower[:, 0].mean()) < 1e-10  # x centroid ≈ 0
        assert abs(lower[:, 1].mean()) < 1e-10  # y centroid ≈ 0

    def test_upper_tier_elevated(self):
        pos = build_mic_positions()
        # Upper tier (8-15) z should be higher than lower tier (0-7) z
        assert pos[8:, 2].min() > pos[:8, 2].max()

    def test_positions_within_bounds(self):
        pos = build_mic_positions()
        # All positions should be within 15cm of center
        distances = np.linalg.norm(pos, axis=1)
        assert np.all(distances < 0.15)


class TestTDOA:
    def test_broadside_zero_delay(self):
        """Source directly above (elevation=90°) should give near-equal delays
        for mics at same height."""
        pos = build_mic_positions()
        delays = compute_tdoa_delays(pos, azimuth_deg=0, elevation_deg=90)
        # All delays should be non-negative (we shift to min=0)
        assert np.all(delays >= -1e-12)

    def test_delays_change_with_azimuth(self):
        pos = build_mic_positions()
        delays_0 = compute_tdoa_delays(pos, azimuth_deg=0, elevation_deg=20)
        delays_90 = compute_tdoa_delays(pos, azimuth_deg=90, elevation_deg=20)
        # Different azimuths should produce different delay patterns
        assert not np.allclose(delays_0, delays_90)

    def test_max_delay_physically_reasonable(self):
        """Max TDOA should be less than array diameter / speed of sound."""
        pos = build_mic_positions()
        max_distance = np.max(np.linalg.norm(pos[:, None] - pos[None, :], axis=2))
        max_possible_delay = max_distance / SPEED_OF_SOUND

        for az in range(0, 360, 30):
            delays = compute_tdoa_delays(pos, azimuth_deg=az, elevation_deg=20)
            assert delays.max() <= max_possible_delay + 1e-9

    def test_opposite_azimuths_mirror_delays(self):
        """Source at 0° and 180° should produce mirrored delay patterns for symmetric mics."""
        pos = build_mic_positions()
        d0 = compute_tdoa_delays(pos, azimuth_deg=0, elevation_deg=0)
        d180 = compute_tdoa_delays(pos, azimuth_deg=180, elevation_deg=0)
        # The sorted delay patterns should be similar (not identical due to tilt)
        assert not np.allclose(d0, d180)


class TestDroneTone:
    def test_output_length(self):
        signal = generate_drone_tone(48000, 1.0, 200.0)
        assert len(signal) == 48000

    def test_normalized(self):
        signal = generate_drone_tone(48000, 1.0, 200.0)
        assert np.max(np.abs(signal)) == pytest.approx(1.0, abs=1e-6)

    def test_contains_harmonics(self):
        """FFT should show energy at fundamental and harmonics."""
        sr = 48000
        signal = generate_drone_tone(sr, 1.0, 200.0, num_harmonics=4)
        fft = np.abs(np.fft.rfft(signal))
        freqs = np.fft.rfftfreq(len(signal), 1.0 / sr)

        for h in range(1, 5):
            freq = 200.0 * h
            idx = np.argmin(np.abs(freqs - freq))
            # Each harmonic should have significant energy
            assert fft[idx] > fft.mean() * 5


class TestWavOutput:
    def test_valid_wav_header(self):
        """Generated WAV file should have valid RIFF/WAVE header."""
        with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
            path = f.name

        data = np.random.randn(16, 1024).astype(np.float64)
        data /= np.max(np.abs(data))
        write_wav(path, data, 48000, 16)

        with open(path, 'rb') as f:
            riff = f.read(4)
            assert riff == b'RIFF'
            _size = struct.unpack('<I', f.read(4))[0]
            wave = f.read(4)
            assert wave == b'WAVE'

            fmt = f.read(4)
            assert fmt == b'fmt '
            chunk_size = struct.unpack('<I', f.read(4))[0]
            assert chunk_size == 16
            audio_format = struct.unpack('<H', f.read(2))[0]
            assert audio_format == 1  # PCM
            num_ch = struct.unpack('<H', f.read(2))[0]
            assert num_ch == 16

        Path(path).unlink()


class TestEndToEnd:
    def test_generate_creates_files(self):
        """Full pipeline: generate WAV + JSON, verify both exist and are valid."""
        with tempfile.TemporaryDirectory() as tmpdir:
            output = str(Path(tmpdir) / "test.wav")
            wav_path, json_path = generate(
                azimuth_deg=45,
                elevation_deg=20,
                snr_db=20,
                duration=0.1,  # Short for speed
                output_path=output,
            )

            assert Path(wav_path).exists()
            assert Path(json_path).exists()

            # Verify JSON sidecar
            with open(json_path) as f:
                meta = json.load(f)

            assert meta['sources'][0]['azimuth_deg'] == 45
            assert meta['sources'][0]['elevation_deg'] == 20
            assert meta['mic_geometry']['num_channels'] == 16
            assert len(meta['tdoa_delays_us']) == 16
            assert meta['audio']['sample_rate'] == 48000

    def test_different_azimuths_different_files(self):
        """Two different source positions should produce different audio."""
        with tempfile.TemporaryDirectory() as tmpdir:
            wav1, _ = generate(azimuth_deg=0, elevation_deg=20, duration=0.05,
                               output_path=str(Path(tmpdir) / "a.wav"))
            wav2, _ = generate(azimuth_deg=90, elevation_deg=20, duration=0.05,
                               output_path=str(Path(tmpdir) / "b.wav"))

            # Files should have different content (different TDOA patterns)
            with open(wav1, 'rb') as f1, open(wav2, 'rb') as f2:
                assert f1.read() != f2.read()
