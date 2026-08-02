"""Tests for manual exploration sessions."""

import json
from pathlib import Path

import pytest

from aufx_test import ExperimentSession, StateSnapshot, Waveform
from aufx_test.explore import capture_snapshot_from_cli
from aufx_test.session import (
    _base_stem_from_output,
    _keyword_from_description,
    artifact_stem,
    expect_match_for_output_role,
    hardware_output_artifact_filename,
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


def test_session_show_without_name_lists_folders(tmp_session_root, capsys):
    from aufx_test.cli import _cmd_session_show

    ExperimentSession.create("Alpha Session", root_dir=tmp_session_root)
    ExperimentSession.create("Beta Session", root_dir=tmp_session_root)
    (tmp_session_root / "not_a_session").mkdir()
    (tmp_session_root / "stray.txt").write_text("ignore\n", encoding="utf-8")

    class Args:
        name = None
        root = tmp_session_root

    assert _cmd_session_show(Args()) == 0
    out = capsys.readouterr().out.strip().splitlines()
    assert out == ["alpha_session", "beta_session"]


def test_keyword_from_description():
    assert _keyword_from_description("Init Serial guitar") == "init"
    assert _keyword_from_description("  half-mix tone  ") == "half"
    assert _keyword_from_description("") == ""
    assert artifact_stem("Init Serial guitar", "abcd1234") == "init_abcd1234"
    assert artifact_stem("", "abcd1234") == "abcd1234"


def test_output_role_filename_helpers():
    assert parse_output_role("artifacts/long_67dc49d2_output_bkn.wav") == "bkn"
    assert parse_output_role("artifacts/long_67dc49d2/long_67dc49d2_output_gld.wav") == "gld"
    assert parse_output_role("artifacts/long_67dc49d2_output_sus.wav") == "sus"
    assert parse_output_role("artifacts/long_67dc49d2_output.wav") is None
    assert parse_output_role("artifacts/long_67dc49d2/long_67dc49d2_output_hw_gld.wav") == "gld"
    assert parse_output_role("artifacts/long_67dc49d2_output_hw_bkn.wav") == "bkn"
    assert parse_output_role("artifacts/long_67dc49d2_output_hw.wav") is None
    assert output_artifact_filename("long_67dc49d2", "bkn") == "long_67dc49d2_output_bkn.wav"
    assert output_artifact_filename("long_67dc49d2") == "long_67dc49d2_output.wav"
    assert (
        hardware_output_artifact_filename("long_67dc49d2", "gld")
        == "long_67dc49d2_output_hw_gld.wav"
    )
    assert hardware_output_artifact_filename("long_67dc49d2") == "long_67dc49d2_output_hw.wav"
    assert _base_stem_from_output("artifacts/x/init_abcd_output_hw_sus.wav") == "init_abcd"
    assert _base_stem_from_output("artifacts/x/init_abcd_output_gld.wav") == "init_abcd"
    assert _base_stem_from_output("artifacts/x/init_abcd_output_hw.wav") == "init_abcd"
    assert expect_match_for_output_role("gld") is True
    assert expect_match_for_output_role("bkn") is False
    assert expect_match_for_output_role("sus") is False
    assert expect_match_for_output_role(None) is None


def test_state_snapshot_round_trips_hardware_fields(tmp_session_root, sine_files):
    """Native SessionSnap writes output_audio_hw / sysex_file; Python must keep them."""
    inp, out, _ = sine_files
    session = ExperimentSession.create("hw-demo", root_dir=tmp_session_root)
    stem = "both_deadbeef"
    stem_dir = session.artifacts_dir / stem
    stem_dir.mkdir(parents=True)
    sw = stem_dir / f"{stem}_output_gld.wav"
    hw = stem_dir / f"{stem}_output_hw_gld.wav"
    syx = stem_dir / f"{stem}.syx"
    sw.write_bytes(out.read_bytes())
    hw.write_bytes(out.read_bytes())
    syx.write_bytes(b"\xf0\x00\xf7")

    snap = StateSnapshot(
        name="Both Capture",
        id="deadbeef",
        source_clip_name="Factory Strings 01",
        input_audio=f"artifacts/{stem}/{stem}_input.wav",
        output_audio=f"artifacts/{stem}/{stem}_output_gld.wav",
        output_audio_hw=f"artifacts/{stem}/{stem}_output_hw_gld.wav",
        sysex_file=f"artifacts/{stem}/{stem}.syx",
        reference_kind="gld",
    )
    (stem_dir / f"{stem}_input.wav").write_bytes(inp.read_bytes())
    session.snapshots.append(snap)
    session.save()

    # Mimic a native-written session.json payload (extra unknown keys ignored).
    payload = json.loads(session.session_file.read_text())
    assert payload["snapshots"][0]["output_audio_hw"] == snap.output_audio_hw
    assert payload["snapshots"][0]["sysex_file"] == snap.sysex_file
    payload["snapshots"][0]["native_only_field"] = "ignored"
    session.session_file.write_text(json.dumps(payload, indent=2) + "\n")

    loaded = ExperimentSession.load(session.session_file, root_dir=tmp_session_root)
    got = loaded.get_snapshot("deadbeef")
    assert got.output_audio == snap.output_audio
    assert got.output_audio_hw == snap.output_audio_hw
    assert got.sysex_file == snap.sysex_file
    assert got.source_clip_name == snap.source_clip_name
    assert loaded.resolve_path(got.output_audio_hw).is_file()
    assert loaded.resolve_path(got.sysex_file).is_file()
    assert "output_hw:" in loaded.summary()
    assert "sysex:" in loaded.summary()


def test_compare_root_mode_uses_snapshot_hardware_fields(tmp_session_root, sine_files):
    """aufx-test compare --root resolves HW/SW via StateSnapshot, not raw JSON."""
    import argparse

    from aufx_test.cli import _cmd_compare

    _, out, _ = sine_files
    session = ExperimentSession.create("compare-demo", root_dir=tmp_session_root)
    stem = "tone_cafef00d"
    stem_dir = session.artifacts_dir / stem
    stem_dir.mkdir(parents=True)
    sw = stem_dir / f"{stem}_output_gld.wav"
    hw = stem_dir / f"{stem}_output_hw_gld.wav"
    sw.write_bytes(out.read_bytes())
    hw.write_bytes(out.read_bytes())

    snap = StateSnapshot(
        name="Tone",
        id="cafef00d",
        output_audio=f"artifacts/{stem}/{stem}_output_gld.wav",
        output_audio_hw=f"artifacts/{stem}/{stem}_output_hw_gld.wav",
        reference_kind="gld",
    )
    session.snapshots.append(snap)
    session.save()

    args = argparse.Namespace(
        compare_config=None,
        corr_min=None,
        root=tmp_session_root,
        actual="compare-demo",
        expected="cafef00d",
        json=True,
        plot=None,
        metrics_plot=None,
    )
    assert _cmd_compare(args) == 0


def test_compare_root_mode_write_report_auto_targets_snapshot_stem(tmp_session_root, sine_files):
    """--write-report auto should place compare artifacts in the snapshot stem folder."""
    import argparse

    from aufx_test.cli import _cmd_compare

    _, out, _ = sine_files
    session = ExperimentSession.create("compare-report-demo", root_dir=tmp_session_root)
    stem = "stringies_259b3b94"
    stem_dir = session.artifacts_dir / stem
    stem_dir.mkdir(parents=True)
    sw = stem_dir / f"{stem}_output_bkn.wav"
    hw = stem_dir / f"{stem}_output_hw_bkn.wav"
    sw.write_bytes(out.read_bytes())
    hw.write_bytes(out.read_bytes())

    snap = StateSnapshot(
        name="stringies",
        id="259b3b94",
        source_clip_name="Stringies Original Clip",
        input_audio=f"artifacts/{stem}/{stem}_input.wav",
        output_audio=f"artifacts/{stem}/{stem}_output_bkn.wav",
        output_audio_hw=f"artifacts/{stem}/{stem}_output_hw_bkn.wav",
        reference_kind="bkn",
    )
    inp = stem_dir / f"{stem}_input.wav"
    inp.write_bytes(out.read_bytes())
    session.snapshots.append(snap)
    session.save()

    args = argparse.Namespace(
        compare_config=None,
        corr_min=None,
        root=tmp_session_root,
        actual="compare-report-demo",
        expected="259b3b94",
        json=True,
        plot=None,
        metrics_plot=None,
        write_report="auto",
    )
    assert _cmd_compare(args) == 0
    report_json = stem_dir / "compare.json"
    report_wave = stem_dir / "compare_waveform.png"
    report_metrics = stem_dir / "compare_metrics.png"
    dry_sw_wave = stem_dir / "compare_dry_vs_software_waveform.png"
    dry_sw_metrics = stem_dir / "compare_dry_vs_software_metrics.png"
    dry_hw_wave = stem_dir / "compare_dry_vs_hardware_waveform.png"
    dry_hw_metrics = stem_dir / "compare_dry_vs_hardware_metrics.png"
    report_html = stem_dir / "compare_report.html"
    assert report_json.is_file()
    assert report_wave.is_file()
    assert report_metrics.is_file()
    assert dry_sw_wave.is_file()
    assert dry_sw_metrics.is_file()
    assert dry_hw_wave.is_file()
    assert dry_hw_metrics.is_file()
    assert report_html.is_file()
    payload = json.loads(report_json.read_text())
    assert payload["passed"] is True
    assert payload["mode"] == "hardware_vs_software"
    assert payload["gated"] is True
    assert "band_analysis" in payload
    html = report_html.read_text()
    assert "AU/FX Compare Report" in html
    assert "Raw JSON" in html
    assert "Hardware vs Software (default)" in html
    assert "Dry vs Software Wet" in html
    assert "Dry vs Hardware Wet" in html
    assert "Source Clip:" in html
    assert "Stringies Original Clip" in html
    assert "metric-ok" in html or "metric-bad" in html
    assert html.index("<h2>Plots</h2>") < html.index("<h2>Band Analysis</h2>")
    assert "plot-stack" in html
    assert "plot-waveform" in html
    assert "plot-metrics" in html
    assert html.index("plot-waveform") < html.index("plot-metrics")


def test_compare_root_mode_software_only_dry_vs_wet_report(tmp_session_root, sine_files):
    """Software-only capture + input yields informational dry/wet report (no gate)."""
    import argparse

    from aufx_test.cli import _cmd_compare

    _, out, _ = sine_files
    session = ExperimentSession.create("compare-sw-only", root_dir=tmp_session_root)
    stem = "tone_swonly01"
    stem_dir = session.artifacts_dir / stem
    stem_dir.mkdir(parents=True)
    sw = stem_dir / f"{stem}_output_gld.wav"
    inp = stem_dir / f"{stem}_input.wav"
    # Slightly different wet so metrics are finite/non-trivial.
    sw.write_bytes(out.read_bytes())
    inp.write_bytes(out.read_bytes())

    snap = StateSnapshot(
        name="tone",
        id="swonly01",
        source_clip_name="Tone Clip",
        input_audio=f"artifacts/{stem}/{stem}_input.wav",
        output_audio=f"artifacts/{stem}/{stem}_output_gld.wav",
        reference_kind="gld",
    )
    session.snapshots.append(snap)
    session.save()

    args = argparse.Namespace(
        compare_config=None,
        corr_min=None,
        root=tmp_session_root,
        actual="compare-sw-only",
        expected="swonly01",
        json=True,
        plot=None,
        metrics_plot=None,
        write_report="auto",
    )
    assert _cmd_compare(args) == 0
    payload = json.loads((stem_dir / "compare.json").read_text())
    assert payload["mode"] == "dry_vs_software"
    assert payload["gated"] is False
    assert payload["passed"] is None
    assert "thresholds" not in payload
    assert (stem_dir / "compare_waveform.png").is_file()
    assert (stem_dir / "compare_metrics.png").is_file()
    html = (stem_dir / "compare_report.html").read_text()
    assert "DRY VS SOFTWARE" in html
    assert "Dry vs Software Wet" in html
    assert "PASSED" not in html and "FAILED" not in html
    assert "Thresholds" not in html
    assert 'class="metric-ok"' not in html and 'class="metric-bad"' not in html
    assert "Tone Clip" in html


def test_compare_root_mode_hardware_only_dry_vs_wet_report(tmp_session_root, sine_files):
    """Hardware-only capture + input yields informational dry/wet report (no gate)."""
    import argparse

    from aufx_test.cli import _cmd_compare

    _, out, _ = sine_files
    session = ExperimentSession.create("compare-hw-only", root_dir=tmp_session_root)
    stem = "tone_hwonly01"
    stem_dir = session.artifacts_dir / stem
    stem_dir.mkdir(parents=True)
    hw = stem_dir / f"{stem}_output_hw_gld.wav"
    inp = stem_dir / f"{stem}_input.wav"
    hw.write_bytes(out.read_bytes())
    inp.write_bytes(out.read_bytes())

    snap = StateSnapshot(
        name="tone",
        id="hwonly01",
        input_audio=f"artifacts/{stem}/{stem}_input.wav",
        output_audio_hw=f"artifacts/{stem}/{stem}_output_hw_gld.wav",
        reference_kind="gld",
    )
    session.snapshots.append(snap)
    session.save()

    args = argparse.Namespace(
        compare_config=None,
        corr_min=None,
        root=tmp_session_root,
        actual="compare-hw-only",
        expected="hwonly01",
        json=True,
        plot=None,
        metrics_plot=None,
        write_report="auto",
    )
    assert _cmd_compare(args) == 0
    payload = json.loads((stem_dir / "compare.json").read_text())
    assert payload["mode"] == "dry_vs_hardware"
    assert payload["gated"] is False
    assert payload["passed"] is None
    html = (stem_dir / "compare_report.html").read_text()
    assert "DRY VS HARDWARE" in html
    assert "Dry vs Hardware Wet" in html
    assert "Hardware vs Software" not in html

def test_promote_and_export_resolve_stem_subfolders(tmp_session_root, sine_files, sample_aupreset):
    """CLI promote/export only need correct relative paths in session.json."""
    inp, out, _ = sine_files
    session = ExperimentSession.create("nested-demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="chamber")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    session.promote_snapshot(snap.id, test_name="test_chamber")
    session.save()

    stem = f"chamber_{snap.id}"
    assert snap.output_audio == f"artifacts/{stem}/{stem}_output.wav"

    setup = session.snapshot_to_test_setup(snap)
    assert Path(setup.input_audio).is_file()
    assert Path(setup.reference_output).is_file()
    assert Path(setup.preset_file).is_file()

    out_path = tmp_session_root / "nested_test.py"
    export_test_module(session, out_path)
    content = out_path.read_text()
    assert f"artifacts/{stem}/{stem}_output.wav" in content
    assert f"artifacts/{stem}/{stem}.aupreset" in content


def test_add_snapshot_copies_artifacts(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="half mix", parameters={"mix": 0.5})
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    session.save()

    stem = f"half_{snap.id}"
    stem_dir = session.artifacts_dir / stem
    assert (stem_dir / f"{stem}.aupreset").exists()
    assert (stem_dir / f"{stem}_input.wav").exists()
    assert (stem_dir / f"{stem}_output.wav").exists()
    assert snap.input_audio == f"artifacts/{stem}/{stem}_input.wav"
    assert snap.output_audio == f"artifacts/{stem}/{stem}_output.wav"
    assert snap.preset_file == f"artifacts/{stem}/{stem}.aupreset"


def test_add_snapshot_reuses_host_staged_artifacts(tmp_session_root, sine_files, sample_aupreset):
    """Host writes into artifacts/<stem>/; session snap should rename, not duplicate."""
    import shutil

    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    host_stem = "init_aabbccdd"
    host_dir = session.artifacts_dir / host_stem
    host_dir.mkdir(parents=True, exist_ok=True)

    host_out = host_dir / f"{host_stem}_output_bkn.wav"
    host_preset = host_dir / f"{host_stem}.aupreset"
    shutil.copy2(out, host_out)
    shutil.copy2(sample_aupreset, host_preset)

    snap = StateSnapshot(name="Init Serial")
    session.add_snapshot(snap, copy_input=inp, copy_output=host_out, copy_preset=host_preset)

    stem = f"init_{snap.id}"
    stem_dir = session.artifacts_dir / stem
    assert (stem_dir / f"{stem}_output_bkn.wav").exists()
    assert (stem_dir / f"{stem}.aupreset").exists()
    assert snap.reference_kind == "bkn"
    assert snap.output_audio == f"artifacts/{stem}/{stem}_output_bkn.wav"
    assert not host_out.exists()
    assert not host_preset.exists()
    assert len(list(session.artifacts_dir.rglob("*_output_bkn.wav"))) == 1
    assert len(list(session.artifacts_dir.rglob("*.aupreset"))) == 1


def test_get_snapshot_accepts_id_name_and_artifact_stem(tmp_session_root, sine_files, sample_aupreset):
    inp, out, _ = sine_files
    session = ExperimentSession.create("demo", root_dir=tmp_session_root)
    snap = StateSnapshot(name="Long Tail")
    session.add_snapshot(snap, copy_input=inp, copy_output=out, copy_preset=sample_aupreset)
    stem = artifact_stem(snap.name, snap.id)

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
    host_dir = session.artifacts_dir / "x"
    host_dir.mkdir(parents=True, exist_ok=True)

    bkn_out = host_dir / "x_output_bkn.wav"
    gld_out = tmp_session_root / "y_output_gld.wav"
    shutil.copy2(out, bkn_out)
    shutil.copy2(out, gld_out)

    broken = StateSnapshot(name="broken capture")
    session.add_snapshot(broken, copy_input=inp, copy_output=bkn_out, copy_preset=sample_aupreset)
    setup = session.promote_snapshot(broken.id)
    assert broken.reference_kind == "bkn"
    assert setup.expect_match is False
    assert Path(setup.reference_output).exists()
    assert Path(setup.input_audio).exists()
    assert Path(setup.preset_file).exists()

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
    assert "assert_setup_comparison" in content
    assert "allow_extra_actual_tail=True" in content


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


def test_import_goldens_from_directory(tmp_session_root, sine_files, sample_aupreset, tmp_path):
    import shutil

    inp, out, _ = sine_files
    goldens = tmp_path / "goldens"
    goldens.mkdir()
    stem = "Vocals- Depth of Space"
    shutil.copy2(sample_aupreset, goldens / f"{stem}.aupreset")
    shutil.copy2(inp, goldens / f"{stem}_input.wav")
    shutil.copy2(out, goldens / f"{stem}_output_gld.wav")
    # Incomplete set should be reported, not imported.
    shutil.copy2(out, goldens / "Broken Incomplete_output_gld.wav")
    # Bare *_output.wav (no role flag) should import as golden.
    bare = "Vocals- Init Serial"
    shutil.copy2(sample_aupreset, goldens / f"{bare}.aupreset")
    shutil.copy2(inp, goldens / f"{bare}_input.wav")
    shutil.copy2(out, goldens / f"{bare}_output.wav")

    session = ExperimentSession.create("import-demo", root_dir=tmp_session_root)
    imported, warnings = session.import_goldens(goldens)
    assert len(imported) == 2
    by_name = {s.name: s for s in imported}
    assert by_name[stem].reference_kind == "gld"
    assert by_name[stem].promoted is True
    assert by_name[stem].expect_match is True
    assert by_name[bare].reference_kind == "gld"
    assert by_name[bare].expect_match is True
    assert any("Incomplete set" in w for w in warnings)

    # Second import skips existing.
    imported2, warnings2 = session.import_goldens(goldens)
    assert imported2 == []
    assert any("Skipped existing" in w for w in warnings2)


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
