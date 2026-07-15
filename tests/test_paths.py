"""Tests for unique output paths."""

from aufx_test.paths import unique_output_path


def test_unique_output_path_creates_tmp(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    path = unique_output_path()
    assert path.parent.name == "tmp"
    assert path.suffix == ".wav"
    assert path.stem.startswith("render_")
    assert len(path.stem.split("_", 1)[1]) == 8
    assert path.parent.exists()


def test_unique_output_paths_differ(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    a = unique_output_path()
    b = unique_output_path()
    assert a != b
