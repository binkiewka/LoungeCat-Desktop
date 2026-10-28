#include "util/UploadResult.h"
#include "util/TimestampReplayIdentity.h"
#include "util/ServerAddress.h"
#include <QtTest>
#include "util/IrcSecrets.h"
#include "util/IrcFormatParser.h"
#include "util/IrcTextChunks.h"
#include "util/HighlightMatcher.h"
#include <QtNetwork>
#include <QTemporaryDir>
#include "network/IrcClient.h"
#include "network/ConnectionManager.h"
#include "network/ConnectionPolicy.h"
#include "ui/chat/ChannelPaneWidget.h"
#include "ui/chat/ChatInputWidget.h"
#include "ui/chat/MessageItemDelegate.h"
#include "ui/chat/MessageListModel.h"
#include <QStandardItemModel>
#include <QAbstractTextDocumentLayout>
#include <QTextLayout>
#include <QPainter>
#include <QImage>
#include <cstdlib>
#include <ctime>
#include <QTextBlock>
#include <QTextFragment>

using namespace loungecat;

class TlsIrcServer : public QTcpServer {
public:
    QStringList lines;
    QString capabilities;
    QStringList capPages;
    QString authResult = QStringLiteral("903");
    bool confirmWhois = true;
    bool skipWelcome = false;
    bool skipCapabilityAck = false;
    int accepted = 0;
    QSslSocket* current = nullptr;
    std::function<void(const QString&)> extra;
    QSslCertificate cert;
    QSslKey key;
    QSslConfiguration previous;

    TlsIrcServer() {
        QFile certFile(QFINDTESTDATA("fixtures/localhost-cert.pem")); certFile.open(QIODevice::ReadOnly);
        QFile keyFile(QFINDTESTDATA("fixtures/localhost-key.pem")); keyFile.open(QIODevice::ReadOnly);
        cert = QSslCertificate(certFile.readAll()); key = QSslKey(keyFile.readAll(), QSsl::Rsa);
        previous = QSslConfiguration::defaultConfiguration();
        auto trusted = previous; trusted.addCaCertificate(cert); QSslConfiguration::setDefaultConfiguration(trusted);
        listen(QHostAddress::LocalHost, 0);
    }
    ~TlsIrcServer() override { QSslConfiguration::setDefaultConfiguration(previous); }
    ServerConfig config() const {
        ServerConfig cfg; cfg.hostname = "localhost"; cfg.port = serverPort(); cfg.nickname = "tester"; cfg.useSsl = true; return cfg;
    }
    void send(const QByteArray& value) { if (current) current->write(value); }
protected:
    void incomingConnection(qintptr descriptor) override {
        auto* peer = new QSslSocket(this); current = peer; ++accepted;
        peer->setLocalCertificate(cert); peer->setPrivateKey(key); peer->setPeerVerifyMode(QSslSocket::VerifyNone);
        if (!peer->setSocketDescriptor(descriptor)) { peer->deleteLater(); return; }
        connect(peer, &QSslSocket::readyRead, this, [this, peer]() {
            while (peer->canReadLine()) {
                const auto line = QString::fromUtf8(peer->readLine()).trimmed(); lines.append(line);
                if (line == "CAP LS 302") {
                    const auto pages = capPages.isEmpty() ? QStringList{capabilities} : capPages;
                    for (int i = 0; i < pages.size(); ++i)
                        peer->write(":fixture CAP * LS " + QByteArray(i + 1 < pages.size() ? "* :" : ":") + pages[i].toUtf8() + "\r\n");
                }
                else if (line.startsWith("CAP REQ :") && !skipCapabilityAck) peer->write(":fixture CAP tester ACK :" + line.mid(9).toUtf8() + "\r\n");
                else if (line == "AUTHENTICATE PLAIN") peer->write("AUTHENTICATE +\r\n");
                else if (line.startsWith("AUTHENTICATE ")) {
                    peer->write(":fixture " + authResult.toUtf8() + " tester :Authentication result\r\n");
                    if (authResult == "904") peer->write(":fixture 001 tester :Unauthenticated welcome\r\n");
                }
                else if (line == "CAP END" && !skipWelcome) peer->write(":fixture 001 tester :Welcome\r\n");
                else if (line.startsWith("WHOIS ") && confirmWhois) peer->write(":fixture 330 tester tester fixture-account :is logged in as\r\n:fixture 318 tester tester :End\r\n");
                else if (line.startsWith("QUIT")) peer->disconnectFromHost();
                if (extra) extra(line);
            }
        });
        peer->startServerEncryption();
    }
};

class FakeClient : public ChatClient {
public:
    int dials = 0;
    bool online = false;
    FakeClient(qint64 id, const ServerConfig& cfg, QObject* parent) : ChatClient(id, cfg, parent) {}
    void connectToServer() override { ++dials; m_retryDisposition = RetryDisposition::Transient; }
    void disconnectFromServer(const QString& = {}) override { online = false; Q_EMIT disconnected(m_serverId, "closed"); }
    void joinChannel(const QString&, const QString& = {}) override {}
    void partChannel(const QString&, const QString& = {}) override {}
    bool sendMessage(const QString&, const QString&, const std::optional<IncomingMessage>& = {}) override { return online; }
    bool sendAction(const QString&, const QString&) override { return online; }
    bool sendNotice(const QString&, const QString&) override { return online; }
    bool sendRawCommand(const QString&) override { return online; }
    void changeNickname(const QString&) override {}
    QString getNickname() const override { return "tester"; }
    bool isConnected() const override { return online; }
    void ready() { online = true; Q_EMIT connected(m_serverId); }
    void fail(RetryDisposition kind) { m_retryDisposition = kind; online = false; Q_EMIT connectionError(m_serverId, "fixture failure"); }
    void mapping(const QString& value) { Q_EMIT isupportUpdated(m_serverId, {{"CASEMAPPING",value}}); }
};

class ReliabilityTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void isupportUpdatesExistingTokensAtCapacity() {
        TlsIrcServer fixture;
        IrcClient client(1,fixture.config());
        client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        fixture.send(":fixture 005 tester CASEMAPPING=ascii CHANTYPES=#! CHATHISTORY=7 :supported\r\n");
        for (int first=0;first<125;first+=8) {
            QStringList tokens;
            for (int n=first;n<qMin(first+8,125);++n) tokens.append(QString("XENTRY%1=1").arg(n));
            fixture.send(QString(":fixture 005 tester %1 :supported\r\n").arg(tokens.join(' ')).toUtf8());
        }
        QTRY_COMPARE_WITH_TIMEOUT(client.isupportTokens().size(),128,5000);
        fixture.send(":fixture 005 tester CASEMAPPING=rfc1459 CHANTYPES=#& CHATHISTORY=11 XEXTRA=1 :supported\r\n");
        QTRY_COMPARE_WITH_TIMEOUT(client.isupportTokens().value("CASEMAPPING"),QString("rfc1459"),5000);
        QCOMPARE(client.isupportTokens().value("CHANTYPES"),QString("#&"));
        QCOMPARE(client.isupportTokens().value("CHATHISTORY"),QString("11"));
        QCOMPARE(client.isupportTokens().size(),128);
        QVERIFY(!client.isupportTokens().contains("XEXTRA"));
    }

    void rejectedListDoesNotOpenAResultsDialog() {
        TlsIrcServer fixture;
        QTemporaryDir temp;
        DatabaseService database(temp.filePath("history.sqlite"));
        PreferencesManager prefs(temp.filePath("prefs.json"));
        prefs.update([](auto& p) { p.loggingEnabled=false; p.notificationsEnabled=false; p.soundAlertsEnabled=false; });
        ConnectionManager manager(&database,nullptr,&prefs,nullptr,nullptr);
        const auto id=manager.addServer(fixture.config());
        QTRY_VERIFY_WITH_TIMEOUT(manager.client(id) && manager.client(id)->isConnected(),5000);
        const auto status=manager.connection(id)->channels.first().name;
        QSignalSpy dialogs(&manager,&ConnectionManager::openChannelListDialogRequested);
        const QString oversized=QStringLiteral("/list ")+QString(9000,'x');
        QVERIFY(!manager.sendUserInput(id,status,oversized));
        QVERIFY(dialogs.isEmpty());
        QVERIFY(!manager.inputHistory(id,status).contains(oversized));
        QVERIFY(manager.sendUserInput(id,status,QStringLiteral("/list #a*")));
        QCOMPARE(dialogs.size(),1);
        QCOMPARE(dialogs.first().at(1).toString(),QStringLiteral("#a*"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains(QStringLiteral("LIST #a*")),5000);
    }

    void sequentialChannelListsDoNotRequireStartNumeric() {
        TlsIrcServer fixture; int requests=0;
        fixture.extra=[&](const QString& line) {
            if (!line.startsWith("LIST")) return;
            ++requests;
            if (requests==3) fixture.send(":fixture 321 tester Channel :Users Name\r\n");
            fixture.send(QString(":fixture 322 tester #item-%1 10 :Topic\r\n:fixture 323 tester :End\r\n").arg(requests).toUtf8());
        };
        IrcClient client(1,fixture.config());
        QSignalSpy lists(&client,&ChatClient::channelListUpdated);
        client.connectToServer(); QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        for (int request=1;request<=3;++request) {
            QVERIFY(client.sendRawCommand("LIST"));
            QTRY_COMPARE_WITH_TIMEOUT(lists.size(),request,5000);
            const auto snapshot=lists.at(request-1);
            const auto rows=qvariant_cast<QList<ChannelListItem>>(snapshot.at(1));
            QCOMPARE(rows.size(),1);
            QCOMPARE(rows.first().channel,QString("#item-%1").arg(request));
            QVERIFY(!snapshot.at(3).toBool());
        }
    }

    void renderedLinksRespectUrlBoundaries_data() {
        QTest::addColumn<QString>("body"); QTest::addColumn<QString>("expected");
        QTest::newRow("balanced-path") << QString("See https://example.invalid/a(b)") << QString("https://example.invalid/a(b)");
        QTest::newRow("wrapped") << QString("(https://example.invalid/path)") << QString("https://example.invalid/path");
        QTest::newRow("sentence-period") << QString("See https://example.invalid/page.") << QString("https://example.invalid/page");
        QTest::newRow("ipv6") << QString("http://[2001:db8::1]/path?q=x") << QString("http://[2001:db8::1]/path?q=x");
        QTest::newRow("query-punctuation") << QString("https://example.invalid/a?sig=a!") << QString("https://example.invalid/a?sig=a!");
        QTest::newRow("javascript") << QString("javascript:alert(1)") << QString();
        QTest::newRow("file") << QString("file:///synthetic/path") << QString();
        QTest::newRow("long") << ("https://example.invalid/" + QString(5000, 'x')) << ("https://example.invalid/" + QString(5000, 'x'));
        QTest::newRow("uppercase-scheme") << QString("HTTPS://example.invalid/path") << QString("HTTPS://example.invalid/path");
        QTest::newRow("invalid-ipv6") << QString("http://[") << QString();
    }
    void renderedLinksRespectUrlBoundaries() {
        QFETCH(QString, body); QFETCH(QString, expected);
        QStandardItemModel model(1, 1);
        const auto index = model.index(0, 0);
        model.setData(index, body, MessageListModel::ContentRole);
        model.setData(index, int(MessageType::NORMAL), MessageListModel::MessageTypeRole);
        QTextDocument document;
        document.setHtml(MessageItemDelegate::buildRichTextHtml(index, ThemeColors::darkTheme(), UserPreferences{}));
        QSet<QString> anchors;
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                if (fragment.isValid() && fragment.charFormat().isAnchor()) anchors.insert(fragment.charFormat().anchorHref());
            }
        }
        QCOMPARE(anchors, expected.isEmpty() ? QSet<QString>{} : QSet<QString>{expected});
        QVERIFY(document.toPlainText().contains(body));
    }
    void searchHighlightPreservesLinkDestinationsAndVisibleText_data() {
        QTest::addColumn<QString>("query");
        for (const auto& value : QStringList{"", "before", "example", "https", "href", "color", "&", "<literal>"})
            QTest::newRow(value.isEmpty() ? "no-search" : value.toUtf8().constData()) << value;
    }
    void searchHighlightPreservesLinkDestinationsAndVisibleText() {
        QFETCH(QString, query);
        const QString url = "https://example.invalid/a?x=y&z=2";
        const QString body = "before <literal> & after " + url;
        QStandardItemModel model(1, 1);
        const auto index = model.index(0, 0);
        model.setData(index, body, MessageListModel::ContentRole);
        model.setData(index, "alice", MessageListModel::SenderRole);
        model.setData(index, int(MessageType::NORMAL), MessageListModel::MessageTypeRole);
        const auto theme = ThemeColors::darkTheme();
        QTextDocument document;
        document.setHtml(MessageItemDelegate::buildRichTextHtml(index, theme, UserPreferences{}, query));
        QSet<QString> anchors;
        QString highlighted;
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                if (!fragment.isValid()) continue;
                const auto format = fragment.charFormat();
                if (format.isAnchor()) anchors.insert(format.anchorHref());
                if (format.background().color() == theme.yellow) highlighted += fragment.text();
            }
        }
        QCOMPARE(anchors, QSet<QString>{url});
        QVERIFY(document.toPlainText().contains(body));
        if (!query.isEmpty() && body.contains(query, Qt::CaseInsensitive))
            QVERIFY(highlighted.contains(query, Qt::CaseInsensitive));
    }
    void formattingInsideUrlsPreservesDestination_data() {
        QTest::addColumn<int>("split");
        for (int position : {0, 3, 11, 26, 29}) QTest::newRow(qPrintable(QString::number(position))) << position;
    }
    void formattingInsideUrlsPreservesDestination() {
        QFETCH(int,split);
        const QString url="https://example.invalid/a?x=y&z=2";
        const QString raw=url.left(split)+QChar(2)+url.mid(split)+QChar(15);
        QStandardItemModel model(1,1);const auto index=model.index(0,0);
        model.setData(index,raw,MessageListModel::ContentRole);model.setData(index,"alice",MessageListModel::SenderRole);
        model.setData(index,int(MessageType::NORMAL),MessageListModel::MessageTypeRole);
        const auto theme=ThemeColors::darkTheme();QTextDocument document;
        document.setHtml(MessageItemDelegate::buildRichTextHtml(index,theme,UserPreferences{},"example"));
        QSet<QString> anchors;QString linked,highlighted;
        for(auto block=document.begin();block.isValid();block=block.next())for(auto it=block.begin();!it.atEnd();++it){
            const auto fragment=it.fragment();if(!fragment.isValid())continue;
            const auto format=fragment.charFormat();if(format.isAnchor()){anchors.insert(format.anchorHref());linked+=fragment.text();}
            if(format.background().color()==theme.yellow)highlighted+=fragment.text();
        }
        QCOMPARE(anchors,QSet<QString>{url});QCOMPARE(linked,url);QVERIFY(document.toPlainText().endsWith(url));
        QVERIFY(highlighted.contains("example"));
    }
    void colourParametersPreserveOrdinaryText_data() {
        QTest::addColumn<QString>("raw");QTest::addColumn<QString>("expected");
        const auto c=QString(QChar(3));
        QTest::newRow("unicode-fg") << (c+"١٢tail") << QString("١٢tail");
        QTest::newRow("unicode-bg") << (c+"04,١٢tail") << QString(",١٢tail");
        QTest::newRow("extra-fg") << (c+"100tail") << QString("0tail");
        QTest::newRow("extra-bg") << (c+"04,123tail") << QString("3tail");
        QTest::newRow("comma-text") << (c+"04,tail") << QString(",tail");
        QTest::newRow("bare-comma") << (c+",tail") << QString(",tail");
        QTest::newRow("trailing-comma") << (c+"04,") << QString(",");
        QTest::newRow("valid-colours") << (c+"04,12red") << QString("red");
    }
    void colourParametersPreserveOrdinaryText() {
        QFETCH(QString,raw);QFETCH(QString,expected);
        QString rendered;for(const auto& span:IrcFormatParser::parse(raw))rendered+=span.text;
        QCOMPARE(rendered,expected);QCOMPARE(IrcFormatParser::stripFormatting(raw),expected);
    }
    void ephemeralIdentityAndInvalidState_data() {
        QTest::addColumn<QString>("mapping"); QTest::addColumn<QString>("first"); QTest::addColumn<QString>("alias");
        QTest::newRow("ascii-case") << QString("ascii") << QString("Alice") << QString("ALICE");
        QTest::newRow("rfc-brackets") << QString("rfc1459") << QString("[Alice]") << QString("{ALICE}");
        QTest::newRow("strict-brackets") << QString("strict-rfc1459") << QString("[Alice]") << QString("{ALICE}");
    }
    void ephemeralIdentityAndInvalidState() {
        QFETCH(QString, mapping); QFETCH(QString, first); QFETCH(QString, alias);
        ConnectionManager manager(nullptr,nullptr,nullptr,nullptr,nullptr); FakeClient* client=nullptr;
        manager.setClientFactory([&](qint64 id,const ServerConfig& cfg,QObject* parent){client=new FakeClient(id,cfg,parent);return client;});
        ServerConfig config; config.hostname="localhost"; config.nickname="tester";
        const auto id=manager.addServer(config); client->ready(); client->mapping(mapping);
        Q_EMIT client->channelJoined(id,"#Room");
        auto channel=[&]{const auto snap=manager.connection(id);return *snap->findChannel("#room");};
        Q_EMIT client->typingUserUpdated(id,"#room",first,"active");
        Q_EMIT client->typingUserUpdated(id,"#ROOM",alias,"active");
        QCOMPARE(channel().currentTypingUsers.size(),1);
        Q_EMIT client->typingUserUpdated(id,"#room",alias,"garbage");
        QCOMPARE(channel().currentTypingUsers.size(),1);
        Q_EMIT client->typingUserUpdated(id,"#room",first,"done");
        QVERIFY(channel().currentTypingUsers.isEmpty());
        Q_EMIT client->typingUserUpdated(id,"#room",first,"active");
        IncomingMessage message;message.id="parent";message.target="#room";message.sender=alias;message.content="sent";
        Q_EMIT client->messageReceived(id,message,true);
        QCOMPARE(channel().currentTypingUsers.size(),1);
        message.id="live";Q_EMIT client->messageReceived(id,message,false);
        QVERIFY(channel().currentTypingUsers.isEmpty());
        Q_EMIT client->reactionReceived(id,"#room","parent",first,"ok");
        Q_EMIT client->reactionReceived(id,"#room","parent",alias,"ok");
        auto reactions=[&]{for(const auto& m:channel().messages)if(m.id=="parent")return m.reactions.value("ok").size();return qsizetype(-1);};
        QCOMPARE(reactions(),1);
        Q_EMIT client->reactionReceived(id,"#room","parent","Bob","ok");
        QCOMPARE(reactions(),2);
        manager.disconnectServer(id);
    }
    void repeatedJoinIsIdempotentWhileRealMembershipAndModesChange() {
        TlsIrcServer fixture; fixture.capabilities="extended-join multi-prefix";
        ConnectionManager manager(nullptr,nullptr,nullptr,nullptr,nullptr);
        const auto id=manager.addServer(fixture.config());
        QTRY_VERIFY(manager.client(id)&&manager.client(id)->isConnected());
        QSignalSpy joins(manager.client(id),&ChatClient::channelJoined);
        auto room=[&]()->std::optional<Channel>{
            const auto connection=manager.connection(id);const auto* found=connection?connection->findChannel("#room"):nullptr;
            return found?std::make_optional(*found):std::nullopt;
        };
        auto roster=[&]{QMap<QString,QSet<UserMode>> users;const auto snapshot=room();if(snapshot)for(const auto& user:snapshot->users)users.insert(user.nickname,user.modes);return users;};
        auto markerSeen=[&](const QString& text){const auto snapshot=room();return snapshot&&std::any_of(snapshot->messages.begin(),snapshot->messages.end(),[&](const IncomingMessage& message){return message.content==text;});};
        fixture.send(":tester!u@localhost JOIN #room tester :Fixture\r\n:fixture 353 tester = #room :tester @+alice\r\n:fixture 366 tester #room :End\r\n:alice!a@localhost TOPIC #room :retained topic\r\n");
        QTRY_VERIFY(room()&&room()->users.size()==2&&room()->topic==QString("retained topic"));
        const auto before=roster();QVERIFY(before.value("alice").contains(UserMode::OP));
        fixture.send(":ALICE!a@localhost JOIN #room account :Alice\r\n:TESTER!u@localhost JOIN #ROOM account :Fixture\r\n:alice!a@localhost PRIVMSG #room :duplicate barrier\r\n");
        QTRY_VERIFY(markerSeen("duplicate barrier"));
        QCOMPARE(roster(),before);QCOMPARE(room()->topic.value_or(""),QString("retained topic"));QCOMPARE(joins.size(),1);
        fixture.send(":alice!a@localhost PART #room :normal departure\r\n:alice!a@localhost JOIN #room account :Alice\r\n:tester!u@localhost PRIVMSG #room :real rejoin barrier\r\n");
        QTRY_VERIFY(markerSeen("real rejoin barrier"));
        QVERIFY(roster().contains("alice"));QVERIFY(roster().value("alice").isEmpty());
        fixture.send(":fixture MODE #room +o alice\r\n");QTRY_VERIFY(roster().value("alice").contains(UserMode::OP));
        fixture.send(":fixture MODE #room -o alice\r\n");QTRY_VERIFY(!roster().value("alice").contains(UserMode::OP));
        fixture.send(":tester!u@localhost PART #room :normal departure\r\n");QTRY_VERIFY(!room());
        fixture.send(":tester!u@localhost JOIN #room tester :Fixture\r\n:fixture 353 tester = #room :tester alice\r\n:fixture 366 tester #room :End\r\n");
        QTRY_VERIFY(room()&&room()->users.size()==2);QCOMPARE(joins.size(),2);QVERIFY(!room()->topic.has_value());
        manager.disconnectServer(id);
    }
    void unsolicitedTopicSnapshotsDoNotAdmitUnjoinedConversations() {
        TlsIrcServer fixture;ConnectionManager manager(nullptr,nullptr,nullptr,nullptr,nullptr);
        const auto id=manager.addServer(fixture.config());QTRY_VERIFY(manager.client(id)&&manager.client(id)->isConnected());
        fixture.send(":tester!u@localhost JOIN #room\r\n:alice!a@localhost TOPIC #room :kept\r\n");
        auto room=[&]()->std::optional<Channel>{const auto connection=manager.connection(id);const auto* found=connection?connection->findChannel("#room"):nullptr;return found?std::make_optional(*found):std::nullopt;};
        QTRY_VERIFY(room()&&room()->topic==QString("kept"));
        const auto count=manager.connection(id)->channels.size();
        fixture.send(":fixture 332 tester #unjoined :other topic\r\n:fixture 331 tester #empty :No topic\r\n:alice!a@localhost PRIVMSG #room :topic barrier\r\n");
        auto observed=[&]{const auto snapshot=room();return snapshot&&std::any_of(snapshot->messages.begin(),snapshot->messages.end(),[](const IncomingMessage& message){return message.content=="topic barrier";});};
        QTRY_VERIFY(observed());
        QCOMPARE(manager.connection(id)->channels.size(),count);QVERIFY(!manager.connection(id)->findChannel("#unjoined"));QVERIFY(!manager.connection(id)->findChannel("#empty"));QCOMPARE(room()->topic.value_or(""),QString("kept"));
        manager.disconnectServer(id);
    }

    void topicSnapshotsReplaceContentAndStaleSetter_data() {
        QTest::addColumn<QByteArray>("reply");
        QTest::addColumn<QString>("expected");
        QTest::newRow("no-topic") << QByteArray(":fixture 331 tester #room :No topic is set\r\n") << QString();
        QTest::newRow("snapshot") << QByteArray(":fixture 332 tester #room :new snapshot\r\n") << QString("new snapshot");
        QTest::newRow("empty-snapshot") << QByteArray(":fixture 332 tester #room :\r\n") << QString();
    }
    void topicSnapshotsReplaceContentAndStaleSetter() {
        QFETCH(QByteArray, reply); QFETCH(QString, expected);
        TlsIrcServer fixture;
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        const auto id = manager.addServer(fixture.config());
        QTRY_VERIFY(manager.client(id) && manager.client(id)->isConnected());
        auto channel = [&]() -> std::optional<Channel> {
            const auto connection = manager.connection(id);
            const auto* found = connection ? connection->findChannel("#room") : nullptr;
            return found ? std::optional<Channel>(*found) : std::nullopt;
        };
        fixture.send(":tester!u@localhost JOIN #room\r\n:alice!u@localhost TOPIC #room :initial\r\n");
        QTRY_VERIFY(channel() && channel()->topic.value_or("") == "initial");
        QCOMPARE(channel()->topicSetBy.value_or(""), QString("alice"));
        fixture.send(reply);
        QTRY_VERIFY(channel() && channel()->topic.value_or("") == expected);
        QVERIFY(!channel()->topicSetBy.has_value());
        QVERIFY(!channel()->topicSetTime.has_value());
        manager.disconnectServer(id);
    }
    void missingCommandArgumentsExplainSyntaxAndRetainTheDraft() {
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        FakeClient* client = nullptr;
        manager.setClientFactory([&](qint64 id, const ServerConfig& cfg, QObject* parent) {
            client = new FakeClient(id, cfg, parent); return client;
        });
        ServerConfig cfg; cfg.hostname = "offline.invalid"; cfg.nickname = "tester";
        const auto id = manager.addServer(cfg, false); client->ready();
        const auto status = manager.connection(id)->channels.first().name;
        ChannelPaneWidget pane(&manager, nullptr, nullptr, false); pane.setTargetChannel(id, status);
        auto* composer = pane.findChild<ChatInputWidget*>();
        auto* editor = composer->findChild<ChatTextEdit*>();
        const QStringList missing{"/join","/j","/msg","/msg alice","/privmsg alice","/query","/me","/notice alice","/nick","/whois","/who","/kick","/k","/ban","/unban","/kb","/voice","/devoice","/op","/deop","/quiet","/unquiet","/mode","/invite","/raw","/quote","/unignore"};
        for (const auto& text : missing + QStringList{"/jon #room", "/"}) {
            composer->restoreDraft({text, {}, int(text.size()), -1});
            QTest::keyClick(editor, Qt::Key_Return);
            QCOMPARE(editor->toPlainText(), text);
            QVERIFY(manager.inputHistory(id, status).isEmpty());
            const auto connection = manager.connection(id);
            const auto* channel = connection->findChannel(status);
            QVERIFY(channel && !channel->messages.isEmpty());
            const auto reason = channel->messages.last().content;
            if (missing.contains(text)) {
                QVERIFY2(reason.startsWith("Usage: /") && reason.contains('<'), qPrintable(reason));
            } else {
                QVERIFY(reason.contains("Unknown command") && reason.contains("/help"));
            }
        }
    }
    void directionalOverridesCannotReverseDisplayedUrlFilenames() {
        const QString rlo(QChar(0x202e));
        const QString url = "https://example.invalid/gpj.exe";
        for (auto type : {MessageType::NORMAL, MessageType::ACTION, MessageType::NOTICE}) {
            for (bool inSender : {false, true}) {
                QStandardItemModel model(1, 1);
                const auto index = model.index(0, 0);
                const QString raw = inSender ? url : "https://example.invalid/" + rlo + "gpj.exe";
                model.setData(index, raw, MessageListModel::ContentRole);
                model.setData(index, inSender ? "alice" + rlo : QString("alice"), MessageListModel::SenderRole);
                model.setData(index, int(type), MessageListModel::MessageTypeRole);
                QTextDocument document;
                document.setHtml(MessageItemDelegate::buildRichTextHtml(index, ThemeColors::darkTheme(), UserPreferences{}));
                document.setTextWidth(1200);
                document.documentLayout()->documentSize();
                const auto plain = document.toPlainText();
                QVERIFY(plain.contains("[RLO]"));
                QVERIFY(!plain.contains(QChar(0x202e)));
                const auto at = plain.indexOf("gpj.exe");
                QVERIFY(at >= 0);
                const auto block = document.findBlock(at);
                const auto line = block.layout()->lineForTextPosition(at - block.position());
                QVERIFY(line.isValid());
                for (int n = 0; n < 6; ++n)
                    QVERIFY(line.cursorToX(at - block.position() + n) < line.cursorToX(at - block.position() + n + 1));
                QCOMPARE(index.data(MessageListModel::ContentRole).toString(), raw);
                QSet<QString> anchors;
                for (auto b = document.begin(); b.isValid(); b = b.next())
                    for (auto f = b.begin(); !f.atEnd(); ++f)
                        if (f.fragment().charFormat().isAnchor()) anchors.insert(f.fragment().charFormat().anchorHref());
                if (inSender) QCOMPARE(anchors, QSet<QString>{url});
                else QVERIFY(anchors.isEmpty()); // Never silently rewrite an unsafe destination.
            }
        }
    }
    void cachedTimestampRenderingFollowsTheCurrentSystemTimezone() {
        struct RestoreTimezone {
            QByteArray value = qgetenv("TZ");
            bool existed = qEnvironmentVariableIsSet("TZ");
            ~RestoreTimezone() { if (existed) qputenv("TZ", value); else qunsetenv("TZ"); ::tzset(); }
        } restore;
        qputenv("TZ", "UTC"); ::tzset();
        QStandardItemModel model(1, 1); const auto index = model.index(0, 0);
        model.setData(index, QDateTime::fromString("2024-06-01T12:00:00Z", Qt::ISODate).toMSecsSinceEpoch(), MessageListModel::TimestampRole);
        model.setData(index, "alice", MessageListModel::SenderRole);
        model.setData(index, "clock fixture", MessageListModel::ContentRole);
        model.setData(index, int(MessageType::NORMAL), MessageListModel::MessageTypeRole);
        model.setData(index, qulonglong(1), MessageListModel::RenderRevisionRole);
        auto render = [&](MessageItemDelegate& delegate) {
            QImage image(800, 100, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
            QPainter painter(&image); QStyleOptionViewItem option; option.rect = QRect(0, 0, 800, 100); option.font = QApplication::font();
            delegate.paint(&painter, option, index); painter.end(); return image;
        };
        MessageItemDelegate retained; render(retained);
        for (const auto& zone : {"UTC", "Europe/Berlin", "America/New_York", "Asia/Kathmandu"}) {
            qputenv("TZ", zone); ::tzset();
            MessageItemDelegate fresh;
            QCOMPARE(render(retained), render(fresh));
        }
    }
    void rejectedQuerySendKeepsDraftAndSelection() {
        struct RejectingClient : FakeClient {
            using FakeClient::FakeClient;
            bool sendMessage(const QString&, const QString&, const std::optional<IncomingMessage>& = {}) override { return false; }
        };
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        RejectingClient* client = nullptr;
        manager.setClientFactory([&](qint64 id, const ServerConfig& cfg, QObject* parent) {
            client = new RejectingClient(id, cfg, parent); return client;
        });
        ServerConfig cfg; cfg.hostname = "offline.invalid"; cfg.nickname = "tester";
        const auto id = manager.addServer(cfg, false); client->ready();
        const auto status = manager.connection(id)->channels.first().name;
        manager.setActiveChannel(id, status);
        ChannelPaneWidget pane(&manager, nullptr, nullptr, false); pane.setTargetChannel(id, status);
        auto* composer = pane.findChild<ChatInputWidget*>();
        auto* editor = composer->findChild<ChatTextEdit*>();
        const QString command = "/query alice private draft";
        composer->restoreDraft({command, {}, int(command.size()), -1});
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(editor->toPlainText(), command);
        QVERIFY(manager.inputHistory(id, status).isEmpty());
        QVERIFY(!manager.connection(id)->findChannel("alice"));
        QCOMPARE(manager.connection(id)->currentChannel, std::optional<QString>(status));
        QVERIFY(manager.openQuery(id, "alice")); // Opening without sending remains available.
        QVERIFY(manager.connection(id)->findChannel("alice"));
    }
    void mergedInputHistoryHonorsConfiguredLimit_data() {
        QTest::addColumn<int>("limit");
        QTest::newRow("disabled") << 0;
        QTest::newRow("small") << 3;
        QTest::newRow("default") << 50;
    }
    void mergedInputHistoryHonorsConfiguredLimit() {
        QFETCH(int, limit);
        QTemporaryDir directory;
        PreferencesManager preferences(directory.filePath("prefs.json"));
        preferences.update([&](auto& p) { p.inputHistorySize = limit; });
        ConnectionManager manager(nullptr, nullptr, &preferences, nullptr, nullptr);
        FakeClient* client = nullptr;
        manager.setClientFactory([&](qint64 id, const ServerConfig& cfg, QObject* parent) {
            client = new FakeClient(id, cfg, parent); return client;
        });
        ServerConfig cfg; cfg.hostname = "offline.invalid"; cfg.nickname = "tester";
        const auto id = manager.addServer(cfg, false);
        client->ready(); client->mapping("ascii");
        manager.openQuery(id, "[alice]"); manager.openQuery(id, "{alice}");
        const int count = limit / 2 + 1;
        for (int n = 0; n < count; ++n) {
            QVERIFY(manager.sendUserInput(id, "[alice]", QString("first-%1").arg(n)));
            QVERIFY(manager.sendUserInput(id, "{alice}", QString("second-%1").arg(n)));
        }
        client->mapping("rfc1459");
        QCOMPARE(manager.inputHistory(id, "{alice}").size(), limit);
        QCOMPARE(manager.inputHistory(id, "[alice]"), manager.inputHistory(id, "{alice}"));
    }
    void historyNavigationDraftBelongsToItsConversation_data() {
        QTest::addColumn<QStringList>("otherHistory");
        QTest::newRow("identical-history") << QStringList{"old", "recent"};
        QTest::newRow("different-history") << QStringList{"other recent"};
    }
    void historyNavigationDraftBelongsToItsConversation() {
        QFETCH(QStringList, otherHistory);
        ChatInputWidget input(nullptr);
        auto* editor = input.findChild<ChatTextEdit*>();
        QVERIFY(editor);
        input.setHistory({"old", "recent"});
        input.restoreDraft({"first private draft", {}, 19, -1});
        QTest::keyClick(editor, Qt::Key_Up);
        QCOMPARE(editor->toPlainText(), QString("recent"));
        const auto first = input.draft();
        input.setHistory(otherHistory);
        input.restoreDraft({"second private draft", {}, 20, -1});
        QTest::keyClick(editor, Qt::Key_Up);
        const auto second = input.draft();
        input.setHistory({"old", "recent"});
        input.restoreDraft(first);
        QTest::keyClick(editor, Qt::Key_Down);
        QCOMPARE(editor->toPlainText(), QString("first private draft"));
        input.setHistory(otherHistory);
        input.restoreDraft(second);
        QTest::keyClick(editor, Qt::Key_Down);
        QCOMPARE(editor->toPlainText(), QString("second private draft"));
    }
    void nicknameHighlightsFollowMappingAndVisibleText_data() {
        QTest::addColumn<QString>("nick"); QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("mapping"); QTest::addColumn<bool>("expected");
        QTest::newRow("rfc-brackets") << QString("[tom]") << QString("hello {tom}") << QString("rfc1459") << true;
        QTest::newRow("ascii-brackets") << QString("[tom]") << QString("hello {tom}") << QString("ascii") << false;
        QTest::newRow("rfc-caret") << QString("t^m") << QString("hello t~m") << QString("rfc1459") << true;
        QTest::newRow("strict-caret") << QString("t^m") << QString("hello t~m") << QString("strict-rfc1459") << false;
        QTest::newRow("nick-boundary") << QString("tom") << QString("tom~suffix") << QString("rfc1459") << false;
        QTest::newRow("formatted") << QString("tom") << (QString("to") + QChar(2) + "m") << QString("rfc1459") << true;
    }
    void nicknameHighlightsFollowMappingAndVisibleText() {
        QFETCH(QString, nick); QFETCH(QString, text); QFETCH(QString, mapping); QFETCH(bool, expected);
        QCOMPARE(HighlightMatcher::containsNickMention(text, nick, mapping), expected);
        QCOMPARE(HighlightMatcher::containsHighlight(text, nick, {}, mapping), expected);
    }
    void completionRefreshesRosterAndPreservesEditedText() {
        ChatTextEdit input(nullptr);
        input.setCompletionContext({"albert", "alice"}, {"#room"}, {"join"});
        input.setDraftText("al"); auto cursor = input.textCursor(); cursor.setPosition(2); input.setTextCursor(cursor);
        QTest::keyClick(&input, Qt::Key_Tab); QCOMPARE(input.toPlainText(), QString("albert: "));
        input.setCompletionContext({"albert"}, {"#room"}, {"join"});
        QTest::keyClick(&input, Qt::Key_Tab); QCOMPARE(input.toPlainText(), QString("albert: "));
        input.setCompletionContext({"alex"}, {"#room"}, {"join"});
        QTest::keyClick(&input, Qt::Key_Tab); QCOMPARE(input.toPlainText(), QString("alex: "));
        QTest::keyClicks(&input, "keep draft");
        QTest::keyClick(&input, Qt::Key_Tab); QCOMPARE(input.toPlainText(), QString("alex: keep draft"));
    }
    void completionReplacesTheWholeWordAndLeavesCaretBeforeSuffix() {
        ChatTextEdit input(nullptr);
        input.setCompletionContext({"albert", "alice"}, {}, {});
        input.setDraftText("alic suffix"); auto cursor = input.textCursor(); cursor.setPosition(2); input.setTextCursor(cursor);
        QTest::keyClick(&input, Qt::Key_Tab);
        QCOMPARE(input.toPlainText(), QString("albert: suffix")); QCOMPARE(input.textCursor().position(), 8);
        QTest::keyClick(&input, Qt::Key_Backtab);
        QCOMPARE(input.toPlainText(), QString("alice: suffix")); QCOMPARE(input.textCursor().position(), 7);
        input.setCompletionContext({}, {}, {});
        QTest::keyClick(&input, Qt::Key_Tab); QCOMPARE(input.toPlainText(), QString("alic suffix"));
        QCOMPARE(input.textCursor().position(), 2);
    }
    void departedChannelIgnoresLateRosterUntilRequested_data() {
        QTest::addColumn<bool>("kick");
        QTest::newRow("kick") << true; QTest::newRow("part") << false;
    }
    void departedChannelIgnoresLateRosterUntilRequested() {
        QFETCH(bool, kick);
        TlsIrcServer server; QTemporaryDir temp;
        DatabaseService db(temp.filePath("db.sqlite")); PreferencesManager prefs(temp.filePath("prefs.json"));
        ConnectionManager manager(&db, nullptr, &prefs, nullptr, nullptr);
        auto id = manager.addServer(server.config());
        QTRY_VERIFY(manager.client(id) && manager.client(id)->isConnected());
        bool barrier = false;
        connect(manager.client(id), &ChatClient::messageReceived, this,
            [&](qint64, const IncomingMessage& message, bool) { if (message.content == "exit-barrier") barrier = true; });
        auto users = [&]() { auto c = manager.connection(id); const auto* ch = c->findChannel("#room"); return ch ? ch->users.size() : -1; };
        server.send(":tester!u@h JOIN #room\r\n:fixture 353 tester = #room :tester alice bob\r\n:fixture 366 tester #room :End\r\n");
        QTRY_COMPARE(users(), 3);
        server.send(":fixture 353 tester = #room :tester alice bob\r\n");
        server.send(kick ? ":op!u@h KICK #ROOM tester :Removed\r\n" : ":tester!u@h PART #ROOM :Leaving\r\n");
        server.send(":fixture 353 tester = #room :tester alice bob\r\n:fixture 366 tester #room :End\r\n:fixture NOTICE * :exit-barrier\r\n");
        QTRY_VERIFY(barrier); QCOMPARE(users(), kick ? 0 : -1);
        manager.joinChannel(id, "#room");
        QTRY_VERIFY(server.lines.contains("JOIN #room"));
        server.send(":tester!u@h JOIN #room\r\n:fixture 353 tester = #room :tester newuser\r\n:fixture 366 tester #room :End\r\n");
        QTRY_COMPARE(users(), 2);
        barrier = false;
        server.send(":op!u@h KICK #room tester :Removed\r\n:fixture NOTICE * :exit-barrier\r\n");
        QTRY_VERIFY(barrier); QCOMPARE(users(), 0);
        QVERIFY(manager.client(id)->sendRawCommand("NAMES #room"));
        QTRY_VERIFY(server.lines.contains("NAMES #room"));
        server.send(":fixture 353 tester = #room :someone\r\n:fixture 366 tester #room :End\r\n");
        QTRY_COMPARE(users(), 1);
        manager.disconnectServer(id);
    }
    void duplicateEchoCannotAcknowledgeTheNextIdenticalSend() {
        TlsIrcServer server; server.capabilities = "message-tags echo-message";
        IrcClient client(1, server.config()); QList<IncomingMessage> updates; bool barrier = false;
        connect(&client, &ChatClient::messageUpdated, this,
            [&](qint64, const QString&, const IncomingMessage& message) { updates.append(message); });
        connect(&client, &ChatClient::messageReceived, this,
            [&](qint64, const IncomingMessage& message, bool) { if (message.content == "echo-barrier") barrier = true; });
        client.connectToServer(); QTRY_VERIFY(client.isConnected());
        QVERIFY(client.sendMessage("#room", "identical")); QVERIFY(client.sendMessage("#room", "identical"));
        QTRY_COMPARE(server.lines.count("PRIVMSG #room :identical"), 2);
        server.send("@msgid=one :tester!u@h PRIVMSG #room :identical\r\n"
                    "@msgid=one :tester!u@h PRIVMSG #room :identical\r\n"
                    ":fixture NOTICE * :echo-barrier\r\n");
        QTRY_VERIFY(barrier); QCOMPARE(updates.size(), 1);
        QCOMPARE(updates.first().id, QString("one"));
        server.send("@msgid=two :tester!u@h PRIVMSG #room :identical\r\n");
        QTRY_COMPARE(updates.size(), 2);
        QCOMPARE(updates[1].id, QString("two"));
        QCOMPARE(updates[1].deliveryStatus, QString("delivered"));
        client.disconnectFromServer(); QCOMPARE(updates.size(), 2);
    }
    void incomingTagsUseTheirOwnWireByteLimit_data() {
        QTest::addColumn<bool>("unicode"); QTest::addColumn<bool>("over");
        QTest::newRow("ascii-valid") << false << false;
        QTest::newRow("ascii-over") << false << true;
        QTest::newRow("utf8-valid") << true << false;
        QTest::newRow("utf8-over") << true << true;
    }
    void incomingTagsUseTheirOwnWireByteLimit() {
        QFETCH(bool, unicode); QFETCH(bool, over);
        TlsIrcServer server; server.capabilities = "message-tags";
        IrcClient client(1, server.config()); QList<IncomingMessage> received;
        connect(&client, &ChatClient::messageReceived, this,
            [&](qint64, const IncomingMessage& message, bool) { if (message.content == "tag-body") received.append(message); });
        QSignalSpy errors(&client, &ChatClient::connectionError);
        client.connectToServer(); QTRY_VERIFY(client.isConnected());
        const QString value = unicode ? QString(2727, QChar(0x732b)) + QString(over ? 2 : 1, 'x') : QString(over ? 8183 : 8182, 'x');
        const QByteArray wire = ("@+reply=" + value + " :alice!u@h PRIVMSG #room :tag-body\r\n").toUtf8();
        QCOMPARE(wire.indexOf(' ') + 1, over ? 8192 : 8191);
        server.send(wire);
        if (over) { QTRY_VERIFY(!errors.isEmpty()); QVERIFY(received.isEmpty()); QVERIFY(!client.isConnected()); }
        else { QTRY_COMPARE(received.size(), 1); QCOMPARE(received.first().replyToMessageId.value_or(""), value); }
        client.disconnectFromServer();
    }
    void longCapabilityListsDoNotCrowdOutRequiredAuthentication() {
        TlsIrcServer server;
        for (int page = 0; page < 10; ++page) {
            QStringList caps;
            for (int n = 0; n < 15; ++n) caps << QString("example.test/cap%1=value").arg(page * 15 + n);
            server.capPages << caps.join(' ');
        }
        server.capPages << "sasl=EXTERNAL,PLAIN message-tags echo-message";
        auto cfg = server.config(); cfg.useSasl = true; cfg.saslUsername = "fixture"; cfg.saslPassword = "fixture-only";
        IrcClient client(1, cfg); client.connectToServer();
        QTRY_VERIFY(client.isConnected());
        QVERIFY(server.lines.contains("AUTHENTICATE PLAIN"));
        QVERIFY(server.lines.contains("CAP REQ :sasl message-tags echo-message"));
        QCOMPARE(server.lines.count("CAP END"), 1);
        client.disconnectFromServer();
    }
    void rejectedSendsAreTargetedAndReplayDoesNotFailLiveMessages() {
        TlsIrcServer server; server.capabilities = "echo-message batch message-tags";
        IrcClient client(1, server.config());
        QList<IncomingMessage> updates, errors;
        connect(&client, &ChatClient::messageUpdated, this,
            [&](qint64, const QString&, const IncomingMessage& message) { updates.append(message); });
        connect(&client, &ChatClient::messageReceived, this,
            [&](qint64, const IncomingMessage& message, bool) { if (message.type == MessageType::ERROR) errors.append(message); });
        client.connectToServer(); QTRY_VERIFY(client.isConnected());
        QVERIFY(client.sendMessage("#room", "first"));
        QVERIFY(client.sendMessage("#room", "second"));
        QVERIFY(client.sendMessage("#other", "unrelated"));
        QTRY_VERIFY(server.lines.contains("PRIVMSG #other :unrelated"));
        server.send(":fixture 404 tester #ROOM :Cannot send\r\n");
        QTRY_COMPARE(updates.size(), 1);
        QCOMPARE(updates.first().content, QString("first"));
        QCOMPARE(updates.first().deliveryStatus, QString("failed"));
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(errors.first().target, QString("#ROOM"));
        server.send(":fixture BATCH +h chathistory #room\r\n@batch=h :fixture 404 tester #room :Old error\r\n:fixture BATCH -h\r\n"
                    ":fixture 403 tester #room :Missing channel\r\n:fixture 401 tester missingNick :Missing nick\r\n"
                    ":fixture 442 tester #room :Not joined\r\n:fixture 404 tester :Malformed\r\n");
        QTRY_COMPARE(errors.size(), 6);
        QCOMPARE(updates.size(), 1);
        QCOMPARE(errors[2].target, QString("#room"));
        QCOMPARE(errors[3].target, QString("missingNick"));
        QVERIFY(errors.last().target.startsWith("* "));
        client.disconnectFromServer();
        QTRY_COMPARE(updates.size(), 3);
        QCOMPARE(updates[1].content, QString("second"));
        QCOMPARE(updates[2].content, QString("unrelated"));
        QVERIFY(!client.sendMessage("#room", "offline"));
    }
    void playbackContextSurvivesNestedAndMultilineBatches_data() {
        QTest::addColumn<QString>("type");QTest::addColumn<bool>("nested");QTest::addColumn<bool>("historical");
        QTest::newRow("direct")<<QString("chathistory")<<false<<true;
        QTest::newRow("draft")<<QString("draft/chathistory")<<false<<true;
        QTest::newRow("znc")<<QString("znc.in/playback")<<false<<true;
        QTest::newRow("nested-unknown")<<QString("example.test/unknown")<<true<<true;
        QTest::newRow("nested-multiline")<<QString("draft/multiline")<<true<<true;
        QTest::newRow("live-unknown")<<QString("example.test/unknown")<<false<<false;
        QTest::newRow("live-multiline")<<QString("draft/multiline")<<false<<false;
        QTest::newRow("opaque-case")<<QString("CHATHISTORY")<<false<<false;
    }
    void playbackContextSurvivesNestedAndMultilineBatches() {
        QFETCH(QString,type);QFETCH(bool,nested);QFETCH(bool,historical);
        TlsIrcServer fixture;fixture.capabilities="message-tags server-time batch draft/multiline";
        IrcClient client(1,fixture.config());QSignalSpy received(&client,&ChatClient::messageReceived);
        client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);received.clear();
        if(nested)fixture.send(":fixture BATCH +parent chathistory #room\r\n");
        fixture.send((nested?QByteArray("@batch=parent "):QByteArray())+":fixture BATCH +child "+type.toUtf8()+" #room\r\n");
        fixture.send("@batch=child;msgid=old;time=2020-01-01T00:00:00Z :alice!u@h PRIVMSG #room :first\r\n");
        if(type=="draft/multiline")fixture.send("@batch=child;draft/multiline-concat :alice!u@h PRIVMSG #room :second\r\n");
        fixture.send((nested?QByteArray("@batch=parent "):QByteArray())+":fixture BATCH -child\r\n");
        if(nested)fixture.send(":fixture BATCH -parent\r\n");
        QTRY_COMPARE_WITH_TIMEOUT(received.size(),1,5000);
        const auto message=received.first()[1].value<IncomingMessage>();
        QCOMPARE(received.first()[2].toBool(),historical);QCOMPARE(message.id,QString("old"));
        QCOMPARE(message.content,type=="draft/multiline"?QString("firstsecond"):QString("first"));
        QCOMPARE(message.timestamp,QDateTime::fromString("2020-01-01T00:00:00Z",Qt::ISODate).toMSecsSinceEpoch());
        fixture.send("@msgid=now :alice!u@h PRIVMSG #room :live\r\n");
        QTRY_COMPARE(received.size(),2);QVERIFY(!received.last()[2].toBool());
    }

    void liveAccountTagPublishesEverySharedRosterImmediately() {
        TlsIrcServer fixture;fixture.capabilities="message-tags batch account-tag extended-join away-notify";
        ConnectionManager manager(nullptr,nullptr,nullptr,nullptr,nullptr);const auto id=manager.addServer(fixture.config());
        QTRY_VERIFY_WITH_TIMEOUT(manager.client(id)&&manager.client(id)->isConnected(),5000);
        fixture.send(":tester!u@h JOIN #one tester-account :Tester\r\n:alice!u@h JOIN #one initial-account :Alice\r\n:tester!u@h JOIN #two tester-account :Tester\r\n:alice!u@h JOIN #two initial-account :Alice\r\n");
        QTRY_VERIFY(manager.connection(id)->findChannel("#two"));
        auto account=[&](const QString& channel){const auto state=manager.connection(id);const auto* c=state->findChannel(channel);if(c)for(const auto&u:c->users)if(u.nickname=="alice")return u.account.value_or("");return QString("missing");};
        QTRY_COMPARE(account("#two"),QString("initial-account"));
        fixture.send("@account=current-account;msgid=tag-live :alice!u@h PRIVMSG #one :current\r\n");
        QTRY_COMPARE(account("#one"),QString("current-account"));
        QCOMPARE(account("#two"),QString("current-account"));
        fixture.send(":fixture BATCH +hist chathistory #one\r\n@batch=hist;account=past-account;msgid=tag-old :alice!u@h PRIVMSG #one :old\r\n:fixture BATCH -hist\r\n");
        auto historyArrived=[&](){const auto c=manager.connection(id);for(const auto&m:c->findChannel("#one")->messages)if(m.id=="tag-old")return m.senderAccount=="past-account";return false;};
        QTRY_VERIFY(historyArrived());QCOMPARE(account("#one"),QString("current-account"));QCOMPARE(account("#two"),QString("current-account"));
        fixture.send(":alice!u@h AWAY :Refresh\r\n");
        QTRY_VERIFY([&]{const auto c=manager.connection(id);for(const auto&u:c->findChannel("#two")->users)if(u.nickname=="alice")return u.isAway;return false;}());
        QCOMPARE(account("#one"),QString("current-account"));QCOMPARE(account("#two"),QString("current-account"));
    }

    void replayDoesNotChangeCurrentRosterActivityOrTyping() {
        TlsIrcServer fixture;fixture.capabilities="message-tags server-time batch account-tag extended-join away-notify";
        QTemporaryDir temp;DatabaseService db(temp.filePath("db.sqlite"));PreferencesManager prefs(temp.filePath("prefs.json"));
        prefs.update([](auto&p){p.notificationsEnabled=false;p.soundAlertsEnabled=false;});
        ConnectionManager manager(&db,nullptr,&prefs,nullptr,nullptr);const auto id=manager.addServer(fixture.config());
        QTRY_VERIFY_WITH_TIMEOUT(manager.client(id)&&manager.client(id)->isConnected(),5000);
        auto snapshot=[&](){const auto c=manager.connection(id);return *c->findChannel("#room");};
        fixture.send(":tester!u@h JOIN #room tester-account :Tester\r\n:alice!u@h JOIN #room current-account :Alice\r\n@msgid=current;time=2026-10-23T12:00:00Z :alice!u@h PRIVMSG #room :current\r\n");
        QTRY_VERIFY(manager.connection(id)->findChannel("#room"));
        QTRY_VERIFY(snapshot().lastActivityTimestamp>0);
        fixture.send("@+typing=active :alice!u@h TAGMSG #room\r\n");QTRY_VERIFY(snapshot().currentTypingUsers.contains("alice"));
        const auto before=snapshot();const auto active=manager.lastActiveTimes(id,"#room");
        fixture.send(":fixture BATCH +h chathistory #room\r\n@batch=h;account=old-account;msgid=old;time=2020-01-01T00:00:00Z :alice!u@h PRIVMSG #room :old\r\n@batch=h;+typing=done :alice!u@h TAGMSG #room\r\n:fixture BATCH -h\r\n:alice!u@h AWAY :away-now\r\n");
        auto away=[&](){for(const auto&u:snapshot().users)if(u.nickname=="alice")return u.isAway;return false;};
        QTRY_VERIFY(away());const auto after=snapshot();
        QVERIFY(after.currentTypingUsers.contains("alice"));QCOMPARE(after.lastActivityTimestamp,before.lastActivityTimestamp);
        QCOMPARE(manager.lastActiveTimes(id,"#room"),active);QCOMPARE(after.unreadCount,before.unreadCount);
        for(const auto&u:after.users)if(u.nickname=="alice")QCOMPARE(u.account.value_or(""),QString("current-account"));
        bool metadata=false;for(const auto&m:after.messages)if(m.id=="old"){metadata=true;QCOMPARE(m.senderAccount.value_or(""),QString("old-account"));}
        QVERIFY(metadata);
        fixture.send("@+typing=done :alice!u@h TAGMSG #room\r\n");QTRY_VERIFY(snapshot().currentTypingUsers.isEmpty());
        fixture.send(":fixture BATCH +p znc.in/playback #room\r\n@batch=p;+typing=active :alice!u@h TAGMSG #room\r\n:fixture BATCH -p\r\n:alice!u@h AWAY\r\n");
        QTRY_VERIFY(!away());QVERIFY(snapshot().currentTypingUsers.isEmpty());
        fixture.send("@+typing=active :alice!u@h TAGMSG #room\r\n");QTRY_VERIFY(snapshot().currentTypingUsers.contains("alice"));
        fixture.send("@msgid=fresh;account=new-account :alice!u@h PRIVMSG #room :fresh\r\n:alice!u@h AWAY :again\r\n");
        QTRY_VERIFY(away());QVERIFY(snapshot().currentTypingUsers.isEmpty());
        for(const auto&u:snapshot().users)if(u.nickname=="alice")QCOMPARE(u.account.value_or(""),QString("new-account"));
    }

    void timelineRemainsSortedAfterLiveEchoAndCasefoldUpdates() {
        QTemporaryDir temp;PreferencesManager prefs(temp.filePath("prefs.json"));
        prefs.update([](auto&p){p.notificationsEnabled=false;p.soundAlertsEnabled=false;});
        ConnectionManager manager(nullptr,nullptr,&prefs,nullptr,nullptr);FakeClient* client=nullptr;
        manager.setClientFactory([&](qint64 id,const ServerConfig&cfg,QObject*parent){client=new FakeClient(id,cfg,parent);return client;});
        ServerConfig config;config.hostname="fixture.invalid";config.nickname="tester";
        const auto id=manager.addServer(config);client->ready();client->mapping("ascii");
        auto send=[&](const QString& key,qint64 time,const QString& target,bool history=false){
            IncomingMessage message;message.id=key;message.target=target;message.sender="alice";message.content=key;message.timestamp=time;
            Q_EMIT client->messageReceived(id,message,history);
        };
        auto ids=[&](){QStringList rows;const auto connection=manager.connection(id);const auto*channel=connection->findChannel("#Room");if(channel)for(const auto&m:channel->messages)rows<<m.id;return rows;};
        send("future",3000,"#Room");send("past",1000,"#Room");send("middle",2000,"#Room",true);
        QCOMPARE(ids(),(QStringList{"past","middle","future"}));
        IncomingMessage echo;echo.id="echo";echo.target="#Room";echo.sender="tester";echo.content="echo";echo.timestamp=500;echo.isSelf=true;echo.deliveryStatus="delivered";
        Q_EMIT client->messageUpdated(id,"future",echo);
        QCOMPARE(ids(),(QStringList{"echo","past","middle"}));
        send("alias",250,"#room");client->mapping("rfc1459");
        QCOMPARE(ids(),(QStringList{"alias","echo","past","middle"}));
        send("tie-a",1000,"#Room");send("tie-b",1000,"#Room");
        QCOMPARE(ids(),(QStringList{"alias","echo","past","tie-a","tie-b","middle"}));
    }

    void serverHostSyntaxHandlesDomainsAndLiterals() {
        QCOMPARE(normalizedServerHost(" bücher.example ").value(), QString("xn--bcher-kva.example"));
        QCOMPARE(normalizedServerHost("[::1]").value(), QString("::1"));
        QCOMPARE(normalizedServerHost("IRC.Example.").value(), QString("irc.example."));
        for (const auto& input : QStringList{"localhost", "127.0.0.1", "2001:db8::1", "fe80::1%eth0"}) QVERIFY(normalizedServerHost(input));
        for (const auto& input : QStringList{"", " ", "bad host", "https://irc.example", "irc.example/path", "irc.example:6697", "-bad.example", "bad..example", "[127.0.0.1]", "[::1", "::invalid", "256.0.0.1", "01.2.3.4", "127.1", QString(64,'a')+".example", "x\n.example"}) QVERIFY2(!normalizedServerHost(input), qPrintable(input));
    }
    void invalidHostStopsBeforeOpeningATransport() {
        ServerConfig config;config.hostname="https://irc.example";config.nickname="fixture";
        IrcClient client(1,config);QSignalSpy errors(&client,&ChatClient::connectionError);
        client.connectToServer();QCOMPARE(errors.size(),1);QVERIFY(!client.isConnected());
        QCOMPARE(client.retryDisposition(),RetryDisposition::UserActionRequired);
        QVERIFY(client.findChildren<QAbstractSocket*>().isEmpty());
    }
    void capabilityAckPagesAreAppliedAtomically() {
        TlsIrcServer fixture;fixture.capabilities="echo-message server-time";fixture.skipCapabilityAck=true;
        IrcClient client(1,fixture.config());client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("CAP REQ :server-time echo-message"),5000);
        fixture.send(":fixture CAP tester ACK :unrequested-fixture\r\n");
        QTest::qWait(100);QVERIFY(!fixture.lines.contains("CAP END"));
        fixture.send(":fixture CAP tester ACK * :echo-message\r\n");
        QTest::qWait(100);QVERIFY(client.enabledCapabilities().isEmpty());QVERIFY(!client.isConnected());
        fixture.send(":fixture CAP tester ACK :server-time\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        QCOMPARE(client.enabledCapabilities(),(QSet<QString>{"echo-message","server-time"}));
    }
    void rejectedOptionalCapabilityRequestFinishesRegistration() {
        TlsIrcServer fixture;fixture.capabilities="echo-message server-time";fixture.skipCapabilityAck=true;
        fixture.extra=[&](const QString& line){if(line.startsWith("CAP REQ :"))fixture.send(":fixture CAP tester NAK :"+line.mid(9).toUtf8()+"\r\n");};
        IrcClient client(1,fixture.config());client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);QVERIFY(client.enabledCapabilities().isEmpty());
    }

    void unsolicitedCapabilityRepliesDoNotChangeState() {
        TlsIrcServer fixture; fixture.capabilities="echo-message";
        IrcClient client(1,fixture.config());client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        QVERIFY(client.enabledCapabilities().contains("echo-message"));
        fixture.send(":fixture CAP tester DEL :echo-message\r\n");
        QTRY_VERIFY(!client.enabledCapabilities().contains("echo-message"));
        fixture.send(":fixture CAP tester ACK :echo-message unrequested-fixture\r\n");
        QTest::qWait(100);QVERIFY(client.enabledCapabilities().isEmpty());
        fixture.send(":fixture CAP tester NEW :echo-message\r\n");
        QTRY_VERIFY(client.enabledCapabilities().contains("echo-message"));
        QVERIFY(client.isConnected());
    }

    void rejectedRegistrationNicknameRequiresUserAction() {
        TlsIrcServer fixture; fixture.skipWelcome=true;
        fixture.extra=[&](const QString& line){if(line.startsWith("NICK "))fixture.send(":fixture 432 * tester :Erroneous nickname\r\n");};
        IrcClient client(1,fixture.config());QSignalSpy errors(&client,&ChatClient::connectionError);
        client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),5000);
        QVERIFY(!client.isConnected());QCOMPARE(client.retryDisposition(),RetryDisposition::UserActionRequired);
    }
    void rejectedNickChangeDoesNotDropAnEstablishedSession() {
        TlsIrcServer fixture;IrcClient client(1,fixture.config());client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        fixture.send(":fixture 432 tester 1bad :Erroneous nickname\r\n");QTest::qWait(100);
        QVERIFY(client.isConnected());QCOMPARE(client.getNickname(),QString("tester"));
    }

    void remotePasteResultCannotChooseAnUntrustedDestination() {
        const QStringList hosts{QStringLiteral("0x0.st")};
        QVERIFY(validUploadResult("https://0x0.st/abc.txt", hosts));
        for (const auto& value : QStringList{"http://0x0.st/a", "https://attacker.invalid/a", "https://user:secret@0x0.st/a", "https://0x0.st/a trailing", "https://0x0.st/a\r\nPRIVMSG #x :bad", "https://0x0.st:443/a", "https://0x0.st/a#fragment"})
            QVERIFY2(!validUploadResult(value, hosts), qPrintable(value));
        QVERIFY(!validUploadResult(QString("https://0x0.st/a") + QChar(0) + "b", hosts));
    }

    void serviceCredentialsNeverReachHistoryEchoOrReplay() {
        TlsIrcServer fixture;fixture.capabilities="message-tags echo-message batch";
        QTemporaryDir directory;DatabaseService db(directory.filePath("db.sqlite"));
        ConnectionManager manager(&db,nullptr,nullptr,nullptr,nullptr);
        auto id=manager.addServer(fixture.config());QTRY_VERIFY_WITH_TIMEOUT(manager.client(id)->isConnected(),5000);
        const auto status=manager.connection(id)->channels.first().name;
        QVERIFY(manager.sendUserInput(id,status,"/msg NickServ IDENTIFY synthetic-secret"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("PRIVMSG NickServ :IDENTIFY synthetic-secret"),3000);
        fixture.send(":tester!u@h PRIVMSG NickServ :IDENTIFY synthetic-secret\r\n");
        fixture.send(":fixture BATCH +b chathistory NickServ\r\n@batch=b :tester!u@h PRIVMSG NickServ :IDENTIFY synthetic-secret\r\n:fixture BATCH -b\r\n");
        QTest::qWait(200);
        QVERIFY(manager.inputHistory(id,status).isEmpty());
        const auto snapshot = manager.connection(id);
        QVERIFY(snapshot.has_value());
        for(const auto& ch:snapshot->channels)for(const auto& msg:ch.messages)QVERIFY(!msg.content.contains("synthetic-secret"));
        QVERIFY(manager.sendUserInput(id,status,"/msg NickServ HELP"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("PRIVMSG NickServ :HELP"),3000);
        QCOMPARE(manager.inputHistory(id,status).size(),1);
    }
    void serviceAliasesAndRawCredentialsAreRecognized() {
        for(const auto& input:QStringList{"/ns identify secret","/msg nIcKsErV@services.test IDENTIFY account secret","/raw PRIVMSG NickServ :REGISTER secret mail","/quote AUTHENTICATE c2VjcmV0","/oper name secret","/join #room key","/cs SET PASSWORD secret"})
            QVERIFY2(ircSecrets::input("#room",input),qPrintable(input));
        QVERIFY(ircSecrets::input("NickServ","IDENTIFY secret"));
        QVERIFY(!ircSecrets::input("#room","ordinary text"));
        QVERIFY(!ircSecrets::input("#room","/msg NickServ HELP"));
        QVERIFY(!ircSecrets::input("#room","/msg Alice IDENTIFY this topic"));
    }
    void plaintextSavedCredentialsAreRejected_data() {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void plaintextSavedCredentialsAreRejected() {
        QFETCH(int, kind);
        QTcpServer fixture; QVERIFY(fixture.listen(QHostAddress::LocalHost,0));
        ServerConfig cfg; cfg.hostname="127.0.0.1";cfg.port=fixture.serverPort();cfg.nickname="tester";cfg.useSsl=false;
        if (kind == 0) { cfg.useSasl=true;cfg.saslPassword="fixture-only"; }
        if (kind == 1) cfg.nickServPassword="fixture-only";
        if (kind == 2) cfg.onConnectCommands="PRIVMSG NickServ :IDENTIFY fixture-only";
        if (kind == 3) cfg.channelKeys.insert("#locked","fixture-only");
        IrcClient client(1,cfg);QSignalSpy errors(&client,&ChatClient::connectionError);
        client.connectToServer();QTest::qWait(100);
        QCOMPARE(errors.size(),1);QVERIFY(!fixture.hasPendingConnections());QVERIFY(!client.isConnected());
        QCOMPARE(client.retryDisposition(),RetryDisposition::UserActionRequired);
    }
    void requiredSaslFailsClosed_data() {
        QTest::addColumn<QString>("caps");QTest::addColumn<QString>("result");
        QTest::newRow("missing") << "" << "903";
        QTest::newRow("unsupported") << "sasl=EXTERNAL" << "903";
        QTest::newRow("rejected-credentials") << "sasl=PLAIN" << "904";
    }
    void requiredSaslFailsClosed() {
        QFETCH(QString,caps);QFETCH(QString,result);
        TlsIrcServer fixture;fixture.capabilities=caps;fixture.authResult=result;
        auto cfg=fixture.config();cfg.useSasl=true;cfg.saslUsername="fixture";cfg.saslPassword="fixture-only";cfg.autoJoinChannels="#room";
        IrcClient client(1,cfg);QSignalSpy errors(&client,&ChatClient::connectionError);QSignalSpy ready(&client,&ChatClient::connected);
        client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),5000);QTest::qWait(100);
        QCOMPARE(ready.size(),0);QVERIFY(!client.isConnected());QCOMPARE(client.retryDisposition(),RetryDisposition::UserActionRequired);
        QVERIFY(std::none_of(fixture.lines.begin(),fixture.lines.end(),[](const QString& line){return line.startsWith("JOIN ");}));
    }
    void successfulSaslRegistersBeforeJoining_data() {
        QTest::addColumn<QString>("result");QTest::newRow("authenticated") << "903";QTest::newRow("already-authenticated") << "907";
    }
    void successfulSaslRegistersBeforeJoining() {
        QFETCH(QString,result);
        TlsIrcServer fixture;fixture.capabilities="sasl=PLAIN";fixture.authResult=result;
        auto cfg=fixture.config();cfg.useSasl=true;cfg.saslUsername="fixture";cfg.saslPassword="fixture-only";cfg.autoJoinChannels="#room";
        IrcClient client(1,cfg);client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("JOIN #room"),3000);
        QVERIFY(fixture.lines.indexOf("JOIN #room") > fixture.lines.indexOf("CAP END"));
    }
    void outgoingBurstIsPacedControlIsPromptAndDisconnectRetiresQueue() {
        TlsIrcServer fixture;auto cfg=fixture.config();IrcClient client(1,cfg);client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        for(int i=0;i<64;++i)client.joinChannel(QString("#burst%1").arg(i));
        client.sendRawCommand("PONG :current-challenge");QTest::qWait(120);
        const auto sent=std::count_if(fixture.lines.begin(),fixture.lines.end(),[](const QString& line){return line.startsWith("JOIN #burst");});
        QVERIFY2(sent<=4,qPrintable(QString::number(sent)));QVERIFY(fixture.lines.contains("PONG :current-challenge"));
        client.disconnectFromServer();QTest::qWait(700);const auto before=fixture.lines.size();
        client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);QTest::qWait(700);
        for(const auto& line:fixture.lines.mid(before))QVERIFY(!line.startsWith("JOIN #burst"));
    }
    void rejectedRegisteredNickKeepsConfirmedIdentity() {
        TlsIrcServer fixture;IrcClient client(1,fixture.config());client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        fixture.send(":fixture 433 tester occupied :Nickname in use\r\n");QTest::qWait(150);
        QCOMPARE(client.getNickname(),QString("tester"));QVERIFY(!fixture.lines.contains("NICK tester_"));
    }
    void certificateMismatchIsActionableRatherThanRetryable() {
        TlsIrcServer fixture;auto cfg=fixture.config();cfg.pinnedCertFingerprint=QString(64,'0');
        IrcClient client(1,cfg);QSignalSpy errors(&client,&ChatClient::connectionError);client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),5000);QVERIFY(!client.isConnected());QCOMPARE(client.retryDisposition(),RetryDisposition::UserActionRequired);
        QVERIFY(!fixture.lines.contains("CAP LS 302"));
    }
    void equivalentQueriesMergeAndNegotiatedAsciiCanSeparateThem() {
        ConnectionManager manager(nullptr,nullptr,nullptr,nullptr,nullptr);FakeClient* client=nullptr;
        manager.setClientFactory([&](qint64 id,const ServerConfig& cfg,QObject* parent){client=new FakeClient(id,cfg,parent);return client;});
        ServerConfig cfg;cfg.hostname="offline.invalid";cfg.nickname="tester";const auto id=manager.addServer(cfg,false);
        manager.openQuery(id,"[alice]");manager.openQuery(id,"{alice}");QCOMPARE(manager.connection(id)->channels.size(),2);
        client->mapping("ascii");manager.openQuery(id,"[alice]");manager.openQuery(id,"{alice}");QCOMPARE(manager.connection(id)->channels.size(),3);
        client->mapping("rfc1459");QCOMPARE(manager.connection(id)->channels.size(),2);
    }
    void connectIsIdempotentAndReadyCancelsRetry() {
        ConnectionManager manager(nullptr,nullptr,nullptr,nullptr,nullptr);FakeClient* client=nullptr;
        manager.setClientFactory([&](qint64 id,const ServerConfig& cfg,QObject* parent){client=new FakeClient(id,cfg,parent);return client;});
        ServerConfig cfg;cfg.hostname="offline.invalid";cfg.nickname="tester";const auto id=manager.addServer(cfg,false);
        manager.connectServer(id);manager.connectServer(id);QCOMPARE(client->dials,1);
        client->ready();manager.connectServer(id);QCOMPARE(client->dials,1);
        client->fail(RetryDisposition::Transient);client->ready();QTest::qWait(6500);QCOMPARE(client->dials,1);
        client->fail(RetryDisposition::UserActionRequired);QVERIFY(manager.connection(id)->policy.paused);QCOMPARE(manager.connection(id)->state,ConnectionState::ERROR);
        manager.connectServer(id);QCOMPARE(client->dials,2);QVERIFY(!manager.connection(id)->policy.paused);
    }
    void mappingChangeRetainsOffscreenDraftsAndAliasSelection() {
        ConnectionManager manager(nullptr,nullptr,nullptr,nullptr,nullptr);FakeClient* client=nullptr;
        manager.setClientFactory([&](qint64 id,const ServerConfig& cfg,QObject* parent){client=new FakeClient(id,cfg,parent);return client;});
        ServerConfig cfg;cfg.hostname="offline.invalid";cfg.nickname="tester";const auto id=manager.addServer(cfg,false);
        client->mapping("ascii");manager.openQuery(id,"[alice]");manager.openQuery(id,"bob");
        ChannelPaneWidget pane(&manager,nullptr,nullptr,false);auto* composer=pane.findChild<ChatInputWidget*>();QVERIFY(composer);
        pane.setTargetChannel(id,"[alice]");composer->restoreDraft({"retained draft",std::nullopt,14,-1});pane.setTargetChannel(id,"bob");
        client->mapping("rfc1459");pane.setTargetChannel(id,"{alice}");QCOMPARE(composer->draft().text,QString("retained draft"));
        pane.setTargetChannel(id,"[alice]");QCOMPARE(composer->draft().text,QString("retained draft"));
    }
    void ignoreListUsesNegotiatedIdentityWithoutDroppingStoredPreferences() {
        QTemporaryDir directory;PreferencesManager preferences(directory.filePath("prefs.json"));
        QVERIFY(preferences.addIgnoredUser("[Alice]","ascii"));QVERIFY(preferences.isUserIgnored("{alice}","rfc1459"));
        QVERIFY(!preferences.isUserIgnored("{alice}","ascii"));QVERIFY(preferences.removeIgnoredUser("{alice}","rfc1459"));QVERIFY(preferences.preferences().ignoredUsers.isEmpty());
    }
    void keyedJoinPersistsAndNickServWaitsForAccountEvidence() {
        TlsIrcServer fixture;fixture.confirmWhois=false;
        QTemporaryDir directory;DatabaseService db(directory.filePath("fixture.db"));QVERIFY(db.isReady());
        auto cfg=fixture.config();cfg.nickServPassword="fixture-only";
        ConnectionManager manager(&db,nullptr,nullptr,nullptr,nullptr);const auto id=manager.addServer(cfg,false);
        manager.joinChannel(id,"#locked","fixture-key");
        QCOMPARE(db.getAllServerConfigs().first().channelKeys.value("#locked"),QString("fixture-key"));
        manager.connectServer(id);QTRY_VERIFY_WITH_TIMEOUT(manager.client(id)->isConnected(),5000);QTest::qWait(200);
        QVERIFY(!fixture.lines.contains("JOIN #locked fixture-key"));
        fixture.send(":fixture 330 tester tester fixture-account :is logged in as\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("JOIN #locked fixture-key"),3000);
        manager.partChannel(id,"#locked");QVERIFY(db.getAllServerConfigs().first().channelKeys.isEmpty());
    }
    void multiJoinValidatesWholeListAndKeepsKeyPositions() {
        TlsIrcServer fixture;
        QTemporaryDir directory; DatabaseService db(directory.filePath("fixture.db"));
        ConnectionManager manager(&db,nullptr,nullptr,nullptr,nullptr);
        const auto id=manager.addServer(fixture.config(),false);
        manager.connectServer(id); QTRY_VERIFY_WITH_TIMEOUT(manager.client(id)->isConnected(),5000);
        QVERIFY(manager.connection(id)->encrypted);
        manager.joinChannel(id,"#one,#two",",second-key");
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("JOIN #one") && fixture.lines.contains("JOIN #two second-key"),3000);
        QCOMPARE(db.getAllServerConfigs().first().channelKeys.value("#two"),QString("second-key"));
        QSignalSpy errors(&manager,&ConnectionManager::operationError);
        manager.joinChannel(id,"#must-not-send,");
        manager.joinChannel(id,"#must-not-send", "one,two");
        QCOMPARE(errors.size(),2); QTest::qWait(200);
        QVERIFY(!fixture.lines.contains("JOIN #must-not-send"));
        QVERIFY(!db.getAllServerConfigs().first().autoJoinChannels.contains("must-not-send"));
    }
    void credentialPreferencesArePrivateOnCreateLoadAndReplace() {
        QTemporaryDir directory;const auto path=directory.filePath("preferences.json");
        PreferencesManager prefs(path);
        QVERIFY(prefs.update([](auto&p){p.imgbbApiKey="synthetic-test-key";}));
        const auto publicBits=QFileDevice::ReadGroup|QFileDevice::WriteGroup|QFileDevice::ExeGroup|
            QFileDevice::ReadOther|QFileDevice::WriteOther|QFileDevice::ExeOther;
        QVERIFY(!(QFile::permissions(path)&publicBits));
        QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();original.close();
        QVERIFY(QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ReadGroup|QFileDevice::ReadOther));
        PreferencesManager loaded(path);QVERIFY(loaded.lastError().isEmpty());
        QVERIFY(!(QFile::permissions(path)&publicBits));
        QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),bytes);original.close();
        QVERIFY(loaded.update([](auto&p){p.spellCheckEnabled=!p.spellCheckEnabled;}));
        QVERIFY(!(QFile::permissions(path)&publicBits));
    }

    void longChatSplitsScalarsAndReservesTheVisiblePrefix() {
        const QString text = QString::fromUtf8("🌍漢").repeated(150);
        const auto chunks = ircTextChunks(text, 101);
        QVERIFY(chunks); QCOMPARE(chunks->join(""), text);
        for (const auto& part : *chunks) {
            QVERIFY(part.toUtf8().size() <= 101);
            QCOMPARE(QString::fromUtf8(part.toUtf8()), part);
        }
        QVERIFY(!ircTextChunks("hello\r\nJOIN #oops", 400));
        QVERIFY(!ircTextChunks(QString(9000, 'x'), 400));
        QVERIFY(!ircTextChunks(QString(200, 'x'), 4));
        QVERIFY(!ircTextChunks(QString(QChar(0xd800)), 400));
        TlsIrcServer fixture; IrcClient client(1,fixture.config());client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        const QString prefix = ":tester!u@" + QString(200,'h') + " ";
        fixture.send((prefix + "JOIN #room\r\n").toUtf8());QTest::qWait(100);
        const QString message = QString::fromUtf8("🌍").repeated(150);
        QVERIFY(client.sendMessage("#room",message));
        auto collected=[&](){QString result;for(const auto& line:fixture.lines)if(line.startsWith("PRIVMSG #room :"))result+=line.mid(15);return result;};
        QTRY_COMPARE_WITH_TIMEOUT(collected(),message,10000);
        for(const auto& line:fixture.lines)if(line.startsWith("PRIVMSG #room :"))
            QVERIFY((prefix+line).toUtf8().size()<=510);
        QVERIFY(!client.sendMessage("#room","bad\nJOIN #oops"));
        QVERIFY(!client.sendMessage("NickServ", "IDENTIFY " + QString(600,'x')));
    }

    void chatChunksKeepGraphemesAndRejectOversizedClustersBeforeSending() {
        for (const auto& cluster : {QString::fromUtf8("👩‍👩‍👧‍👦"), QString::fromUtf8("👍🏽"),
                QString::fromUtf8("🇬🇧"), QString::fromUtf8("é"), QString::fromUtf8("1️⃣"),
                QString::fromUtf8("❤️")}) {
            const auto prefix = QString(100 - cluster.left(cluster[0].isHighSurrogate() ? 2 : 1).toUtf8().size(), 'a');
            const auto text = prefix + cluster + "zz";
            const auto parts = ircTextChunks(text, 100);
            QVERIFY(parts); QCOMPARE(parts->join(""), text); QCOMPARE(parts->first(), prefix);
            for (const auto& part : *parts) QVERIFY(part.toUtf8().size() <= 100);
        }
        const auto oversized = QString("a") + QString(QChar(0x301)).repeated(300);
        QVERIFY(!ircTextChunks(oversized, 400));
        TlsIrcServer fixture; IrcClient client(1, fixture.config()); client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);
        QVERIFY(!client.sendMessage("#room", QString(600, 'x') + oversized));
        QTest::qWait(200);
        for (const auto& line : fixture.lines) QVERIFY(!line.startsWith("PRIVMSG #room :"));
        QVERIFY(client.sendMessage("#room", "still-usable"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("PRIVMSG #room :still-usable"), 3000);
    }

    void historyCtcpDoesNotTriggerRepliesAndFloodLeavesRoomForChat() {
        TlsIrcServer fixture;fixture.capabilities="message-tags batch";
        IrcClient client(1,fixture.config());client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        fixture.send(":fixture BATCH +outer chathistory #room\r\n@batch=outer BATCH +inner unknown #room\r\n@batch=inner :Past!u@h PRIVMSG tester :\001PING old\001\r\nBATCH -inner\r\nBATCH -outer\r\n");
        QTest::qWait(200);
        for(const auto& line:fixture.lines)QVERIFY(!line.startsWith("NOTICE Past"));
        QByteArray flood;for(int i=0;i<50;i++)flood+=":Flood!u@h PRIVMSG tester :\001VERSION\001\r\n";
        fixture.send(flood);QTest::qWait(200);QVERIFY(client.sendMessage("#room","after-flood"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.lines.contains("PRIVMSG #room :after-flood"),3000);
        QTest::qWait(1000);int replies=0;for(const auto& line:fixture.lines)if(line.startsWith("NOTICE Flood"))replies++;
        QCOMPARE(replies,3);QVERIFY(client.isConnected());
    }

    void tenThousandUserRosterFitsButAnExcessUserIsRejected() {
        TlsIrcServer fixture;IrcClient client(1,fixture.config());QSignalSpy roster(&client,&ChatClient::userListUpdated);
        client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        QByteArray input;
        for(int at=0;at<10000;at+=20) {
            QStringList names;for(int i=at;i<at+20;i++)names<<QString("Peer%1").arg(i);
            input+=":fixture 353 tester = #large :"+names.join(' ').toUtf8()+"\r\n";
        }
        input+=":fixture 366 tester #large :End\r\n";fixture.send(input);
        QTRY_VERIFY_WITH_TIMEOUT(!roster.isEmpty(),10000);
        QCOMPARE(roster.last()[2].value<QList<ChannelUser>>().size(),10000);QVERIFY(client.isConnected());
        QSignalSpy errors(&client,&ChatClient::connectionError);
        fixture.send(":Excess!u@h JOIN #large\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(),3000);QVERIFY(!client.isConnected());
    }

    void rosterSnapshotIsSilentAndRetainsInterleavedMembershipChanges() {
        TlsIrcServer fixture;IrcClient client(1,fixture.config());QSignalSpy messages(&client,&ChatClient::messageReceived);QSignalSpy roster(&client,&ChatClient::userListUpdated);
        client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        fixture.send(":tester!u@host JOIN #room\r\n:fixture 353 tester = #room :tester [alice] bob\r\n:new!u@host JOIN #room\r\n:{alice}!u@host QUIT :gone\r\n:fixture 366 tester #room :End\r\n");
        QTRY_VERIFY_WITH_TIMEOUT(!roster.isEmpty(),3000);
        const auto users=roster.last()[2].value<QList<ChannelUser>>();QCOMPARE(users.size(),3);
        QVERIFY(std::none_of(users.begin(),users.end(),[](const ChannelUser& u){return u.nickname=="[alice]";}));
        int joins=0;for(const auto& row:messages)if(row[1].value<IncomingMessage>().type==MessageType::JOIN)++joins;
        QCOMPARE(joins,1);
    }
    void bulkChatAdmissionPreflightsEveryLogicalLineBeforeAnySubmission() {
        for (int count : {1,64,65,200,-1}) {
            TlsIrcServer fixture;fixture.capabilities="echo-message";
            QTemporaryDir temp;DatabaseService db(temp.filePath("db"));MessageCache cache(temp.filePath("cache"));PreferencesManager prefs(temp.filePath("prefs"));
            prefs.update([](auto&p){p.pastebinEnabled=false;p.urlShorteningEnabled=false;p.notificationsEnabled=false;p.soundAlertsEnabled=false;p.loggingEnabled=false;p.processLinkPreviewsFromOthers=false;p.urlImageDisplayMode=UrlImageDisplayMode::DISABLED;});
            TextLogService logs(temp.filePath("logs"));SoundService sounds;ConnectionManager manager(&db,&cache,&prefs,&logs,&sounds);
            auto cfg=fixture.config();cfg.username="synthetic";cfg.realName="Synthetic admission test";const auto id=manager.addServer(cfg);
            QTRY_VERIFY_WITH_TIMEOUT(manager.client(id)&&manager.client(id)->isConnected(),5000);
            fixture.send(":tester!u@localhost JOIN #room\r\n");
            QTRY_VERIFY_WITH_TIMEOUT(manager.connection(id)&&manager.connection(id)->findChannel("#room"),3000);
            QStringList lines;for(int index=0;index<count;++index)lines<<QString("batch-line-%1").arg(index);
            if(count==-1)lines={QStringLiteral("batch-line-valid-prefix"),QStringLiteral("x")+QString(500,QChar(0x0301))};
            const auto text=lines.join('\n');int failures=0;bool intact=false;
            const auto observer=QObject::connect(&manager,&ConnectionManager::inputFailed,&manager,[&,text](qint64,const QString&,const QString&draft,const std::optional<IncomingMessage>&,const QString&){++failures;intact=draft==text;});
            const bool expected=count>0&&count<=64;
            QCOMPARE(manager.sendUserInput(id,"#room",text),expected);
            auto wireCount=[&]{return std::count_if(fixture.lines.begin(),fixture.lines.end(),[](const QString&line){return line.startsWith("PRIVMSG #room :batch-line-");});};
            if(expected)QTRY_VERIFY_WITH_TIMEOUT(wireCount()>0,3000);else QTest::qWait(150);
            const auto connection=manager.connection(id);QVERIFY(connection);const auto*room=connection->findChannel("#room");QVERIFY(room);
            const auto local=std::count_if(room->messages.begin(),room->messages.end(),[](const IncomingMessage&m){return m.isSelf&&m.content.startsWith("batch-line-");});
            QCOMPARE(local,expected?count:0);QCOMPARE(failures,expected?0:1);
            if(!expected){QCOMPARE(wireCount(),0);QVERIFY(intact);}
            QObject::disconnect(observer);manager.disconnectServer(id,"Admission control completed");
        }
    }

    void timestampReplayIdentityPreservesRepeatedPayloadOccurrencesAndBoundsState() {
        TimestampReplayIdentity ids;const QString time="2020-01-01T00:00:00.000Z";
        const QStringList fields{"1","#room","Alice","PRIVMSG","same"};
        const auto first=ids.next(time,fields),second=ids.next(time,fields);
        QVERIFY(first!=second);
        QCOMPARE(ids.next(time,fields,"history"),first);QCOMPARE(ids.next(time,fields,"history"),second);
        ids.end("history");QCOMPARE(ids.next(time,fields,"history"),first);
        TimestampReplayIdentity fresh;QCOMPARE(fresh.next(time,fields,"another transport"),first);
        const auto next=ids.next(time,fields);QVERIFY(next!=first&&next!=second);
        QVERIFY(ids.next({},fields)!=ids.next({},fields));
        TimestampReplayIdentity bounded(2);
        for(int i=0;i<50;++i){auto payload=fields;payload<<QString::number(i);bounded.next(time,payload,"h");QVERIFY(bounded.retainedEntries()<=4);}
        bounded.end("h");QVERIFY(bounded.retainedEntries()<=2);
    }
    void timestampOnlyHistoryKeepsStableIdsAcrossNestedReplayButNotRepeatedLiveFrames() {
        TlsIrcServer fixture;fixture.capabilities="server-time batch draft/chathistory";
        IrcClient client(1,fixture.config());QSignalSpy rows(&client,&ChatClient::messageReceived);
        client.connectToServer();QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(),5000);
        const QByteArray frame="@time=2020-01-01T00:00:00.000Z :alice!u@h PRIVMSG #room :repeat";
        fixture.send(frame+"\r\n"+frame+"\r\n");
        auto chat=[&]{QList<QPair<IncomingMessage,bool>> found;for(const auto&row:rows){const auto m=row[1].value<IncomingMessage>();if(m.content=="repeat")found.append({m,row[2].toBool()});}return found;};
        QTRY_COMPARE_WITH_TIMEOUT(chat().size(),2,3000);const auto a=chat()[0].first.id,b=chat()[1].first.id;QVERIFY(a!=b);
        fixture.send(":fixture BATCH +h chathistory #room\r\n@batch=h :fixture BATCH +inner labeled-response\r\n@batch=inner;time=2020-01-01T00:00:00.000Z :alice!u@h PRIVMSG #room :repeat\r\n@batch=h :fixture BATCH -inner\r\n@batch=h;time=2020-01-01T00:00:00.000Z :alice!u@h PRIVMSG #room :repeat\r\n:fixture BATCH -h\r\n");
        QTRY_COMPARE_WITH_TIMEOUT(chat().size(),4,3000);QCOMPARE(chat()[2].first.id,a);QCOMPARE(chat()[3].first.id,b);QVERIFY(chat()[2].second&&chat()[3].second);
        fixture.send(frame+"\r\n@time=invalid :alice!u@h PRIVMSG #room :repeat\r\n@time=invalid :alice!u@h PRIVMSG #room :repeat\r\n");
        QTRY_COMPARE_WITH_TIMEOUT(chat().size(),7,3000);QVERIFY(chat()[4].first.id!=a&&chat()[4].first.id!=b);QVERIFY(chat()[5].first.id!=chat()[6].first.id);
    }

    void policyHasStableResetJitterAndStickyPause() {
        ConnectionPolicy policy;QVERIFY(policy.begin());QVERIFY(!policy.begin());policy.attempts=7;policy.connected(100);
        policy.stable(120099);QCOMPARE(policy.attempts,7);policy.stable(120100);QCOMPARE(policy.attempts,0);
        policy.failed(RetryDisposition::UserActionRequired);policy.failed(RetryDisposition::Transient);QVERIFY(!policy.canRetry());
        QVERIFY(policy.begin());policy.failed(RetryDisposition::Transient);QVERIFY(policy.canRetry());policy.stop();QVERIFY(!policy.canRetry());
        QVERIFY(ConnectionPolicy::retryDelay(2,0.0)<ConnectionPolicy::retryDelay(2,1.0));QVERIFY(ConnectionPolicy::retryDelay(100,1.0)<=300000);
    }
};
QTEST_MAIN(ReliabilityTests)
#include "test_irc_reliability.moc"
