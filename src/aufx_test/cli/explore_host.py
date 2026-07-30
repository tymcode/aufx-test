"""Explore and host command implementations."""

from __future__ import annotations

import argparse

from ..explore import run_explore
from ..host_app import launch_host_app
from ..session import ExperimentSession
from .session_loader import _load_session


def _cmd_explore(args: argparse.Namespace) -> int:
    if args.name:
        session = _load_session(args.name, args.root)
    else:
        name = args.new_name or input("Session name: ").strip()
        session = ExperimentSession.create(
            name,
            plugin_path=args.plugin,
            description=args.description or "",
            root_dir=args.root,
        )
    run_explore(session, params_file=args.params_file)
    return 0


def _cmd_host(args: argparse.Namespace) -> int:
    process = launch_host_app(
        config=args.config,
        host_app_bin=args.host_app,
        project_root_dir=args.project_root,
    )
    if args.detach:
        print(f"Launched plugin host (pid {process.pid})")
        return 0
    return process.wait()
