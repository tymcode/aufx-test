"""Headless plugin host backed by an external renderer subprocess."""

from __future__ import annotations

import json
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .audio import Waveform
from .aupreset import AUpresetError, validate_aupreset
from .paths import unique_output_path


class RendererError(RuntimeError):
    """Raised when the external plugin renderer fails."""


def _normalise_plugin_ref(value: str | Path) -> str:
    """Preserve plugin IDs (AudioUnit:...) while resolving filesystem paths.

    Session files may record either a plugin bundle path or a JUCE plugin
    identifier such as ``AudioUnit:Effects/aufx,QDV1,TDSP``. Blindly running
    identifiers through ``Path.resolve()`` used to turn them into bogus
    absolute paths (``/repo/AudioUnit:Effects/...``) that the renderer then
    rejected with "Plugin not found". Heuristic: a colon in a non-absolute
    string means "identifier, leave it alone" — safe on macOS because POSIX
    paths here never contain colons.
    """
    if isinstance(value, Path):
        return str(value.expanduser().resolve())

    text = str(value).strip()
    if not text:
        return text

    # Renderer accepts plugin identifiers (e.g. AudioUnit:Effects/...).
    if ":" in text and not text.startswith ("/"):
        return text

    return str(Path(text).expanduser().resolve())


@dataclass
class SubprocessPluginHost:
    """Drive a plugin through a headless CLI renderer.

    The renderer is expected to accept at minimum::

        renderer --plugin PATH --input PATH --output PATH [--preset PATH]

    State is applied via ``load_preset()`` (.aupreset). Individual parameters
    passed to ``set_parameters()`` are sent as optional ``--param name=value``
    overrides after the preset is loaded.

    See ``docs/plugin-renderer.md`` for the full CLI contract your JUCE host
    should implement.
    """

    renderer_bin: str | Path
    plugin_path: str | Path
    work_dir: Path | None = None
    output_dir: Path = field(default_factory=lambda: Path("tmp"))
    block_size: int = 512
    sample_rate: int | None = None
    tail_silence_seconds: float = 1.0
    silence_threshold_db: float = -60.0
    max_tail_seconds: float = 120.0
    extra_args: list[str] = field(default_factory=list)
    validate_presets: bool = True

    _preset_path: Path | None = field(default=None, init=False, repr=False)
    _params: dict[str, float | int | bool | str] = field(default_factory=dict, init=False, repr=False)
    _temp_dir: tempfile.TemporaryDirectory[str] | None = field(default=None, init=False, repr=False)
    last_output_path: Path | None = field(default=None, init=False, repr=False)

    def __post_init__(self) -> None:
        self.renderer_bin = Path(self.renderer_bin)
        self.plugin_path = _normalise_plugin_ref(self.plugin_path)
        if not self.renderer_bin.exists():
            raise FileNotFoundError(f"Renderer binary not found: {self.renderer_bin}")

    def load_plugin(self, path: str | Path) -> None:
        self.plugin_path = _normalise_plugin_ref(path)

    def load_preset(self, path: str | Path) -> None:
        """Load plugin state from an .aupreset file."""
        preset_path = Path(path).expanduser().resolve()
        if self.validate_presets:
            validate_aupreset(preset_path)
        self._preset_path = preset_path

    def set_parameters(self, params: dict[str, float | int | bool | str]) -> None:
        """Set parameter overrides applied after the preset state is loaded."""
        self._params.update(params)

    def get_parameters(self) -> dict[str, float | int | bool]:
        """Return current parameter overrides (not live plugin state)."""
        return dict(self._params)

    def reset(self) -> None:
        self._params.clear()
        self._preset_path = None

    def process(self, input_waveform: Waveform, *, output_path: str | Path | None = None) -> Waveform:
        """Render ``input_waveform`` through the plugin and return the output."""
        work_dir = self._ensure_work_dir()
        input_path = work_dir / "input.wav"
        resolved_output = (
            Path(output_path) if output_path is not None else unique_output_path(directory=self.output_dir)
        )
        resolved_output.parent.mkdir(parents=True, exist_ok=True)
        self.last_output_path = resolved_output.resolve()
        input_waveform.to_file(input_path)

        cmd = self._build_command(input_path, resolved_output)
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise RendererError(
                "Plugin renderer failed\n"
                f"  command: {' '.join(cmd)}\n"
                f"  stdout: {result.stdout.strip()}\n"
                f"  stderr: {result.stderr.strip()}"
            )
        if not resolved_output.exists():
            raise RendererError(f"Renderer did not produce output file: {resolved_output}")
        return Waveform.from_file(resolved_output)

    def dump_plugin_parameters(self) -> dict[str, Any]:
        """Ask the renderer to dump parameter metadata (optional renderer support)."""
        cmd = [
            str(self.renderer_bin),
            "--plugin",
            str(self.plugin_path),
            "--dump-parameters",
            "--format",
            "json",
        ]
        if self._preset_path is not None:
            cmd.extend(["--preset", str(self._preset_path)])
        cmd.extend(self.extra_args)

        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise RendererError(result.stderr.strip() or "Failed to dump plugin parameters")
        return json.loads(result.stdout)

    def _build_command(self, input_path: Path, output_path: Path) -> list[str]:
        if self._preset_path is None and not self._params:
            raise RendererError(
                "No plugin state loaded. Call load_preset() with an .aupreset before process()."
            )

        cmd = [
            str(self.renderer_bin),
            "--plugin",
            str(self.plugin_path),
            "--input",
            str(input_path),
            "--output",
            str(output_path),
            "--block-size",
            str(self.block_size),
            "--tail-silence",
            str(self.tail_silence_seconds),
            "--silence-threshold-db",
            str(self.silence_threshold_db),
            "--max-tail",
            str(self.max_tail_seconds),
        ]
        if self.sample_rate is not None:
            cmd.extend(["--sample-rate", str(self.sample_rate)])
        if self._preset_path is not None:
            cmd.extend(["--preset", str(self._preset_path)])
        for key, value in self._params.items():
            cmd.extend(["--param", f"{key}={value}"])
        cmd.extend(self.extra_args)
        return cmd

    def _ensure_work_dir(self) -> Path:
        if self.work_dir is not None:
            self.work_dir.mkdir(parents=True, exist_ok=True)
            return self.work_dir
        if self._temp_dir is None:
            self._temp_dir = tempfile.TemporaryDirectory(prefix="aufx-test-")
        return Path(self._temp_dir.name)

    def close(self) -> None:
        if self._temp_dir is not None:
            self._temp_dir.cleanup()
            self._temp_dir = None

    def __enter__(self) -> SubprocessPluginHost:
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()
