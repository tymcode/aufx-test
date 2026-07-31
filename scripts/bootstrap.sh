#!/usr/bin/env bash
# One-shot developer setup: JUCE submodule, Python venv, native build.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Initializing git submodules (JUCE)"
git submodule update --init --recursive

pick_python() {
  if [[ -n "${PYTHON:-}" ]] && command -v "$PYTHON" >/dev/null 2>&1; then
    command -v "$PYTHON"
    return
  fi
  local candidates=(
    python3.13
    python3.12
    python3.11
    python3.10
    python3
    /opt/homebrew/opt/python@3.13/bin/python3.13
    /opt/homebrew/bin/python3
    /usr/local/opt/python@3.13/bin/python3.13
    /usr/local/bin/python3
  )
  local c
  for c in "${candidates[@]}"; do
    if command -v "$c" >/dev/null 2>&1 || [[ -x "$c" ]]; then
      local bin
      bin="$(command -v "$c" 2>/dev/null || echo "$c")"
      if "$bin" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null; then
        if "$bin" -c 'import ssl' 2>/dev/null; then
          echo "$bin"
          return
        fi
      fi
    fi
  done
  echo "error: need Python >= 3.10 with a working ssl module" >&2
  exit 1
}

PYTHON_BIN="$(pick_python)"
echo "==> Using Python: $PYTHON_BIN ($("$PYTHON_BIN" -V))"

if [[ ! -d .venv ]]; then
  echo "==> Creating .venv"
  "$PYTHON_BIN" -m venv .venv
fi

# shellcheck disable=SC1091
source .venv/bin/activate
python -m pip install -U pip
pip install -e ".[dev]"

if [[ ! -f host.config.json ]]; then
  echo "==> Seeding host.config.json from host.config.example.json"
  cp host.config.example.json host.config.json
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake not found (need CMake >= 3.22)" >&2
  exit 1
fi

BUILD_DIR="${BUILD_DIR:-$ROOT/native/build}"
echo "==> Configuring native Release build -> $BUILD_DIR"
cmake -S native -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> Building plugin_renderer + plugin_host_app"
cmake --build "$BUILD_DIR" --target plugin_renderer --config Release
cmake --build "$BUILD_DIR" --target plugin_host_app --config Release

APP="$BUILD_DIR/plugin_host_app/plugin_host_app_artefacts/Release/AU Effects Explorer.app"
echo
echo "Setup complete."
echo "Launch (repo-rooted exploration data):"
echo "  open \"$APP\" --args --config \"$(pwd)/host.config.json\" --project-root \"$(pwd)\""
echo
echo "Python CLI (after: source .venv/bin/activate):"
echo "  aufx-test --help"
echo "  pytest"
echo
echo "Package a universal zip for other Macs:"
echo "  ./scripts/package_mac_app.sh"
