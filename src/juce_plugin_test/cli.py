"""Command-line entry point."""

from __future__ import annotations

import argparse
import json
import sys

from . import __version__
from .audio import Waveform
from .comparison import compare_waveforms
from .graphing import plot_comparison, plot_difference_metrics


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare two audio files for plugin testing")
    parser.add_argument("actual", help="Actual output WAV")
    parser.add_argument("expected", help="Expected reference WAV")
    parser.add_argument("--snr-min", type=float, default=30.0)
    parser.add_argument("--corr-min", type=float, default=0.95)
    parser.add_argument("--plot", metavar="PATH", help="Save comparison plot to PATH")
    parser.add_argument("--json", action="store_true", help="Print metrics as JSON")
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    args = parser.parse_args(argv)

    actual = Waveform.from_file(args.actual)
    expected = Waveform.from_file(args.expected)
    result = compare_waveforms(
        actual,
        expected,
        snr_db_min=args.snr_min,
        correlation_min=args.corr_min,
    )

    if args.json:
        print(json.dumps({"passed": result.passed, **result.metrics.as_dict()}, indent=2))
    else:
        print(result.summary())

    if args.plot:
        plot_comparison(actual, actual, expected, save_path=args.plot)

    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
