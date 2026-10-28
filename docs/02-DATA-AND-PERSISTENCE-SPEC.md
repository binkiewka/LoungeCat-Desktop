> Historical conversion plan/specification. Current implemented dependencies, connection contracts and verification are in ../README.md and reliability/README.md. Alternative libraries and old fixed-delay behavior below are not current runtime guarantees.

> Historical conversion design, not the current feature/verification contract. See [current README](../README.md) and [v2 migration notes](MIGRATION-v2.md).

# 02 — Data Models, Persistence & File Format Specification

This document specifies every data structure, SQLite table, JSON file format, and plaintext log convention required for **100% drop-in compatibility** with existing `~/.loungecat/` user directories created by [`LoungeCat-Desktop`](https://github.com/binkiewka/LoungeCat-Desktop/tree/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0).

---

## 1. Application Data Directory & Environment Overrides

Ported from [`PlatformUtils.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/desktopMain/kotlin/com/loungecat/irc/util/PlatformUtils.kt) and ``Main.kt`` (unpublished Kotlin working-copy reference):

### 1.1 Base Directory Resolution (`PlatformUtils::resolveAppDataDirectory()`)
- **Linux**: `$HOME/.loungecat`
- **Windows**: `%APPDATA%/LoungeCat` (fallback `$HOME/.loungecat`)
- **macOS**: `$HOME/Library/Application Support/LoungeCat`

### 1.2 Environment Variable Overrides
| Environment Variable | Default Fallback | Purpose |
|---|---|---|
| `LOUNGECAT_DB_PATH` | `<appDataDir>/loungecat.db` | Custom path to SQLite server configurations database |
| `LOUNGECAT_CONFIG` | `<appDataDir>/preferences.json` | Custom path to user preferences JSON file |
| `HEADLESS` | `false` (`"true"` or `"1"` enables) | Maximizes main window on launch and skips `QSystemTrayIcon` creation |

### 1.3 Directory Tree Inside `~/.loungecat/`
```text
~/.loungecat/
├── app.lock                          # Single-instance advisory lock file
├── loungecat.db                      # SQLite 3 database (server_configs table)
├── preferences.json                  # UserPreferences JSON object
├── cache/
│   └── messages/
│       └── <serverId>_<safeName>.json # Per-channel message history (max 2000 messages)
├── themes/
│   ├── _theme_template.json.example  # Auto-generated reference template (ignored by loader)
│   └── <CustomTheme>.json            # Custom user themes
└── logs/
    └── <safeServerName>/
        └── <safeChannelName>.log     # Plaintext IRC chat logs
```

---

## 2. Single-Instance Lock (`app.lock`)

In ``Main.kt`` (unpublished Kotlin working-copy reference), the Kotlin app opens `~/.loungecat/app.lock` via `RandomAccessFile(lockFile, "rw").channel.tryLock()`.
- **C++ Implementation**: Use `QLockFile(appDataDir.filePath("app.lock"))` with `setStaleLockTime(0)` and `tryLock(100)`.
- If locking fails, display a native `QMessageBox::critical(nullptr, "LoungeCat Already Running", "Another instance of LoungeCat is already running.")` and exit with status `1`.

---

## 3. SQLite Database Schema (`loungecat.db`)

Ported from [`DatabaseService.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/desktopMain/kotlin/com/loungecat/irc/data/database/DatabaseService.kt).

> [!IMPORTANT]
> Even though the legacy Kotlin repository included Room dependencies, `DatabaseService.kt` uses direct JDBC SQLite (`jdbc:sqlite:...`) with a single `server_configs` table and does **not** create `room_master_table`. Therefore, C++ `Qt6::Sql` (`QSQLITE`) can read and write `loungecat.db` directly without any Room metadata overhead.

### 3.1 Table DDL (`server_configs`)
```sql
CREATE TABLE IF NOT EXISTS server_configs (
    id INTEGER PRIMARY KEY,
    server_name TEXT NOT NULL,
    hostname TEXT NOT NULL,
    port INTEGER NOT NULL DEFAULT 6697,
    use_ssl INTEGER NOT NULL DEFAULT 1,
    nickname TEXT NOT NULL,
    alt_nickname TEXT DEFAULT '',
    username TEXT,
    real_name TEXT,
    use_sasl INTEGER NOT NULL DEFAULT 0,
    sasl_username TEXT,
    sasl_password TEXT,
    auto_join_channels TEXT,
    on_connect_commands TEXT,
    nick_serv_password TEXT,
    nick_serv_command TEXT,
    pinned_cert_fingerprint TEXT,
    trust_on_first_use INTEGER DEFAULT 0,
    auto_connect INTEGER DEFAULT 1,
    proxy_type TEXT,
    proxy_host TEXT,
    proxy_port INTEGER,
    proxy_username TEXT,
    proxy_password TEXT
);
```

### 3.2 Automatic Schema Migration on Startup
On initialization, query `PRAGMA table_info(server_configs)` to collect existing column names (lowercased), then execute `ALTER TABLE server_configs ADD COLUMN <col_def>` for any missing column in this list:
1. `alt_nickname TEXT DEFAULT ''`
2. `use_sasl INTEGER NOT NULL DEFAULT 0`
3. `sasl_username TEXT`
4. `sasl_password TEXT`
5. `on_connect_commands TEXT`
6. `pinned_cert_fingerprint TEXT`
7. `trust_on_first_use INTEGER DEFAULT 0`
8. `auto_connect INTEGER DEFAULT 1`
9. `proxy_type TEXT`
10. `proxy_host TEXT`
11. `proxy_port INTEGER`
12. `proxy_username TEXT`
13. `proxy_password TEXT`

### 3.3 Column Mapping to `ServerConfig` (`src/data/model/ServerConfig.h`)

| SQLite Column | SQLite Type | C++ Field | Default Value | Serialization Rule |
|---|---|---|---|---|
| `id` | `INTEGER PRIMARY KEY` | `qint64 id` | `0` (or `std::chrono::system_clock::now()` ms if `0` on insert) | Note: In [`DatabaseService.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/desktopMain/kotlin/com/loungecat/irc/data/database/DatabaseService.kt#L121), `id = if (config.id == 0L) System.currentTimeMillis() else config.id` |
| `server_name` | `TEXT NOT NULL` | `QString serverName` | `""` | Direct string |
| `hostname` | `TEXT NOT NULL` | `QString hostname` | `""` | Direct string |
| `port` | `INTEGER NOT NULL` | `int port` | `6697` | Direct int |
| `use_ssl` | `INTEGER NOT NULL` | `bool useSsl` | `true` | `1` = `true`, `0` = `false` |
| `nickname` | `TEXT NOT NULL` | `QString nickname` | `""` | Direct string |
| `alt_nickname` | `TEXT` | `QString altNickname` | `""` | `NULL` mapped to `""` |
| `username` | `TEXT` | `QString username` | `""` | Empty string stored as `NULL`, `NULL` read as `""` |
| `real_name` | `TEXT` | `QString realName` | `""` | Empty string stored as `NULL`, `NULL` read as `""` |
| `use_sasl` | `INTEGER NOT NULL` | `bool useSasl` | `false` | `1` = `true`, `0` = `false` |
| `sasl_username` | `TEXT` | `QString saslUsername` | `""` | Empty string stored as `NULL`, `NULL` read as `""` |
| `sasl_password` | `TEXT` | `QString saslPassword` | `""` | Empty string stored as `NULL`, `NULL` read as `""` |
| `auto_join_channels` | `TEXT` | `QString autoJoinChannels` | `""` | Comma-separated string (e.g. `"#linux,#c++"`), empty stored as `NULL` |
| `on_connect_commands` | `TEXT` | `QString onConnectCommands` | `""` | Newline-separated commands, empty stored as `NULL` |
| `nick_serv_password` | `TEXT` | `QString nickServPassword` | `""` | Empty stored as `NULL` |
| `nick_serv_command` | `TEXT` | `QString nickServCommand` | `""` | Empty stored as `NULL` |
| `pinned_cert_fingerprint` | `TEXT` | `std::optional<QString> pinnedCertFingerprint` | `std::nullopt` | SHA-256 hex fingerprint |
| `trust_on_first_use` | `INTEGER` | `bool trustOnFirstUse` | `false` | `1` = `true`, `0` = `false` |
| `auto_connect` | `INTEGER` | `bool autoConnect` | `true` | `1` = `true`, `0` = `false` |
| `proxy_type` | `TEXT` | `ProxyType proxyType` | `ProxyType::NONE` | `"NONE"`, `"SOCKS5"`, `"HTTP"` (case-insensitive fallback to `NONE`) |
| `proxy_host` | `TEXT` | `QString proxyHost` | `""` | Empty stored as `NULL` |
| `proxy_port` | `INTEGER` | `int proxyPort` | `1080` | If `<= 0` when reading, default to `1080` |
| `proxy_username` | `TEXT` | `QString proxyUsername` | `""` | Empty stored as `NULL` |
| `proxy_password` | `TEXT` | `QString proxyPassword` | `""` | Empty stored as `NULL` |

---

## 4. User Preferences JSON (`preferences.json`)

Ported from [`UserPreferences.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/commonMain/kotlin/com/loungecat/irc/data/model/UserPreferences.kt) and ``PreferencesManager.kt`` (unpublished Kotlin working-copy reference).

### 4.1 Complete JSON Schema & Default Values

```json
{
  "fontSize": "MEDIUM",
  "timestampFormat": "HOURS_24",
  "urlImageDisplayMode": "INLINE",
  "coloredNicknames": true,
  "joinPartQuitMode": "SHOW_ALL",
  "smartHideMinutes": 10,
  "ignoredUsers": [],
  "activityIndicatorMinutes": 5,
  "timezone": null,
  "spellCheckEnabled": true,
  "spellCheckLanguage": "en_US",
  "theme": "Dark",
  "pastebinEnabled": true,
  "pastebinThreshold": 400,
  "highlightWords": [],
  "notificationsEnabled": true,
  "notifyOnMention": true,
  "notifyOnHighlight": true,
  "notifyOnPrivateMessage": true,
  "soundAlertsEnabled": false,
  "soundOnMention": true,
  "soundOnHighlight": true,
  "soundOnPrivateMessage": true,
  "soundVolume": 0.7,
  "splitViewEnabled": false,
  "inputHistorySize": 50,
  "loggingEnabled": false,
  "historyReplayLines": 50,
  "processLinkPreviewsFromOthers": false,
  "trustedPreviewUsers": [],
  "useProxyForPreviews": true,
  "imgbbApiKey": null,
  "urlShorteningEnabled": true,
  "urlShorteningThreshold": 200
}
```

### 4.2 Serialization & Compatibility Rules
- **Ignore Unknown Keys**: When reading `preferences.json`, ignore any unknown keys without failing.
- **Always Encode Defaults**: Write all fields on save using pretty-printed JSON (`QJsonDocument::Indented`), using `QSaveFile` for atomic replacement.
- **Enum String Values**:
  - `fontSize`: `"SMALL"` (multiplier `0.85f`), `"MEDIUM"` (`1.0f`), `"LARGE"` (`1.15f`)
  - `timestampFormat`: `"HOURS_12"`, `"HOURS_24"`, `"RELATIVE"`
  - `urlImageDisplayMode`: `"INLINE"`, `"COMPACT"`, `"DISABLED"`
  - `joinPartQuitMode`: `"SHOW_ALL"`, `"HIDE_ALL"`, `"GROUPED"`, `"SMART_HIDE"`
- **Set Normalization**:
  - `ignoredUsers` and `highlightWords` are stored as JSON arrays of lowercase strings (`user.trimmed().toLower()`).
  - `trustedPreviewUsers` preserves trimmed strings in a `QSet<QString>` (matched case-insensitively at runtime).

---

## 5. Message Cache JSON (`~/.loungecat/cache/messages/`)

Ported from [`MessageCache.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/desktopMain/kotlin/com/loungecat/irc/data/cache/MessageCache.kt).

### 5.1 Filename Convention
Given `qint64 serverId` and `QString channelName`:
1. Replace every character in `channelName` matching `[^a-zA-Z0-9_#-]` with `_`:
   ```cpp
   QString safeName = channelName;
   safeName.replace(QRegularExpression("[^a-zA-Z0-9_#-]"), "_");
   QString fileName = QString("%1_%2.json").arg(serverId).arg(safeName);
   ```
2. Path: `~/.loungecat/cache/messages/<serverId>_<safeName>.json`.

### 5.2 JSON Structure (`CachedChannel` & `CachedMessage`)
```json
{
  "serverId": 1712345678901,
  "channelName": "#linux",
  "topic": "Welcome to #linux",
  "messages": [
    {
      "id": "b8c4e1a0-54f2-4a1b-9c3d-123456789abc",
      "target": "#linux",
      "sender": "alice",
      "content": "Hello everyone!",
      "timestamp": 1712345680000,
      "type": "NORMAL",
      "isSelf": false
    }
  ]
}
```

### 5.3 Caching & Pruning Policies
- **Filtered Message Types**: Only cache messages whose `MessageType` is **not** `JOIN`, `PART`, `QUIT`, or `MOTD` (`shouldCacheMessage`).
- **Disk Retention (`MAX_MESSAGES_PER_CHANNEL = 2000`)**: Keep only the most recent 2,000 messages per channel file on disk.
- **In-Memory Retention (`MAX_IN_MEMORY_MESSAGES = 500`)**: Every 15 minutes (`PRUNE_INTERVAL_MS = 15 * 60 * 1000`), prune in-memory channel caches exceeding 500 messages down to the latest 500 messages.
- **Write Debouncing (`SAVE_DEBOUNCE_MS = 2000`)**: Each modified channel schedules a 2,000 ms single-shot timer before flushing its JSON to disk. On app exit (`flushSync()`), all pending channel caches are written immediately.
- **Replay on Join**: When joining a channel, load up to `preferences.historyReplayLines` (default `50`) cached messages into the UI before new live messages arrive, avoiding duplicate message IDs.

---

## 6. Plaintext Chat Logs (`~/.loungecat/logs/`)

Ported from [`TextLogService.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/desktopMain/kotlin/com/loungecat/irc/service/TextLogService.kt).

### 6.1 Sanitization & Path Resolution
- **Server Folder Name**:
  ```cpp
  QString safeServerName = serverName.trimmed();
  safeServerName.replace(QRegularExpression("[^a-zA-Z0-9.-]"), "_");
  while (safeServerName.endsWith('_')) safeServerName.chop(1);
  if (safeServerName.isEmpty()) safeServerName = "Unknown_Server";
  ```
  If an existing directory in `~/.loungecat/logs/` matches `safeServerName` case-insensitively, reuse that existing directory.
- **Channel File Name**:
  ```cpp
  QString safeChannelName = channelName.trimmed();
  safeChannelName.replace(QRegularExpression("[^a-zA-Z0-9#._-]"), "_");
  while (safeChannelName.endsWith('_')) safeChannelName.chop(1);
  if (safeChannelName.isEmpty()) safeChannelName = "unknown_channel";
  ```
  Match existing `<safeChannelName>.log` case-insensitively if present.

### 6.2 Log Line Format
Only `MessageType::NORMAL` and `MessageType::ACTION` are written when `preferences.loggingEnabled == true`:
```text
[2026-10-18 14:32:05] <nickname> Message text here
```
- Timestamp format: `yyyy-MM-dd HH:mm:ss` in local system time.

### 6.3 Legacy Folder Migration (`migrateLegacyFolders()`)
On startup, scan `~/.loungecat/logs/` for directories ending with `_`. For each `Name_`:
- Target clean name is `Name` (trailing `_` stripped).
- Merge files from `Name_` into `Name` (appending content if a log file exists in both), then rename `Name_` to `Name_bak`.

---

## 7. Backup Export / Import (`ConfigExporter`) & Chat Export (`ChatExporter`)

### 7.1 Configuration Backup JSON (`ConfigExporter.kt`)
```json
{
  "version": 1,
  "timestamp": 1712345678901,
  "preferences": { ... },
  "servers": [ ... ]
}
```
On import:
1. Update `PreferencesManager` with `backup.preferences`.
2. For each `ServerConfig` in `backup.servers`, insert into `DatabaseService` with `id = 0` (generating a fresh ID) if no existing server has both the same `hostname` and `nickname`.

### 7.2 Chat History Export (`ChatExporter.kt`)
- **JSON Format**: Pretty-printed JSON array of `IncomingMessage` objects.
- **TXT Format**:
  ```text
  Chat History for #channel
  Exported on Mon Oct 18 14:32:05 CEST 2026
  ----------------------------------------

  [2026-10-18 14:30:01] <alice> Hello!
  [2026-10-18 14:30:10] * bob waves
  [2026-10-18 14:30:15] -Server- *** charlie has joined #channel
  [2026-10-18 14:30:20] [System] Topic is: Welcome
  ```
