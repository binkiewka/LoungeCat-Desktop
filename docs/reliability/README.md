# Native connection reliability repairs — 2026-10-23

## Result

C-01, H-01/H-02/H-05/H-06/H-07, M-02/M-03/M-04/M-05 and D-01 are repaired in source and covered by local regression checks. D-02 documentation, CI source and licensing are also resolved: on the owner's delegated choice, the application is **GPL-3.0-or-later**, with the complete license and explicit grant in the project root. All 16 original review findings are closed within the locally verified scope; hosted CI, physical-network and release gates below are separate.

## Policy and transport ownership

- `ConnectionPolicy` owns desired/paused/dialing/ready/attempt/stability decisions. `ConnectionManager` owns transport factories, Qt timer adapters and attempt generations. Connect is idempotent while dialing/ready; explicit Reconnect is separate. Automatic retries are canceled on success or manual stop; terminal pause is sticky. Stable readiness requires 120 seconds. A precise stability timer avoids a coarse timer firing before the threshold.
- Retry base steps are 5/15/30/60/300 seconds with 0.75–1.25 jitter, clamped to 1–300 seconds. The chosen delay is shown in the server conversation. The policy accepts time/random values explicitly for deterministic tests.
- Saved SASL/NickServ/on-connect actions and channel keys require TLS before dialing. Configuration/required-auth/certificate failures pause rather than redial indefinitely. SASL PLAIN must be offered and succeed (903/907) before readiness; EXTERNAL is not claimed without a client-certificate configuration.
- `IrcRegistration` keeps required/authenticated/failed/post-connect state. Confirmed nickname changes only on accepted server identity evidence; registered 433 retains identity. Initial collision gets one configured alternate, then asks for user repair, not an endless suffix ladder.
- Non-control output has a 256-line/128-KiB queue, 180-second age limit, four initial tokens, refill one per 500 ms, and a 256-KiB socket-write budget. PING/PONG/registration control stays prompt. Disconnect/error retirement clears pending data and fails pending sends; queue acceptance is not server delivery. The fixed rate is conservative, not a promise of compatibility with every network's flood policy.
- NickServ-only automation waits up to 15 seconds for standardized own-account evidence (900/own 330), using bounded WHOIS checks rather than notice text or a fixed sleep. Timeout keeps the registered connection but withholds joins/commands; manual identify/join remains possible.

## Identity and persistence

Negotiated ascii/strict-rfc1459/rfc1459 identity is used across protocol roster state, self comparisons, channel/query lookup, caches, sidebars, ignores and composer drafts. CASEMAPPING changes re-key/merge equivalent conversations while preserving unique messages and draft text. Roster snapshots remain silent; interleaved real membership changes are reconciled.

Structured `channelKeys` JSON survives config round trips and additive SQLite `channel_keys` migration. Manual keyed join remembers the key; self JOIN does not erase it; deliberate PART forgets it. No database reset or historical log deletion was introduced. Credential-bearing local database files are owner-only, not encrypted at rest. Current portable exports omit passwords, channel/API keys and saved commands and disable auto-connect; older backups may contain secrets. See the current root README for that later policy change.

## Measured verification

- **83 distinct QtTest data-row/case checks passed** across three executables: 16 core +48 existing defensive +19 new reliability checks. Setup/cleanup rows are excluded. Qt itself reports 89 PASS rows including six setup/cleanup rows.
- The same three executables passed again under **AddressSanitizer + UndefinedBehaviorSanitizer with leak detection enabled**, without suppressions. Repeated sanitizer runs are not counted as additional distinct cases. TSan was not run in this repair and no TSan result is claimed.
- Normal and sanitizer builds include the application and UI libraries. Tests include local TCP/TLS peers, rejected SASL/config/cert cases, paced64JOIN/control behavior, retry cancellation, confirmed nickname, silent roster snapshot, keyed database round trip, negotiated query identity/ignore matching and real offscreen composer draft retention.
- The new SHA-pinned read-only CI source builds normal/ASan matrix on push/PR. Its YAML was locally parsed; no hosted run or publication occurred.

Reproduce:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
cmake --preset asan
cmake --build --preset asan --parallel 2
ASAN_OPTIONS=detect_leaks=1 ctest --preset asan
```

## Verification limits and remaining gates

These repairs address the targeted source review; they do not prove absence of bugs or certify the whole application. All network tests used synthetic local peers. No normal profile, live IRC network, personal app installation, deployment or publication was used. No production signing key was used. Hosted CI has been prepared, not executed.

Before a public binary release, run a physical Wi-Fi/mobile/VPN handover and suspend/resume matrix, long-running reconnect/roster/history soak tests, explicit process-death behavior checks, and real bouncer interoperability/upstream connection-count checks. No dozens-of-ghost-identities or fake NAMES-as-JOIN storm was reproduced in the original review; do not advertise that a proven live incident was fixed. A direct client cannot prevent all IRC-visible churn during host outages: persistent bouncer/core architecture is the route to keeping the upstream session alive. Cache restoration does not recover messages never received.

Third-party notice files are a component guide, not a completed exhaustive binary/license compliance inventory. No GPL/AGPL implementation was copied from the comparison clients.
