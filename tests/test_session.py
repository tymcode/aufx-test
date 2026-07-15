"""Tests for manual exploration sessions."""

import json
from pathlib import Path

import pytest

from aufx_test import ExperimentSession, StateSnapshot, Waveform
from aufx_test.explore import capture_snapshot_from_cli
from aufx_test.session import (
    _artifact_stem,
    _keyword_from_description,
    expect_match_for_output_role,
    output_artifact_filename,
    parse_output_role,
)
from aufx_test.testgen import export_setups_json, export_test_module


@pytest.fixture
def tmp_session_root(tmp_path):
    return tmp_path / "sessions"


@pytest.fixture
def sine_files(tmp_path, sample_rate):
    inp = tmp_path / "input.wav"
    out = tmp_path / "output.wav"
    ref = tmp_path / "reference.wav"
    Waveform.sine(440.0, 0.25, sample_rate=sample_rate).to_file(inp)
    Waveform.sine(440.0, 0.25, sample_rate=sample_rate, amplitude=0.4).to_file(out)
    Waveform.sine(440.0, 0.25, sample_rate=sample_rate, amplitude=0.4).to_file(ref)
    return inp, out, ref


def test_create_and_save_session(tmp_session_root):
    session = ExperimentSession.create("My Plugin Tests", plugin_path="/path/to/Plugin.vst3", root_dir=tmp_session_root)
    assert session.session_file.exists()
    loaded = ExperimentSession.load(session.session_file)
    assert loaded.name == "My Plugin Tests"
    assert loaded.plugin_path == "/path/to/Plugin.vst3"


def test_keyword_from_description():
    assert _keyword_from_description("Init Serial guitar") == "init"
    assert _keyword_from_description("  half-mix tone  ") == "half"
    assert _keyword_from_description("") == ""
    assert _artifact_stem("Init Serial guitar", "abcd1234") == "init_abcd1234"
    assert _artifact_stem("", "abcd1234") == "abcd1234"


def test_output_role_filename_helpers():
    assert parse_output_role("artifacts/long_67dc49d2_output_bkn.wav") == "bkn"
    assert parse_output_role("artifacts/long_67dc49d2_output_gld.wav") == "gld"
    assert parse_output_role("artifacts/long_67dc49d2_output_sus.wav") == "sus"
    assert parse_output_role("artifacts/long_67dc49d2_output.wav") is None
    assert output_artifact_filename("long_67dc49d2", "bkn") == "long_67dc49d2_output_bkn.wav"
    assert output_artifact_filename("long_67dc49d2") == "long_67dc49d2_output.wav"
    assert expect_match_for_output_role("gld") is True
    assert expect_match_for_output_role("bkn") is False
    assert expect_match_for_output_role("sus") is False
    assert expect_match_for_output_role(None) is None


def test_add_snapshot_copies_artifacts(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="half mix", parameters={"mix": 0.5})
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    session.save()

    stem = f"half_{snap.id}"
    assert (session.artifacts_dir / f"{stem}.aupreset").exists()
    assert (session.artifacts_dir / f"{stem}_input.wav").exists()
    assert (session.artifacts_dir / f"{stem}_output.wav").exists()
    assert snap.input_audio == f"artifacts/{stem}_input.wav"
    assert snap.output_audio == f"artifacts/{stem}_output.wav"
    assert snap.preset_file == f"artifacts/{stem}.aupreset"


def test_add_snapshot_reuses_host_staged_artifacts(tmp_session_root, sine_files, sample_aupreset):
    """Host writes into artifacts/; session snap should rename, not duplicate."""
    import shutil

    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    session.artifacts_dir.mkdir(parents=True, exist_ok=True)

    host_out = session.artifacts_dir / "init_aabbccdd_output_bkn.wav"
    host_preset = session.artifacts_dir / "init_aabbccdd.aupreset"
    shutil.copy2(out, host_out)
    shutil.copy2(sample_aupreset, host_preset)

    snap = StateSnapshot(name="Init Serial")
    session.add_snapshot(snap, copy_input=inp, copy_output=host_out, copy_preset=host_preset)

    stem = f"init_{snap.id}"
    assert (session.artifacts_dir / f"{stem}_output_bkn.wav").exists()
    assert (session.artifacts_dir / f"{stem}.aupreset").exists()
    assert snap.reference_kind == "bkn"
    assert snap.output_audio == f"artifacts/{stem}_output_bkn.wav"
    assert not host_out.exists()
    assert not host_preset.exists()
    assert len(list(session.artifacts_dir.glob("*_output_bkn.wav"))) == 1
    assert len(list(session.artifacts_dir.glob("*.aupreset"))) == 1


def test_get_snapshot_accepts_id_name_and_artifact_stem(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="Long Tail")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    stem = _artifact_stem(snap.name, snap.id)

    assert session.get_snapshot(snap.id).id == snap.id
    assert session.get_snapshot("Long Tail").id == snap.id
    assert session.get_snapshot("long_tail").id == snap.id
    assert session.get_snapshot(stem).id == snap.id
    assert session.get_snapshot(f"{stem}.aupreset".removesuffix(".aupreset")).id == snap.id


def test_promote_snapshot(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="baseline")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)

    setup = session.promote_snapshot(snap.id, test_name="test_baseline")
    assert setup.name == "test_baseline"
    assert setup.preset_file is not None
    assert setup.expect_match is True
    assert snap.promoted
    assert Path(setup.reference_output).exists()


def test_promote_negative_case(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="broken bug")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)

    setup = session.promote_snapshot(snap.id, expect_match=False)
    assert setup.expect_match is False
    assert snap.expect_match is False

    # Re-promote can clear the negative flag without renaming.
    setup = session.promote_snapshot(snap.id, expect_match=True)
    assert setup.expect_match is True
    assert setup.name == "broken_bug"


def test_promote_infers_expect_match_from_output_role(tmp_session_root, sine_files, sample_aupreset):
    import shutil

    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    session.artifacts_dir.mkdir(parents=True, exist_ok=True)

    bkn_out = session.artifacts_dir / "x_output_bkn.wav"
    gld_out = tmp_session_root / "y_output_gld.wav"
    shutil.copy2(out, bkn_out)
    shutil.copy2(out, gld_out)

    broken = StateSnapshot(name="broken capture")
    session.add_snapshot(broken, copy_input=inp, copy_output=bkn_out, copy_preset=sample_aupreset)
    setup = session.promote_snapshot(broken.id)
    assert broken.reference_kind == "bkn"
    assert setup.expect_match is False

    golden = StateSnapshot(name="golden capture")
    session.add_snapshot(golden, copy_input=inp, copy_output=gld_out, copy_preset=sample_aupreset)
    setup = session.promote_snapshot(golden.id)
    assert golden.reference_kind == "gld"
    assert setup.expect_match is True

    # Explicit CLI override still wins over filename role.
    setup = session.promote_snapshot(broken.id, expect_match=True)
    assert setup.expect_match is True


def test_promote_requires_preset(tmp_session_root, sine_files):
    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="no preset")
    session.add_snapshot(snap, copy_input=inp, copy_output=out)
    with pytest.raises(ValueError, match=".aupreset"):
        session.promote_snapshot(snap.id)


def test_promote_requires_output(tmp_session_root, sine_files):
    inp, _, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="incomplete")
    session.add_snapshot(snap, copy_input=inp)
    with pytest.raises(ValueError, match="output_audio"):
        session.promote_snapshot(snap.id)


def test_capture_snapshot_from_cli(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("cli-demo", root_dir=tmp_session_root)
    snap = capture_snapshot_from_cli(
        session,
        name="snap1",
        parameters={"gain": 1.0},
        input_audio=inp,
        output_audio=out,
        preset_file=sample_aupreset,
        notes="manual bounce",
    )
    assert snap.notes == "manual bounce"
    assert session.session_file.exists()
    assert len(session.snapshots) == 1
    assert snap.preset_file is not None


def test_export_test_module(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("export-demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="case_a")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    session.promote_snapshot(snap.id, test_name="test_case_a")
    neg = StateSnapshot(name="case_neg")
    session.add_snapshot(neg, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    session.promote_snapshot(neg.id, expect_match=False)

    out_path = tmp_session_root / "generated_test.py"
    export_test_module(session, out_path)
    assert out_path.exists()
    content = out_path.read_text()
    assert "test_case_a" in content
    assert "load_preset" in content
    assert '"expect_match": false' in content
    assert 'setup.get("expect_match", True)' in content


def test_export_setups_json(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("json-demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="case_b")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    session.promote_snapshot(snap.id)

    out_path = tmp_session_root / "setups.json"
    export_setups_json(session, out_path)
    data = json.loads(out_path.read_text())
    assert data["session"] == "json-demo"
    assert len(data["setups"]) == 1


def test_session_summary(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("summary-demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="test snap", parameters={"mix": 0.5})
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    text = session.summary()
    assert "summary-demo" in text
    assert "mix=0.5" in text
    assert snap.id in text
    assert ".aupreset" in text


def test_export_developer_presets(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("share-demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="share me", notes="try this edge case")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)

    manifest = session.export_developer_presets(tmp_session_root / "share")
    assert manifest.exists()
    data = json.loads(manifest.read_text())
    assert data["session"] == "share-demo"
    assert len(data["snapshots"]) == 1
    assert (tmp_session_root / "share" / "presets").exists()
