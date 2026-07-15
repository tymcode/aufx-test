"""Tests for SubprocessPluginHost."""

from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

from juce_plugin_test import SubprocessPluginHost, Waveform
from juce_plugin_test.subprocess_host import RendererError


@pytest.fixture
def fake_renderer(tmp_path):
    renderer = tmp_path / "plugin_renderer"
    renderer.write_text("#!/bin/sh\nexit 0\n")
    renderer.chmod(0o755)
    return renderer


def test_process_requires_preset(fake_renderer, sample_aupreset, sine_mono):
    host = SubprocessPluginHost(fake_renderer, "/path/to/Plugin.component")
    with pytest.raises(RendererError, match="No plugin state loaded"):
        host.process(sine_mono)


def _mock_run_create_output(cmd, **_kwargs):
    output = Path(cmd[cmd.index("--output") + 1])
    output.parent.mkdir(parents=True, exist_ok=True)
    Waveform.sine(440, 0.1, sample_rate=48000).to_file(output)
    return MagicMock(returncode=0, stdout="", stderr="")


def test_process_builds_expected_command(fake_renderer, sample_aupreset, sine_mono, tmp_path):
    host = SubprocessPluginHost(
        fake_renderer,
        "/path/to/Plugin.component",
        work_dir=tmp_path / "work",
        output_dir=tmp_path / "tmp",
        sample_rate=48000,
    )
    host.load_preset(sample_aupreset)

    with patch("juce_plugin_test.subprocess_host.subprocess.run", side_effect=_mock_run_create_output) as run:
        host.process(sine_mono)

    cmd = run.call_args.args[0]
    output_arg = cmd[cmd.index("--output") + 1]
    assert str(tmp_path / "tmp") in output_arg
    assert host.last_output_path is not None
    assert host.last_output_path.name.startswith("render_")
    assert "--sample-rate" in cmd
    assert "48000" in cmd


def test_process_raises_on_renderer_failure(fake_renderer, sample_aupreset, sine_mono, tmp_path):
    host = SubprocessPluginHost(
        fake_renderer,
        "/path/to/Plugin.component",
        work_dir=tmp_path / "work",
        output_dir=tmp_path / "tmp",
    )
    host.load_preset(sample_aupreset)

    with patch("juce_plugin_test.subprocess_host.subprocess.run") as run:
        run.return_value = MagicMock(returncode=1, stdout="", stderr="plugin failed")
        with pytest.raises(RendererError, match="plugin failed"):
            host.process(sine_mono)


def test_param_overrides_appended_after_preset(fake_renderer, sample_aupreset, sine_mono, tmp_path):
    host = SubprocessPluginHost(
        fake_renderer,
        "/path/to/Plugin.component",
        work_dir=tmp_path / "work",
        output_dir=tmp_path / "tmp",
    )
    host.load_preset(sample_aupreset)
    host.set_parameters({"mix": 0.5})

    with patch("juce_plugin_test.subprocess_host.subprocess.run", side_effect=_mock_run_create_output) as run:
        host.process(sine_mono)

    cmd = run.call_args.args[0]
    assert "--param" in cmd
    assert "mix=0.5" in cmd


def test_missing_renderer_binary(tmp_path):
    with pytest.raises(FileNotFoundError):
        SubprocessPluginHost(tmp_path / "missing_renderer", "/plugin.component")
