"""Tests for mismatch reporting helpers."""

from pathlib import Path

import numpy as np
import pytest

from aufx_test import Waveform
from aufx_test.comparison import ComparisonThresholds, compare_waveforms
from aufx_test.reporting import assert_setup_comparison, write_html_report, write_mismatch_report


def test_write_mismatch_report_writes_artifacts(sine_mono, sample_rate, tmp_path):
    different = Waveform.sine(880.0, 0.5, sample_rate=sample_rate)
    result = compare_waveforms(different, sine_mono)
    assert not result.passed

    report_dir = write_mismatch_report(
        tmp_path,
        name="demo_case",
        actual=different,
        expected=sine_mono,
        result=result,
        input_audio=sine_mono,
        thresholds=ComparisonThresholds(),
        setup={"name": "demo_case", "expect_match": True},
    )

    assert report_dir == tmp_path / "demo_case"
    assert (report_dir / "actual.wav").is_file()
    assert (report_dir / "expected.wav").is_file()
    assert (report_dir / "input.wav").is_file()
    assert (report_dir / "mismatch.json").is_file()
    assert (report_dir / "waveform.png").is_file()
    assert (report_dir / "metrics.png").is_file()

    payload = (report_dir / "mismatch.json").read_text()
    assert "demo_case" in payload
    assert "failures" in payload


def test_assert_setup_comparison_dumps_on_failure(sine_mono, sample_rate, tmp_path):
    different = Waveform.sine(880.0, 0.5, sample_rate=sample_rate)
    setup = {"name": "fail_case", "expect_match": True}
    with pytest.raises(AssertionError, match="Mismatch report"):
        assert_setup_comparison(
            different,
            sine_mono,
            setup=setup,
            input_audio=sine_mono,
            results_root=tmp_path,
        )
    assert (tmp_path / "fail_case" / "mismatch.json").is_file()


def test_assert_setup_comparison_allows_extra_tail(sine_mono, sample_rate, tmp_path):
    tail = np.zeros((sample_rate // 2, sine_mono.num_channels))
    longer = sine_mono.with_data(np.vstack([sine_mono.data, tail]))
    setup = {"name": "tail_ok", "expect_match": True}
    result = assert_setup_comparison(
        longer,
        sine_mono,
        setup=setup,
        results_root=tmp_path,
        allow_extra_actual_tail=True,
    )
    assert result.passed
    assert not (tmp_path / "tail_ok").exists()


def test_write_html_report_links_mismatch_artifacts(sine_mono, sample_rate, tmp_path):
    different = Waveform.sine(880.0, 0.5, sample_rate=sample_rate)
    result = compare_waveforms(different, sine_mono)
    mismatch = write_mismatch_report(
        tmp_path,
        name="share_case",
        actual=different,
        expected=sine_mono,
        result=result,
        input_audio=sine_mono,
    )

    output = write_html_report(
        tmp_path / "report.html",
        test_records=[
            {"nodeid": "test_audio.py::test_share_case", "outcome": "failed", "duration": 1.25},
            {"nodeid": "test_audio.py::test_ok", "outcome": "passed", "duration": 0.25},
        ],
        mismatch_reports=[mismatch],
    )

    html = output.read_text()
    assert "1 passed" in html
    assert "1 failed" in html
    assert "share_case" in html
    assert "share_case/actual.wav" in html
    assert "share_case/waveform.png" in html
