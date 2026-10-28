# Defensive robustness repairs — 2026-10-18

## Libera launch-path follow-up

The original repair verification used an out-of-tree executable. The usual `build/loungecat` executable was still an older build; it has now been replaced atomically with the verified repair executable, with the previous binary retained in the separate Libera follow-up evidence directory. Fully quit the app (including its tray process) and relaunch to use it.

`liberaStyleTlsRegistration` now tests the actual Qt encrypted-socket path with peer verification enabled, Libera-style pre-registration notices/capabilities, and both SASL-disabled and SASL-PLAIN registration. Its public localhost-only certificate/key are disposable fixtures, not credentials. Existing plain-TCP fixtures did not cover this path. No production certificate checks or personal settings were changed.

This change set addresses the 25 architecture/code-quality findings in the local audit. Scope: C++/Qt ownership, resource bounds, persistence, error handling, thread confinement, and UI state. Existing preview URL, content, proxy, and TLS safeguards are retained. No exploit demonstrations or real-server testing are required.

## Runtime contracts

- **Owner thread:** connection manager, IRC clients, Qt network objects, cache APIs/timers, log submission, UI models, settings mutations, and dictionary language changes belong to their creating GUI thread. SQLite connections are never passed into workers; foreign-thread CRUD calls fail before accessing Qt SQL.
- **Snapshots:** settings readers receive synchronized values, not references. Spelling workers receive immutable dictionary/text snapshots; revision checks discard results for an edited document. Network callbacks capture owned values and guarded QObjects, with connection generations checked before submitting.
- **Workers:** image cache has two owned workers; upload encoding, logs, spelling, autocorrection, audio, and system information each have bounded owned pools. Worker closures do not dereference widgets or service owners. Owners join their workers during destruction. A stalled filesystem/driver call cannot be forcibly interrupted safely; no hard real-time shutdown guarantee is made.
- **Send state:** a refused send retains the editor. An accepted echo-capable send is pending until the matching self echo supplies the authoritative ID; expiry/disconnection marks it failed. Without echo support, a successful socket admission is labelled submitted, not delivered. These are not exactly-once or remote-recipient read guarantees.
- **History:** memory pruning never declares the smaller window authoritative on disk. Saves merge by message ID, retain up to 2,000 messages, and replace files atomically. Failed writes retain dirty data with an explicit error. Normal startup/history loads and debounced writes use workers; explicit synchronous access/flush, shutdown, and casemapping transitions may wait for storage.
- **Drafts:** each pane retains conversation-specific text, reply, cursor and history position. Closed conversations release retained pane/manager state. Draft admission is limited to 64 KiB; rejected edits leave the last accepted text. Drafts are in-memory, not a crash-recovery store.

## Resource limits and backpressure

| Resource | Limit / response |
| --- | --- |
| Servers / conversations | 16 servers, 128 conversations total; reject additional admission visibly |
| UI history | 10,000 messages / 16 MiB content-and-sender budget across conversations, plus bounded metadata |
| Cache working state | 32 channels; clean windows up to 500 messages / approximately 1 MiB each; dirty admission bounded to approximately 32 MiB / 16,000 messages |
| Durable cache | 2,000 messages per channel, 32 MiB per JSON file; refuse unsafe replacement and preserve originals |
| IRC receive work | 64 KiB socket buffer, 16 KiB line limit, 128 lines per event-loop turn |
| IRC batches | 16 active, 500 messages each, aggregate 1 MiB accounted content, 30-second expiry |
| Rosters | 128 channels, 8,192 live users plus 8,192 pending snapshot users; snapshots expire |
| LIST / WHOIS | 2,000 LIST rows; 200 pending/cached WHOIS entries, bounded channel lists and pending expiry |
| Send continuation / echo | 32 outstanding input pipelines; 64 pending self echoes; stale generations cannot submit |
| Previews | Existing active-request caps; 64 pending HTML requests and 64 pending images; 10-second total deadlines; bounded failure cooldown |
| Image disk cache | Disposable image files only: 256 files, 128 MiB, 30 days; cleanup errors reported |
| Upload preparation | Two admitted uploads, one encoder; files at most 8 MiB, clipboard bitmap at most 32 MiB, bounded response and total deadline |
| Text logs | One writer, 64 queued records; 2 MiB segments plus three archives; 512 files / 128 MiB total |
| Audio | One owned worker; busy alerts coalesce; nonblocking partial-write/recovery and bounded drain polling |

These are subsystem budgets, not a claim about the whole process RSS. Qt/plugin allocations and temporary parsing/encoding copies also consume memory. Existing historical files are never deleted to meet a log quota: logging stops with an explicit error until the owner archives logs. Only disposable image-cache data is automatically evicted.

## Migration and compatibility

Cache filenames use a digest of the same IRC-canonical identity as the memory key. Server CASEMAPPING is honoured for cache identity. Legacy files are accepted only when their embedded server/channel matches, merged by message ID, and retained as backups. A legacy collision that already overwrote another channel cannot reconstruct the missing history. Corrupt canonical files are preserved and reported rather than replaced.

Reply/account/reaction/preview/delivery metadata uses the canonical message serializer. Query nickname changes move drafts, local histories, selection, typing state, and cached-history aliases. A conflicting existing query is kept separately and reported.

Legacy log folders are renamed only when there is no destination collision. If both versions exist, both remain untouched and an explicit error requests manual reconciliation; no append-merge/retry duplication is attempted.

Settings/backup imports validate schema version, required structures, field types, enums, and numeric ranges before applying anything. Invalid imports do not reset settings. I/O failures are surfaced; settings are published only after a successful atomic save. A backup import spans settings and SQLite, not a single cross-file transaction: runtime failures must be reported, and partial import is not represented as a complete success.

## Finding-to-repair evidence

Tests below are in `tests/test_loungecat_repairs.cpp`; the existing 16-method core suite is retained.

| Finding | Repair | Principal regression / evidence |
| --- | --- | --- |
| C-01 | Value captures and manager lifetime guards | `uploadFailureCallbackSurvivesCommandStackAndServerRemoval` |
| C-02 | Guarded client pointers, client-context timers, generation checks | upload lifecycle, `ownerDestructionJoinsPendingWorkers` |
| H-01 | Durable merge independent of memory pruning | `cachePruningPreservesDurableHistory` |
| H-02 | Digested canonical filenames, embedded identity validation | `cacheNamesDoNotCollide`, legacy migration |
| H-03 | Per-conversation editor/reply/cursor/history state | `draftsAndReplyContextsAreConversationScoped` |
| H-04 | Explicit admission, pending/submitted/failed/echoed state | offline draft and local echo tests |
| H-05 | Database readiness and affected-row checks before publishing state | phantom-record, failed-update and foreign-thread tests |
| H-06 | Initial-error backoff, registration and PONG deadlines | initial failure and deadline tests |
| H-07 | Bounded socket/line/batches/rosters/LIST/WHOIS and global manager state | admission/queue tests; static bound review; no flood test |
| H-08 | Matching-pane refresh, stable-ID model diffs, bounded document cache | persistent-index and unchanged-pane tests |
| H-09 | Size/dimension admission and owned asynchronous encoding | `uploadPreparationExecutesAwayFromUiThread`, ordinary file-read failure |
| M-01 | Complete message serialization and metadata upsert | `cachePreservesMetadata`, async live-metadata merge |
| M-02 | Consistent IRC casemap keys and validated legacy lookup | `cacheCanonicalCaseAndLegacyMigration` |
| M-03 | Strict validation plus defensive clamps at use sites | `backupValidationAndRoundTrip` |
| M-04 | Monotonic generations invalidate delayed setup/reconnect | delayed-setup and reconnect-cancellation tests |
| M-05 | NAMES deltas/tombstones, snapshot expiry | `namesSnapshotReconcilesInterleavedMembership` |
| M-06 | Query rename migrates retained identity/state | `nicknameChangeMovesQueryDraftAndHistory` |
| M-07 | Close/remove clears maps and retires cache workers/timers without deleting history | closed-query, retired-cache and clear-cancellation tests |
| M-08 | Atomic writes, durable-before-publish preferences, surfaced export/database/log errors | failed-write/snapshot tests; UI error-path review |
| M-09 | Collision-preserving atomic folder rename | `legacyLogCollisionPreservesBothCopies` |
| M-10 | Disposable-cache eviction; non-destructive bounded log rotation | image quota and log rotation tests |
| M-11 | Owned image/dictionary/spelling/sysinfo/history workers | async image reload, snapshot, worker teardown tests; sysinfo statically reviewed |
| M-12 | Single coalescing owned audio pool; bounded ALSA write/recovery/drain | busy-worker test; physical audio intentionally untested |
| D-01 | Explicit confinement and synchronized value snapshots | foreign-thread DB rejection and spelling snapshot tests |
| D-02 | UI library regression target, minimum Qt, warning/sanitizer presets | build/CTest receipts; isolated main-window render |

## Building and checking

Qt **6.4 or newer** is required; verified locally with Qt **6.4.2** and GCC **13.3**. This is not a claim of validation on every Qt minor version.

```sh
cmake --preset debug
cmake --build --preset debug -j2
ctest --preset debug
cmake --preset asan
cmake --build --preset asan -j2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --preset asan
```

The `tsan` preset is available, but this host's TSan runtime fails even a minimal threaded startup check with `unexpected memory mapping`. No TSan pass or proof of race freedom is claimed. ASan/UBSan do not replace race detection or live-device testing.

Tests create temporary profiles, set `LOUNGECAT_DATA_DIR` and an isolated cache path before creating QApplication, and use offscreen rendering. HTTP fixtures proxy only through loopback; IRC fixtures are local. No production credentials, public uploads, or personal chat history are needed. Set `LOUNGECAT_TEST_SCREENSHOT` to retain the UI-render fixture as a PNG.

The repair evidence directory outside the source tree contains the original-source archive, SHA-256 baseline, change patch, build/test receipts, sanitizer logs, and screenshot. The original audit is historical; its old defect-assertion fixture is superseded by the project regression suite.
