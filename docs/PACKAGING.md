# Platforms and prebuilt packages

The native v2 application currently builds as a dynamically linked Linux executable.
No v2 AppImage, deb, rpm, Flatpak, Windows installer or macOS bundle has been
published. These are packaging options and recommendations, not available downloads.

## Recommended first release

Start with **Linux x86_64 AppImage and `.deb`**. Add other package families and
architectures after their build and runtime checks are in place.

| Format | Intended audience | What still needs doing |
| --- | --- | --- |
| **AppImage** | Many desktop Linux distributions of the same CPU architecture | Bundle Qt/plugins and runtime dependencies; choose and test a minimum glibc baseline and desktop integration. An AppImage is not automatically universal across old/new Linux systems. |
| **`.deb`** | Selected Debian, Ubuntu and Linux Mint releases | Native package metadata, dependency versions, desktop entry/icon, install/remove checks and a tested distro baseline. One deb is not guaranteed to fit every Debian-derived release. |
| **Flatpak** | Most desktop distributions with Flatpak support | Manifest, suitable KDE/Qt runtime, sandbox permissions and checks for networking, file access, tray/notifications and desktop portals. Flathub publication is a separate step. |
| **`.rpm`** | Selected Fedora and openSUSE releases | RPM spec and separate distro/dependency validation. These distributions need not use identical package names or runtime versions. |
| **`.tar.gz`** | Users who can supply compatible system Qt/runtime libraries | A clear runtime-dependency list, launch instructions, icons and checksums. This is a bundle, not an installer or a self-contained portable build. |

Arch users could additionally be served by a maintained `PKGBUILD`/AUR package;
that is a distro recipe, not proof of a tested standalone binary. Native source
builds remain an option wherever the required compiler, CMake and Qt versions exist.

## Architectures

Current binary build evidence is **Linux x86_64**. Linux ARM64/aarch64 is a possible
additional target, but requires a separate build, dependency bundle and runtime
validation. A single x86_64 AppImage/deb does not run natively on ARM64.

## Windows and macOS

- **Windows:** a portable executable/zip, setup `.exe` or `.msi` is possible in
  principle with Qt. A Windows toolchain, dependency deployment, platform fixes
  if needed, and real Windows runtime testing are required. It is not produced
  by renaming or packaging the Linux executable. Windows v2 is not validated.
- **macOS:** an `.app`/`.dmg` would likewise need a separate macOS build, deployment,
  signing/notarization decisions and runtime testing. It is not currently a
  validated or prepared v2 target.

Before shipping any binary, retain the application/dependency licenses and provide
the matching native source and build information. Packaging should use the native
CMake build; the archived Kotlin v1 packaging scripts do not build the v2 app.
