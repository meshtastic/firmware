#!/usr/bin/env python3
"""
Vigil — Synthetic multi-channel audio generator for desktop testing.

Architecture decision #19: Python synthetic audio generator.

Generates 16-channel WAV files with simulated drone tones and correct
TDOA (Time Difference of Arrival) delays based on 3D mic geometry.

Usage:
    python generate.py --azimuth 45 --elevation 20 --snr 15 -o test_45az_20el.wav

Output:
    - 16-channel WAV file with TDOA-delayed drone tone
    - JSON sidecar with expected DoA for automated test validation

Mic geometry: dual-tier frustum array (architecture decision #3)

    Lower tier (8 mics): tilted 10° up from horizontal, evenly spaced at 45° intervals
    Upper tier (8 mics): rotated 22.5° offset, tilted 45° up

    Top view (lower tier):          Side view:
         7   0                        upper ╱╲ 45° tilt
        ╱     ╲                       tier ╱  ╲
      6│   ●   │1                          │    │
      5│       │2                    lower ╱────╲ 10° tilt
        ╲     ╱                      tier
         4   3
"""

import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

# Speed of sound in air (m/s)
SPEED_OF_SOUND = 343.0

# Default parameters
DEFAULT_SAMPLE_RATE = 48000
DEFAULT_DURATION = 1.0       # seconds
DEFAULT_FRAME_SIZE = 1024
DEFAULT_DRONE_FREQ = 200.0   # Hz (typical drone motor fundamental)
DEFAULT_NUM_CHANNELS = 16

# Frustum geometry (meters)
LOWER_RADIUS = 0.08   # 80mm radius for lower tier
UPPER_RADIUS = 0.05   # 50mm radius for upper tier
TIER_SPACING = 0.06   # 60mm vertical separation between tiers
LOWER_TILT_DEG = 10.0
UPPER_TILT_DEG = 45.0


def build_mic_positions():
    """
    Build 3D mic positions for the dual-tier frustum array.

    Returns:
        np.ndarray of shape (16, 3) — x, y, z positions in meters

    Lower tier (mics 0-7): 8 mics at 45° intervals, tilted 10° up
    Upper tier (mics 8-15): 8 mics at 45° intervals (22.5° offset), tilted 45° up
    """
    positions = np.zeros((16, 3))

    for i in range(8):
        # Lower tier
        angle_rad = np.radians(i * 45.0)
        tilt_rad = np.radians(LOWER_TILT_DEG)
        r = LOWER_RADIUS
        positions[i] = [
            r * np.cos(angle_rad) * np.cos(tilt_rad),
            r * np.sin(angle_rad) * np.cos(tilt_rad),
            r * np.sin(tilt_rad),
        ]

        # Upper tier (22.5° rotational offset)
        angle_rad = np.radians(i * 45.0 + 22.5)
        tilt_rad = np.radians(UPPER_TILT_DEG)
        r = UPPER_RADIUS
        positions[i + 8] = [
            r * np.cos(angle_rad) * np.cos(tilt_rad),
            r * np.sin(angle_rad) * np.cos(tilt_rad),
            TIER_SPACING + r * np.sin(tilt_rad),
        ]

    return positions


def direction_vector(azimuth_deg, elevation_deg):
    """Convert azimuth/elevation to unit direction vector."""
    az = np.radians(azimuth_deg)
    el = np.radians(elevation_deg)
    return np.array([
        np.cos(el) * np.cos(az),
        np.cos(el) * np.sin(az),
        np.sin(el),
    ])


def compute_tdoa_delays(mic_positions, azimuth_deg, elevation_deg):
    """
    Compute TDOA delays for a far-field source at given direction.

    For a far-field source, the delay at each mic is:
        tau_i = -(d · m_i) / c

    where d is the unit direction vector toward the source,
    m_i is the mic position, and c is the speed of sound.

    Returns:
        np.ndarray of shape (num_mics,) — delays in seconds (relative to array center)
    """
    d = direction_vector(azimuth_deg, elevation_deg)
    # Negative because sound arrives LATER at mics further from the source
    delays = -mic_positions @ d / SPEED_OF_SOUND
    # Make relative to earliest arrival (all delays >= 0)
    delays -= delays.min()
    return delays


def generate_drone_tone(sample_rate, duration, freq, num_harmonics=4):
    """
    Generate a drone motor tone with harmonics.

    Typical drone produces fundamental + harmonics from motor/prop interaction.
    """
    t = np.arange(int(sample_rate * duration)) / sample_rate
    signal = np.zeros_like(t)
    for h in range(1, num_harmonics + 1):
        amplitude = 1.0 / h  # Harmonics decay
        signal += amplitude * np.sin(2 * np.pi * freq * h * t)
    # Normalize to [-1, 1]
    signal /= np.max(np.abs(signal))
    return signal


def apply_tdoa_delay(signal, delay_seconds, sample_rate):
    """Apply fractional-sample delay using sinc interpolation."""
    delay_samples = delay_seconds * sample_rate
    int_delay = int(np.floor(delay_samples))
    frac_delay = delay_samples - int_delay

    # Integer delay via shift
    delayed = np.zeros_like(signal)
    if int_delay < len(signal):
        delayed[int_delay:] = signal[:len(signal) - int_delay]

    # Fractional delay via linear interpolation (good enough for testing)
    if frac_delay > 0 and len(delayed) > 1:
        shifted = np.zeros_like(delayed)
        shifted[1:] = delayed[:-1]
        delayed = (1.0 - frac_delay) * delayed + frac_delay * shifted

    return delayed


def add_noise(signal, snr_db):
    """Add white Gaussian noise at specified SNR."""
    signal_power = np.mean(signal ** 2)
    if signal_power == 0:
        return signal
    noise_power = signal_power / (10 ** (snr_db / 10))
    noise = np.random.normal(0, np.sqrt(noise_power), len(signal))
    return signal + noise


def write_wav(filename, data, sample_rate, num_channels):
    """
    Write a multi-channel WAV file (16-bit PCM).

    data: np.ndarray of shape (num_channels, num_samples)
    """
    num_samples = data.shape[1]
    bits_per_sample = 16
    byte_rate = sample_rate * num_channels * bits_per_sample // 8
    block_align = num_channels * bits_per_sample // 8
    data_size = num_samples * block_align

    # Interleave channels: [ch0_s0, ch1_s0, ..., ch15_s0, ch0_s1, ...]
    interleaved = np.zeros(num_samples * num_channels, dtype=np.int16)
    for ch in range(num_channels):
        # Clip and convert to 16-bit
        clipped = np.clip(data[ch], -1.0, 1.0)
        interleaved[ch::num_channels] = (clipped * 32767).astype(np.int16)

    with open(filename, 'wb') as f:
        # RIFF header
        f.write(b'RIFF')
        f.write(struct.pack('<I', 36 + data_size))
        f.write(b'WAVE')

        # fmt chunk
        f.write(b'fmt ')
        f.write(struct.pack('<I', 16))  # chunk size
        f.write(struct.pack('<H', 1))   # PCM format
        f.write(struct.pack('<H', num_channels))
        f.write(struct.pack('<I', sample_rate))
        f.write(struct.pack('<I', byte_rate))
        f.write(struct.pack('<H', block_align))
        f.write(struct.pack('<H', bits_per_sample))

        # data chunk
        f.write(b'data')
        f.write(struct.pack('<I', data_size))
        f.write(interleaved.tobytes())


def generate(azimuth_deg, elevation_deg, snr_db=20.0, duration=DEFAULT_DURATION,
             sample_rate=DEFAULT_SAMPLE_RATE, drone_freq=DEFAULT_DRONE_FREQ,
             num_sources=1, output_path=None):
    """
    Generate a synthetic multi-channel audio file.

    Returns:
        tuple: (wav_path, json_path) — paths to generated files
    """
    mic_positions = build_mic_positions()
    num_channels = len(mic_positions)
    num_samples = int(sample_rate * duration)

    # Generate base drone tone
    drone_signal = generate_drone_tone(sample_rate, duration, drone_freq)

    # Compute TDOA delays
    delays = compute_tdoa_delays(mic_positions, azimuth_deg, elevation_deg)

    # Build multi-channel output
    data = np.zeros((num_channels, num_samples))
    for ch in range(num_channels):
        delayed = apply_tdoa_delay(drone_signal, delays[ch], sample_rate)
        data[ch] = add_noise(delayed, snr_db)

    # Generate output paths
    if output_path is None:
        output_path = f"drone_az{int(azimuth_deg)}_el{int(elevation_deg)}_snr{int(snr_db)}.wav"

    wav_path = Path(output_path)
    json_path = wav_path.with_suffix('.json')

    # Write WAV
    write_wav(str(wav_path), data, sample_rate, num_channels)

    # Write expected DoA sidecar
    expected = {
        "sources": [{
            "azimuth_deg": azimuth_deg,
            "elevation_deg": elevation_deg,
            "drone_freq_hz": drone_freq,
            "snr_db": snr_db,
        }],
        "mic_geometry": {
            "num_channels": num_channels,
            "positions_m": mic_positions.tolist(),
            "lower_radius_m": LOWER_RADIUS,
            "upper_radius_m": UPPER_RADIUS,
            "tier_spacing_m": TIER_SPACING,
        },
        "audio": {
            "sample_rate": sample_rate,
            "duration_s": duration,
            "num_samples": num_samples,
            "frame_size": DEFAULT_FRAME_SIZE,
            "num_frames": num_samples // DEFAULT_FRAME_SIZE,
        },
        "tdoa_delays_us": (delays * 1e6).tolist(),
    }

    with open(json_path, 'w') as f:
        json.dump(expected, f, indent=2)

    return str(wav_path), str(json_path)


def main():
    parser = argparse.ArgumentParser(
        description='Vigil synthetic audio generator — creates 16-channel WAV '
                    'files with TDOA-delayed drone tones for desktop testing.')
    parser.add_argument('--azimuth', type=float, required=True,
                        help='Source azimuth in degrees (0-360)')
    parser.add_argument('--elevation', type=float, default=20.0,
                        help='Source elevation in degrees (0-90, default: 20)')
    parser.add_argument('--snr', type=float, default=20.0,
                        help='Signal-to-noise ratio in dB (default: 20)')
    parser.add_argument('--duration', type=float, default=DEFAULT_DURATION,
                        help=f'Duration in seconds (default: {DEFAULT_DURATION})')
    parser.add_argument('--freq', type=float, default=DEFAULT_DRONE_FREQ,
                        help=f'Drone frequency in Hz (default: {DEFAULT_DRONE_FREQ})')
    parser.add_argument('--sample-rate', type=int, default=DEFAULT_SAMPLE_RATE,
                        help=f'Sample rate (default: {DEFAULT_SAMPLE_RATE})')
    parser.add_argument('-o', '--output', type=str, default=None,
                        help='Output WAV file path')

    args = parser.parse_args()

    wav_path, json_path = generate(
        azimuth_deg=args.azimuth,
        elevation_deg=args.elevation,
        snr_db=args.snr,
        duration=args.duration,
        sample_rate=args.sample_rate,
        drone_freq=args.freq,
        output_path=args.output,
    )

    print(f"Generated: {wav_path}")
    print(f"Expected DoA: {json_path}")

    # Print TDOA info
    with open(json_path) as f:
        meta = json.load(f)
    delays = meta['tdoa_delays_us']
    print(f"Max TDOA: {max(delays):.1f} µs ({max(delays)/1e6*SPEED_OF_SOUND*1000:.2f} mm path difference)")


if __name__ == '__main__':
    main()
