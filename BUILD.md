# Building LoungeCat Desktop 2.0

Linux, C++20, CMake 3.24 or newer, and Qt 6.4 or newer are required. The native
application uses Qt Widgets, not the Kotlin/Compose or Android toolchains.

## Debian / Ubuntu

```sh
sudo apt-get install build-essential cmake pkg-config qt6-base-dev libqt6svg6-dev libqt6sql6-sqlite qt6-qpa-plugins libasound2-dev
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --parallel 2
./build/release/loungecat --version
```

ALSA development headers are optional; without them the ALSA sound backend is
not compiled. Qt's SQLite driver, the selected display platform plugin, a
working Qt TLS backend and normal system runtime libraries must be installed.
This produces a dynamically linked executable, not an AppImage or installer.

For a first v2 session, keep the existing Kotlin profile separate:

```sh
LOUNGECAT_DATA_DIR="$HOME/.loungecat-v2-preview" ./build/release/loungecat
```

Leave `LOUNGECAT_DB_PATH` and `LOUNGECAT_CONFIG` unset when using this isolated
profile, since those optional overrides take precedence. Read
[migration notes](docs/MIGRATION-v2.md) before opening an existing profile.

## Contributor regression checks

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
```

The repository includes normal, ASan/UBSan and TSan presets. The read-only CI
workflow builds/tests normal and ASan variants; it does not publish releases.
The tests use synthetic local fixtures and isolated temporary profiles.
Documented historical regression runs are not proof of a hosted run on this
branch. See [limitations](docs/LIMITATIONS.md).

## Build the CI packages locally

Use Docker so the build baseline and dependency names match CI; this does not
install the app or development packages on the host. Start from a committed source
checkout so the generated matching-source archive identifies the exact revision.

```sh
docker build -t loungecat-builder -f packaging/Dockerfile.build .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" loungecat-builder bash packaging/build.sh
```

Artifacts are written to `dist/`. The build verifies SHA-256-pinned deployment
tools before execution; see `packaging/tools.lock.json`. To reproduce each clean
runtime check, follow [the packaging guide](docs/PACKAGING.md).
