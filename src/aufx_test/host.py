"""Plugin host protocol for VST/AU integration."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Protocol

from .audio import Waveform


class PluginHost(Protocol):
    """Interface for driving a plugin in automated tests.

    Implement this protocol with your headless VST/AU host. A typical JUCE
    integration might expose a CLI like::

        juce_renderer --plugin MyEffect.component --input in.wav --output out.wav \\
                      --preset state.aupreset

    Then wrap it in a ``SubprocessPluginHost``.
    """

    def load_plugin(self, path: str) -> None: ...

    def set_parameters(self, params: dict[str, float | int | bool]) -> None: ...

    def load_preset(self, path: str | Path) -> None:
        """Load plugin state from a preset file (.aupreset, .fxp, etc.)."""
        ...

    def process(self, input_waveform: Waveform) -> Waveform: ...

    def reset(self) -> None: ...

    def get_parameters(self) -> dict[str, float | int | bool]:
        """Return current parameter values. Optional — not all hosts support this."""
        ...


class PassthroughHost:
    """No-op host for testing the framework without a real plugin."""

    def __init__(self, gain: float = 1.0) -> None:
        self.gain = gain
        self._params: dict[str, Any] = {}
        self._preset_path: Path | None = None

    def load_plugin(self, path: str) -> None:
        self._plugin_path = path

    def load_preset(self, path: str | Path) -> None:
        self._preset_path = Path(path)

    def set_parameters(self, params: dict[str, float | int | bool]) -> None:
        self._params.update(params)
        if "gain" in params:
            self.gain = float(params["gain"])

    def process(self, input_waveform: Waveform) -> Waveform:
        return input_waveform.with_data(input_waveform.data * self.gain)

    def reset(self) -> None:
        self._params.clear()
        self._preset_path = None
        self.gain = 1.0

    def get_parameters(self) -> dict[str, float | int | bool]:
        return dict(self._params)
