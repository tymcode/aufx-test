#!/usr/bin/env bash
# Build and package AU Effects Explorer as a shareable universal Mac .app zip.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/native/build-universal}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
APP_NAME="AU Effects Explorer"
CODESIGN_IDENTITY="${CODESIGN_IDENTITY:--}"
VERSION_FILE="$ROOT/native/plugin_host_app/VERSION"
GEN_DIR="$BUILD_DIR/plugin_host_app/generated"

echo "==> Bumping build number in VERSION"
mkdir -p "$GEN_DIR"
cmake \
  -DAUFX_VERSION_FILE="$VERSION_FILE" \
  -DAUFX_GEN_DIR="$GEN_DIR" \
  -DAUFX_BUMP_VERSION=ON \
  -P "$ROOT/native/plugin_host_app/cmake/BumpVersion.cmake"

echo "==> Configuring universal Release build (arm64 + x86_64)"
cmake -S "$ROOT/native" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-13.0}"

echo "==> Building plugin_host_app"
cmake --build "$BUILD_DIR" --target plugin_host_app --config Release

APP_SRC="$(find "$BUILD_DIR" -name "${APP_NAME}.app" -type d | head -n 1)"
if [[ -z "$APP_SRC" || ! -d "$APP_SRC" ]]; then
  echo "error: could not find ${APP_NAME}.app under $BUILD_DIR" >&2
  exit 1
fi

echo "==> Found app at $APP_SRC"
rm -rf "$DIST_DIR/${APP_NAME}.app"
mkdir -p "$DIST_DIR"
# Avoid copying resource-fork / xattr detritus into the dist bundle.
ditto --norsrc --noextattr "$APP_SRC" "$DIST_DIR/${APP_NAME}.app"

# Strip any remaining xattrs; they break codesign and create AppleDouble `._*` in zips.
xattr -cr "$DIST_DIR/${APP_NAME}.app"
find "$DIST_DIR/${APP_NAME}.app" -name '._*' -delete

BIN="$DIST_DIR/${APP_NAME}.app/Contents/MacOS/${APP_NAME}"
if [[ ! -x "$BIN" ]]; then
  echo "error: missing app binary at $BIN" >&2
  exit 1
fi

echo "==> Verifying universal binary"
LIPO_INFO="$(lipo -info "$BIN" 2>&1 || true)"
echo "    $LIPO_INFO"
if ! echo "$LIPO_INFO" | grep -q 'arm64'; then
  echo "error: packaged binary is missing arm64" >&2
  exit 1
fi
if ! echo "$LIPO_INFO" | grep -q 'x86_64'; then
  echo "error: packaged binary is missing x86_64" >&2
  exit 1
fi

echo "==> Codesigning (identity: $CODESIGN_IDENTITY)"
codesign --force --deep --sign "$CODESIGN_IDENTITY" "$DIST_DIR/${APP_NAME}.app"
codesign --verify --deep --strict "$DIST_DIR/${APP_NAME}.app"

ZIP_PATH="$DIST_DIR/AU-Effects-Explorer-macOS.zip"
rm -f "$ZIP_PATH"
# --norsrc/--noextattr prevents AppleDouble `._*` sidecars that invalidate the
# sealed signature when recipients unzip with Archive Utility / unzip.
(
  cd "$DIST_DIR"
  ditto -c -k --norsrc --noextattr --keepParent "${APP_NAME}.app" "AU-Effects-Explorer-macOS.zip"
)

echo "==> Verifying zip round-trip signature"
VERIFY_DIR="$(mktemp -d /tmp/aufx-zip-verify-XXXX)"
unzip -q "$ZIP_PATH" -d "$VERIFY_DIR"
if find "$VERIFY_DIR" -name '._*' | grep -q .; then
  echo "error: zip still contains AppleDouble ._* files" >&2
  find "$VERIFY_DIR" -name '._*' | head
  rm -rf "$VERIFY_DIR"
  exit 1
fi
codesign --verify --deep --strict "$VERIFY_DIR/${APP_NAME}.app"
rm -rf "$VERIFY_DIR"
echo "    zip round-trip OK"

if [[ -f "$VERSION_FILE" ]]; then
  SEMVER="$(sed -n '1p' "$VERSION_FILE" | tr -d '[:space:]')"
  BUILD="$(sed -n '2p' "$VERSION_FILE" | tr -d '[:space:]')"
  echo
  echo "Version: ${SEMVER}+${BUILD}"
fi

echo
echo "Binary info:"
lipo -info "$BIN" 2>/dev/null || true
otool -l "$BIN" 2>/dev/null | awk '/minos/{print "minos",$2; exit}'

echo
echo "Packaged: $ZIP_PATH"
echo
echo "Gatekeeper note for recipients:"
echo "  1. Unzip the archive (do not run from inside the zip)."
echo "  2. Clear quarantine if needed: xattr -cr \"AU Effects Explorer.app\""
echo "  3. Right-click the app → Open (first launch), or:"
echo "     System Settings → Privacy & Security → Open Anyway"
echo "  4. This build is universal (arm64 + x86_64) and requires macOS 13+."
echo "  5. Audio Units must already be installed under"
echo "     /Library/Audio/Plug-Ins/Components/ (or ~/Library/...)."
