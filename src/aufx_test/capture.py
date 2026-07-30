"""Before/after waveform capture helpers for plugin parameter tests."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

from .audio import Waveform
from .comparison import ComparisonResult, ComparisonThresholds, compare_waveforms, compute_difference_metrics
from .host import PluginHost


class RenderFn(Protocol):
    def __call__(self) -> Waveform: ...


AdjustFn = Callable[[], None]


@dataclass
class CapturePair:
    """Waveforms captured before and after a parameter change."""

    before: Waveform
    after: Waveform
    parameter_changes: dict[str, Any] = field(default_factory=dict)

    def compare_after_to_reference(
        self,
        reference: Waveform | str | Path,
        *,
        thresholds: ComparisonThresholds | None = None,
        **kwargs: Any,
    ) -> ComparisonResult:
        ref = reference if isinstance(reference, Waveform) else Waveform.from_file(reference)
        return compare_waveforms(self.after, ref, thresholds=thresholds, **kwargs)

    def metrics_before_vs_after(self):
        return compute_difference_metrics(self.before, self.after)


def capture_before_after(
    render: RenderFn,
    adjust_controls: dict[str, Any] | None = None,
    *,
    on_adjust: AdjustFn | None = None,
) -> CapturePair:
    """Capture output before and after adjusting plugin controls.

    Parameters
    ----------
    render:
        Callable that renders/processes audio and returns a ``Waveform``.
        Called twice — once before and once after adjustment.
    adjust_controls:
        Descriptive record of parameter changes (for reporting). If ``on_adjust``
        is not provided, this is informational only.
    on_adjust:
        Callable that applies control changes between captures. When using a
        ``PluginHost``, pass ``lambda: host.set_parameters(adjust_controls)``.
    """
    before = render()
    if on_adjust is not None:
        on_adjust()
    after = render()
    return CapturePair(
        before=before,
        after=after,
        parameter_changes=adjust_controls or {},
    )


def capture_with_host(
    host: PluginHost,
    input_waveform: Waveform,
    adjust_controls: dict[str, Any],
) -> CapturePair:
    """Capture before/after using a ``PluginHost`` implementation."""
    return capture_before_after(
        render=lambda: host.process(input_waveform),
        adjust_controls=adjust_controls,
        on_adjust=lambda: host.set_parameters(adjust_controls),
    )


def compare_captured_to_reference(
    captured: CapturePair | Waveform,
    reference: Waveform | str | Path,
    *,
    use_after: bool = True,
    thresholds: ComparisonThresholds | None = None,
    **kwargs: Any,
) -> ComparisonResult:
    """Compare a captured waveform (or the 'after' snapshot) to a reference."""
    actual = captured.after if isinstance(captured, CapturePair) and use_after else captured
    if isinstance(actual, CapturePair):
        actual = actual.after
    ref = reference if isinstance(reference, Waveform) else Waveform.from_file(reference)
    return compare_waveforms(actual, ref, thresholds=thresholds, **kwargs)
