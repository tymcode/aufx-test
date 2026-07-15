"""Tests for .aupreset utilities."""

import plistlib

import pytest

from juce_plugin_test.aupreset import AUpresetError, import_aupreset, validate_aupreset


def test_validate_aupreset(sample_aupreset):
    info = validate_aupreset(sample_aupreset)
    assert info.name == "Test Preset"
    assert info.state_bytes > 0
    assert info.format == "au-classic"


def test_validate_rejects_missing_data(tmp_path):
    path = tmp_path / "empty.aupreset"
    plistlib.dump({"name": "No state"}, path.open("wb"))
    with pytest.raises(AUpresetError, match="state blob"):
        validate_aupreset(path)


def test_validate_rejects_wrong_extension(tmp_path):
    path = tmp_path / "preset.json"
    path.write_text("{}")
    with pytest.raises(AUpresetError, match=".aupreset"):
        validate_aupreset(path)


def test_import_aupreset(sample_aupreset, tmp_path):
    dest = tmp_path / "copied.aupreset"
    import_aupreset(sample_aupreset, dest)
    assert dest.exists()
    assert validate_aupreset(dest).name == "Test Preset"
