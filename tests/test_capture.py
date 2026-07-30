"""Tests for capture helpers and plugin host."""

from aufx_test import capture_before_after, capture_with_host, compare_captured_to_reference


def test_capture_before_after(passthrough_host, sine_mono):
    host = passthrough_host

    def render():
        return host.process(sine_mono)

    pair = capture_before_after(
        render,
        adjust_controls={"gain": 0.5},
        on_adjust=lambda: host.set_parameters({"gain": 0.5}),
    )
    assert pair.before.data.max() > pair.after.data.max()
    assert pair.parameter_changes == {"gain": 0.5}


def test_capture_with_host(passthrough_host, sine_mono):
    host = passthrough_host
    pair = capture_with_host(host, sine_mono, {"gain": 2.0})
    assert pair.after.data.max() == pair.before.data.max() * 2.0


def test_compare_captured_to_reference(passthrough_host, sine_mono):
    host = passthrough_host
    host.set_parameters({"gain": 1.0})
    reference = host.process(sine_mono)
    pair = capture_with_host(host, sine_mono, {"gain": 1.0})
    result = compare_captured_to_reference(pair, reference)
    assert result.passed
