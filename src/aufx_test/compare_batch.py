"""Discover and run dry→preset→render→golden compares under a compares/ tree."""

from __future__ import annotations

import json
import os
import re
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from .audio import Waveform
from .compare_config import load_compare_config
from .comparison import ComparisonThresholds
from .naming import slugify
from .paths import project_root
from .reporting import assert_setup_comparison, write_html_report
from .silence import trim_leading_silence
from .subprocess_host import RendererError, SubprocessPluginHost

DEFAULT_RENDERER = (
    project_root()
    / "native/build/plugin_renderer/plugin_renderer_artefacts/Release/plugin_renderer"
)

DEFAULT_SETTLE_SECONDS = 1.5


_GOLDEN_TAIL_RE = re.compile(r"^(?P<num>\d{2,3})-(?P<slug>.+)$")


@dataclass(frozen=True)
class CompareCase:
    """One golden paired with its dry source and AU preset."""

    name: str
    golden: Path
    dry: Path
    preset: Path
    source: str
    preset_num: str
    preset_slug: str


@dataclass
class CompareCaseResult:
    name: str
    status: str  # passed | failed | skipped | error
    message: str = ""
    report_dir: Path | None = None
    actual_path: Path | None = None


@dataclass
class CompareBatchResult:
    cases: list[CompareCaseResult] = field(default_factory=list)

    @property
    def passed(self) -> int:
        return sum(1 for c in self.cases if c.status == "passed")

    @property
    def failed(self) -> int:
        return sum(1 for c in self.cases if c.status == "failed")

    @property
    def skipped(self) -> int:
        return sum(1 for c in self.cases if c.status == "skipped")

    @property
    def errors(self) -> int:
        return sum(1 for c in self.cases if c.status == "error")

    def to_dict(self) -> dict[str, Any]:
        return {
            "passed": self.passed,
            "failed": self.failed,
            "skipped": self.skipped,
            "errors": self.errors,
            "cases": [
                {
                    "name": c.name,
                    "status": c.status,
                    "message": c.message,
                    "report_dir": str(c.report_dir) if c.report_dir else None,
                    "actual_path": str(c.actual_path) if c.actual_path else None,
                }
                for c in self.cases
            ],
        }


def list_dry_sources(dry_dir: Path) -> dict[str, Path]:
    """Map dry stem → path for ``*.wav`` under ``dry_dir``."""
    return {p.stem: p for p in sorted(dry_dir.glob("*.wav")) if p.is_file()}


def find_preset_for_number(presets_dir: Path, preset_num: str) -> Path | None:
    """Return the unique ``{NN}-*.aupreset`` for ``preset_num``, or None."""
    # Accept both zero-padded and raw digits in the golden (04 vs 4).
    candidates: list[Path] = []
    try:
        n = int(preset_num)
    except ValueError:
        n = -1
    patterns = {preset_num}
    if n >= 0:
        patterns.add(f"{n:02d}")
        if n >= 100:
            patterns.add(str(n))
    for prefix in patterns:
        candidates.extend(presets_dir.glob(f"{prefix}-*.aupreset"))
    # Deduplicate
    unique = sorted({c.resolve() for c in candidates if c.is_file()})
    if not unique:
        return None
    if len(unique) > 1:
        names = ", ".join(p.name for p in unique)
        raise ValueError(f"Multiple presets for number {preset_num!r}: {names}")
    return unique[0]


def parse_golden_name(stem: str, dry_sources: Iterable[str]) -> tuple[str, str, str] | None:
    """Split ``{Source}-{NN}-{Preset-Slug}`` using known dry stems (longest first)."""
    sources = sorted(dry_sources, key=len, reverse=True)
    for source in sources:
        prefix = f"{source}-"
        if not stem.startswith(prefix):
            continue
        rest = stem[len(prefix) :]
        match = _GOLDEN_TAIL_RE.match(rest)
        if not match:
            return None
        return source, match.group("num"), match.group("slug")
    return None


def discover_compare_cases(
    compares_dir: str | Path,
    *,
    dry_dir: str | Path | None = None,
    presets_dir: str | Path | None = None,
    goldens_dir: str | Path | None = None,
    filter_glob: str | None = None,
) -> tuple[list[CompareCase], list[str]]:
    """Discover compare cases under a device compares folder.

    Expected layout::

        <compares_dir>/
          dry/{Source}.wav
          presets/au/{NN}-{Preset Name}.aupreset
          goldens/{Source}-{NN}-{Preset-Slug}.wav

    Returns ``(cases, warnings)``. Cases whose dry or preset is missing are omitted
    and listed in ``warnings``.
    """
    root = Path(compares_dir)
    dry = Path(dry_dir) if dry_dir is not None else root / "dry"
    presets = Path(presets_dir) if presets_dir is not None else root / "presets" / "au"
    goldens = Path(goldens_dir) if goldens_dir is not None else root / "goldens"

    if not dry.is_dir():
        raise FileNotFoundError(f"Dry sources folder not found: {dry}")
    if not presets.is_dir():
        raise FileNotFoundError(f"Presets folder not found: {presets}")
    if not goldens.is_dir():
        raise FileNotFoundError(f"Goldens folder not found: {goldens}")

    dry_map = list_dry_sources(dry)
    if not dry_map:
        raise FileNotFoundError(f"No dry WAV files in {dry}")

    pattern = filter_glob or "*.wav"
    cases: list[CompareCase] = []
    warnings: list[str] = []

    for golden in sorted(goldens.glob(pattern)):
        if not golden.is_file() or golden.suffix.lower() != ".wav":
            continue
        parsed = parse_golden_name(golden.stem, dry_map.keys())
        if parsed is None:
            warnings.append(f"Skip {golden.name}: could not match a dry source prefix")
            continue
        source, preset_num, preset_slug = parsed
        dry_path = dry_map[source]
        try:
            preset_path = find_preset_for_number(presets, preset_num)
        except ValueError as exc:
            warnings.append(f"Skip {golden.name}: {exc}")
            continue
        if preset_path is None:
            warnings.append(
                f"Skip {golden.name}: no preset {preset_num}-*.aupreset under {presets}"
            )
            continue
        cases.append(
            CompareCase(
                name=slugify(golden.stem),
                golden=golden.resolve(),
                dry=dry_path.resolve(),
                preset=preset_path.resolve(),
                source=source,
                preset_num=preset_num,
                preset_slug=preset_slug,
            )
        )
    return cases, warnings


def resolve_renderer_bin(explicit: str | Path | None = None) -> Path:
    if explicit is not None:
        return Path(explicit).expanduser().resolve()
    env = os.environ.get("AUFX_PLUGIN_RENDERER") or os.environ.get("JUCE_PLUGIN_RENDERER")
    if env:
        return Path(env).expanduser().resolve()
    return DEFAULT_RENDERER.resolve()


def resolve_plugin_path(
    *,
    explicit: str | Path | None = None,
    plugin_id: str | None = None,
    host_config: str | Path | None = None,
) -> str:
    """Resolve a plugin path/ID from CLI, env, or host.config.json."""
    if explicit is not None:
        return str(explicit)

    env = os.environ.get("AUFX_TEST_PLUGIN") or os.environ.get("JUCE_TEST_PLUGIN")
    if env:
        return env

    config_path = Path(host_config) if host_config is not None else project_root() / "host.config.json"
    if not config_path.is_file():
        raise FileNotFoundError(
            "No plugin specified. Pass --plugin / --plugin-id, set AUFX_TEST_PLUGIN, "
            f"or provide {config_path}"
        )
    data = json.loads(config_path.read_text(encoding="utf-8"))
    plugins = [p for p in (data.get("plugins") or []) if isinstance(p, dict)]
    if not plugins:
        raise ValueError(f"No plugins listed in {config_path}")

    wanted = plugin_id or data.get("default_plugin")
    chosen = None
    if wanted:
        for entry in plugins:
            if entry.get("id") == wanted:
                chosen = entry
                break
        if chosen is None and plugin_id:
            raise ValueError(f"plugin id {plugin_id!r} not found in {config_path}")
    if chosen is None:
        chosen = plugins[0]
    path = chosen.get("path")
    if not path:
        raise ValueError(f"Plugin entry {chosen.get('id')!r} has no path in {config_path}")
    return str(path)


def render_with_settle(
    host: SubprocessPluginHost,
    dry_wave: Waveform,
    *,
    settle_seconds: float = DEFAULT_SETTLE_SECONDS,
    output_path: str | Path | None = None,
) -> Waveform:
    """Render ``dry_wave`` after feeding ``settle_seconds`` of silence.

    Preset changes leave the plugin transitioning from the previous effect.
    Running a short silence lead-in lets that settle before the dry source
    begins; the lead-in is then trimmed from the rendered output so the
    returned waveform lines up with onset-trimmed goldens.
    """
    if settle_seconds <= 0:
        actual = host.process(dry_wave, output_path=None)
    else:
        settle = Waveform.silence(
            settle_seconds,
            sample_rate=dry_wave.sample_rate,
            channels=dry_wave.num_channels,
        )
        combined = dry_wave.with_data(np.vstack([settle.data, dry_wave.data]))
        rendered = host.process(combined, output_path=None)
        trim = min(int(round(settle_seconds * rendered.sample_rate)), rendered.num_samples)
        actual = rendered.with_data(rendered.data[trim:])

    # Residual plugin latency / fade-in after the fixed settle window.
    actual = trim_leading_silence(actual)
    if output_path is not None:
        actual.to_file(output_path)
    return actual


def run_compare_batch(
    cases: list[CompareCase],
    *,
    plugin_path: str,
    renderer_bin: str | Path,
    results_root: str | Path = "test-results/compare-batch",
    thresholds: ComparisonThresholds | None = None,
    keep_actuals: bool = True,
    fail_fast: bool = False,
    write_html: bool = True,
    settle_seconds: float = DEFAULT_SETTLE_SECONDS,
) -> CompareBatchResult:
    """Render each case through the plugin and compare against its golden."""
    results = Path(results_root)
    results.mkdir(parents=True, exist_ok=True)
    actuals_dir = results / "actuals"
    if keep_actuals:
        actuals_dir.mkdir(parents=True, exist_ok=True)

    if thresholds is None:
        thresholds = load_compare_config().thresholds

    batch = CompareBatchResult()
    mismatch_dirs: list[Path] = []
    records: list[dict[str, Any]] = []

    with SubprocessPluginHost(renderer_bin=renderer_bin, plugin_path=plugin_path) as host:
        for case in cases:
            actual_path = actuals_dir / f"{case.golden.stem}.wav" if keep_actuals else None
            try:
                host.reset()
                host.load_preset(case.preset)
                dry_wave = Waveform.from_file(case.dry)
                actual = render_with_settle(
                    host,
                    dry_wave,
                    settle_seconds=settle_seconds,
                    output_path=actual_path,
                )
                expected = Waveform.from_file(case.golden)
                setup = {
                    "name": case.name,
                    "plugin_path": plugin_path,
                    "input_audio": str(case.dry),
                    "reference_output": str(case.golden),
                    "preset_file": str(case.preset),
                    "source": case.source,
                    "preset_num": case.preset_num,
                    "preset_slug": case.preset_slug,
                    "settle_seconds": settle_seconds,
                    "expect_match": True,
                    "thresholds": asdict(thresholds),
                }
                assert_setup_comparison(
                    actual,
                    expected,
                    setup=setup,
                    thresholds=thresholds,
                    input_audio=dry_wave,
                    results_root=results / "fails",
                    allow_extra_actual_tail=True,
                )
                batch.cases.append(
                    CompareCaseResult(
                        name=case.name,
                        status="passed",
                        actual_path=actual_path,
                    )
                )
                records.append({"nodeid": case.name, "outcome": "passed", "duration": 0.0})
            except AssertionError as exc:
                text = str(exc)
                report_dir = None
                for line in text.splitlines():
                    if line.startswith("Mismatch report:"):
                        report_dir = Path(line.split(":", 1)[1].strip())
                        break
                if report_dir is not None:
                    mismatch_dirs.append(report_dir)
                batch.cases.append(
                    CompareCaseResult(
                        name=case.name,
                        status="failed",
                        message=text,
                        report_dir=report_dir,
                        actual_path=actual_path,
                    )
                )
                records.append({"nodeid": case.name, "outcome": "failed", "duration": 0.0})
                if fail_fast:
                    break
            except (RendererError, OSError, ValueError) as exc:
                batch.cases.append(
                    CompareCaseResult(
                        name=case.name,
                        status="error",
                        message=str(exc),
                        actual_path=actual_path,
                    )
                )
                records.append({"nodeid": case.name, "outcome": "failed", "duration": 0.0})
                if fail_fast:
                    break

    summary_path = results / "summary.json"
    summary_path.write_text(json.dumps(batch.to_dict(), indent=2) + "\n", encoding="utf-8")

    if write_html:
        write_html_report(
            results / "report.html",
            test_records=records,
            mismatch_reports=mismatch_dirs,
        )
    return batch
