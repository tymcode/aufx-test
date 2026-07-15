"""Launch the native plugin host GUI for manual exploration."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


def project_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_host_app_bin(root: Path | None = None) -> Path:
    root = root or project_root()
    candidates = [
        root
        / "native/build/plugin_host_app/plugin_host_app_artefacts/Release/Plugin Host.app/Contents/MacOS/Plugin Host",
        root / "native/build/plugin_host_app/plugin_host_app_artefacts/Release/plugin_host_app",
        root / "native/build/plugin_host_app/Plugin Host.app/Contents/MacOS/Plugin Host",
    ]
    for path in candidates:
        if path.exists():
            return path
    return candidates[0]


def default_config_path(root: Path | None = None) -> Path:
    return (root or project_root()) / "host.config.json"


def default_python_cli(root: Path | None = None) -> Path:
    root = root or project_root()
    venv_cli = root / ".venv/bin/juce-plugin-test"
    if venv_cli.exists():
        return venv_cli
    which = shutil.which("juce-plugin-test")
    if which:
        return Path(which)
    return Path(sys.executable)


def _expand_path(value: str | Path, root: Path) -> Path:
    text = str(value).strip()
    if text.startswith("~"):
        return Path(text).expanduser()
    path = Path(text)
    if path.is_absolute():
        return path
    return (root / path).resolve()


def load_host_config(config_path: Path, project_root_dir: Path) -> dict[str, Any]:
    data = json.loads(config_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"Config root must be a JSON object: {config_path}")
    plugins = data.get("plugins")
    if not isinstance(plugins, list) or not plugins:
        raise ValueError(f'Config must contain a non-empty "plugins" array: {config_path}')
    return data


def ensure_sessions_from_config(config: dict[str, Any], root: Path) -> Path:
    """Create missing exploration sessions for each configured plugin."""
    from .session import ExperimentSession, _slug

    sessions_root = _expand_path(config.get("sessions_root") or "sessions", root)
    sessions_root.mkdir(parents=True, exist_ok=True)

    for item in config["plugins"]:
        if not isinstance(item, dict):
            continue
        plugin_path = _expand_path(item["path"], root)
        session_name = item.get("session") or f"{item.get('name') or plugin_path.stem} exploration"
        session_file = sessions_root / _slug(session_name) / "session.json"
        if session_file.exists():
            continue
        ExperimentSession.create(
            session_name,
            plugin_path=str(plugin_path.resolve()),
            root_dir=sessions_root,
        ).save()

    return sessions_root


def launch_host_app(
    *,
    config: str | Path | None = None,
    host_app_bin: str | Path | None = None,
    project_root_dir: Path | None = None,
    extra_args: list[str] | None = None,
) -> subprocess.Popen[str]:
    """Launch the native plugin host GUI using host.config.json."""
    root = project_root_dir or project_root()
    config_path = Path(config) if config is not None else default_config_path(root)
    if not config_path.is_absolute():
        config_path = (root / config_path).resolve()
    else:
        config_path = config_path.resolve()

    if not config_path.is_file():
        raise FileNotFoundError(
            f"Host config not found: {config_path}\n"
            "Create host.config.json at the project root (see docs/manual-exploration.md)."
        )

    host_bin = Path(host_app_bin) if host_app_bin is not None else default_host_app_bin(root)
    if not host_bin.exists():
        raise FileNotFoundError(
            f"Plugin host app not found at {host_bin}. "
            "Build it with:\n"
            "  cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release\n"
            "  cmake --build native/build --target plugin_host_app"
        )

    config_data = load_host_config(config_path, root)
    ensure_sessions_from_config(config_data, root)

    cmd = [
        str(host_bin),
        "--config",
        str(config_path),
        "--project-root",
        str(root.resolve()),
    ]
    if extra_args:
        cmd.extend(extra_args)

    env = os.environ.copy()
    return subprocess.Popen(cmd, env=env, cwd=str(root.resolve()))
