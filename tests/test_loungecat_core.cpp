#include <QtTest>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>

#include "util/IrcFormatParser.h"
#include "util/IrcCommandParser.h"
#include "util/NickColorGenerator.h"
#include "util/HighlightMatcher.h"
#include "util/ChatGrouping.h"
#include "util/TabCompletion.h"
#include "util/SpellChecker.h"
#include "data/database/DatabaseService.h"
#include "data/preferences/PreferencesManager.h"
#include "data/cache/MessageCache.h"
#include "service/TextLogService.h"
#include "service/SoundService.h"
#include "service/LinkPreviewService.h"
#include "service/ImageCache.h"
#include "util/NetworkSecurityUtils.h"
#include "network/IrcClient.h"
#include "ui/theme/ThemeManager.h"
#include "ui/chat/MessageListModel.h"
#include "ui/chat/MessageItemDelegate.h"

using namespace loungecat;

class TestLoungeCatCore : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testIrcFormatParser() {
        const QString raw = QStringLiteral("\x02" "Bold\x02 \x03" "04,01RedOnBlack\x0F Plain");
        const QList<IrcTextSpan> spans = IrcFormatParser::parse(raw);
        QCOMPARE(spans.size(), 4);
        QCOMPARE(spans[0].text, QStringLiteral("Bold"));
        QVERIFY(spans[0].bold);

        QCOMPARE(spans[2].text, QStringLiteral("RedOnBlack"));
        QVERIFY(spans[2].foreground.has_value());
        QCOMPARE(spans[2].foreground->rgb(), QColor(0xFF, 0x00, 0x00).rgb());
        QVERIFY(spans[2].background.has_value());
        QCOMPARE(spans[2].background->rgb(), QColor(0x00, 0x00, 0x00).rgb());

        QCOMPARE(spans[3].text, QStringLiteral(" Plain"));
        QVERIFY(!spans[3].bold);
        QVERIFY(!spans[3].foreground.has_value());

        QCOMPARE(IrcFormatParser::stripFormatting(raw), QStringLiteral("Bold RedOnBlack Plain"));
        const QString mono = QStringLiteral("a") + QChar(IrcFormatParser::MONOSPACE) + "b" +
            QChar(IrcFormatParser::MONOSPACE) + "c" + QChar(IrcFormatParser::MONOSPACE) + "d" +
            QChar(IrcFormatParser::RESET) + "e";
        const auto monoSpans = IrcFormatParser::parse(mono);
        QCOMPARE(monoSpans.size(), 5);
        QVERIFY(!monoSpans[0].monospace);
        QVERIFY(monoSpans[1].monospace);
        QVERIFY(!monoSpans[2].monospace);
        QVERIFY(monoSpans[3].monospace);
        QVERIFY(!monoSpans[4].monospace);
        QCOMPARE(IrcFormatParser::stripFormatting(mono), QStringLiteral("abcde"));
        const QString rgb = QChar(IrcFormatParser::HEX_COLOR) + QStringLiteral("FF0000,00ff00RGB") +
            QChar(IrcFormatParser::HEX_COLOR) + " tail";
        const auto rgbSpans = IrcFormatParser::parse(rgb);
        QCOMPARE(rgbSpans.size(), 2);
        QVERIFY(rgbSpans[0].foreground.has_value());
        QVERIFY(rgbSpans[0].background.has_value());
        QCOMPARE(rgbSpans[0].foreground.value(), QColor(255, 0, 0));
        QCOMPARE(rgbSpans[0].background.value(), QColor(0, 255, 0));
        QVERIFY(!rgbSpans[1].foreground.has_value());
        QVERIFY(!rgbSpans[1].background.has_value());
        QCOMPARE(IrcFormatParser::stripFormatting(rgb), QStringLiteral("RGB tail"));
        QCOMPARE(IrcFormatParser::stripFormatting(QChar(4) + QStringLiteral("12ZZ34tail")), QStringLiteral("12ZZ34tail"));
        QCOMPARE(IrcFormatParser::stripFormatting(QChar(4) + QStringLiteral("12345")), QStringLiteral("12345"));
        QCOMPARE(IrcFormatParser::stripFormatting(QChar(4) + QStringLiteral("123456,ABCDtail")), QStringLiteral(",ABCDtail"));
    }

    void testIrcCommandParser() {
        auto jCmd = IrcCommandParser::parse(QStringLiteral("/j linux secret"));
        QVERIFY(jCmd.has_value());
        QVERIFY(std::holds_alternative<cmd::Join>(*jCmd));
        const auto& join = std::get<cmd::Join>(*jCmd);
        QCOMPARE(join.channel, QStringLiteral("#linux"));
        QCOMPARE(join.key.value_or(QString()), QStringLiteral("secret"));

        auto kbCmd = IrcCommandParser::parse(QStringLiteral("/kb troll Spamming channel"));
        QVERIFY(kbCmd.has_value());
        QVERIFY(std::holds_alternative<cmd::Kickban>(*kbCmd));
        const auto& kb = std::get<cmd::Kickban>(*kbCmd);
        QCOMPARE(kb.nickname, QStringLiteral("troll"));
        QCOMPARE(kb.reason.value_or(QString()), QStringLiteral("Spamming channel"));

        auto nsCmd = IrcCommandParser::parse(QStringLiteral("/ns IDENTIFY hunter2"));
        QVERIFY(nsCmd.has_value());
        QVERIFY(std::holds_alternative<cmd::ServiceMessage>(*nsCmd));
        const auto& ns = std::get<cmd::ServiceMessage>(*nsCmd);
        QCOMPARE(ns.service, QStringLiteral("NickServ"));
        QCOMPARE(ns.command, QStringLiteral("IDENTIFY hunter2"));

        auto shrugCmd = IrcCommandParser::parse(QStringLiteral("/shrug idk"));
        QVERIFY(shrugCmd.has_value());
        QVERIFY(std::holds_alternative<cmd::EmoteText>(*shrugCmd));
        QVERIFY(std::get<cmd::EmoteText>(*shrugCmd).text.contains(QStringLiteral("¯\\_(ツ)_/¯")));
    }

    void testNickColorGeneratorMatchesJavaHashCode() {
        // Java "abc".hashCode() == 96354
        QCOMPARE(NickColorGenerator::javaStringHashCode(QStringLiteral("abc")), 96354);
        QCOMPARE(NickColorGenerator::getPaletteIndex(QStringLiteral("abc")), 96354 % 16);

        const QColor darkCol = NickColorGenerator::getColorForNickname(QStringLiteral("binkiewka"), true);
        const QColor lightCol = NickColorGenerator::getColorForNickname(QStringLiteral("binkiewka"), false);
        QVERIFY(darkCol.isValid());
        QVERIFY(lightCol.isValid());
        QVERIFY(darkCol != lightCol);
    }

    void testHighlightMatcher() {
        QSet<QString> words = {QStringLiteral("loungecat"), QStringLiteral("c++")};
        QVERIFY(HighlightMatcher::containsHighlight(QStringLiteral("Hey @binia: check this out"), QStringLiteral("binia"), words));
        QVERIFY(HighlightMatcher::containsHighlight(QStringLiteral("Porting LoungeCat now!"), QStringLiteral("other"), words));
        QVERIFY(!HighlightMatcher::containsHighlight(QStringLiteral("biniaturized word"), QStringLiteral("binia"), words));
    }

    void testChatGroupingModes() {
        QList<IncomingMessage> msgs;
        auto makeMsg = [](const QString& sender, const QString& text, MessageType t, qint64 ts) {
            IncomingMessage m;
            m.sender = sender;
            m.content = text;
            m.type = t;
            m.timestamp = ts;
            return m;
        };

        msgs << makeMsg(QStringLiteral("alice"), QStringLiteral("Hi"), MessageType::NORMAL, 1000);
        msgs << makeMsg(QStringLiteral("bob"), QStringLiteral("bob joined"), MessageType::JOIN, 2000);
        msgs << makeMsg(QStringLiteral("charlie"), QStringLiteral("charlie left"), MessageType::PART, 3000);
        msgs << makeMsg(QStringLiteral("dave"), QStringLiteral("dave quit"), MessageType::QUIT, 4000);
        msgs << makeMsg(QStringLiteral("alice"), QStringLiteral("Bye"), MessageType::NORMAL, 5000);

        const auto hidden = ChatGrouping::processMessages(msgs, JoinPartQuitDisplayMode::HIDE_ALL);
        QCOMPARE(hidden.size(), 2);

        const auto grouped = ChatGrouping::processMessages(msgs, JoinPartQuitDisplayMode::GROUPED);
        QCOMPARE(grouped.size(), 3);
        QVERIFY(std::holds_alternative<JoinPartGroupView>(grouped[1]));
        const auto& grp = std::get<JoinPartGroupView>(grouped[1]);
        QCOMPARE(grp.messages.size(), 3);
        QCOMPARE(grp.summary(), QStringLiteral("1 joined, 1 left, 1 quit"));

        QMap<QString, qint64> activeTimes;
        activeTimes.insert(QStringLiteral("bob"), 1500);
        const auto smart = ChatGrouping::processMessages(msgs, JoinPartQuitDisplayMode::SMART_HIDE, activeTimes, 10, 5000);
        // alice (normal), bob (join, active at 1500), alice (normal) -> 3 items
        QCOMPARE(smart.size(), 3);
    }

    void testTabCompletion() {
        TabCompletion tc;
        const QStringList nicks = {QStringLiteral("alice"), QStringLiteral("alex"), QStringLiteral("bob")};
        const QStringList chans = {QStringLiteral("#linux"), QStringLiteral("#loungecat")};
        const QStringList cmds = {QStringLiteral("/join"), QStringLiteral("/whois")};

        // At start of line: appends ": "
        auto r1 = tc.complete(QStringLiteral("al"), 2, nicks, chans, cmds, false);
        QCOMPARE(r1.text, QStringLiteral("alex: "));
        // Second Tab cycles to next candidate ("alice: ")
        auto r2 = tc.complete(r1.text, r1.cursorPosition, nicks, chans, cmds, false);
        QCOMPARE(r2.text, QStringLiteral("alice: "));

        // Mid-line: appends " "
        tc.reset();
        auto r3 = tc.complete(QStringLiteral("hello bo"), 8, nicks, chans, cmds, false);
        QCOMPARE(r3.text, QStringLiteral("hello bob "));

        // Channel completion
        tc.reset();
        auto r4 = tc.complete(QStringLiteral("join #li"), 8, nicks, chans, cmds, false);
        QCOMPARE(r4.text, QStringLiteral("join #linux "));
    }

    void testDatabaseServiceAndPersistence() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        // 1. DatabaseService CRUD
        DatabaseService db(tmpDir.filePath(QStringLiteral("loungecat.db")));
        ServerConfig cfg;
        cfg.serverName = QStringLiteral("Libera.Chat");
        cfg.hostname = QStringLiteral("irc.libera.chat");
        cfg.port = 6697;
        cfg.useSsl = true;
        cfg.nickname = QStringLiteral("binia");
        cfg.altNickname = QStringLiteral("binia_");
        cfg.autoJoinChannels = QStringLiteral("#linux,#c++");
        cfg.proxyType = ProxyType::SOCKS5;
        cfg.proxyHost = QStringLiteral("127.0.0.1");
        cfg.proxyPort = 9050;

        const qint64 insertedId = db.insertServerConfig(cfg);
        QVERIFY(insertedId > 0);

        const QList<ServerConfig> all = db.getAllServerConfigs();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].serverName, QStringLiteral("Libera.Chat"));
        QCOMPARE(all[0].proxyType, ProxyType::SOCKS5);
        QCOMPARE(all[0].proxyPort, 9050);

        // 2. PreferencesManager JSON roundtrip
        PreferencesManager pm(tmpDir.filePath(QStringLiteral("preferences.json")));
        pm.addIgnoredUser(QStringLiteral("SpamBot"));
        pm.update([](UserPreferences& p) {
            p.theme = QStringLiteral("Neon Night");
            p.pastebinThreshold = 550;
        });

        PreferencesManager pmReloaded(tmpDir.filePath(QStringLiteral("preferences.json")));
        QCOMPARE(pmReloaded.preferences().theme, QStringLiteral("Neon Night"));
        QCOMPARE(pmReloaded.preferences().pastebinThreshold, 550);
        QVERIFY(pmReloaded.isUserIgnored(QStringLiteral("spambot")));

        // 3. MessageCache roundtrip
        MessageCache cache(tmpDir.filePath(QStringLiteral("cache/messages")));
        IncomingMessage m1;
        m1.target = QStringLiteral("#c++");
        m1.sender = QStringLiteral("alice");
        m1.content = QStringLiteral("Testing cache");
        m1.type = MessageType::NORMAL;
        cache.cacheMessage(insertedId, QStringLiteral("#c++"), m1);
        cache.updateTopic(insertedId, QStringLiteral("#c++"), QStringLiteral("C++20 Topic"));
        cache.flushSync();

        MessageCache cacheReloaded(tmpDir.filePath(QStringLiteral("cache/messages")));
        const auto cachedMsgs = cacheReloaded.getCachedMessages(insertedId, QStringLiteral("#c++"));
        QCOMPARE(cachedMsgs.size(), 1);
        QCOMPARE(cachedMsgs[0].content, QStringLiteral("Testing cache"));
        QCOMPARE(cacheReloaded.getCachedTopic(insertedId, QStringLiteral("#c++")).value_or(QString()), QStringLiteral("C++20 Topic"));

        // 4. TextLogService
        TextLogService logger(tmpDir.filePath(QStringLiteral("logs")));
        logger.logMessage(QStringLiteral("Libera.Chat"), QStringLiteral("#c++"), m1);
        QVERIFY(logger.flushSync());
        const QString logFile = logger.resolveLogFilePath(QStringLiteral("Libera.Chat"), QStringLiteral("#c++"));
        QVERIFY(QFile::exists(logFile));
    }

    void testIrcClientMockServerHandshake() {
        QTcpServer mockServer;
        QVERIFY(mockServer.listen(QHostAddress::LocalHost, 0));
        const quint16 port = mockServer.serverPort();

        ServerConfig cfg;
        cfg.serverName = QStringLiteral("TestNet");
        cfg.hostname = QStringLiteral("127.0.0.1");
        cfg.port = port;
        cfg.useSsl = false;
        cfg.nickname = QStringLiteral("tester");
        cfg.autoJoinChannels = QStringLiteral("#test");

        IrcClient client(1, cfg);
        QSignalSpy connectedSpy(&client, &ChatClient::connected);
        QSignalSpy joinedSpy(&client, &ChatClient::channelJoined);
        QSignalSpy usersSpy(&client, &ChatClient::userListUpdated);
        QSignalSpy msgSpy(&client, &ChatClient::messageReceived);

        QTcpSocket* serverPeer = nullptr;
        connect(&mockServer, &QTcpServer::newConnection, this, [&]() {
            serverPeer = mockServer.nextPendingConnection();
            connect(serverPeer, &QTcpSocket::readyRead, this, [&]() {
                const QString data = QString::fromUtf8(serverPeer->readAll());
                if (data.contains(QStringLiteral("USER "))) {
                    serverPeer->write(":irc.test.local 001 tester :Welcome to TestNet\r\n");
                    serverPeer->write(":tester!u@h JOIN #test\r\n");
                    serverPeer->write(":irc.test.local 353 tester = #test :@alice +bob tester\r\n");
                    serverPeer->write(":irc.test.local 366 tester #test :End of /NAMES list.\r\n");
                    serverPeer->write(":alice!u@host PRIVMSG #test :Hello from mock server!\r\n");
                    serverPeer->flush();
                }
            });
        });

        client.connectToServer();
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(joinedSpy.count(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(usersSpy.count(), 1, 3000);

        const auto usersList = usersSpy.first().at(2).value<QList<ChannelUser>>();
        QCOMPARE(usersList.size(), 3);
        QCOMPARE(usersList[0].nickname, QStringLiteral("alice"));
        QCOMPARE(usersList[0].primaryMode(), UserMode::OP);
        QCOMPARE(usersList[1].nickname, QStringLiteral("bob"));
        QCOMPARE(usersList[1].primaryMode(), UserMode::VOICE);

        client.disconnectFromServer();
    }

    void testSpellCheckerAndAutoFix() {
        SpellChecker checker;
        QVERIFY(checker.isCorrect(QStringLiteral("lol")));
        QVERIFY(checker.isCorrect(QStringLiteral("brb")));
        QVERIFY(!checker.isCorrect(QStringLiteral("wut")));
        QVERIFY(!checker.isCorrect(QStringLiteral("teh")));

        const QString fixed = checker.applyAutoCorrections(QStringLiteral("wut is teh plan lol"));
        QCOMPARE(fixed, QStringLiteral("what is the plan lol"));
    }

    void testSpellCorrectionPreservesUrlsAndIrcIdentifiers() {
        DictionarySnapshot dictionary;
        dictionary.words = {"the", "plan", "see", "join", "hello"};
        dictionary.typos = {{"teh", "the"}, {"recieve", "receive"}, {"https", "http"}};
        const QStringList identifiers{
            "https://example.invalid/teh",
            "HTTPS://example.invalid/?q=teh&word=recieve",
            "https://example.invalid/#name-teh",
            "www.example.invalid/?q=teh",
            "ircs://irc.example.invalid/#teh-room",
            "#general-teh", "@alice-teh", "&local-teh",
            "(https://example.invalid/?q=teh)",
        };
        for (const auto& identifier : identifiers) {
            QVERIFY2(SpellChecker::checkSnapshot(dictionary, identifier).isEmpty(), qPrintable(identifier));
            const auto input = QString::fromUtf8("漢字 🌍 ") + "teh " + identifier;
            const auto issues = SpellChecker::checkSnapshot(dictionary, input);
            QCOMPARE(issues.size(), 1);
            QCOMPARE(issues.first().word, QStringLiteral("teh"));
            QCOMPARE(input.mid(issues.first().startOffset, issues.first().length), QStringLiteral("teh"));
            QCOMPARE(issues.first().suggestions.first(), QStringLiteral("the"));
        }
        QVERIFY(SpellChecker::checkSnapshot(dictionary, "/msg teh hello").isEmpty());
        dictionary.enabled = false;
        QVERIFY(SpellChecker::checkSnapshot(dictionary, "teh plan").isEmpty());
    }

    void testSoundServicePcmSynthesis() {
        const QByteArray pcm = SoundService::synthesizePcm16Mono(880.0, 150, 0.7f);
        // 44100 * 0.150 = 6615 samples * 2 bytes = 13230 bytes
        QCOMPARE(pcm.size(), 6615 * 2);
        const QByteArray wav = SoundService::wrapWavHeader(pcm);
        QCOMPARE(wav.size(), 44 + pcm.size());
        QVERIFY(wav.startsWith("RIFF"));
    }

    void testLinkPreviewExtractionAndOpenGraph() {
        const QString text = QStringLiteral("Check out https://example.com/test.png and https://youtu.be/dQw4w9WgXcQ!");
        const QStringList urls = LinkPreviewService::extractUrls(text);
        QCOMPARE(urls.size(), 2);
        QCOMPARE(urls[0], QStringLiteral("https://example.com/test.png"));
        QVERIFY(LinkPreviewService::isImageUrl(urls[0]));
        QVERIFY(LinkPreviewService::isYoutubeUrl(urls[1]));

        const QString html = QStringLiteral(
            "<html><head>"
            "<meta property=\"og:title\" content=\"LoungeCat C++\"/>"
            "<meta property=\"og:description\" content=\"Native Qt6 IRC Client\"/>"
            "<meta property=\"og:site_name\" content=\"GitHub\"/>"
            "</head></html>"
        );
        const UrlPreview og = LinkPreviewService::parseOpenGraphHtml(QStringLiteral("https://github.com/binkiewka/LoungeCat"), html);
        QCOMPARE(og.title.value_or(QString()), QStringLiteral("LoungeCat C++"));
        QCOMPARE(og.description.value_or(QString()), QStringLiteral("Native Qt6 IRC Client"));
        QCOMPARE(og.siteName.value_or(QString()), QStringLiteral("GitHub"));
    }

    void testImageCacheAndRichPreviewHitTesting() {
        // 1. Verify ImageCache singleton and memory operations
        ImageCache& cache = ImageCache::instance();
        cache.clearMemoryCache();
        const QString testImgUrl = QStringLiteral("https://example.com/test_cat.png");
        QTcpServer unusedProxy;
        QVERIFY(unusedProxy.listen(QHostAddress::LocalHost, 0));
        const auto unusedPort = unusedProxy.serverPort();
        unusedProxy.close();
        cache.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), unusedPort));
        QVERIFY(!cache.hasImage(testImgUrl));
        QVERIFY(cache.getImage(QString()).isNull());

        // 2. Verify MessageListModel role and metatype conversions
        IncomingMessage msg;
        msg.sender = QStringLiteral("alice");
        msg.content = QStringLiteral("Check this image: https://example.com/test_cat.png and video https://youtu.be/dQw4w9WgXcQ");
        UrlPreview p1;
        p1.url = QStringLiteral("https://example.com/test_cat.png");
        p1.isImage = true;
        UrlPreview p2;
        p2.url = QStringLiteral("https://youtu.be/dQw4w9WgXcQ");
        p2.title = QStringLiteral("Never Gonna Give You Up");
        p2.siteName = QStringLiteral("YouTube");
        p2.description = QStringLiteral("Music video");
        msg.urlPreviews = {p1, p2};

        MessageListModel model;
        model.setItems({MessageItemView{msg}});
        QCOMPARE(model.rowCount(), 1);

        const QModelIndex idx = model.index(0, 0);
        QVERIFY(idx.isValid());
        QVERIFY(idx.data(MessageListModel::HasUrlPreviewRole).toBool());

        const QVariant prevVar = idx.data(MessageListModel::UrlPreviewsRole);
        QVERIFY(prevVar.canConvert<QList<UrlPreview>>());
        const QList<UrlPreview> previews = prevVar.value<QList<UrlPreview>>();
        QCOMPARE(previews.size(), 2);
        QCOMPARE(previews[0].url, QStringLiteral("https://example.com/test_cat.png"));
        QCOMPARE(previews[1].title.value_or(QString()), QStringLiteral("Never Gonna Give You Up"));

        // 3. Verify MessageItemDelegate rich HTML formatting
        ThemeColors theme = ThemeColors::darkTheme();
        UserPreferences prefs;

        // Mode: INLINE
        prefs.urlImageDisplayMode = UrlImageDisplayMode::INLINE;
        const QString inlineHtml = MessageItemDelegate::buildRichTextHtml(idx, theme, prefs);
        QTRY_VERIFY_WITH_TIMEOUT(cache.hasRecentFailure(testImgUrl), 3000);
        QVERIFY(inlineHtml.contains(QStringLiteral("https://example.com/test_cat.png")));
        QVERIFY(inlineHtml.contains(QStringLiteral("Never Gonna Give You Up")));
        QVERIFY(inlineHtml.contains(QStringLiteral("YOUTUBE")));
        QVERIFY(inlineHtml.contains(QStringLiteral("<table")));

        // Mode: COMPACT
        prefs.urlImageDisplayMode = UrlImageDisplayMode::COMPACT;
        const QString compactHtml = MessageItemDelegate::buildRichTextHtml(idx, theme, prefs);
        QVERIFY(compactHtml.contains(QStringLiteral("Never Gonna Give You Up")));
        QVERIFY(compactHtml.contains(QStringLiteral("[YouTube]")));
        QVERIFY(!compactHtml.contains(QStringLiteral("<table")));

        // Mode: DISABLED
        prefs.urlImageDisplayMode = UrlImageDisplayMode::DISABLED;
        const QString disabledHtml = MessageItemDelegate::buildRichTextHtml(idx, theme, prefs);
        QVERIFY(!disabledHtml.contains(QStringLiteral("Never Gonna Give You Up")));
        QVERIFY(!disabledHtml.contains(QStringLiteral("YOUTUBE")));

        // 4. Verify anchorAt hit testing functionality
        MessageItemDelegate delegate;
        delegate.setThemeAndPreferences(theme, prefs);
        const QRect rect(0, 0, 800, 100);
        const QFont font;

        // A coordinate with no link should safely return empty string
        const QString noAnchor = delegate.anchorAt(idx, rect, QPoint(1, 1), font);
        QVERIFY(noAnchor.isEmpty());
    }

    void testNetworkSecurityAndAntiSndAndIpLeaks() {
        // 1. SSRF & Local network address blocking
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("127.0.0.1")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("localhost")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("sub.localhost")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("192.168.1.1")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("10.0.0.1")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("172.16.0.1")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("169.254.169.254"))); // Cloud metadata
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("nas.local")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("router.lan")));
        QVERIFY(NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("hidden.onion")));

        // Public hosts should NOT be marked private
        QVERIFY(!NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("github.com")));
        QVERIFY(!NetworkSecurityUtils::isPrivateOrLoopbackHost(QStringLiteral("1.1.1.1")));

        // 2. Anti-tracking / IP logger blocking
        QVERIFY(NetworkSecurityUtils::isKnownTrackerOrLogger(QStringLiteral("grabify.link")));
        QVERIFY(NetworkSecurityUtils::isKnownTrackerOrLogger(QStringLiteral("test.grabify.link")));
        QVERIFY(NetworkSecurityUtils::isKnownTrackerOrLogger(QStringLiteral("iplogger.org")));
        QVERIFY(NetworkSecurityUtils::isKnownTrackerOrLogger(QStringLiteral("2no.co")));
        QVERIFY(NetworkSecurityUtils::isKnownTrackerOrLogger(QStringLiteral("canarytokens.org")));
        QVERIFY(!NetworkSecurityUtils::isKnownTrackerOrLogger(QStringLiteral("wikipedia.org")));

        // 3. Complete URL safety validation (SSRF, dangerous schemes, ports, credentials)
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("file:///etc/passwd")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("ftp://files.example.com")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("javascript:alert(1)")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("data:text/html,evil")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("http://admin:pass@example.com/")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("http://localhost:8080/admin")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("http://192.168.1.1/reset")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("http://169.254.169.254/latest/meta-data")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("https://grabify.link/track123")));
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("http://example.com:22/"))); // Port 22 SSH blocked
        QVERIFY(!NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("http://example.com:6667/"))); // Port 6667 IRC blocked

        // Legitimate public web URLs
        QVERIFY(NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("https://github.com/torvalds/linux")));
        QVERIFY(NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("https://example.com/cat.jpg")));
        QVERIFY(NetworkSecurityUtils::isSafeWebUrl(QStringLiteral("http://example.com:8080/image.png")));

        // 4. Safe image MIME types vs dangerous / executable types
        QVERIFY(NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("image/png")));
        QVERIFY(NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("image/jpeg; charset=utf-8")));
        QVERIFY(NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("image/webp")));
        QVERIFY(NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("image/svg+xml")));
        QVERIFY(!NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("application/octet-stream")));
        QVERIFY(!NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("application/x-executable")));
        QVERIFY(!NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("application/x-sh")));
        QVERIFY(!NetworkSecurityUtils::isSafeImageContentType(QStringLiteral("text/html")));

        // 5. Generic User-Agent does not leak client details
        const QString ua = NetworkSecurityUtils::genericUserAgent();
        QVERIFY(!ua.contains(QStringLiteral("LoungeCat")));
        QVERIFY(!ua.contains(QStringLiteral("Qt")));
        QVERIFY(!ua.contains(QStringLiteral("Linux")));
    }

    void testThemeManagerAndExporters() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        ThemeManager tm(tmpDir.filePath(QStringLiteral("themes")));
        QVERIFY(tm.availableThemeNames().size() >= 7);
        tm.setActiveTheme(QStringLiteral("Stormy Sea"));
        QCOMPARE(tm.currentTheme().name, QStringLiteral("Stormy Sea"));

        tm.setActiveTheme(QStringLiteral("Obsidian Veil"));
        QCOMPARE(tm.currentTheme().name, QStringLiteral("Obsidian Veil"));
        QCOMPARE(tm.currentTheme().chatBackground, QColor(0x00, 0x00, 0x00));
        QCOMPARE(tm.currentTheme().accentColor, QColor(0x00, 0xf2, 0xff));
        QCOMPARE(tm.currentTheme().unreadBadge, QColor(0x00, 0xe6, 0x76));

        ThemeColors custom = ThemeColors::darkTheme();
        custom.name = QStringLiteral("Cyber Cat");
        QVERIFY(tm.saveCustomTheme(custom));
        QCOMPARE(tm.currentTheme().name, QStringLiteral("Cyber Cat"));
    }

    void testIrcV3TagParsingAndEscaping() {
        const QString rawVal = QStringLiteral("semi; space \\slash\r\n");
        const QString escaped = IrcClient::escapeIrcTagValue(rawVal);
        QCOMPARE(escaped, QStringLiteral("semi\\:\\sspace\\s\\\\slash\\r\\n"));
        QCOMPARE(IrcClient::unescapeIrcTagValue(escaped), rawVal);

        const auto parsedTime = IrcClient::parseServerTimeIso8601(QStringLiteral("2026-10-18T12:00:00.500Z"));
        QVERIFY(parsedTime.has_value());
        QCOMPARE(*parsedTime, QDateTime::fromString(QStringLiteral("2026-10-18T12:00:00.500Z"), Qt::ISODateWithMs).toMSecsSinceEpoch());

        const ParsedIrcLine line = IrcClient::parseIrcLine(
            QStringLiteral("@msgid=msg-42;time=2026-10-18T12:00:00.000Z;account=binkiewka;+draft/reply=msg-01;note=hello\\sworld :alice!u@host PRIVMSG #c++ :IRCv3 rocks!")
        );
        QCOMPARE(line.command, QStringLiteral("PRIVMSG"));
        QCOMPARE(line.prefix, QStringLiteral("alice!u@host"));
        QCOMPARE(line.params.size(), 2);
        QCOMPARE(line.params[0], QStringLiteral("#c++"));
        QCOMPARE(line.params[1], QStringLiteral("IRCv3 rocks!"));
        QCOMPARE(line.tags.value(QStringLiteral("msgid")), QStringLiteral("msg-42"));
        QCOMPARE(line.tags.value(QStringLiteral("account")), QStringLiteral("binkiewka"));
        QCOMPARE(line.tags.value(QStringLiteral("+draft/reply")), QStringLiteral("msg-01"));
        QCOMPARE(line.tags.value(QStringLiteral("note")), QStringLiteral("hello world"));

        // Verify new IRCv3 commands in IrcCommandParser
        auto chCmd = IrcCommandParser::parse(QStringLiteral("/chathistory 75"));
        QVERIFY(chCmd.has_value());
        QVERIFY(std::holds_alternative<cmd::Chathistory>(*chCmd));
        QCOMPARE(std::get<cmd::Chathistory>(*chCmd).limit, 75);

        auto reactCmd = IrcCommandParser::parse(QStringLiteral("/react msg-42 🚀"));
        QVERIFY(reactCmd.has_value());
        QVERIFY(std::holds_alternative<cmd::React>(*reactCmd));
        QCOMPARE(std::get<cmd::React>(*reactCmd).targetRef, QStringLiteral("msg-42"));
        QCOMPARE(std::get<cmd::React>(*reactCmd).emoji, QStringLiteral("🚀"));

        auto setnameCmd = IrcCommandParser::parse(QStringLiteral("/setname New Real Name"));
        QVERIFY(setnameCmd.has_value());
        QVERIFY(std::holds_alternative<cmd::Setname>(*setnameCmd));
        QCOMPARE(std::get<cmd::Setname>(*setnameCmd).realname, QStringLiteral("New Real Name"));
    }

    void testIrcV3CapNegotiationBatchReactionsAndTyping() {
        QTcpServer mockServer;
        QVERIFY(mockServer.listen(QHostAddress::LocalHost, 0));
        const quint16 port = mockServer.serverPort();

        ServerConfig cfg;
        cfg.serverName = QStringLiteral("IRCv3Net");
        cfg.hostname = QStringLiteral("127.0.0.1");
        cfg.port = port;
        cfg.useSsl = false;
        cfg.nickname = QStringLiteral("tester");

        IrcClient client(2, cfg);
        QSignalSpy connectedSpy(&client, &ChatClient::connected);
        QSignalSpy msgSpy(&client, &ChatClient::messageReceived);
        QSignalSpy typingSpy(&client, &ChatClient::typingUserUpdated);
        QSignalSpy reactionSpy(&client, &ChatClient::reactionReceived);
        QSignalSpy usersSpy(&client, &ChatClient::userListUpdated);

        QTcpSocket* serverPeer = nullptr;
        bool capLsSent = false;
        bool registered = false;

        connect(&mockServer, &QTcpServer::newConnection, this, [&]() {
            serverPeer = mockServer.nextPendingConnection();
            connect(serverPeer, &QTcpSocket::readyRead, this, [&]() {
                const QString data = QString::fromUtf8(serverPeer->readAll());
                if (data.contains(QStringLiteral("CAP LS 302")) && !capLsSent) {
                    capLsSent = true;
                    serverPeer->write(":irc.v3.local CAP * LS * :message-tags server-time echo-message batch labeled-response\r\n");
                    serverPeer->write(":irc.v3.local CAP * LS :msgid multi-prefix away-notify account-notify extended-join chghost setname invite-notify userhost-in-names cap-notify standard-replies draft/chathistory draft/multiline\r\n");
                    serverPeer->flush();
                }
                if (data.contains(QStringLiteral("CAP REQ :"))) {
                    serverPeer->write(":irc.v3.local CAP tester ACK :message-tags server-time echo-message batch labeled-response msgid multi-prefix away-notify account-notify extended-join chghost setname invite-notify userhost-in-names cap-notify standard-replies draft/chathistory draft/multiline\r\n");
                    serverPeer->flush();
                }
                if (data.contains(QStringLiteral("CAP END")) && !registered) {
                    registered = true;
                    serverPeer->write(":irc.v3.local 001 tester :Welcome to IRCv3Net\r\n");
                    serverPeer->write(":tester!u@h JOIN #v3 * :Tester\r\n");
                    // userhost-in-names + multi-prefix
                    serverPeer->write(":irc.v3.local 353 tester = #v3 :@+alice!alice@old.host tester!u@h\r\n");
                    serverPeer->write(":irc.v3.local 366 tester #v3 :End of /NAMES list.\r\n");

                    // 1. chathistory batch
                    serverPeer->write(":irc.v3.local BATCH +hist1 chathistory #v3\r\n");
                    serverPeer->write("@batch=hist1;msgid=hist-msg-1;time=2026-10-18T10:00:00.000Z;account=alice_acc :alice!alice@old.host PRIVMSG #v3 :History message one\r\n");
                    serverPeer->write(":irc.v3.local BATCH -hist1\r\n");

                    // 2. draft/multiline batch
                    serverPeer->write("@msgid=multi-99;account=alice_acc :alice!alice@old.host BATCH +ml1 draft/multiline #v3\r\n");
                    serverPeer->write("@batch=ml1 :alice!alice@old.host PRIVMSG #v3 :Line 1 of multiline\r\n");
                    serverPeer->write("@batch=ml1 :alice!alice@old.host PRIVMSG #v3 :Line 2 of multiline\r\n");
                    serverPeer->write("@batch=ml1;draft/multiline-concat :alice!alice@old.host PRIVMSG #v3 : (concatenated)\r\n");
                    serverPeer->write(":alice!alice@old.host BATCH -ml1\r\n");

                    // 3. TAGMSG typing & reaction
                    serverPeer->write("@+typing=active :alice!alice@old.host TAGMSG #v3\r\n");
                    serverPeer->write("@+draft/reply=hist-msg-1;+draft/react=🎉 :alice!alice@old.host TAGMSG #v3\r\n");

                    // 4. chghost, away-notify, account-notify
                    serverPeer->write(":alice!alice@old.host CHGHOST newuser new.vhost\r\n");
                    serverPeer->write(":alice!newuser@new.vhost AWAY :Busy coding\r\n");
                    serverPeer->write(":alice!newuser@new.vhost ACCOUNT alice_verified\r\n");
                    serverPeer->flush();
                }
            });
        });

        client.connectToServer();
        QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 3000);
        QVERIFY(client.enabledCapabilities().contains(QStringLiteral("message-tags")));
        QVERIFY(client.enabledCapabilities().contains(QStringLiteral("server-time")));
        QVERIFY(client.enabledCapabilities().contains(QStringLiteral("draft/multiline")));

        QTRY_VERIFY_WITH_TIMEOUT(typingSpy.count() >= 1, 3000);
        QCOMPARE(typingSpy.first().at(1).toString(), QStringLiteral("#v3"));
        QCOMPARE(typingSpy.first().at(2).toString(), QStringLiteral("alice"));
        QCOMPARE(typingSpy.first().at(3).toString(), QStringLiteral("active"));

        QTRY_VERIFY_WITH_TIMEOUT(reactionSpy.count() >= 1, 3000);
        QCOMPARE(reactionSpy.first().at(1).toString(), QStringLiteral("#v3"));
        QCOMPARE(reactionSpy.first().at(2).toString(), QStringLiteral("hist-msg-1"));
        QCOMPARE(reactionSpy.first().at(3).toString(), QStringLiteral("alice"));
        QCOMPARE(reactionSpy.first().at(4).toString(), QStringLiteral("🎉"));

        // Find the history message and multiline message in msgSpy
        bool foundHistoryMsg = false;
        bool foundMultilineMsg = false;
        for (const auto& call : msgSpy) {
            const IncomingMessage m = call.at(1).value<IncomingMessage>();
            const bool isReplay = call.at(2).toBool();
            if (m.id == QStringLiteral("hist-msg-1")) {
                foundHistoryMsg = true;
                QVERIFY(isReplay);
                QCOMPARE(m.content, QStringLiteral("History message one"));
                QCOMPARE(m.senderAccount.value_or(QString()), QStringLiteral("alice_acc"));
            }
            if (m.id == QStringLiteral("multi-99")) {
                foundMultilineMsg = true;
                QVERIFY(!isReplay);
                QCOMPARE(m.content, QStringLiteral("Line 1 of multiline\nLine 2 of multiline (concatenated)"));
            }
        }
        QVERIFY(foundHistoryMsg);
        QVERIFY(foundMultilineMsg);

        // Verify user list updated with chghost, away-notify, and account-notify
        QVERIFY(!usersSpy.isEmpty());
        const auto latestUsers = usersSpy.last().at(2).value<QList<ChannelUser>>();
        bool foundAlice = false;
        for (const auto& u : latestUsers) {
            if (u.nickname == QStringLiteral("alice")) {
                foundAlice = true;
                QVERIFY(u.modes.contains(UserMode::OP));
                QVERIFY(u.modes.contains(UserMode::VOICE));
                QVERIFY(u.isAway);
                QCOMPARE(u.account.value_or(QString()), QStringLiteral("alice_verified"));
                QCOMPARE(u.host.value_or(QString()), QStringLiteral("new.vhost"));
            }
        }
        QVERIFY(foundAlice);

        client.disconnectFromServer();
    }
};

int main(int argc, char** argv) {
    QTemporaryDir isolated;
    qputenv("LOUNGECAT_DATA_DIR", isolated.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", isolated.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    TestLoungeCatCore test;
    const auto result = QTest::qExec(&test, argc, argv);
    ImageCache::shutdown();
    return result;
}
#include "test_loungecat_core.moc"
