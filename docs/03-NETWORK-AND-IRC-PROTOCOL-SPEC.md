> Historical conversion plan/specification. Current implemented dependencies, connection contracts and verification are in ../README.md and reliability/README.md. Alternative libraries and old fixed-delay behavior below are not current runtime guarantees.

> Historical conversion design, not the current feature/verification contract. See [current README](../README.md) and [v2 migration notes](MIGRATION-v2.md).

# 03 — Network, IRC Protocol, Slash Commands & External Services Specification

This specification details the networking layer, IRC protocol state machine, TLS/proxy bridging, auto-reconnect timers, all 45+ slash commands, and external HTTP services ported from [`LoungeCat-Desktop`](https://github.com/binkiewka/LoungeCat-Desktop/tree/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0).

---

## 1. Abstract `ChatClient` Interface (`src/network/ChatClient.h`)

Ported from [`ChatClient.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/commonMain/kotlin/com/loungecat/irc/network/ChatClient.kt). Keeping `ChatClient` as an abstract `QObject` base class decouples `ConnectionManager` from the wire protocol and preserves clean extensibility for a future Matrix module.

### 1.1 Virtual Methods
```cpp
class ChatClient : public QObject {
    Q_OBJECT
public:
    explicit ChatClient(qint64 serverId, const ServerConfig& config, QObject* parent = nullptr);
    ~ChatClient() override = default;

    virtual void connectToServer() = 0;
    virtual void disconnectFromServer() = 0;
    virtual void joinChannel(const QString& channel) = 0;
    virtual void partChannel(const QString& channel, const QString& reason = {}) = 0;
    virtual void sendMessage(const QString& target, const QString& message) = 0;
    virtual void sendAction(const QString& target, const QString& action) = 0;
    virtual void sendRawCommand(const QString& command) = 0;
    virtual void changeNickname(const QString& newNick) = 0;
    virtual QString getNickname() const = 0;
    virtual bool isConnected() const = 0;
    virtual void startSilentWhois(const QString& nickname) {}
    virtual std::optional<WhoisInfo> getCachedWhois(const QString& nickname) const { return std::nullopt; }
```

### 1.2 `ChatEvent` Signals
Every `ChatEvent` sealed class member in [`ChatClient.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/commonMain/kotlin/com/loungecat/irc/network/ChatClient.kt#L20-L43) maps to a signal on `ChatClient`:
- `void connected(qint64 serverId)`
- `void disconnected(qint64 serverId, const QString& reason)`
- `void connectionError(qint64 serverId, const QString& message)`
- `void messageReceived(qint64 serverId, const IncomingMessage& message)`
- `void channelJoined(qint64 serverId, const QString& channel)`
- `void channelParted(qint64 serverId, const QString& channel)`
- `void userJoined(qint64 serverId, const QString& channel, const ChannelUser& user)`
- `void userParted(qint64 serverId, const QString& channel, const QString& nickname, const QString& reason)`
- `void userQuit(qint64 serverId, const QString& nickname, const QString& reason)`
- `void userKicked(qint64 serverId, const QString& channel, const QString& kickedUser, const QString& kicker, const QString& reason)`
- `void nicknameChanged(qint64 serverId, const QString& oldNick, const QString& newNick)`
- `void topicChanged(qint64 serverId, const QString& channel, const QString& topic, const QString& setBy)`
- `void userListUpdated(qint64 serverId, const QString& channel, const QList<ChannelUser>& users)`
- `void userModeChanged(qint64 serverId, const QString& channel, const QString& nickname, const QSet<UserMode>& modes)`
- `void serverInfoReceived(qint64 serverId, const QString& info)`
- `void latencyUpdated(qint64 serverId, qint64 latencyMs)`
- `void whoisUpdated(qint64 serverId, const QString& nickname, const WhoisInfo& info)`

---

## 2. `IrcClient` State Machine (`src/network/IrcClient.h/.cpp`)

Ported from ``IrcClient.kt`` (unpublished Kotlin working-copy reference) and ``SslSocksBridge.kt`` (unpublished Kotlin working-copy reference).

### 2.1 Socket, Proxy & TLS Pinning (`QSslSocket`)
In Kotlin, `SslSocksBridge` was needed because Kitteh IRC Client Library couldn't layer TLS over a JVM SOCKS5 proxy without resolving DNS locally. In **Qt 6**, `QSslSocket` natively supports layering TLS over `QNetworkProxy::Socks5Proxy` and `QNetworkProxy::HttpProxy` with remote hostname resolution (`QNetworkProxy::HostNameLookupCapability`).

1. **Proxy Configuration**:
   - `ProxyType::NONE`: `socket->setProxy(QNetworkProxy::NoProxy)`
   - `ProxyType::SOCKS5`: `QNetworkProxy(QNetworkProxy::Socks5Proxy, config.proxyHost, config.proxyPort, config.proxyUsername, config.proxyPassword)`
   - `ProxyType::HTTP`: `QNetworkProxy(QNetworkProxy::HttpProxy, config.proxyHost, config.proxyPort, config.proxyUsername, config.proxyPassword)`
2. **TLS Fingerprint Verification & TOFU**:
   - Connect `QSslSocket::sslErrors` and `QSslSocket::peerVerifyError`.
   - Compute SHA-256 of the leaf certificate DER bytes:
     ```cpp
     QByteArray der = socket->peerCertificate().toDer();
     QString hexFingerprint = QString::fromLatin1(
         QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex()
     ).toLower();
     ```
   - If `config.pinnedCertFingerprint` is non-empty:
     - Normalize expected fingerprint (strip `:` and spaces, lowercase).
     - If `hexFingerprint != expected`, abort connection and emit `connectionError("Certificate fingerprint mismatch! Expected ..., got ...")`.
     - If matched, ignore self-signed / hostname SSL errors and continue.
   - Else if `config.trustOnFirstUse` is `true`:
     - Accept certificate, store `hexFingerprint` into `DatabaseService` (`pinned_cert_fingerprint`), and emit a server message notifying the user that the certificate fingerprint was pinned (TOFU).

### 2.2 Registration, `CAP LS 302` & SASL Handshake
On socket connected (`beginRegistration()`):
1. Always send `CAP LS 302` to initiate IRCv3.2 capability negotiation.
2. Send `NICK <nickname>`
3. Send `USER <username> 0 * :<realName>` (where `<username>` defaults to `config.nickname` if empty, and `<realName>` defaults to `config.nickname` if empty).
4. **IRCv3 Capability Negotiation (`CAP LS 302`, `CAP REQ`, `CAP ACK`, `CAP NEW`, `CAP DEL`)**:
   - Handles multiline `CAP * LS * :<caps>` responses until the final `CAP * LS :<caps>` line arrives.
   - Automatically requests all supported IRCv3.1 / IRCv3.2 / IRCv3.3 capabilities advertised by the server:
     - `sasl` (when `config.useSasl` is enabled)
     - `message-tags`
     - `server-time`
     - `echo-message`
     - `batch`
     - `labeled-response`
     - `msgid`
     - `multi-prefix`
     - `away-notify`
     - `account-notify`
     - `account-tag`
     - `extended-join`
     - `chghost`
     - `setname`
     - `invite-notify`
     - `userhost-in-names`
     - `cap-notify`
     - `standard-replies`
     - `chathistory` / `draft/chathistory`
     - `draft/multiline` / `multiline`
5. **SASL `PLAIN` (400-byte Chunked) & `EXTERNAL` Handshake**:
   - When `CAP * ACK` includes `sasl`: sends `AUTHENTICATE PLAIN` (or `AUTHENTICATE EXTERNAL` if password is empty).
   - On `AUTHENTICATE +`: encodes `\0`-delimited `authzid\0authcid\0passwd` in Base64 and sends in 400-byte chunks (`AUTHENTICATE +` if the final chunk is exactly 400 bytes).
   - On `903` (`RPL_SASLSUCCESS`) or `904`/`905` (`ERR_SASLFAIL`): sends `CAP END`.

### 2.3 Welcome (`001`) Post-Connect Sequence
Upon receiving numeric `001` (`RPL_WELCOME`):
1. Mark `m_isConnected = true`, start the 30-second keepalive timer (`PING :keepalive`), and emit `connected(m_serverId)`.
2. **NickServ Authentication**:
   - If `!config.nickServPassword.isEmpty()`:
     - If `!config.nickServCommand.isEmpty()`, replace `{password}` in `config.nickServCommand` and send raw.
     - Otherwise send `PRIVMSG NickServ :IDENTIFY <nickServPassword>`.
3. **On-Connect Commands** (delayed 500 ms via `QTimer::singleShot`):
   - Split `config.onConnectCommands` by `\n`, trim each line, strip leading `/` if present, and send each non-empty line via `sendRawCommand()`.
4. **Auto-Join Channels** (delayed 500 ms after on-connect commands):
   - Split `config.autoJoinChannels` by `,`, trim each entry, prepend `#` if it doesn't start with `#` or `&`, and call `joinChannel()`.

### 2.4 Nickname Collision (`433 ERR_NICKNAMEINUSE`)
 Ported from ``IrcClient.kt`` (unpublished Kotlin working-copy reference):
- Track `m_nickAttempt`:
  - Attempt 1: If `!config.altNickname.isEmpty() && config.altNickname != currentNick`, try `config.altNickname`.
  - Attempt 2: If `currentNick` contains `` ` ``, replace `` ` `` with `_`.
  - Subsequent attempts: Append `_` to `currentNick` (up to 5 attempts).
- Send `NICK <newNick>` and emit a `SERVER` message to `* <serverName>`.

### 2.5 Complete Numeric & Command Dispatch Table

| IRC Command / Numeric | Handler Behavior |
|---|---|
| `PING :token` | Immediately reply `PONG :token`. |
| `PONG` | Compute RTT from last keepalive `PING` timestamp and emit `latencyUpdated(serverId, rttMs)`. |
| `PRIVMSG <target> :<text>` | Check for CTCP `\x01...\x01`. If `\x01ACTION <act>\x01`, emit `MessageType::ACTION`. If other CTCP (e.g. `VERSION`), emit `MessageType::CTCP` and reply `NOTICE <sender> :\x01VERSION LoungeCat IRC Client\x01`. Otherwise emit `MessageType::NORMAL` (routing PMs where `target == currentNick` to channel `sender`). Respects `@time`, `@msgid`, `@account`, `@+reply` / `@+draft/reply`, `@batch`, and `echo-message`. |
| `NOTICE <target> :<text>` | Emit `MessageType::NOTICE` to `target` (or `* <serverName>` if target is `*` or `AUTH` or `currentNick` from server). |
| `TAGMSG <target>` | Handles IRCv3 client-only tags: `+typing` / `+draft/typing` (`active`, `paused`, `done`) -> `typingUserUpdated`, and `+draft/react` / `+react` with `+draft/reply` / `+reply` -> `reactionReceived`. |
| `BATCH <+/-ref> [type] [params...]` | Manages active IRCv3 batches (`chathistory` / `draft/chathistory` history replay with `isHistoryReplay = true`, and `draft/multiline` / `multiline` message concatenation). |
| `JOIN <channel> [account] [:realname]` | Supports both standard RFC 1459 `JOIN` and IRCv3 `extended-join`. If `sender == currentNick` and `chathistory` is enabled, automatically requests `CHATHISTORY LATEST <channel> * 50`. |
| `PART <channel> [:reason]` | If `sender == currentNick`, emit `channelParted`. Else remove user from channel roster and emit `userParted` + `MessageType::PART`. |
| `QUIT [:reason]` | For every channel containing `sender`, emit `MessageType::QUIT`, remove user, and emit `userQuit`. |
| `KICK <ch> <victim> [:reason]` | Emit `MessageType::KICK` and `userKicked`. If `victim == currentNick`, mark channel parted or notify user. |
| `NICK :<newNick>` | Update user key across all channel rosters, emit `MessageType::NICK` to shared channels, and emit `nicknameChanged`. |
| `AWAY [:message]` | IRCv3 `away-notify`: updates `ChannelUser::isAway` and `ChannelUser::awayMessage` across all shared channels. |
| `ACCOUNT <account>` | IRCv3 `account-notify`: updates `ChannelUser::account` (`*` clears account) across all shared channels. |
| `CHGHOST <newuser> <newhost>` | IRCv3 `chghost`: updates `ChannelUser::hostname` to `<newuser>@<newhost>` across all shared channels. |
| `SETNAME :<realname>` | IRCv3 `setname`: updates `ChannelUser::realname` across all shared channels. |
| `INVITE <nick> <channel>` | IRCv3 `invite-notify`: emits an informational `MessageType::SERVER` notification. |
| `FAIL` / `WARN` / `NOTE` | IRCv3 `standard-replies`: formats `<command> <code> [context] :<description>` to the server console. |
| `TOPIC <ch> :<topic>` | Update channel topic and emit `topicChanged` + `MessageType::TOPIC`. |
| `MODE <target> <modes> [args...]` | Parse `+`/`-` mode flags (`q`, `a`, `o`, `h`, `v` mapped to `UserMode::OWNER`, `ADMIN`, `OP`, `HALFOP`, `VOICE`), update `ChannelUser::modes`, emit `userModeChanged` and `MessageType::MODE`. |
| `001`–`005`, `250`–`255`, `265`, `266` | Emit `MessageType::SERVER` to `* <serverName>`. `005` (`RPL_ISUPPORT`) also parses `KEY=VALUE` tokens into `m_isupport`. |
| `332` (`RPL_TOPIC`), `333` (`RPL_TOPICWHOTIME`) | Update channel topic / setter and emit `topicChanged`. |
| `353` (`RPL_NAMREPLY`), `366` (`RPL_ENDOFNAMES`) | Supports `multi-prefix` (all mode prefixes `~&@%+` parsed per user) and `userhost-in-names` (`nick!user@host`). On `366`, emits sorted `userListUpdated`. |
| `375`, `372`, `376` (`MOTD`) | Emit `MessageType::MOTD` to `* <serverName>`. |
| `322` (`RPL_LIST`), `323` (`RPL_LISTEND`) | Populate channel list dialog entries (`channel`, `userCount`, `topic`). |
| `301`, `311`, `312`, `313`, `317`, `318`, `319`, `330`, `671`, `307`, `314`, `369`, `378`, `379`, `276` | Accumulate fields into `m_pendingWhois[nickLower]`. On `318` (`RPL_ENDOFWHOIS`) or `369` (`RPL_ENDOFWHOWAS`), store in `m_whoisCache` (`MAX_WHOIS_CACHE_SIZE = 200`), emit `whoisUpdated`, and if not in `m_silentWhoisTargets`, emit formatted `MessageType::WHOIS` to current/server tab. |
| `400`–`599` (except `433`) | Format numeric error description and emit `MessageType::ERROR` to active/server tab. |

---

## 3. Multi-Server Orchestration (`ConnectionManager`)

Ported from ``DesktopConnectionManager.kt`` (unpublished Kotlin working-copy reference).

### 3.1 Auto-Reconnect & Exponential Backoff
When a server disconnects unexpectedly (`!conn.manuallyDisconnected`):
1. Increment `reconnectAttempt` (0-indexed on first retry).
2. Calculate delay:
   - `attempt == 0`: `5,000 ms` (5s)
   - `attempt == 1`: `15,000 ms` (15s)
   - `attempt == 2`: `30,000 ms` (30s)
   - `attempt == 3`: `60,000 ms` (1m)
   - `attempt >= 4`: `300,000 ms` (5m)
3. Set `ConnectionState::RECONNECTING` and post a `SERVER` message to `* <serverName>` announcing the retry delay.
4. **Stability Reset (`120,000 ms`)**: Once a connection remains `CONNECTED` for 120 seconds continuously, reset `reconnectAttempt = 0`.

### 3.2 Outgoing Message Pipeline
When the user submits text in `ChatInputWidget`:
1. Record input in per-channel history deque (capped at `preferences.inputHistorySize`, default `50`).
2. Check if input starts with `/`:
   - Parse with `IrcCommandParser::parse(input)`.
   - Execute command (see Section 4).
3. For regular messages:
   - **URL Shortening**: If `preferences.urlShorteningEnabled`, find any URL with length `>= preferences.urlShorteningThreshold` (default 200) and replace it asynchronously via `UrlShortenerService::shortenUrl()`.
   - **Pastebin Auto-Upload**: If `preferences.pastebinEnabled` and (`message.length() > preferences.pastebinThreshold` or `message.count('\n') > 5`), upload via `PastebinService::upload()` and send `[Pastebin] <url> (<lines> lines, <chars> chars)` instead.
   - Otherwise, if multi-line (`<= 5` lines), split by `\n` and send each non-empty line via `client->sendMessage(target, line)`.

---

## 4. Complete Slash Command Reference (`IrcCommandParser`)

Ported from [`IrcCommandParser.kt`](https://github.com/binkiewka/LoungeCat-Desktop/blob/e0c82bf8f4358be8c48cfe3d39ffc6fa1a0a55f0/shared/src/commonMain/kotlin/com/loungecat/irc/util/IrcCommandParser.kt). All 45+ commands and aliases must be supported identically:

| Command | Aliases | Arguments & Behavior |
|---|---|---|
| `/join` | `/j` | `<#channel> [key]` — Joins channel (prepends `#` if missing). |
| `/part` | `/leave`, `/p` | `[#channel] [reason]` — Parts specified or current channel. |
| `/close` | `/wc` | `[target]` — Parts channel or closes PM/query tab. |
| `/quit` | `/disconnect`, `/exit` | `[reason]` — Disconnects from current server (`manuallyDisconnected = true`). |
| `/msg` | `/privmsg`, `/m`, `/q` | `<target> <message>` — Sends private/channel message and opens query tab if PM. |
| `/query` | `/pm` | `<nickname> [message]` — Opens private message tab with `nickname` and optionally sends message. |
| `/me` | `/action` | `<action>` — Sends CTCP `\x01ACTION <action>\x01`. |
| `/notice` | `/n` | `<target> <message>` — Sends `NOTICE <target> :<message>`. |
| `/nick` | — | `<newNickname>` — Sends `NICK <newNickname>`. |
| `/whois` | `/wi`, `/wii` | `<nickname>` — Sends `WHOIS <nickname>` (`/wii` sends `WHOIS <nick> <nick>` for idle time). |
| `/whowas` | `/ww` | `<nickname>` — Sends `WHOWAS <nickname>`. |
| `/who` | — | `<mask>` — Sends `WHO <mask>`. |
| `/topic` | `/t` | `[#channel] [newTopic]` — Views or sets channel topic. |
| `/kick` | `/k` | `<nickname> [reason]` — Sends `KICK <currentChannel> <nickname> :<reason>`. |
| `/ban` | `/b` | `<maskOrNick>` — Sends `MODE <currentChannel> +b <maskOrNick>`. |
| `/unban` | — | `<maskOrNick>` — Sends `MODE <currentChannel> -b <maskOrNick>`. |
| `/kickban` | `/kb` | `<nickname> [reason]` — Sends `MODE +b` followed by `KICK`. |
| `/voice` | `/v` | `<nickname>` — Sends `MODE <currentChannel> +v <nickname>`. |
| `/devoice` | `/dv` | `<nickname>` — Sends `MODE <currentChannel> -v <nickname>`. |
| `/op` | — | `<nickname>` — Sends `MODE <currentChannel> +o <nickname>`. |
| `/deop` | `/dop` | `<nickname>` — Sends `MODE <currentChannel> -o <nickname>`. |
| `/halfop` | `/hop` | `<nickname>` — Sends `MODE <currentChannel> +h <nickname>`. |
| `/dehalfop` | `/dhop` | `<nickname>` — Sends `MODE <currentChannel> -h <nickname>`. |
| `/quiet` | `/mute` | `<maskOrNick>` — Sends `MODE <currentChannel> +q <maskOrNick>`. |
| `/unquiet` | `/unmute` | `<maskOrNick>` — Sends `MODE <currentChannel> -q <maskOrNick>`. |
| `/mode` | — | `[target] <modes>` — Sends raw `MODE` command. |
| `/invite` | `/inv` | `<nickname> [#channel]` — Sends `INVITE <nickname> <channel>`. |
| `/away` | — | `[message]` — Sets or clears away status (`AWAY :<message>`). |
| `/back` | — | Clears away status (`AWAY`). |
| `/raw` | `/quote` | `<ircCommand>` — Sends raw line directly to server. |
| `/list` | — | `[filter]` — Sends `LIST` and opens `ChannelListDialog`. |
| `/names` | — | `[#channel]` — Sends `NAMES <channel>`. |
| `/Ignore` | `/ignore` | `<nickname>` — Adds lowercase nick to `preferences.ignoredUsers`. |
| `/unignore` | — | `<nickname>` — Removes nick from `preferences.ignoredUsers`. |
| `/ignorelist` | `/ignores` | Prints current ignored users list to active chat. |
| `/clear` | `/cls` | Clears messages in current channel and deletes its disk cache file. |
| `/clearall` | — | Clears messages across all channels on current server. |
| `/cycle` | `/rejoin`, `/hop` (no args) | Parts and immediately rejoins current channel. |
| `/ping` | `/lag` | Sends `PING` to measure server latency. |
| `/time` | — | Sends `TIME` to server. |
| `/version` | `/ver` | `[target]` — Sends CTCP `VERSION` or server `VERSION`. |
| `/motd` | — | Requests server `MOTD`. |
| `/lusers` | — | Requests server `LUSERS`. |
| `/links` | — | Requests server `LINKS`. |
| `/stats` | — | `[query]` — Requests server `STATS`. |
| `/trace` | — | `[target]` — Requests server `TRACE`. |
| `/admin` | — | Requests server `ADMIN` info. |
| `/info` | — | Requests server `INFO`. |
| `/servlist` | — | Lists currently configured/connected servers in chat. |
| `/connect` | `/server` | `<hostname> [port] [+ssl]` — Quick-connects to a server. |
| `/reconnect` | — | Disconnects and reconnects current server. |
| `/ns` | `/nickserv` | `<command...>` — Sends `PRIVMSG NickServ :<command>`. |
| `/cs` | `/chanserv` | `<command...>` — Sends `PRIVMSG ChanServ :<command>`. |
| `/ms` | `/memoserv` | `<command...>` — Sends `PRIVMSG MemoServ :<command>`. |
| `/os` | `/operserv` | `<command...>` — Sends `PRIVMSG OperServ :<command>`. |
| `/hs` | `/hostserv` | `<command...>` — Sends `PRIVMSG HostServ :<command>`. |
| `/bs` | `/botserv` | `<command...>` — Sends `PRIVMSG BotServ :<command>`. |
| `/slap` | — | `<nickname>` — Sends `/me slaps <nickname> around a bit with a large trout`. |
| `/shrug` | — | `[message]` — Appends `¯\_(ツ)_/¯` and sends message. |
| `/tableflip` | — | `[message]` — Appends `(╯°□°）╯︵ ┻━┻` and sends message. |
| `/unflip` | — | `[message]` — Appends `┬─┬ ノ( ゜-゜ノ)` and sends message. |
| `/lenny` | — | `[message]` — Appends `( ͡° ͜ʖ ͡°)` and sends message. |
| `/sysinfo` | — | Collects system hardware/OS stats via `SystemInfoCollector` and sends to current channel. |
| `/uploadimage` | `/img` | `[filePath]` — Uploads image from path or system clipboard via `ImgBbService` and inserts URL. |
| `/help` | `/commands`, `/?` | `[command]` — Opens `HelpDialog` or prints command usage in chat. |

---

## 5. External HTTP Services (`src/service/`)

All HTTP requests use `QNetworkAccessManager` with a 10-second transfer timeout (`QNetworkRequest::setTransferTimeout(10000)`).

### 5.1 `LinkPreviewService`
Ported from ``LinkPreviewService.kt`` (unpublished Kotlin working-copy reference):
- **Privacy Gate**: Only fetch previews if `message.isSelf || preferences.processLinkPreviewsFromOthers || preferences.trustedPreviewUsers.contains(message.sender)`.
- **Proxy Routing**: If `preferences.useProxyForPreviews` is `true` and the server has a `SOCKS5` or `HTTP` proxy configured, apply that `QNetworkProxy` to the request's `QNetworkAccessManager`.
- **Direct Image Detection**: URLs ending in `.jpg`, `.jpeg`, `.png`, `.gif`, `.webp`, `.bmp`, or `.svg` (ignoring query parameters) immediately return `UrlPreview(url, isImage = true)` without scraping HTML.
- **YouTube / Vimeo oEmbed**:
  - YouTube URLs (`youtube.com/watch?v=...`, `youtu.be/...`, `youtube.com/shorts/...`) query `https://www.youtube.com/oembed?url=<encodedUrl>&format=json` and extract `title`, `author_name`, `thumbnail_url`.
  - Vimeo URLs query `https://vimeo.com/api/oembed.json?url=<encodedUrl>`.
- **General OpenGraph Scraping**:
  - Download at most the first `65,536` bytes (`MAX_HTML_BYTES = 64 * 1024`) of `text/html` responses (`Range: bytes=0-65535` + abort on `readyRead` if size exceeds 64 KB).
  - Extract `og:title` (fallback `<title>`), `og:description` (fallback `<meta name="description">`), `og:image`, and `og:site_name`.
- **Concurrency & Caching**:
  - Limit active network fetches to `MAX_CONCURRENT_URL_PREVIEWS = 3`.
  - Cache up to `200` `UrlPreview` results in memory.

### 5.2 `PastebinService`
- Endpoint: `POST https://0x0.st`
- Format: `QHttpMultiPart::FormDataType` with part `"file"; filename="paste.txt"` (`text/plain; charset=utf-8`).
- Header: `User-Agent: LoungeCat-IRC/1.0`.
- Returns trimmed response body URL.

### 5.3 `ImgBbService`
- Endpoint: `POST https://api.imgbb.com/1/upload`
- Form fields (`application/x-www-form-urlencoded`): `key=<preferences.imgbbApiKey>` and `image=<base64EncodedPngOrFileBytes>`.
- Parses JSON response `data.url` (or `data.display_url`).

### 5.4 `UrlShortenerService`
- Endpoint: `GET https://is.gd/create.php?format=simple&url=<percentEncodedUrl>`
- Returns shortened URL if HTTP 200 and starts with `http`, otherwise falls back to original URL.

### 5.5 `UpdateCheckService`
- Endpoint: `GET https://api.github.com/repos/binkiewka/LoungeCat-Desktop/releases/latest`
- Interval: Immediate check on startup + every 30 minutes (`QTimer`).
- Parses `tag_name` (stripping leading `v`), compares `(major, minor, patch)` tuple against `APP_VERSION`, and emits `updateAvailable(VersionInfo)` if newer.
