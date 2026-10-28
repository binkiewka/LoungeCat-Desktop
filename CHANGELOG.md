# Changelog

## 2.0.0 — unreleased native successor

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

No complete v1 feature-parity claim, performance speedup figure, cross-platform
certification or self-contained installer is implied. This entry describes
source preparation; no v2 binary release is published by this branch.

## Kotlin v1.x

Earlier releases, including `v1.0.30`, remain in the repository's release/tag
history. `legacy/kotlin-v1` identifies the last published Kotlin source revision.
