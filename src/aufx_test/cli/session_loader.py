"""Session discovery and loading helpers for CLI commands."""

from __future__ import annotations

from pathlib import Path

from ..session import ExperimentSession, slugify


def _list_session_folders(root: Path) -> list[str]:
    """Folder names under root that contain a session.json (sorted)."""
    if not root.is_dir():
        return []
    names = [
        path.parent.name
        for path in sorted(root.glob("*/session.json"))
        if path.parent.is_dir()
    ]
    return names


def _load_session(name: str, root: Path) -> ExperimentSession:
    """Resolve a session by folder name, slugified name, or explicit path.

    Deliberately forgiving because sessions are created from two places (this
    CLI and the native host app, which slugifies display names) and users type
    whichever name they remember. Falls back to scanning every session.json
    under root and matching on the stored display name. Note the default root
    is ./sessions relative to the CWD — pass --root when working against the
    host app's sessions dir (~/Library/AU Effects Explorer/sessions).
    """
    candidates = [
        root / name,
        root / slugify(name),
        Path(name),
    ]
    for candidate in candidates:
        session_file = candidate / "session.json" if candidate.is_dir() else candidate
        if session_file.exists():
            return ExperimentSession.load(session_file, root_dir=root)

    for path in root.glob("*/session.json"):
        session = ExperimentSession.load(path, root_dir=root)
        if session.name == name or path.parent.name == name or path.parent.name == slugify(name):
            return session
    raise FileNotFoundError(f"Session not found: {name!r} in {root}")
