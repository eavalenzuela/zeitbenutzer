#!/usr/bin/env bash
# Build the macOS .pkg installer. Run this ON A MAC — it uses macdeployqt,
# iconutil, and productbuild, none of which exist on Linux.
#
# Prereqs:
#   - Xcode command-line tools (provides iconutil, productbuild)
#   - Qt6 for macOS, with its bin/ on PATH so macdeployqt is found
#     (e.g.  export PATH="$HOME/Qt/6.9.2/macos/bin:$PATH")
#   - CMake >= 3.21
#
# Usage:  packaging/build-macos-pkg.sh [build-dir]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-macos}"
APP="$BUILD/zeitbenutzer.app"

command -v macdeployqt >/dev/null || {
  echo "error: macdeployqt not on PATH — add your Qt macOS bin/ dir." >&2; exit 1; }

# 1. A crisp .icns from the iconset (overwrites the Pillow-baked fallback).
echo "==> Building .icns from iconset"
iconutil -c icns "$REPO/packaging/icons/zeitbenutzer.iconset" \
         -o "$REPO/packaging/icons/zeitbenutzer.icns"

# 2. Configure + build the .app bundle.
echo "==> Configuring + building"
cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j

# 3. Bundle Qt frameworks + plugins (incl. the QSQLITE sql driver) into the .app.
echo "==> Running macdeployqt"
macdeployqt "$APP" -always-overwrite

# To sign + notarize for distribution outside your own machine, run macdeployqt
# with:  -codesign="Developer ID Application: Your Name (TEAMID)"
# then  notarytool submit / staple the resulting .pkg. Unsigned packages work
# locally but Gatekeeper will warn on other Macs.

# 4. Produce the .pkg via CPack (productbuild generator, installs to /Applications).
echo "==> Packaging .pkg"
( cd "$BUILD" && cpack -G productbuild )

echo "==> Done:"
ls -1 "$BUILD"/*.pkg
