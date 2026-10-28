# Native v2 qualification and known limitations

- Linux is the validated target. Windows/macOS, every Linux distribution and
  compositor, and full Kotlin feature parity are not certified.
- The current delivery is source and a buildable dynamically linked Qt executable;
  no AppImage, Flatpak or Debian package is included.
- Full Kotlin-profile upgrade/downgrade compatibility is not certified. Use a
  separate preview profile and keep private backups; see [migration](MIGRATION-v2.md).
- Existing native regression evidence covers 221 distinct checks in each of
  three configurations (Clang, ASan/UBSan and without ALSA). These are historical
  source-backed results, not new runs caused by repository preparation.
- The earlier 24-hour passive soak received 23,280/23,280 messages on its pinned
  binary. Five source paths changed later; it is not an exact-v2-build soak.
- Three previously reviewed Cppcheck manager-contract warnings remain documented;
  no warning suppression or all-tools-clean claim is made. Qt/runtime race-tool
  qualifications are not a general proof of race freedom.
- Actual screen-reader coverage and some broader compatibility/performance
  checks remain unqualified. No quantified speed or memory improvement is claimed.
- Native local database credentials are owner-only but not encrypted at rest.
  Current portable backups omit secrets; historical exports may contain them.
- Additional authentication mechanisms, notification privacy controls and
  installer packaging remain separate feature decisions.

The historical conversion designs under `docs/01` through `docs/05` describe
plans/reference mappings, not a promise that every proposed feature exists.
Historical repair notes retain their original measured scope and dates. The root
README describes current behavior; a complete all-features pass is not claimed.
