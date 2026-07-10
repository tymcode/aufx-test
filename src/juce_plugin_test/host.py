"""Plugin host protocol for VST/AU integration."""

from __future__ import annotations

from typing import Any, Protocol

from .audio import Waveform


class PluginHost(Protocol):
    """Interface for driving a plugin in automated tests.

    Implement this protocol with your headless VST/AU host. A typical JUCE
    integration might expose a CLI like::

        juce_renderer --plugin MyPlugin.vst3 --input in.wav --output out.wav \\
                      --param mix=0.5 --param gain=12

    Then wrap it in a ``SubprocessPluginHost`` (below).
    """

    def load_plugin(self, path: str) -> None: ...

    def set_parameters(self, params: dict[str, float | int | bool]) -> None: ...

    def process(self, input_waveform: Waveform) -> Waveform: ...

    def reset(self) -> None: ...


class PassthroughHost:
    """No-op host for testing the framework without a real plugin."""

    def __init__(self, gain: float = 1.0) -> None:
        self.gain = gain
        self._params: dict[str, Any] = {}

    def load_plugin(self, path: str) -> None:
        self._plugin_path = path

    def set_parameters(self, params: dict[str, float | int | bool]) -> None:
        self._params.update(params)
        if "gain" in params:
            self.gain = float(params["gain"])

    def process(self, input_waveform: Waveform) -> Waveform:
        return input_waveform.with_data(input_waveform.data * self.gain)

    def reset(self) -> None:
        self._params.clear()
        self.gain = 1.0
