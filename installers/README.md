# Installers

Distributable packages for zeitbenutzer. Packaging is driven by **CPack** from
the top-level `CMakeLists.txt`, so the install layout is defined once and shared
by both targets. Supporting files live in [`../packaging/`](../packaging).

| File | Platform | Built where |
|------|----------|-------------|
| `zeitbenutzer-<ver>-Linux.deb` | Debian/Ubuntu (amd64) | Linux |
| `zeitbenutzer-<ver>-Darwin.pkg` | macOS 11+ | **macOS only** |

## Linux — `.deb`

```bash
cmake -S . -B build
cmake --build build -j
( cd build && cpack -G DEB )
```

Installs the executable to `/usr/bin/zeitbenutzer`, a `.desktop` entry, and
hicolor icons (16–512px). Qt dependencies are resolved automatically by
`dpkg-shlibdeps`; the runtime-loaded SQLite driver (`libqt6sql6-sqlite`) is
added explicitly since it isn't a link-time dependency.

Install / remove:

```bash
sudo apt install ./zeitbenutzer-0.1.0-Linux.deb   # pulls in Qt deps
sudo apt remove zeitbenutzer
```

## macOS — `.pkg`

Must be built on a Mac (needs `macdeployqt`, `iconutil`, `productbuild`). A
helper script does configure → build → bundle Qt → package:

```bash
export PATH="$HOME/Qt/6.9.2/macos/bin:$PATH"   # so macdeployqt is found
packaging/build-macos-pkg.sh
```

This produces `zeitbenutzer.app` (Qt frameworks + the SQLite plugin bundled
in), then a `.pkg` that installs it to `/Applications`.

**Signing/notarization** is optional — unsigned packages run locally but
Gatekeeper warns on other Macs. See the comments in the script for the
`-codesign=` / `notarytool` steps if you distribute it.

## Versioning

The version comes from `project(zeitbenutzer VERSION x.y.z …)` in the top-level
`CMakeLists.txt` — bump it there and both packages and the macOS bundle pick it
up.
