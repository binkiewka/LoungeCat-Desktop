# Third-party components

LoungeCat C++ application source is GPL-3.0-or-later; see [LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT). This does not replace these dependencies' licenses or their redistribution obligations.

- **Earlier LoungeCat Kotlin desktop source/reference material**: MIT; retain [the original copyright and grant](third_party/licenses/LoungeCat-Kotlin-MIT.txt). The GPL grant for the native successor does not retroactively relicense earlier MIT revisions.
- **Qt 6**: Core, Gui, Widgets, Network, Sql, Concurrent, Svg and QtTest for tests. Follow the licenses/notices for the exact Qt build and modules shipped; see [Qt open-source licensing](https://www.qt.io/licensing/open-source-lgpl-obligations) and the module source LICENSES directories. Dynamic linkage is not by itself a complete redistribution checklist.
- **ALSA** (optional on Linux): linked through the system library when available; retain the applicable [alsa-lib LGPL notices](https://github.com/alsa-project/alsa-lib).
- **TLS/SQLite and platform dependencies**: supplied by the chosen Qt/system build. Identify the exact OpenSSL/other TLS backend and SQLite distribution before packaging binaries, and include their notices as applicable.
- The repository's `tests/fixtures/localhost-key.pem` and `tests/fixtures/paste-local-key.pem` is a deliberately public, synthetic local test identity, not a production private key.

No implementation code from the GPL/AGPL clients used in the source comparison was copied into LoungeCat. The CMake link graph is the source of truth; this is a component guide, not an exhaustive binary-license compliance certification.
