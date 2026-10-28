# Kotlin v1 to native v2

LoungeCat Desktop v2 is the native C++20 / Qt 6 successor in the existing
`binkiewka/LoungeCat-Desktop` repository. Kotlin v1 source and tags remain in
Git history. `legacy/kotlin-v1` preserves published commit
`e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0` (v1.0.30). It does not include later,
uncommitted local Kotlin edits. Android is separate and is not published here.

## Profiles and rollback

Both implementations default to `~/.loungecat/` on Linux. Do not treat this as
proof of complete Kotlin-to-native profile compatibility. Existing additive
database migrations and historical cache handling are implemented, but a full
end-user v1-to-v2 upgrade/downgrade path is not certified.

1. Close the Kotlin application and retain a private backup of the whole old
   profile before trying v2. Raw profiles/databases and older exports can contain
   credentials; do not put them in Git or attach them to issues.
2. Start v2 with a separate `LOUNGECAT_DATA_DIR`, as shown in [BUILD.md](../BUILD.md).
   Leave `LOUNGECAT_DB_PATH` and `LOUNGECAT_CONFIG` unset so they do not point back
   at old files. Configure a fresh profile first; do not copy a live database.
3. Do not run both versions against the same profile. Sharing data files is not
   a supported way to migrate or to run versions side by side.
4. If rolling back, close v2 and use the retained Kotlin executable and original
   untouched profile/backup. Opening a v2-modified profile in v1 is not certified.

Current native portable exports deliberately omit secrets and disable
auto-connect. Re-enter passwords, channel keys, API keys and required commands
after import. Matching JSON fields do not certify cross-version import parity.

## Licensing

The native successor is GPL-3.0-or-later. Earlier Kotlin revisions remain MIT;
their original grant is retained under `third_party/licenses/`. Tags and prior
published releases are not rewritten. Dependency licenses remain separate.

## Publication boundary

The migration branch and review PR are source preparation, not a release or a
default-branch replacement. Native source checks replace the Kotlin release
workflow on this branch. Installer packaging, final release validation and the
`v2.0.0` release/tag are separate steps.
