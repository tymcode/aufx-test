"""Optional pytest fixture for real plugin rendering.

Copy to conftest.py (or import from here) after building native/plugin_renderer.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]

DEFAULT_RENDERER = (
    ROOT
    / "native/build/plugin_renderer/plugin_renderer_artefacts/Release/plugin_renderer"
)


@pytest.fixture(scope="session")
def plugin_host():
    from juce_plugin_test import SubprocessPluginHost

    renderer = Path(os.environ.get("JUCE_PLUGIN_RENDERER", DEFAULT_RENDERER))
    plugin = os.environ.get("JUCE_TEST_PLUGIN")
    if plugin is None:
        pytest.skip("Set JUCE_TEST_PLUGIN to the .component/.vst3 path")

    if not renderer.exists():
        pytest.skip(f"plugin_renderer not built: {renderer}")

    with SubprocessPluginHost(renderer_bin=renderer, plugin_path=plugin) as host:
        yield host
