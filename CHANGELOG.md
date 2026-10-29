# Changelog

## 2.0.1 — branding and additional Linux packages

- Restore the original Kotlin desktop cat artwork in the native window, sidebar,
  desktop/package icon and subtle chat watermark.
- Refresh the README gallery to show the restored branding.
- Add native Fedora 44 x86_64 RPM builds and clean installation/runtime tests.
- Add a Gentoo amd64 EAPI 8 ebuild/overlay, tested by Portage against the exact source.
- Gate automatic release uploads on every package target and verify the Gentoo source Manifest.
- Make the optional ALSA backend selectable as AUTO, ON or OFF for reproducible USE flags.
- Fix text-log worker lookup for newer Qt meta-object checks without changing queued log handling.

## 2.0.0 — native successor

- Replace the Kotlin/Compose desktop implementation with C++20 and Qt 6 Widgets.
- Keep the existing desktop repository history, v1 tags and Kotlin legacy branch.
- Include multi-server IRC, split panes, formatted messages, local history,
  themes, notifications, proxies, previews and uploads.
- Include the reviewed connection, authentication, identity, worker-lifetime,
  persistence, settings and UI repairs and their existing regression fixtures.
- Portable exports omit credentials, API/channel keys and saved commands, and
  disable auto-connect. Earlier backups and local databases can contain secrets.
- Use GPL-3.0-or-later for the native successor, retaining the earlier Kotlin
  MIT copyright/license notice. Earlier releases retain their original license.
- Add native source-check CI and public build, migration and limitation notes.
- Build preview AppImage/deb artifacts on Debian 12, retain matching source/checksums
  and dependency notices, and smoke-test each package on clean Debian 12/Ubuntu 24.04.

No complete v1 feature-parity claim, performance speedup figure, cross-platform
certification or self-contained installer is implied. This entry describes
source changes. Branch/PR packages are previews; version tags publish binaries
automatically only after all native and package checks succeed.

## Kotlin v1.x

Earlier releases, including `v1.0.30`, remain in the repository's release/tag
history. `legacy/kotlin-v1` identifies the last published Kotlin source revision.
