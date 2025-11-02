#!/usr/bin/env python3

"""
Analyze audio_probe log output for latency drift.

Usage:
  python scripts/analyze_audio_probe.py <logfile> [--base-offset-ms N]

If --base-offset-ms is omitted, the script estimates it from the first
data sample (elapsed_real_ms - audio_timeline_ms).
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from statistics import mean, median, pstdev
from typing import Iterable, List, Optional

LINE_RE = re.compile(
    r"Elapsed real:\s*(?P<elapsed>[0-9.]+)\s*ms\s+Audio timeline:\s*(?P<audio>[0-9.]+)\s*ms"
)


@dataclass
class Sample:
    elapsed_ms: float
    audio_ms: float
    drift_ms: float


def parse_samples(lines: Iterable[str], base_offset_ms: Optional[float]) -> List[Sample]:
    samples: List[Sample] = []
    inferred_offset = None

    for line in lines:
        match = LINE_RE.search(line)
        if not match:
            continue
        elapsed = float(match.group("elapsed"))
        audio = float(match.group("audio"))

        if base_offset_ms is None and inferred_offset is None:
            inferred_offset = elapsed - audio

        offset = base_offset_ms if base_offset_ms is not None else inferred_offset or 0.0
        drift = (elapsed - offset) - audio
        samples.append(Sample(elapsed, audio, drift))

    return samples


def summarize(samples: List[Sample]) -> None:
    if not samples:
        print("No samples parsed.")
        return

    drifts = [s.drift_ms for s in samples]
    print(f"Samples: {len(samples)}")
    print(f"Drift ms -> mean: {mean(drifts):.4f}, median: {median(drifts):.4f}, min: {min(drifts):.4f}, max: {max(drifts):.4f}, stddev: {pstdev(drifts):.4f}")
    print(f"First sample drift: {drifts[0]:.4f} ms, last sample drift: {drifts[-1]:.4f} ms")


def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze audio_probe output.")
    parser.add_argument("logfile", help="Path to audio_probe log")
    parser.add_argument(
        "--base-offset-ms",
        type=float,
        default=None,
        help="Known capture offset (ms) to subtract from elapsed real timeline.",
    )
    args = parser.parse_args()

    with open(args.logfile, "r", encoding="utf-8") as fh:
        samples = parse_samples(fh, args.base_offset_ms)

    summarize(samples)


if __name__ == "__main__":
    main()

