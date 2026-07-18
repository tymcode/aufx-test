"""Pytest integration for shareable AU/FX HTML reports."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from .reporting import write_html_report

_MISMATCH_REPORT_RE = re.compile(r"Mismatch report:\s*(.+)")


def pytest_addoption(parser: Any) -> None:
    group = parser.getgroup("aufx-test")
    group.addoption(
        "--aufx-html",
        metavar="PATH",
        default=None,
        help="Write an AU/FX HTML report with links to mismatch audio and plots",
    )


def pytest_configure(config: Any) -> None:
    config._aufx_test_records = []
    config._aufx_mismatch_reports = []


def pytest_runtest_logreport(report: Any) -> None:
    if report.when != "call":
        return
    config = getattr(report, "config", None)
    # TestReport does not expose config, so pytest attaches our active state
    # through the module globals initialized by pytest_sessionstart.
    if _active_state is None:
        return
    _active_state["records"].append(
        {
            "nodeid": report.nodeid,
            "outcome": report.outcome,
            "duration": report.duration,
        }
    )
    if report.failed:
        text = str(report.longrepr)
        for match in _MISMATCH_REPORT_RE.finditer(text):
            path = Path(match.group(1).strip())
            if not path.is_absolute():
                path = _active_state["root"] / path
            _active_state["reports"].append(path)


_active_state: dict[str, Any] | None = None


def pytest_sessionstart(session: Any) -> None:
    global _active_state
    _active_state = {
        "records": session.config._aufx_test_records,
        "reports": session.config._aufx_mismatch_reports,
        "root": Path(str(session.config.rootpath)),
    }


def pytest_sessionfinish(session: Any, exitstatus: int) -> None:
    global _active_state
    output = session.config.getoption("--aufx-html")
    if output:
        path = Path(output)
        if not path.is_absolute():
            path = Path(str(session.config.rootpath)) / path
        report = write_html_report(
            path,
            test_records=session.config._aufx_test_records,
            mismatch_reports=list(dict.fromkeys(session.config._aufx_mismatch_reports)),
        )
        terminal = session.config.pluginmanager.get_plugin("terminalreporter")
        if terminal is not None:
            terminal.write_sep("=", f"AU/FX HTML report: {report}")
    _active_state = None
