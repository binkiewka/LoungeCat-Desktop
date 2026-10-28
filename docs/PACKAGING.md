# Linux packages and automatic releases

CI builds **x86_64 AppImage and `.deb`** on Debian 12 (glibc 2.36, Qt 6.4).
Each format is downloaded into clean Debian 12 and Ubuntu 24.04 runtime containers
and checked before release. The runtime images start without Qt installed, so
AppImage's bundled libraries cannot be masked by build-machine dependencies.

## Download and run

Tagged, successfully tested versions are uploaded automatically to
[GitHub Releases](https://github.com/binkiewka/LoungeCat-Desktop/releases).
Branch/PR builds are available from the successful
[CI run](https://github.com/binkiewka/LoungeCat-Desktop/actions/workflows/ci.yml)
as `loungecat-linux-x86_64` (14-day retention; GitHub login may be required).

Every build includes the matching source tarball, `SHA256SUMS`, version/commit
manifest and build dependency inventory. Verify the downloaded assets:

```sh
sha256sum --check SHA256SUMS
chmod +x LoungeCat-2.0.0-x86_64.AppImage
LOUNGECAT_DATA_DIR="$HOME/.loungecat-v2-preview" ./LoungeCat-2.0.0-x86_64.AppImage
```

If FUSE is unavailable, use `APPIMAGE_EXTRACT_AND_RUN=1` with the same command.
The desktop/display stack and working graphics drivers still come from the host.
For a Debian/Ubuntu package, install using apt so dependencies are resolved:

```sh
sudo apt install ./loungecat_2.0.0_amd64.deb
LOUNGECAT_DATA_DIR="$HOME/.loungecat-v2-preview" loungecat
```

Use the version in the downloaded filenames. Leave `LOUNGECAT_DB_PATH` and
`LOUNGECAT_CONFIG` unset for the separate preview profile. Read
[migration/rollback notes](MIGRATION-v2.md) before opening old Kotlin data.

## What CI verifies

- Release build, desktop entry/icon and license payload, checksums and matching source.
- `.deb` install on both clean distros using only declared runtime dependencies;
  removal must remove the executable while retaining user profile data.
- Actual AppImage extraction/run without privileged FUSE. Required SQLite, xcb,
  offscreen and OpenSSL backend plugins and OpenSSL libraries must be bundled.
- A separate test-only Qt probe (not installed in either package): SQLite round trip,
  certificate-verified loopback TLS, SVG rendering and actual widget rendering.
  AppImage probe mappings must prove Qt/SQLite/OpenSSL loaded from the bundle.
- The **actual packaged application** on a private Xvfb/X11 display: visible main
  window, Settings open/close, normal shutdown and durable owner-only database.
- Existing native normal/ASan regression jobs remain required for tag publication.

This is targeted package/runtime coverage, not every feature, desktop compositor,
physical audio device, graphics driver or Linux distribution. Native Wayland is
not bundled/tested; Wayland desktops need XWayland. The current targets are x86_64;
ARM64 needs separate builds and runtime validation.

## Automatic publication

Push a stable tag matching the CMake version, for example `v2.0.0`. CI builds and
checks both package formats on both distributions. The release job depends on
**all** native, build and package-smoke jobs succeeding; a failure blocks upload.
Before publication it verifies tag/version, exact source commit and every checksum.
Assets are uploaded to a draft first; only a completed upload is published. An
already published release is never overwritten by a rerun. No Android files are
built or uploaded. Branch/PR pushes never publish releases.

The pipeline uses only its narrowly scoped release-job `GITHUB_TOKEN`; no new
personal token or external publication service is needed. Deployment tools are
locked to GitHub asset IDs and SHA-256 hashes in `packaging/tools.lock.json`.

## Reproduce package tests locally

After [building packages](../BUILD.md), test each distribution/format combination
inside disposable containers, not by installing on the development host:

```sh
docker build --build-arg BASE=debian:12-slim -t loungecat-runtime -f packaging/Dockerfile.runtime .
mkdir -p build/package-results
docker run --rm -v "$PWD/dist:/packages" -v "$PWD/build/package-results:/results" \
  loungecat-runtime python3 /packages/test-support/smoke.py \
  --format appimage --dist /packages --output /results
```

Repeat with `--format deb` in a fresh container. Build the runtime image with
`BASE=ubuntu:24.04` for the second distro. Logs and JSON receipts are uploaded as
CI evidence. CI's `test-support` files are validation tools/public synthetic TLS
fixtures; they are not shipped inside the installed application.

## Other formats/platforms

| Target | Status |
| --- | --- |
| Debian 12 / Ubuntu 24.04 x86_64 | AppImage and deb build + clean-runtime CI coverage |
| Linux Mint 22.x | Ubuntu-derived target; no claim of a separate clean-Mint package matrix |
| Fedora/openSUSE | AppImage may work with the required runtime baseline; not yet tested. `.rpm` needs its own build/dependency checks. |
| Flatpak / Arch AUR | Separate manifests/recipes and desktop-integration testing needed |
| ARM64 | Separate dependency bundles, build and runtime tests needed |
| Windows `.exe`/`.msi` | Separate Qt/Windows build, deployment and testing; not validated |
| macOS `.app`/`.dmg` | Separate macOS build, deployment, signing/notarization choices and tests; not validated |

The AppImage is dynamically linked to the host's glibc/display stack; it is not
universal across arbitrary old Linux systems or CPU architectures. The deb uses
system Qt instead of vendoring it; it is not guaranteed to install on all Debian
family releases. Keep bundled dependency notices and the matching GPL source.
