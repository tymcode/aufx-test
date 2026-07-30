"""Command-line entry point."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .compare import _cmd_compare
from .parser import build_parser
from .session_cmds import _cmd_session_show

__all__ = ["main", "_cmd_compare", "_cmd_session_show"]


def _subparser(parser: argparse.ArgumentParser, name: str) -> argparse.ArgumentParser:
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action.choices[name]
    raise RuntimeError(f"subparser {name!r} not found")


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        return 0

    if args.command == "session" and not getattr(args, "session_command", None):
        _subparser(parser, "session").print_help()
        return 0

    if args.command == "session":
        args.root = getattr(args, "root", Path("sessions"))

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
