#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <QtNetwork>
#include <QtSql>
#include <QtTest>
#include "util/ChatExporter.h"
#include "util/NickColorGenerator.h"
#include "ui/sidebar/UserListSidebarWidget.h"
#include <functional>
#include <thread>
#include <optional>
#include <variant>
#include <algorithm>
#include <cmath>
#define private public
#include "network/ConnectionManager.h"
#include "network/IrcClient.h"
#include "service/ImageCache.h"
#include "ui/chat/ChannelPaneWidget.h"
#include "ui/chat/ChatInputWidget.h"
#include "data/cache/MessageCache.h"
#include "service/SoundService.h"
#include "service/ImgBbService.h"
#include "ui/chat/MessageItemDelegate.h"
#include "ui/window/MainWindow.h"
#include "ui/sidebar/ServerSidebarWidget.h"
#include "ui/dialog/SettingsDialog.h"
#include "ui/dialog/ChannelListDialog.h"
#undef private
#include "ui/chat/MessageListModel.h"
#include "ui/chat/MessageListView.h"
#include "ui/chat/MessageItemDelegate.h"
#include "util/ConfigExporter.h"
#include <QAbstractItemModelTester>
#include <QtConcurrent>
#include <QSemaphore>
#include "ui/theme/IconFactory.h"
#include "service/UpdateCheckService.h"

using namespace loungecat;

class LocalHttpFixture : public QTcpServer {
public:
    QByteArray response;
    int requests = 0;

    LocalHttpFixture() {
        listen(QHostAddress::LocalHost, 0);
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (hasPendingConnections()) {
                auto* peer = nextPendingConnection();
                connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
                connect(peer, &QTcpSocket::readyRead, peer, [this, peer]() {
                    if (peer->property("answered").toBool()) return;
                    peer->readAll();
                    peer->setProperty("answered", true);
                    ++requests;
                    peer->write(response);
                    peer->disconnectFromHost();
                });
            }
        });
    }

    ServerConfig config() const {
        ServerConfig result;
        result.proxyType = ProxyType::HTTP;
        result.proxyHost = QStringLiteral("127.0.0.1");
        result.proxyPort = serverPort();
        return result;
    }

    QNetworkProxy proxy() const {
        return QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), serverPort());
    }

    void respond(const QByteArray& status, const QByteArray& type, const QByteArray& body) {
        response = "HTTP/1.1 " + status + "\r\nContent-Type: " + type +
            "\r\nContent-Length: " + QByteArray::number(body.size()) +
            "\r\nConnection: close\r\n\r\n" + body;
    }
};

static QByteArray tinyPng() {
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}


class LocalTlsIrcFixture : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslKey key;
    std::function<void(QSslSocket*, const QByteArray&)> lineReceived;
    bool encrypted = false;
    explicit LocalTlsIrcFixture(const QSslCertificate& cert, const QSslKey& privateKey)
        : certificate(cert), key(privateKey) {}
protected:
    void incomingConnection(qintptr descriptor) override {
        auto* peer = new QSslSocket(this);
        peer->setLocalCertificate(certificate);
        peer->setPrivateKey(key);
        // This server does not request a client certificate; client-side verification stays enabled.
        peer->setPeerVerifyMode(QSslSocket::VerifyNone);
        if (!peer->setSocketDescriptor(descriptor)) { delete peer; return; }
        connect(peer, &QSslSocket::encrypted, this, [this, peer]() {
            encrypted = true;
            peer->write(":irc.local NOTICE * :*** Checking Ident\r\n");
            peer->write(":irc.local NOTICE * :*** Looking up your hostname...\r\n");
        });
        connect(peer, &QSslSocket::readyRead, this, [this, peer]() {
            while (peer->canReadLine()) {
                const auto line = peer->readLine().trimmed();
                if (lineReceived) lineReceived(peer, line);
            }
        });
        peer->startServerEncryption();
    }
};

class OrderedInputClient : public ChatClient {
public:
    bool online = false;
    QList<QPair<QString, QString>> sent;
    OrderedInputClient(qint64 id, const ServerConfig& config, QObject* parent) : ChatClient(id, config, parent) {}
    void connectToServer() override { online = true; Q_EMIT connected(m_serverId); }
    void disconnectFromServer(const QString& = {}) override { online = false; Q_EMIT disconnected(m_serverId, "closed"); }
    void joinChannel(const QString& channel, const QString& = {}) override { Q_EMIT channelJoined(m_serverId, channel); }
    void partChannel(const QString& channel, const QString& = {}) override { Q_EMIT channelParted(m_serverId, channel); }
    bool sendMessage(const QString& target, const QString& message, const std::optional<IncomingMessage>& = {}) override {
        if (!online) return false;
        sent.append({target, message});
        return true;
    }
    bool sendMessages(const QString& target, const QStringList& messages, const std::optional<IncomingMessage>& = {}) override {
        if (!online) return false;
        for (const auto& message : messages) sent.append({target, message});
        return true;
    }
    bool sendAction(const QString&, const QString&) override { return online; }
    bool sendNotice(const QString&, const QString&) override { return online; }
    bool sendRawCommand(const QString&) override { return online; }
    void changeNickname(const QString&) override {}
    QString getNickname() const override { return "tester"; }
    bool isConnected() const override { return online; }
    bool allowsPublicPaste(const QString& target) const override { return online && target.startsWith('#'); }
};

class LocalPastePairFixture : public QTcpServer {
public:
    QMap<QString, QSslSocket*> pending;
    QSslConfiguration original = QSslConfiguration::defaultConfiguration();
    QSslCertificate certificate;
    QSslKey key;
    LocalPastePairFixture() {
        QFile cert(QFINDTESTDATA("fixtures/paste-local-cert.pem")), privateKey(QFINDTESTDATA("fixtures/paste-local-key.pem"));
        cert.open(QIODevice::ReadOnly); privateKey.open(QIODevice::ReadOnly);
        certificate = QSslCertificate(cert.readAll()); key = QSslKey(privateKey.readAll(), QSsl::Rsa);
        auto trusted = original; trusted.addCaCertificate(certificate);
        QSslConfiguration::setDefaultConfiguration(trusted);
        listen(QHostAddress::LocalHost, 0);
    }
    ~LocalPastePairFixture() override { QSslConfiguration::setDefaultConfiguration(original); }
    QNetworkProxy proxy() const { return QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", serverPort()); }
    void respond(const QString& name) {
        auto* peer = pending.take(name);
        if (!peer) return;
        const auto body = QString("https://0x0.st/%1-fixture").arg(name).toUtf8();
        peer->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        peer->disconnectFromHost();
    }
protected:
    void incomingConnection(qintptr descriptor) override {
        auto* peer = new QSslSocket(this);
        peer->setLocalCertificate(certificate); peer->setPrivateKey(key);
        // The server requests no client certificate; the HTTP client still verifies 0x0.st.
        peer->setPeerVerifyMode(QSslSocket::VerifyNone);
        if (!peer->setSocketDescriptor(descriptor)) { delete peer; return; }
        connect(peer, &QSslSocket::readyRead, this, [this, peer]() {
            if (peer->property("requestComplete").toBool()) return;
            const bool tunnel = peer->property("tunnel").toBool();
            if (tunnel && !peer->isEncrypted()) return;
            auto bytes = peer->property("buffer").toByteArray() + peer->readAll();
            peer->setProperty("buffer", bytes);
            const auto boundary = bytes.indexOf("\r\n\r\n");
            if (boundary < 0) return;
            if (!tunnel) {
                if (!bytes.startsWith("CONNECT 0x0.st:443 ")) { peer->disconnectFromHost(); return; }
                peer->setProperty("buffer", QByteArray()); peer->setProperty("tunnel", true);
                peer->write("HTTP/1.1 200 Connection established\r\n\r\n"); peer->flush();
                peer->startServerEncryption();
                return;
            }
            static const QRegularExpression length("\r\nContent-Length: (\\d+)\r\n", QRegularExpression::CaseInsensitiveOption);
            const auto match = length.match(QString::fromLatin1(bytes.left(boundary + 4)));
            if (!match.hasMatch() || bytes.size() < boundary + 4 + match.captured(1).toInt()) return;
            const auto body = bytes.mid(boundary + 4);
            const QString name = body.contains("first-fixture") ? "first" : body.contains("second-fixture") ? "second" : "";
            if (name.isEmpty()) { peer->disconnectFromHost(); return; }
            peer->setProperty("requestComplete", true);
            pending.insert(name, peer);
        });
    }
};

class DefensiveTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void asynchronousPastesPreserveConversationOrderWithoutBlockingOtherTargets() {
        LocalPastePairFixture fixture;
        QVERIFY(fixture.isListening());
        QTemporaryDir temp; DatabaseService database(temp.filePath("history.sqlite"));
        PreferencesManager prefs(temp.filePath("prefs.json"));
        prefs.update([](auto& p) { p.pastebinEnabled=true; p.pastebinThreshold=400; p.urlShorteningEnabled=false; p.loggingEnabled=false; p.notificationsEnabled=false; p.soundAlertsEnabled=false; });
        ConnectionManager manager(&database, nullptr, &prefs, nullptr, nullptr);
        OrderedInputClient* client = nullptr;
        manager.setClientFactory([&](qint64 id, const ServerConfig& config, QObject* owner) { client = new OrderedInputClient(id, config, owner); return client; });
        ServerConfig config; config.hostname="local-fixture.invalid"; config.nickname="tester";
        const auto id=manager.addServer(config);
        manager.joinChannel(id,"#origin"); manager.joinChannel(id,"#other");
        manager.m_pastebinService.m_nam.setProxy(fixture.proxy());
        QVERIFY(manager.sendUserInput(id,"#Origin","first-fixture "+QString(420,'a')));
        auto* firstReply=manager.m_pastebinService.m_nam.findChild<QNetworkReply*>();
        QVERIFY(firstReply);
        QVERIFY(manager.sendUserInput(id,"#origin","second-fixture "+QString(420,'b')));
        QNetworkReply* secondReply=nullptr;
        for(auto* reply:manager.m_pastebinService.m_nam.findChildren<QNetworkReply*>()) if(reply!=firstReply)secondReply=reply;
        QVERIFY(secondReply); QSignalSpy secondFinished(secondReply,&QNetworkReply::finished);
        QVERIFY(manager.sendUserInput(id,"#ORIGIN","tail-after-pastes"));
        QVERIFY(manager.sendUserInput(id,"#other","independent"));
        QCOMPARE(client->sent.size(),1);
        QCOMPARE(client->sent.first(),qMakePair(QString("#other"),QString("independent")));
        QTRY_COMPARE_WITH_TIMEOUT(fixture.pending.size(),2,5000);
        fixture.respond("second"); QTRY_COMPARE_WITH_TIMEOUT(secondFinished.size(),1,5000);
        QCOMPARE(client->sent.size(),1);
        fixture.respond("first"); QTRY_COMPARE_WITH_TIMEOUT(client->sent.size(),4,5000);
        QVERIFY(client->sent[1].second.startsWith("[Pastebin] https://0x0.st/first-fixture"));
        QVERIFY(client->sent[2].second.startsWith("[Pastebin] https://0x0.st/second-fixture"));
        QCOMPARE(client->sent[3].second,QString("tail-after-pastes"));
        QCOMPARE(manager.m_inFlightInputs,0);
        QVERIFY(manager.m_pendingInputPublications.isEmpty());
    }
    void manualDisconnectRetiresSuccessfulPendingPasteResults() {
        LocalPastePairFixture fixture; QVERIFY(fixture.isListening());
        QTemporaryDir temp; DatabaseService database(temp.filePath("history.sqlite"));
        PreferencesManager prefs(temp.filePath("prefs.json"));
        prefs.update([](auto& p) { p.pastebinEnabled=true; p.pastebinThreshold=400; p.urlShorteningEnabled=false; p.loggingEnabled=false; p.notificationsEnabled=false; p.soundAlertsEnabled=false; });
        ConnectionManager manager(&database,nullptr,&prefs,nullptr,nullptr);
        OrderedInputClient* client=nullptr;
        manager.setClientFactory([&](qint64 id,const ServerConfig& config,QObject* owner) { client=new OrderedInputClient(id,config,owner);return client; });
        ServerConfig config;config.hostname="local-fixture.invalid";config.nickname="tester";
        const auto id=manager.addServer(config);manager.joinChannel(id,"#origin");
        manager.m_pastebinService.m_nam.setProxy(fixture.proxy());
        int retained=0;
        connect(&manager,&ConnectionManager::inputFailed,&manager,[&](qint64,const QString&,const QString&,const std::optional<IncomingMessage>&,const QString&){++retained;});
        QVERIFY(manager.sendUserInput(id,"#origin","first-fixture "+QString(420,'a')));
        QVERIFY(manager.sendUserInput(id,"#origin","second-fixture "+QString(420,'b')));
        QTRY_COMPARE_WITH_TIMEOUT(fixture.pending.size(),2,5000);
        manager.disconnectServer(id,"user finished this session");
        fixture.respond("second");fixture.respond("first");
        QTRY_COMPARE_WITH_TIMEOUT(retained,2,5000);
        QVERIFY(client->sent.isEmpty());
        QCOMPARE(manager.m_inFlightInputs,0);
        QVERIFY(manager.m_pendingInputPublications.isEmpty());
    }
    void composerHeightFollowsSoftWrapFontAndWidthWithoutChangingDraft() {
        ChatTextEdit edit(nullptr);edit.setStyleSheet("font-size:14pt;");edit.setFixedWidth(240);edit.show();
        const QString draft=QString("Ordinary draft words ").repeated(20);
        auto fullLines=[&]{return (edit.viewport()->height()-qCeil(2*edit.document()->documentMargin()))/edit.fontMetrics().lineSpacing();};
        edit.setPlainText(draft);
        QTRY_VERIFY(edit.document()->lineCount()>1 && fullLines()>=2);
        QVERIFY(edit.height()<=120);QCOMPARE(edit.toPlainText(),draft);
        QCOMPARE(edit.verticalScrollBarPolicy(),Qt::ScrollBarAsNeeded);
        edit.setFixedWidth(120);QTRY_VERIFY(fullLines()>=2);QCOMPARE(edit.toPlainText(),draft);
        edit.setPlainText("short");QTRY_VERIFY(edit.height()<120&&fullLines()>=1);
        edit.setStyleSheet("font-size:18pt;");QTRY_VERIFY(fullLines()>=1);QCOMPARE(edit.toPlainText(),QString("short"));
        edit.setPlainText(QString("line\n").repeated(20));
        QTRY_VERIFY(edit.verticalScrollBar()->maximum()>0);
        QVERIFY(edit.height()<=120);
    }
    void testContextCopyMessageUsesPlainTextWithoutChangingModel() {
        const QString url = "https://example.invalid/?q=a&b=c";
        const QString plain = "ordinary " + url;
        const QStringList inputs{
            plain, QChar(2) + plain + QChar(15), QChar(29) + plain + QChar(15),
            QChar(3) + QStringLiteral("04,01") + plain + QChar(15),
            QChar(4) + QStringLiteral("FF0000") + plain + QChar(15),
            QChar(0x11) + plain + QChar(0x11),
            plain.left(15) + QChar(2) + plain.mid(15) + QChar(15),
        };
        for (const auto& raw : inputs) {
            MessageListModel model;
            MessageItemDelegate delegate;
            MessageListView view(&model, &delegate);
            UserPreferences prefs;
            prefs.urlImageDisplayMode = UrlImageDisplayMode::DISABLED;
            view.setPreferencesAndTheme(prefs, ThemeColors::darkTheme());
            IncomingMessage message;
            message.content = raw;
            message.sender = "alice";
            model.setItems({MessageItemView{message}});
            view.resize(800, 300);
            view.show();
            QApplication::processEvents();
            const auto itemRect = view.visualRect(model.index(0, 0));
            bool linkHit = false;
            for (int y = itemRect.top(); y <= itemRect.bottom() && !linkHit; y += 2)
                for (int x = itemRect.left(); x <= itemRect.right(); x += 2)
                    if (delegate.anchorAt(model.index(0, 0), itemRect, QPoint(x, y), view.font()) == url) {
                        linkHit = true;
                        break;
                    }
            QVERIFY2(linkHit, "Formatted visible URL must remain clickable even across its scheme");
            const auto position = view.visualRect(model.index(0, 0)).center();
            bool selected = false;
            QObject scope;
            QTimer::singleShot(0, &scope, [&]() {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (!menu) return;
                for (auto* action : menu->actions()) {
                    if (action->text() != "Copy Message Text") continue;
                    action->trigger();
                    selected = true;
                    menu->close();
                    return;
                }
                menu->close();
            });
            QTimer::singleShot(2000, &scope, [&]() {
                if (auto* popup = QApplication::activePopupWidget()) popup->close();
            });
            QApplication::clipboard()->setText("synthetic-sentinel");
            QContextMenuEvent event(QContextMenuEvent::Mouse, position, view.viewport()->mapToGlobal(position));
            QApplication::sendEvent(view.viewport(), &event);
            QVERIFY(selected);
            QCOMPARE(QApplication::clipboard()->text(), plain);
            QCOMPARE(model.index(0, 0).data(MessageListModel::ContentRole).toString(), raw);
        }
        QApplication::clipboard()->clear();
    }

    void ircColorSpansRemainReadableWithoutDiscardingLegibleColors() {
        const auto control=QString(QChar(3));
        const QStringList contents={control+"00,00contrast-marker",control+"01,01contrast-marker",
            control+"04,04contrast-marker",control+"00contrast-marker",control+"01contrast-marker",
            control+"00,01contrast-marker",QStringLiteral("contrast-marker")};
        const QList<ThemeColors> themes={ThemeColors::darkTheme(),ThemeColors::lightTheme()};
        auto luminance=[](const QColor& color){
            auto linear=[](double v){return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4);};
            return .2126*linear(color.redF())+.7152*linear(color.greenF())+.0722*linear(color.blueF());
        };
        for(const auto&theme:themes)for(const auto&content:contents){
            QStandardItemModel model(1,1);const auto index=model.index(0,0);
            model.setData(index,content,MessageListModel::ContentRole);
            model.setData(index,"alice",MessageListModel::SenderRole);
            model.setData(index,int(MessageType::NORMAL),MessageListModel::MessageTypeRole);
            QTextDocument document;document.setHtml(MessageItemDelegate::buildRichTextHtml(index,theme,UserPreferences{},{}));
            const auto at=document.toPlainText().indexOf("contrast-marker");QVERIFY(at>=0);
            QTextCursor cursor(&document);cursor.setPosition(at+2);const auto style=cursor.charFormat();
            const auto fg=style.foreground().style()==Qt::NoBrush?theme.foreground:style.foreground().color();
            const auto bg=style.background().style()==Qt::NoBrush?theme.chatBackground:style.background().color();
            const auto a=luminance(fg),b=luminance(bg);
            QVERIFY((std::max(a,b)+.05)/(std::min(a,b)+.05)>=4.5);
            if(content==control+"00,01contrast-marker"){
                QCOMPARE(fg,QColor(Qt::white));QCOMPARE(bg,QColor(Qt::black));
            }
        }

        // Nickname headings and active roster entries use the same readability
        // policy, including the uncoloured accent fallback on the light theme.
        const auto previousStyle = qApp->styleSheet();
        const auto restoreStyle = qScopeGuard([previousStyle] { qApp->setStyleSheet(previousStyle); });
        QMap<int, QString> paletteNames;
        for (int index = 0; index < 1000 && paletteNames.size() < 16; ++index) {
            const auto nick = QStringLiteral("nick%1").arg(index);
            paletteNames.insert(NickColorGenerator::getPaletteIndex(nick), nick);
        }
        QCOMPARE(paletteNames.size(), 16);
        for (const auto& theme : QList<ThemeColors>{ThemeColors::darkTheme(), ThemeColors::lightTheme(), ThemeColors::obsidianVeilTheme()}) {
            qApp->setStyleSheet(theme.generateStyleSheet());
            UserListSidebarWidget sidebar;
            sidebar.resize(240, 320); sidebar.show(); QCoreApplication::processEvents();
            auto* list = sidebar.findChild<QListWidget*>(); QVERIFY(list);
            for (bool coloured : {true, false}) for (const auto& nick : paletteNames) {
                UserPreferences preferences; preferences.coloredNicknames = coloured;
                QStandardItemModel model(1, 1); const auto index = model.index(0, 0);
                model.setData(index, "fixture body", MessageListModel::ContentRole);
                model.setData(index, nick, MessageListModel::SenderRole);
                model.setData(index, int(MessageType::NORMAL), MessageListModel::MessageTypeRole);
                QTextDocument document;
                document.setHtml(MessageItemDelegate::buildRichTextHtml(index, theme, preferences, {}));
                const auto at = document.toPlainText().indexOf(nick); QVERIFY(at >= 0);
                QTextCursor cursor(&document); cursor.setPosition(at + 1);
                const auto foreground = cursor.charFormat().foreground().color();
                const auto a = luminance(foreground), b = luminance(theme.chatBackground);
                QVERIFY((std::max(a, b) + .05) / (std::min(a, b) + .05) >= 4.5);
                sidebar.applyTheme(theme, coloured); ChannelUser user; user.nickname = nick;
                sidebar.setUsers(1, "#fixture", {user}, {});
                const auto c = luminance(list->item(0)->foreground().color()), d = luminance(theme.sidebarBackground);
                QVERIFY((std::max(c, d) + .05) / (std::min(c, d) + .05) >= 4.5);
            }
        }
    }

    void limitedChannelListsExposeTruncationAndResetIt() {
        ServerConfig cfg; cfg.hostname="unused.invalid"; cfg.nickname="tester";
        IrcClient client(1,cfg);
        QSignalSpy updates(&client,&ChatClient::channelListUpdated);
        client.processLine(":fixture 321 tester Channel :Users Name");
        for(int n=0;n<2001;n++)
            client.processLine(QString(":fixture 322 tester #synthetic-%1 10 :Topic").arg(n));
        client.processLine(":fixture 323 tester :End");
        QCOMPARE(updates.size(),1); const auto first=updates.takeFirst();
        const auto rows=qvariant_cast<QList<ChannelListItem>>(first.at(1));
        QCOMPARE(rows.size(),2000); QVERIFY(first.at(3).toBool());
        QVERIFY(!client.sendRawCommand("LIST")); // Disconnected: admission rejects.
        QCOMPARE(client.m_channelListAccumulator.size(),2000);
        QVERIFY(client.m_channelListTruncated);
        ChannelListDialog dialog(1); dialog.setChannels(rows,true);
        QVERIFY(dialog.m_statusLabel->text().contains("truncated"));
        dialog.m_filterEdit->setText("no-such-channel");
        QVERIFY(dialog.m_statusLabel->text().contains("truncated"));
        client.processLine(":fixture 321 tester Channel :Users Name");
        client.processLine(":fixture 322 tester #fresh 1 :Fresh");
        client.processLine(":fixture 323 tester :End");
        QCOMPARE(updates.size(),1); const auto second=updates.takeFirst();
        QVERIFY(!second.at(3).toBool());
        const auto fresh=qvariant_cast<QList<ChannelListItem>>(second.at(1));
        QCOMPARE(fresh.size(),1); dialog.setChannels(fresh,false);
        QVERIFY(!dialog.m_statusLabel->text().contains("truncated"));
    }

    void serverListFilterIsNotReappliedAsLiteralSearch() {
        for (const QString& filter:QStringList{"*","#a*",">10"}) {
            ChannelListDialog dialog(1,filter);
            dialog.setChannels({{"#alpha",100,"Alpha topic"}});
            QCOMPARE(dialog.m_table->rowCount(),1);
            QVERIFY(dialog.m_filterEdit->text().isEmpty());
            dialog.m_filterEdit->setText("missing");
            QCOMPARE(dialog.m_table->rowCount(),0);
            dialog.m_filterEdit->clear();
            QCOMPARE(dialog.m_table->rowCount(),1);
        }
    }

    void chatToolbarExposesMeaningfulActionNamesToAccessibility() {
        ChannelPaneWidget pane(nullptr,nullptr,nullptr); pane.resize(900,600); pane.show();
        pane.openSearchBar();
        auto* input=pane.findChild<ChatInputWidget*>(); QVERIFY(input);
        IncomingMessage parent; parent.sender="alice"; parent.content="synthetic reply";
        input->setReplyContext(parent); QCoreApplication::processEvents();
        int checked=0;
        for (auto* button:pane.findChildren<QToolButton*>()) {
            if (!button->isVisible()) continue;
            auto* interface=QAccessible::queryAccessibleInterface(button); QVERIFY(interface);
            const QString prefix=button->toolTip().isEmpty()?button->text().trimmed():button->toolTip().section(' ',0,0);
            QVERIFY(!prefix.isEmpty());
            QVERIFY(interface->text(QAccessible::Name).trimmed().startsWith(prefix,Qt::CaseInsensitive));
            ++checked;
        }
        QVERIFY(checked>=13);
        auto* search = pane.findChild<SearchOverlayBar*>(); QVERIFY(search);
        auto* searchInput = search->findChild<QLineEdit*>(); QVERIFY(searchInput);
        auto* searchAccessible = QAccessible::queryAccessibleInterface(searchInput); QVERIFY(searchAccessible);
        QCOMPARE(searchAccessible->text(QAccessible::Name), QString("Search messages"));
    }

    void sidebarToolbarExposesActionNamesToAccessibility() {
        ServerSidebarWidget sidebar; sidebar.resize(320,500); sidebar.show();
        for (auto* button:QList<QToolButton*>{sidebar.m_addServerBtn,sidebar.m_collapseBtn,
                sidebar.m_gridModeBtn,sidebar.m_settingsBtn,sidebar.m_backupBtn,sidebar.m_helpBtn}) {
            auto* interface=QAccessible::queryAccessibleInterface(button);
            QVERIFY(interface);
            QVERIFY(!interface->text(QAccessible::Name).trimmed().isEmpty());
        }
        sidebar.setCollapsed(true);
        auto* interface=QAccessible::queryAccessibleInterface(sidebar.m_collapseBtn);
        QVERIFY(interface->text(QAccessible::Name).contains("Expand"));
        sidebar.m_collapseBtn->setFocus();
        QTest::keyClick(sidebar.m_collapseBtn,Qt::Key_Space);
        QVERIFY(!sidebar.isCollapsed());
        QVERIFY(interface->text(QAccessible::Name).contains("Collapse"));
    }

    void channelListRefreshDoesNotChangeJoinIdentity() {
        for (bool removed : {false,true}) {
            ChannelListDialog dialog(1);
            dialog.setChannels({{"#alpha",100,"Alpha"},{"#beta",50,"Beta"}});
            dialog.m_table->setCurrentCell(1,0);
            QSignalSpy joins(&dialog,&ChannelListDialog::joinChannelRequested);
            QList<ChannelListItem> refreshed={{"#gamma",200,"Gamma"},{"#alpha",100,"Alpha"}};
            if (!removed) refreshed.append({"#beta",50,"Beta"});
            dialog.setChannels(refreshed); dialog.show();
            QPushButton* join=nullptr;
            for (auto* button : dialog.findChildren<QPushButton*>())
                if (button->text()=="Join Selected") join=button;
            QVERIFY(join);
            QTest::mouseClick(join,Qt::LeftButton);
            QCOMPARE(joins.size(),removed?0:1);
            if (!removed) QCOMPARE(joins.first().at(1).toString(),QStringLiteral("#beta"));
            else QCOMPARE(dialog.m_table->currentRow(),-1);
        }
    }

    void emptySidebarKeepsExpandActionInsideItsBounds() {
        ServerSidebarWidget sidebar;
        sidebar.updateConnections({},0,{});
        sidebar.resize(230,500); sidebar.show(); QTest::qWait(1);
        sidebar.setCollapsed(true); QCoreApplication::processEvents();
        const QRect button(sidebar.m_collapseBtn->mapTo(&sidebar,QPoint()),sidebar.m_collapseBtn->size());
        QVERIFY(sidebar.rect().contains(button));
        QVERIFY(sidebar.m_collapseBtn->isVisible());
        QVERIFY(sidebar.m_logoLabel->isHidden());
        QTest::mouseClick(sidebar.m_collapseBtn,Qt::LeftButton);
        QVERIFY(!sidebar.isCollapsed());
        QVERIFY(sidebar.m_logoLabel->isVisible());
        QCOMPARE(sidebar.m_tree->topLevelItemCount(),0);
        QSignalSpy adds(&sidebar,&ServerSidebarWidget::addServerRequested);
        QTest::mouseClick(sidebar.m_addServerBtn,Qt::LeftButton);
        QCOMPARE(adds.size(),1);
    }

    void serverNoticesKeepExplicitChannelTargets() {
        ServerConfig cfg; cfg.hostname="unused.invalid"; cfg.nickname="tester"; cfg.serverName="Fixture";
        IrcClient client(1,cfg);
        client.processLine(":fixture 005 tester CHANTYPES=#& STATUSMSG=@+ :supported");
        QSignalSpy messages(&client,&ChatClient::messageReceived);
        const QList<QPair<QString,QString>> targets={{"#room","#room"},{"&local","&local"},{"@#room","#room"},{"tester","* Fixture"},{"*","* Fixture"}};
        for (const auto& target:targets) {
            client.processLine(QString(":services.invalid NOTICE %1 :synthetic service notice").arg(target.first));
            QCOMPARE(messages.size(),1);
            const auto message=qvariant_cast<IncomingMessage>(messages.takeFirst().at(1));
            QCOMPARE(message.target,target.second);
            QCOMPARE(message.type,MessageType::NOTICE);
            QCOMPARE(message.content,QStringLiteral("synthetic service notice"));
        }
    }

    void incomingStatusTargetsUseTheRealChannel() {
        ServerConfig cfg; cfg.hostname="unused.invalid"; cfg.nickname="tester";
        IrcClient client(1,cfg);
        client.processLine(":fixture 005 tester CHANTYPES=#& STATUSMSG=@+ :supported");
        QCOMPARE(client.normalizedMessageTarget("@#room"),"#room");
        QCOMPARE(client.normalizedMessageTarget("+#room"),"#room");
        QCOMPARE(client.normalizedMessageTarget("#room"),"#room");
        QCOMPARE(client.normalizedMessageTarget("@alice"),"@alice");
        QCOMPARE(client.normalizedMessageTarget("+local"),"+local");
        client.processLine(":fixture 005 tester -STATUSMSG :supported");
        QCOMPARE(client.normalizedMessageTarget("@#room"),"@#room");
        client.processLine(":fixture 005 tester STATUSMSG=@+ :supported");
        QSignalSpy messages(&client,&ChatClient::messageReceived);
        client.processLine(":alice!u@localhost PRIVMSG @#room :hello");
        QCOMPARE(messages.size(),1);
        QCOMPARE(qvariant_cast<IncomingMessage>(messages.first().at(1)).target,"#room");
        QSignalSpy typing(&client,&ChatClient::typingUserUpdated);
        client.processLine("@+typing=active :alice!u@localhost TAGMSG +#room");
        QCOMPARE(typing.size(),1); QCOMPARE(typing.first().at(1).toString(),"#room");
    }

    void notificationTextStripsFormattingWithoutChangingHistory() {
        QTemporaryDir temp;
        DatabaseService db(temp.filePath("db.sqlite"));
        PreferencesManager prefs(temp.filePath("prefs.json"));
        QVERIFY(prefs.update([](auto& p) {
            p.notificationsEnabled = true; p.notifyOnMention = true;
            p.soundAlertsEnabled = false; p.loggingEnabled = false;
            p.urlImageDisplayMode = UrlImageDisplayMode::DISABLED;
        }));
        ConnectionManager manager(&db, nullptr, &prefs, nullptr, nullptr);
        ServerConfig cfg; cfg.hostname="unused.invalid"; cfg.nickname="tester";
        const auto id=manager.addServer(cfg, false); QVERIFY(id>0);
        QSignalSpy notifications(&manager, &ConnectionManager::desktopNotificationRequested);
        const QList<QPair<QString,QString>> cases={
            {"plain", "plain"},
            {QString(QChar(2))+"bold"+QChar(15), "bold"},
            {QString(QChar(29))+"italic"+QChar(15), "italic"},
            {QString(QChar(3))+"04,02color"+QChar(15), "color"},
            {QString(QChar(4))+"FF0000hex"+QChar(15), "hex"},
            {QString(QChar(31))+"under"+QChar(31)+" "+QChar(30)+"strike"+QChar(15), "under strike"},
        };
        const QList<QPair<QString,QString>> hexEdges = {
            {QString(QChar(4))+"FF0text","FF0text"},
            {QString(QChar(4))+"FF0000,00aAword",",00aAword"},
            {QString(QChar(4))+"FF0000,00aAFFword","word"},
            {QString(QChar(4))+"FF0000Atail","Atail"},
            {QString(QChar(4))+"FF0000,tail",",tail"},
            {QString(QChar(4))+"FF0000,00FF00,word",",word"},
        };
        QList<QPair<QString,QString>> allCases = cases;
        allCases.append(hexEdges);
        for (const auto& value: allCases) {
            IncomingMessage message; message.target="#fixture"; message.sender="alice";
            message.content="tester "+value.first;
            Q_EMIT manager.client(id)->messageReceived(id,message,false);
            QCOMPARE(notifications.size(),1);
            QCOMPARE(notifications.takeFirst().at(1).toString(),"tester "+value.second);
            const auto connection=manager.connection(id);
            QCOMPARE(connection->findChannel("#fixture")->messages.last().content,message.content);
        }
    }

    void textLogsKeepConversationIdentityAndLegacyDataSeparate() {
        QTemporaryDir directory;
        QVERIFY(QDir().mkpath(directory.filePath("Network")));
        const auto legacyPath = directory.filePath("Network/#c.log");
        QFile legacy(legacyPath); QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write("legacy history\n"); legacy.close();
        TextLogService logs(directory.path());
        IncomingMessage a; a.sender = "alice"; a.content = "private A";
        IncomingMessage b = a; b.content = "private B";
        const QList<QStringList> cases{{"#c++", "#c"}, {QString::fromUtf8("#café"), QString::fromUtf8("#cafè")},
            {QString::fromUtf8("#猫"), QString::fromUtf8("#犬")}, {QString(300, 'a'), QString(299, 'a') + 'b'}};
        for (const auto& c : cases) {
            QVERIFY(logs.logMessage("Network", c[0], a));
            QVERIFY(logs.logMessage("Network", c[1], b));
            QVERIFY(logs.flushSync());
            const auto pa = logs.resolveLogFilePath("Network", c[0]);
            const auto pb = logs.resolveLogFilePath("Network", c[1]);
            QVERIFY(pa != pb);
            QVERIFY(QFileInfo(pa).fileName().toUtf8().size() < 200);
            QFile fa(pa), fb(pb); QVERIFY(fa.open(QIODevice::ReadOnly)); QVERIFY(fb.open(QIODevice::ReadOnly));
            const auto ca = fa.readAll(), cb = fb.readAll();
            QVERIFY(ca.contains("private A") && !ca.contains("private B"));
            QVERIFY(cb.contains("private B") && !cb.contains("private A"));
        }
        QVERIFY(logs.logMessage("Work", "alice", a, 1));
        QVERIFY(logs.logMessage("Work", "alice", b, 2));
        QVERIFY(logs.flushSync());
        QVERIFY(logs.resolveLogFilePath("Work", "alice", 1) != logs.resolveLogFilePath("Work", "alice", 2));
        QCOMPARE(logs.resolveLogFilePath("Work", "alice", 1), logs.resolveLogFilePath("Renamed", "alice", 1));
        QVERIFY(legacy.open(QIODevice::ReadOnly)); QCOMPARE(legacy.readAll(), QByteArray("legacy history\n"));
    }

    void chatExportsUseSelectedTimezoneAndPreserveData() {
        IncomingMessage message; message.target = "#export"; message.sender = "alice";
        message.content = QString::fromUtf8("hello \002bold\002 & <two> 猫");
        message.timestamp = QDateTime::fromString("2026-03-29T01:30:00Z", Qt::ISODate).toMSecsSinceEpoch();
        const QList<QStringList> cases{{"UTC", "2026-03-29 01:30:00"},
            {"Europe/Berlin", "2026-03-29 03:30:00"},
            {"America/New_York", "2026-03-28 21:30:00"},
            {"Asia/Kathmandu", "2026-03-29 07:15:00"}};
        QTemporaryDir temp;
        for (const auto& c : cases) {
            const auto result = ChatExporter::formatMessages("#export", {message}, ExportFormat::TXT, c[0]);
            QVERIFY(result.contains("[" + c[1] + "]"));
            QVERIFY(result.contains(QString::fromUtf8("hello bold & <two> 猫")));
            QVERIFY(!result.contains(QChar(2)));
            const auto path = temp.filePath("export.txt");
            QVERIFY(ChatExporter::exportToFile(path, "#export", {message}, ExportFormat::TXT, c[0]));
            QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY(QString::fromUtf8(file.readAll()).contains("[" + c[1] + "]"));
        }
        const auto local = QDateTime::fromMSecsSinceEpoch(message.timestamp).toString("yyyy-MM-dd HH:mm:ss");
        QVERIFY(ChatExporter::formatMessages("#export", {message}, ExportFormat::TXT).contains("[" + local + "]"));
        QVERIFY(ChatExporter::formatMessages("#export", {message}, ExportFormat::TXT, QString("invalid/zone")).contains("[" + local + "]"));
        const auto json = QJsonDocument::fromJson(ChatExporter::formatMessages("#export", {message}, ExportFormat::JSON, QString("Asia/Kathmandu")).toUtf8()).array().first().toObject();
        QCOMPARE(json["timestamp"].toInteger(), message.timestamp);
        QCOMPARE(json["content"].toString(), message.content);
    }

    void searchNavigationMatchesVisibleText() {
        QTemporaryDir directory;
        DatabaseService db(directory.filePath("db.sqlite"));
        PreferencesManager prefs(directory.filePath("prefs.json"));
        ConnectionManager manager(&db, nullptr, &prefs, nullptr, nullptr);
        ChannelPaneWidget pane(&manager, &prefs, nullptr, false);
        QList<ChatViewItem> rows;
        const QStringList texts{"plainneedle", QString("bold") + QChar(2) + "needle",
            QString("color") + QChar(3) + "04needle", QString::fromUtf8("unicode Café 猫")};
        for (const auto& text : texts) {
            IncomingMessage message; message.id = QString::number(rows.size());
            message.sender = "alice"; message.content = text;
            rows.append(MessageItemView{message});
        }
        pane.m_model->setItems(rows);
        for (const auto& query : {QString("plainneedle"), QString("boldneedle"), QString("colorneedle"),
            QString::fromUtf8("CAFÉ 猫"), QString(" plainneedle ")}) {
            pane.findSearchMatches(query);
            QCOMPARE(pane.m_searchMatchRows.size(), 1);
        }
        pane.findSearchMatches("04");
        QVERIFY(pane.m_searchMatchRows.isEmpty());
        pane.findSearchMatches("ALICE");
        QCOMPARE(pane.m_searchMatchRows.size(), 4);
        pane.findSearchMatches("   ");
        QVERIFY(pane.m_searchMatchRows.isEmpty());
    }

    void composingInputDoesNotSubmitOrReplaceTheDraft() {
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter, Qt::Key_Tab, Qt::Key_Backtab, Qt::Key_Up, Qt::Key_Down}) {
            ChatInputWidget input(nullptr);
            input.restoreDraft({"al", {}, 2, -1});
            input.setCompletionContext({"alice"}, {}, {});
            input.setHistory({"previous"});
            auto* edit = input.findChild<ChatTextEdit*>();
            QSignalSpy sent(&input, &ChatInputWidget::inputSubmitted);
            QInputMethodEvent preedit(QString::fromUtf8("猫"), {});
            QApplication::sendEvent(edit, &preedit);
            QVERIFY(edit->isComposing());
            QTest::keyClick(edit, key);
            input.findChild<QToolButton*>("primaryButton")->click();
            edit->insertControlChar(u'\x02');
            QVERIFY(sent.isEmpty());
            QCOMPARE(edit->toPlainText(), QString("al"));
            QVERIFY(edit->isComposing());
            QInputMethodEvent commit;
            commit.setCommitString(QString::fromUtf8("猫"));
            QApplication::sendEvent(edit, &commit);
            QVERIFY(!edit->isComposing());
            QTest::keyClick(edit, Qt::Key_Return);
            QCOMPARE(sent.size(), 1);
            QCOMPARE(sent.first().first().toString(), QString::fromUtf8("al猫"));
        }
    }

    void completionPreservesUndoAndRedoHistory() {
        ChatTextEdit edit(nullptr);
        edit.setCompletionContext({"alice"}, {}, {});
        QTest::keyClicks(&edit, "al");
        QTest::keyClick(&edit, Qt::Key_Tab);
        QCOMPARE(edit.toPlainText(), QString("alice: "));
        edit.undo();
        QCOMPARE(edit.toPlainText(), QString("al"));
        edit.undo();
        QVERIFY(edit.toPlainText().isEmpty());
        edit.redo();
        QCOMPARE(edit.toPlainText(), QString("al"));
        edit.redo();
        QCOMPARE(edit.toPlainText(), QString("alice: "));
    }

    void iconRasterHonoursRequestedSize() {
        for (const int size : {12, 16, 20, 24, 32}) {
            const auto icon = IconFactory::create(IconType::Send, Qt::white, size);
            for (const qreal dpr : {1.0, 1.25, 2.0}) {
                const auto image = icon.pixmap(QSize(size, size), dpr);
                QVERIFY(!image.isNull());
                QVERIFY(image.deviceIndependentSize().width() <= size + 0.5);
                QVERIFY(image.deviceIndependentSize().height() <= size + 0.5);
            }
        }
    }

    void composerResizePreservesUsableDraftAndActions() {
        ChatInputWidget input(nullptr);
        input.setStyleSheet(ThemeColors::darkTheme().generateStyleSheet());
        input.setNicknameBadge("tester");
        InputDraft draft; draft.text = QString::fromUtf8("draft café 猫"); draft.cursor = 5;
        input.restoreDraft(draft);
        input.show();
        auto* edit = input.findChild<ChatTextEdit*>();
        QVERIFY(edit);
        for (int width : {760, 370, 760, 370}) {
            input.resize(width, input.sizeHint().height());
            QCoreApplication::processEvents();
            QTRY_VERIFY(edit->viewport()->width() >= 180);
            QCOMPARE(input.draft().text, draft.text);
            QCOMPARE(input.draft().cursor, draft.cursor);
            for (auto* button : input.findChildren<QToolButton*>()) {
                if (button->isVisible()) QVERIFY(input.rect().contains(QRect(button->mapTo(&input, QPoint(0, 0)), button->size())));
            }
        }
        QSignalSpy sent(&input, &ChatInputWidget::inputSubmitted);
        auto* send = input.findChild<QToolButton*>("primaryButton");
        QVERIFY(send);
        QTest::mouseClick(send, Qt::LeftButton);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().first().toString(), draft.text);
    }

    void sendIconContrastsWithPrimaryBackground() {
        ChatInputWidget input(nullptr);
        for (const auto& theme : {ThemeColors::darkTheme(), ThemeColors::lightTheme()}) {
            input.applyTheme(theme);
            auto* send = input.findChild<QToolButton*>("primaryButton");
            QVERIFY(send);
            const auto image = send->icon().pixmap(20, 20).toImage();
            int contrasting = 0;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                if (color.alpha() > 200 && color.rgb() != theme.accentColor.rgb()) ++contrasting;
            }
            QVERIFY(contrasting > 10);
        }
    }


    void liberaStyleTlsRegistration_data() {
        QTest::addColumn<bool>("sasl");
        QTest::newRow("TLS without SASL") << false;
        QTest::newRow("TLS with SASL PLAIN") << true;
    }

    void liberaStyleTlsRegistration() {
        QFETCH(bool, sasl);
        QVERIFY(QSslSocket::supportsSsl());
        QFile certFile(QFINDTESTDATA("fixtures/localhost-cert.pem"));
        QFile keyFile(QFINDTESTDATA("fixtures/localhost-key.pem"));
        QVERIFY(certFile.open(QIODevice::ReadOnly)); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        QSslCertificate certificate(certFile.readAll());
        QSslKey key(keyFile.readAll(), QSsl::Rsa);
        QVERIFY(!certificate.isNull()); QVERIFY(!key.isNull());
        const auto previous = QSslConfiguration::defaultConfiguration();
        const auto restore = qScopeGuard([previous]() { QSslConfiguration::setDefaultConfiguration(previous); });
        auto trusted = previous;
        trusted.addCaCertificate(certificate);
        QSslConfiguration::setDefaultConfiguration(trusted);
        LocalTlsIrcFixture fixture(certificate, key);
        QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));

        bool nick = false, user = false, capEnd = false, auth = false, welcomed = false;
        fixture.lineReceived = [&](QSslSocket* peer, const QByteArray& line) {
            if (line == "CAP LS 302") {
                // Same capability names and value-form SASL advertisement observed on Libera.
                peer->write(":irc.local CAP * LS :account-notify away-notify batch chghost extended-join multi-prefix sasl=ECDSA-NIST256P-CHALLENGE,EXTERNAL,PLAIN,SCRAM-SHA-512 tls account-tag cap-notify echo-message invite-notify labeled-response message-tags no-implicit-names server-time solanum.chat/identify-msg solanum.chat/oper solanum.chat/realhost\r\n");
            } else if (line.startsWith("NICK ")) nick = true;
            else if (line.startsWith("USER ")) user = true;
            else if (line.startsWith("CAP REQ :"))
                peer->write(":irc.local CAP tester ACK :" + line.mid(9) + "\r\n");
            else if (line == "AUTHENTICATE PLAIN") peer->write("AUTHENTICATE +\r\n");
            else if (line.startsWith("AUTHENTICATE ")) {
                auth = true;
                peer->write(":irc.local 903 tester :SASL authentication successful\r\n");
            } else if (line == "CAP END") capEnd = true;
            if (nick && user && capEnd && !welcomed) {
                welcomed = true;
                peer->write(":irc.local 001 tester :Welcome\r\n");
            }
        };
        ServerConfig config;
        config.hostname = "localhost"; config.port = fixture.serverPort(); config.nickname = "tester";
        config.useSsl = true; config.useSasl = sasl;
        if (sasl) { config.saslUsername = "tester"; config.saslPassword = "loopback-fixture-only"; }
        IrcClient client(1, config);
        QSignalSpy ready(&client, &ChatClient::connected);
        QSignalSpy errors(&client, &ChatClient::connectionError);
        client.connectToServer();
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 8000);
        QVERIFY(fixture.encrypted); QVERIFY(nick); QVERIFY(user); QVERIFY(capEnd);
        QCOMPARE(auth, sasl);
        QVERIFY(client.m_socket->isEncrypted());
        QVERIFY(client.m_socket->sslHandshakeErrors().isEmpty());
        QCOMPARE(errors.size(), 0);
        QVERIFY(!client.m_registrationTimer.isActive());
        QVERIFY(client.m_pendingCapRequests.isEmpty());
        QVERIFY(!client.m_capNegotiating);
        QVERIFY(client.enabledCapabilities().contains("server-time"));
    }
    void previewImageFastPathRespectsCacheLimit() {
        LinkPreviewService service;
        int completed = 0;
        for (int index = 0; index < LinkPreviewService::MAX_CACHE_ENTRIES + 8; ++index) {
            service.fetchPreview(QStringLiteral("https://example.com/%1.png").arg(index), {}, false,
                [&](const UrlPreview& preview) { QVERIFY(!preview.failed); ++completed; });
        }
        QCOMPARE(completed, LinkPreviewService::MAX_CACHE_ENTRIES + 8);
        QCOMPARE(service.m_cache.size(), LinkPreviewService::MAX_CACHE_ENTRIES);
    }

    void previewQueueRejectsExcessWork() {
        LinkPreviewService service;
        service.m_activeRequests = LinkPreviewService::MAX_CONCURRENT_REQUESTS;
        int rejected = 0;
        for (int index = 0; index < LinkPreviewService::MAX_PENDING_REQUESTS + 2; ++index) {
            service.fetchPreview(QStringLiteral("https://example.com/article/%1").arg(index), {}, false,
                [&](const UrlPreview& preview) { if (preview.failed) ++rejected; });
        }
        QCOMPARE(service.m_queue.size(), LinkPreviewService::MAX_PENDING_REQUESTS);
        QCOMPARE(rejected, 2);
    }

    void previewOrdinaryHttpSuccess() {
        LocalHttpFixture fixture;
        fixture.respond("200 OK", "text/html", "<title>Local fixture</title>");
        LinkPreviewService service;
        std::optional<UrlPreview> result;
        service.fetchPreview("http://example.com/article", fixture.config(), true,
            [&](const UrlPreview& preview) { result = preview; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
        QVERIFY(!result->failed);
        QCOMPARE(result->title.value_or(QString()), QStringLiteral("Local fixture"));
        QCOMPARE(service.m_activeRequests, 0);
    }

    void previewHttpFailureDoesNotParseErrorPage() {
        LocalHttpFixture fixture;
        fixture.respond("503 Service Unavailable", "text/html", "<title>Maintenance</title>");
        LinkPreviewService service;
        std::optional<UrlPreview> result;
        service.fetchPreview("http://example.com/maintenance", fixture.config(), true,
            [&](const UrlPreview& preview) { result = preview; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
        QVERIFY(result->failed);
        QVERIFY(!result->title.has_value());
        QCOMPARE(service.m_activeRequests, 0);
    }

    void previewCancellationReturnsOneFailure() {
        LocalHttpFixture fixture;
        LinkPreviewService service;
        int completed = 0;
        service.fetchPreview("http://example.com/cancelled", fixture.config(), true,
            [&](const UrlPreview& preview) { QVERIFY(preview.failed); ++completed; });
        const auto replies = service.m_nam.findChildren<QNetworkReply*>();
        QCOMPARE(replies.size(), 1);
        bool hasDeadline = false;
        for (auto* timer : replies.first()->findChildren<QTimer*>()) {
            if (timer->isSingleShot() && timer->interval() == 10000 && timer->isActive()) hasDeadline = true;
        }
        QVERIFY(hasDeadline);
        replies.first()->abort();
        QCOMPARE(completed, 1);
        QCOMPARE(service.m_activeRequests, 0);
    }

    void previewIncompleteResponseIsFailure() {
        LocalHttpFixture fixture;
        fixture.response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 100\r\nConnection: close\r\n\r\n<title>Incomplete</title>";
        LinkPreviewService service;
        std::optional<UrlPreview> result;
        service.fetchPreview("http://example.com/incomplete", fixture.config(), true,
            [&](const UrlPreview& preview) { result = preview; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 3000);
        QVERIFY(result->failed);
    }

    void diskImageHitsRespectMemoryLimit() {
        QTemporaryDir directory;
        ImageCache cache;
        cache.m_diskCacheDir = directory.path();
        const QByteArray image = tinyPng();
        for (int index = 0; index < ImageCache::MAX_MEMORY_IMAGES + 1; ++index) {
            const QString url = QStringLiteral("https://example.com/%1.png").arg(index);
            QFile file(cache.diskCachePath(url));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(image), image.size());
            file.close();
            QVERIFY(cache.getImage(url).isNull());
            QTRY_VERIFY_WITH_TIMEOUT(!cache.getImage(url).isNull(), 3000);
        }
        QCOMPARE(cache.m_memoryCache.size(), ImageCache::MAX_MEMORY_IMAGES);
        QVERIFY(!cache.m_memoryCache.contains("https://example.com/0.png"));
    }

    void imageQueueRejectsExcessWork() {
        QTemporaryDir directory;
        ImageCache cache;
        cache.m_diskCacheDir = directory.path();
        for (int index = 0; index < ImageCache::MAX_CONCURRENT_DOWNLOADS; ++index) {
            cache.m_activeDownloads.insert(QString::number(index));
        }
        for (int index = 0; index < ImageCache::MAX_PENDING_DOWNLOADS + 2; ++index) {
            cache.getImage(QStringLiteral("https://example.com/queued-%1.png").arg(index));
        }
        QCOMPARE(cache.m_downloadQueue.size(), ImageCache::MAX_PENDING_DOWNLOADS);
        QCOMPARE(cache.m_queuedUrls.size(), ImageCache::MAX_PENDING_DOWNLOADS);
        QCOMPARE(cache.m_recentFailures.size(), 2);
    }

    void imageFailureDoesNotRetryOnRepaint() {
        LocalHttpFixture fixture;
        fixture.respond("503 Service Unavailable", "text/plain", "Unavailable");
        QTemporaryDir directory;
        ImageCache cache;
        cache.m_diskCacheDir = directory.path();
        cache.setProxy(fixture.proxy());
        QSignalSpy failed(&cache, &ImageCache::imageFailed);
        const QString url = QStringLiteral("http://example.com/missing.png");
        QVERIFY(cache.getImage(url).isNull());
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY(cache.hasRecentFailure(url));
        for (int index = 0; index < 5; ++index) QVERIFY(cache.getImage(url).isNull());
        QCOMPARE(fixture.requests, 1);
        QVERIFY(cache.m_activeDownloads.isEmpty());
    }

    void imageFailureHistoryIsBoundedAndExpires() {
        ImageCache cache;
        for (int index = 0; index < ImageCache::MAX_MEMORY_IMAGES + 2; ++index) {
            cache.rememberFailure(QString::number(index));
        }
        QCOMPARE(cache.m_recentFailures.size(), ImageCache::MAX_MEMORY_IMAGES);
        cache.m_recentFailures.insert("expired", QDeadlineTimer(0));
        QVERIFY(!cache.hasRecentFailure("expired"));
        cache.clearMemoryCache();
        QVERIFY(cache.m_recentFailures.isEmpty());
    }

    void imageSuccessPersistsAndReloads() {
        LocalHttpFixture fixture;
        fixture.respond("200 OK", "image/png", tinyPng());
        QTemporaryDir directory;
        ImageCache cache;
        cache.m_diskCacheDir = directory.path();
        cache.setProxy(fixture.proxy());
        QSignalSpy loaded(&cache, &ImageCache::imageLoaded);
        const QString url = QStringLiteral("http://example.com/small.png");
        QVERIFY(cache.getImage(url).isNull());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
        const auto sharedPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        QCOMPARE(QFileInfo(cache.diskCachePath(url)).permissions() & sharedPermissions, QFileDevice::Permissions{});
        cache.clearMemoryCache();
        QVERIFY(cache.getImage(url).isNull());
        QTRY_COMPARE_WITH_TIMEOUT(cache.getImage(url).size(), QSize(2, 2), 3000);
        QCOMPARE(fixture.requests, 1);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 1);
    }

    void existingImageCacheHitsHardenLegacyModes() {
        LocalHttpFixture fixture;
        fixture.respond("200 OK", "image/png", tinyPng());
        QTemporaryDir directory;
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::ExeOther));
        ImageCache cache;
        cache.m_diskCacheDir = directory.path();
        cache.setProxy(fixture.proxy());
        const QString url = QStringLiteral("http://example.com/ordinary-legacy-thumbnail.png");
        QFile file(cache.diskCachePath(url));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(tinyPng()), qint64(tinyPng().size()));
        file.close();
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
            QFileDevice::ReadGroup | QFileDevice::ReadOther));
        QSignalSpy loaded(&cache, &ImageCache::imageLoaded);
        QVERIFY(cache.getImage(url).isNull());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
        QCOMPARE(cache.getImage(url).size(), QSize(2, 2));
        QCOMPARE(fixture.requests, 0);
        const auto sharedPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        QCOMPARE(QFileInfo(directory.path()).permissions() & sharedPermissions, QFileDevice::Permissions{});
        QCOMPARE(QFileInfo(file.fileName()).permissions() & sharedPermissions, QFileDevice::Permissions{});
    }

    void failedPreviewHasAccuratePlaceholder() {
        ImageCache::instance().rememberFailure("https://example.com/unavailable.png");
        IncomingMessage message;
        message.content = QStringLiteral("https://example.com/unavailable.png");
        MessageListModel model;
        model.setItems({MessageItemView{message}});
        const QString html = MessageItemDelegate::buildRichTextHtml(model.index(0), ThemeColors::darkTheme(), {});
        QVERIFY(html.contains("Image preview unavailable"));
        QVERIFY(!html.contains("Loading image preview"));
    }

    void cachePruningPreservesDurableHistory() {
        QTemporaryDir directory;
        {
            MessageCache cache(directory.path());
            QList<IncomingMessage> messages;
            for (int index = 0; index < 600; ++index) messages.append(IncomingMessage{});
            cache.cacheMessages(1, "#room", messages);
            cache.flushSync();
            cache.pruneInMemoryCaches();
            cache.flushSync();
        }
        MessageCache reloaded(directory.path());
        QCOMPARE(reloaded.getCachedMessages(1, "#room", 2000).size(), 600);
    }

    void cacheNamesDoNotCollide() {
        QVERIFY(MessageCache::sanitizeChannelFileName(1, "#c++") !=
            MessageCache::sanitizeChannelFileName(1, "#c__"));
    }

    void cachePreservesMetadata() {
        QTemporaryDir directory;
        {
            MessageCache cache(directory.path());
            IncomingMessage message;
            message.replyToMessageId = "previous";
            message.reactions["thumbs-up"].insert("alice");
            cache.cacheMessage(1, "#room", message);
            cache.flushSync();
        }
        MessageCache reloaded(directory.path());
        const auto message = reloaded.getCachedMessages(1, "#room").first();
        QCOMPARE(message.replyToMessageId.value_or(QString()), QString("previous"));
        QVERIFY(message.reactions["thumbs-up"].contains("alice"));
    }

    void offlineSendDoesNotInventSelfMessage() {
        IrcClient client(1, {});
        QSignalSpy received(&client, &ChatClient::messageReceived);
        QVERIFY(!client.sendMessage("#room", "Ordinary offline draft"));
        QCOMPARE(received.count(), 0);
    }

    void draftsAndReplyContextsAreConversationScoped() {
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        ServerConfig config;
        config.id = 1;
        const auto serverId = manager.addServer(config, false);
        manager.openQuery(serverId, "alice");
        manager.openQuery(serverId, "bob");
        ChannelPaneWidget pane(&manager, nullptr, nullptr, false);
        pane.setTargetChannel(serverId, "alice");
        auto* input = pane.findChild<ChatInputWidget*>();
        auto* editor = input->findChild<ChatTextEdit*>();
        editor->setPlainText("Draft for Alice");
        IncomingMessage reply;
        reply.sender = "alice";
        input->setReplyContext(reply);
        pane.setTargetChannel(serverId, "bob");
        QVERIFY(editor->toPlainText().isEmpty());
        QVERIFY(!input->replyContext());
        editor->setPlainText("Draft for Bob");
        pane.setTargetChannel(serverId, "alice");
        QCOMPARE(editor->toPlainText(), QString("Draft for Alice"));
        QCOMPARE(input->replyContext()->sender, QString("alice"));
        QTest::keyClick(editor, Qt::Key_Return);
        QCOMPARE(editor->toPlainText(), QString("Draft for Alice"));
    }

    void initialConnectionFailureSchedulesRetry() {
        QTcpServer fixture;
        QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));
        const auto unusedPort = fixture.serverPort();
        fixture.close();
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        ServerConfig config;
        config.id = 1;
        config.hostname = "127.0.0.1";
        config.port = unusedPort;
        config.useSsl = false;
        const auto serverId = manager.addServer(config, false);
        manager.connectServer(serverId);
        QTRY_COMPARE_WITH_TIMEOUT(manager.connection(serverId)->state, ConnectionState::RECONNECTING, 3000);
        QVERIFY(manager.m_reconnectTimers.contains(serverId));
        manager.disconnectServer(serverId);
        QVERIFY(!manager.m_reconnectTimers.contains(serverId) || !manager.m_reconnectTimers[serverId]->isActive());
    }

    void failedDatabaseDoesNotCreatePhantomRecord() {
        ConnectionManager transient(nullptr, nullptr, nullptr, nullptr, nullptr);
        ServerConfig sameId; sameId.id = 1;
        QVERIFY(transient.addServer(sameId, false) != transient.addServer(sameId, false));
        QCOMPARE(transient.connections().size(), 2);
        QTemporaryDir directory;
        DatabaseService database(directory.path());
        ConnectionManager manager(&database, nullptr, nullptr, nullptr, nullptr);
        QCOMPARE(manager.addServer({}, false), qint64(-1));
        QVERIFY(!manager.connection(-1));
        QVERIFY(manager.connections().isEmpty());
    }
    void cacheCanonicalCaseAndLegacyMigration() {
        QTemporaryDir directory;
        IncomingMessage message; message.content = "legacy";
        QFile legacy(directory.filePath("1__Room.json"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(QJsonDocument(QJsonObject{{"serverId", 1}, {"channelName", "#Room"},
            {"messages", QJsonArray{message.toJson()}}}).toJson());
        legacy.close();
        MessageCache cache(directory.path());
        QCOMPARE(cache.getCachedMessages(1, "#room").size(), 1);
        message.content = "updated"; message.senderAccount = "account";
        cache.cacheMessage(1, "#ROOM", message);
        QVERIFY(cache.flushSync());
        QVERIFY(QFile::exists(legacy.fileName()));
        MessageCache next(directory.path());
        const auto messages = next.getCachedMessages(1, "#Room");
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages.first().content, QString("updated"));
        QCOMPARE(messages.first().senderAccount.value_or(QString()), QString("account"));
        QCOMPARE(cache.channelKey(1, "#[Room]"), cache.channelKey(1, "#{room}"));
        cache.setCaseMapping(1, "ascii");
        QVERIFY(cache.channelKey(1, "#[Room]") != cache.channelKey(1, "#{room}"));
    }

    void cacheFailedWriteRetainsDirtyData() {
        QTemporaryDir directory;
        const auto path = directory.filePath("cache");
        MessageCache cache(path);
        IncomingMessage message; message.content = "retain this";
        cache.cacheMessage(1, "#room", message);
        QVERIFY(QDir().rmdir(path));
        QFile obstruction(path); QVERIFY(obstruction.open(QIODevice::WriteOnly)); obstruction.close();
        QVERIFY(!cache.flushSync());
        QVERIFY(cache.m_channels.first().dirty);
        QCOMPARE(cache.m_channels.first().messages.first().content, message.content);
        QVERIFY(QFile::remove(path)); QVERIFY(QDir().mkpath(path));
        QVERIFY(cache.flushSync());
    }

    void asyncCacheClearCannotResurrectHistory() {
        QTemporaryDir directory;
        MessageCache cache(directory.path());
        IncomingMessage message;
        cache.cacheMessage(1, "#room", message);
        cache.startWrite(cache.channelKey(1, "#room"));
        bool loaded = false;
        cache.loadChannelAsync(1, "#room", 50, [&](const CachedChannel&) { loaded = true; });
        cache.clearChannelCache(1, "#room");
        QVERIFY(cache.flushSync());
        QCoreApplication::processEvents();
        QVERIFY(!loaded);
        QVERIFY(cache.getCachedMessages(1, "#room").isEmpty());
    }

    void asyncHistoryMergesNewerLiveMetadata() {
        QTemporaryDir directory;
        MessageCache cache(directory.path());
        IncomingMessage message; message.content = "old";
        cache.cacheMessage(1, "#room", message);
        QVERIFY(cache.flushSync());
        bool loaded = false;
        CachedChannel result;
        cache.loadChannelAsync(1, "#room", 50, [&](const CachedChannel& data) { result = data; loaded = true; });
        message.content = "live metadata";
        cache.cacheMessage(1, "#room", message);
        QTRY_VERIFY_WITH_TIMEOUT(loaded, 3000);
        QCOMPARE(result.messages.size(), 1);
        QCOMPARE(result.messages.first().content, message.content);
    }

    void settingsFailureLeavesSnapshotUnchanged() {
        QTemporaryDir directory;
        PreferencesManager prefs(directory.filePath("preferences.json"));
        const auto original = prefs.preferences();
        QVERIFY(QFile::remove(prefs.configFilePath()));
        QVERIFY(QDir().mkpath(prefs.configFilePath()));
        QSignalSpy failure(&prefs, &PreferencesManager::storageError);
        QVERIFY(!prefs.update([](UserPreferences& value) { value.theme = "Changed"; }));
        QCOMPARE(prefs.preferences(), original);
        QCOMPARE(failure.size(), 1);
    }

    void backupValidationAndRoundTrip() {
        UserPreferences prefs;
        ServerConfig server; server.hostname = "irc.example.test"; server.nickname = "tester";
        auto backup = ConfigExporter::parseBackup(ConfigExporter::serializeBackup(prefs, {server}));
        QVERIFY(backup); QCOMPARE(backup->servers.size(), 1); QCOMPARE(backup->preferences, prefs);
        auto record = server.toJson(); record["id"] = 1.5;
        auto invalidId = QJsonDocument::fromJson(ConfigExporter::serializeBackup(prefs, {server}).toUtf8()).object();
        invalidId["servers"] = QJsonArray{record};
        QVERIFY(!ConfigExporter::parseBackup(QString::fromUtf8(QJsonDocument(invalidId).toJson())));
        auto object = QJsonDocument::fromJson(ConfigExporter::serializeBackup(prefs, {server}).toUtf8()).object();
        auto settings = object["preferences"].toObject();
        settings["historyReplayLines"] = -10; object["preferences"] = settings;
        QVERIFY(!ConfigExporter::parseBackup(QString::fromUtf8(QJsonDocument(object).toJson())));
        object["preferences"] = prefs.toJson(); object["version"] = 2;
        QVERIFY(!ConfigExporter::parseBackup(QString::fromUtf8(QJsonDocument(object).toJson())));
    }

    void portableBackupsOmitSecretsWithoutChangingLocalRecords() {
        UserPreferences prefs;
        prefs.imgbbApiKey = "qa-api-marker";
        ServerConfig server;
        server.hostname = "irc.example.test"; server.nickname = "portable-tester";
        server.saslPassword = "qa-sasl-marker"; server.nickServPassword = "qa-nickserv-marker";
        server.proxyPassword = "qa-proxy-marker"; server.channelKeys.insert("#ordinary", "qa-channel-marker");
        server.onConnectCommands = "ordinary qa-command-marker";
        server.nickServCommand = "ordinary qa-template-marker";
        server.autoConnect = true; server.autoJoinChannels = "#ordinary";
        const auto originalServer = server.toJson();
        const auto originalPrefs = prefs.toJson();
        const auto serialized = ConfigExporter::serializeBackup(prefs, {server});
        const auto root = QJsonDocument::fromJson(serialized.toUtf8()).object();
        const auto portable = root["servers"].toArray().first().toObject();
        QVERIFY(root["secretsOmitted"].toBool());
        for (const auto& key : {"saslPassword", "nickServPassword", "proxyPassword", "channelKeys", "onConnectCommands", "nickServCommand"})
            QVERIFY(!portable.contains(key));
        QVERIFY(!root["preferences"].toObject().contains("imgbbApiKey"));
        for (const auto& secret : {"qa-api-marker", "qa-sasl-marker", "qa-nickserv-marker", "qa-proxy-marker", "qa-channel-marker", "qa-command-marker", "qa-template-marker"})
            QVERIFY(!serialized.contains(secret));
        const auto restored = ConfigExporter::parseBackup(serialized);
        QVERIFY(restored); QVERIFY(restored->secretsOmitted);
        QCOMPARE(restored->servers.first().hostname, server.hostname);
        QCOMPARE(restored->servers.first().nickname, server.nickname);
        QCOMPARE(restored->servers.first().autoJoinChannels, server.autoJoinChannels);
        QVERIFY(!restored->servers.first().autoConnect);
        QCOMPARE(server.toJson(), originalServer); QCOMPARE(prefs.toJson(), originalPrefs);
        auto legacy = root; legacy.remove("secretsOmitted");
        legacy["preferences"] = originalPrefs; legacy["servers"] = QJsonArray{originalServer};
        const auto legacyRestored = ConfigExporter::parseBackup(QString::fromUtf8(QJsonDocument(legacy).toJson()));
        QVERIFY(legacyRestored); QVERIFY(!legacyRestored->secretsOmitted);
        QCOMPARE(legacyRestored->preferences, prefs);
        QCOMPARE(legacyRestored->servers.first().toJson(), originalServer);
        QTemporaryDir directory;
        const auto path = directory.filePath("portable.json");
        QVERIFY(ConfigExporter::exportToFile(path, prefs, {server}));
        const auto imported = ConfigExporter::importFromFile(path);
        QVERIFY(imported); QVERIFY(imported->secretsOmitted);
        QVERIFY(!(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)));
    }

    void databaseWritesDoNotPublishMissingRecords() {
        QTemporaryDir directory;
        DatabaseService database(directory.filePath("db.sqlite"));
        QVERIFY(database.isReady());
        ConnectionManager manager(&database, nullptr, nullptr, nullptr, nullptr);
        ServerConfig config; config.hostname = "irc.example.test"; config.nickname = "tester";
        const auto id = manager.addServer(config, false); QVERIFY(id > 0);
        QVERIFY(database.deleteServerConfig(id));
        config.id = id; config.nickname = "changed";
        manager.updateServer(config);
        QCOMPARE(manager.connection(id)->config.nickname, QString("tester"));
        manager.removeServer(id);
        QVERIFY(manager.connection(id)); // Failed deletion does not destroy live state.
    }

    void databaseRejectsForeignThreadBeforeSqlAccess() {
        QTemporaryDir directory;
        DatabaseService database(directory.filePath("db.sqlite"));
        auto* owner = QThread::currentThread();
        bool foreign = false;
        qint64 result = 0;
        std::thread worker([&]() {
            foreign = QThread::currentThread() != owner;
            result = database.insertServerConfig({});
        });
        worker.join();
        QVERIFY(foreign);
        QCOMPARE(result, qint64(-1));
        QVERIFY(database.getAllServerConfigs().isEmpty());
    }

    void modelUpdatesKeepPersistentIndexesAndAvoidReset() {
        MessageListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        IncomingMessage a, b, c; a.content = "A"; b.content = "B"; c.content = "C";
        model.setItems({MessageItemView{a}, MessageItemView{b}});
        QPersistentModelIndex retained(model.index(1));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        model.setItems({MessageItemView{b}, MessageItemView{c}});
        QCOMPARE(retained.row(), 0); QCOMPARE(retained.data(MessageListModel::MessageIdRole).toString(), b.id);
        b.content = "updated"; model.setItems({MessageItemView{b}, MessageItemView{c}});
        QCOMPARE(retained.data(MessageListModel::ContentRole).toString(), b.content);
        QCOMPARE(resets.size(), 0);
    }

    void renderedDocumentsRetireRemovedRowsAndDisposedModels() {
        MessageItemDelegate delegate;
        auto model = std::make_unique<MessageListModel>();
        MessageListView view(model.get(), &delegate);
        auto makeItem = [](int serial) {
            IncomingMessage message;
            message.id = QStringLiteral("cache-row-%1").arg(serial);
            message.sender = QStringLiteral("tester");
            message.content = QStringLiteral("Ordinary row %1").arg(serial);
            return ChatViewItem{MessageItemView{message}};
        };
        QList<ChatViewItem> items{makeItem(0), makeItem(1), makeItem(2)};
        model->setItems(items);
        for (int cycle = 0; cycle < 6; ++cycle) {
            QPointer<QTextDocument> removed = delegate.documentFor(model->index(0), 400, QFont());
            QPointer<QTextDocument> retained = delegate.documentFor(model->index(1), 400, QFont());
            QVERIFY(removed); QVERIFY(retained);
            items.removeFirst();
            items.append(makeItem(cycle + 3));
            model->setItems(items);
            QCoreApplication::processEvents();
            QVERIFY(removed.isNull());
            QCOMPARE(delegate.documentFor(model->index(0), 400, QFont()), retained.data());
        }
        model->setItems({});
        QCoreApplication::processEvents();
        QVERIFY(delegate.m_documents.isEmpty());

        model->setItems(items);
        QPointer<QTextDocument> pending = delegate.documentFor(model->index(0), 400, QFont());
        model->setItems({}); // Leave a queued retirement pending while its model dies.
        model.reset();
        QVERIFY(pending.isNull());
        QCoreApplication::processEvents();
        QVERIFY(delegate.m_documents.isEmpty());
    }

    void renderedDocumentsRetireEditedRevisionsButReuseUnchangedRows() {
        MessageListModel model;
        MessageItemDelegate delegate;
        MessageListView view(&model, &delegate);
        IncomingMessage edited, unchanged;
        edited.content = QStringLiteral("Original ordinary text");
        unchanged.content = QStringLiteral("Unchanged ordinary text");
        model.setItems({MessageItemView{edited}, MessageItemView{unchanged}});
        QPointer<QTextDocument> stable = delegate.documentFor(model.index(1), 400, QFont());
        for (int cycle = 0; cycle < 6; ++cycle) {
            QPointer<QTextDocument> previous = delegate.documentFor(model.index(0), 400, QFont());
            edited.content = QStringLiteral("Updated ordinary text %1").arg(cycle);
            model.setItems({MessageItemView{edited}, MessageItemView{unchanged}});
            QCoreApplication::processEvents();
            QVERIFY(previous.isNull());
            QCOMPARE(delegate.documentFor(model.index(1), 400, QFont()), stable.data());
            QVERIFY(delegate.documentFor(model.index(0), 400, QFont())->toPlainText().contains(edited.content));
        }
        model.setItems({});
        QCoreApplication::processEvents();
        QVERIFY(stable.isNull());
        QVERIFY(delegate.m_documents.isEmpty());
    }

    void unchangedPaneDoesNotRebuildDocuments() {
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        const auto id = manager.addServer({}, false);
        manager.openQuery(id, "alice");
        IncomingMessage message; message.content = "Hello";
        manager.appendMessageToChannel(id, "alice", message);
        ChannelPaneWidget pane(&manager, nullptr, nullptr);
        pane.setTargetChannel(id, "alice");
        QSignalSpy changes(pane.m_model, &QAbstractItemModel::dataChanged);
        QSignalSpy inserts(pane.m_model, &QAbstractItemModel::rowsInserted);
        const auto index = pane.m_model->index(0);
        const auto* first = pane.m_delegate->documentFor(index, 400, QFont());
        for (int i = 0; i < 3; ++i) pane.refreshPane();
        QCOMPARE(pane.m_delegate->documentFor(index, 400, QFont()), first);
        QCOMPARE(changes.size(), 0); QCOMPARE(inserts.size(), 0);
        pane.setTargetChannel(999, "missing");
        pane.setTargetChannel(id, "alice");
        QCOMPARE(pane.m_model->rowCount(), 1);
    }

    void closedQueriesReleaseDraftHistoryAndActivity() {
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        const auto id = manager.addServer({}, false);
        manager.openQuery(id, "alice"); manager.openQuery(id, "bob");
        ChannelPaneWidget pane(&manager, nullptr, nullptr);
        pane.setTargetChannel(id, "alice");
        pane.m_inputWidget->findChild<ChatTextEdit*>()->setPlainText("draft");
        pane.setTargetChannel(id, "bob");
        manager.m_inputHistories[QString("%1:alice").arg(id)] = {"history"};
        manager.closeChannelOrQuery(id, "alice");
        QTRY_VERIFY(pane.m_drafts.isEmpty());
        QVERIFY(manager.inputHistory(id, "alice").isEmpty());
        manager.removeServer(id);
        QVERIFY(manager.m_channelLoads.isEmpty());
        QVERIFY(manager.m_typingExpires.isEmpty());
    }

    void nicknameChangeMovesQueryDraftAndHistory() {
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        const auto id = manager.addServer({}, false);
        manager.openQuery(id, "alice"); manager.openQuery(id, "bob");
        ChannelPaneWidget pane(&manager, nullptr, nullptr);
        pane.setTargetChannel(id, "alice");
        pane.m_inputWidget->findChild<ChatTextEdit*>()->setPlainText("draft");
        pane.setTargetChannel(id, "bob");
        manager.m_inputHistories[QString("%1:alice").arg(id)] = {"history"};
        Q_EMIT manager.client(id)->nicknameChanged(id, "alice", "alicia");
        QVERIFY(manager.connection(id)->findChannel("alicia"));
        QVERIFY(!manager.connection(id)->findChannel("alice"));
        QCOMPARE(manager.inputHistory(id, "alicia"), QStringList{"history"});
        pane.setTargetChannel(id, "alicia");
        QCOMPARE(pane.m_inputWidget->draft().text, QString("draft"));
    }

    void namesSnapshotReconcilesInterleavedMembership() {
        IrcClient client(1, {});
        client.processLine(":irc.local 353 tester = #room :alice bob");
        client.processLine(":alice!u@h PART #room :Bye");
        client.processLine(":carol!u@h JOIN #room");
        client.processLine(":bob!u@h NICK :robert");
        client.processLine(":irc.local 353 tester = #room :alice bob");
        client.processLine(":irc.local 366 tester #room :End");
        const auto users = client.m_channelUsers.value("#room");
        QVERIFY(!users.contains("alice")); QVERIFY(!users.contains("bob"));
        QVERIFY(users.contains("carol")); QVERIFY(users.contains("robert"));
        QVERIFY(client.m_pendingNames.isEmpty()); QVERIFY(client.m_namesRemoved.isEmpty());
    }

    void registrationAndPongDeadlinesFailCleanly() {
        QTcpServer fixture; QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));
        ServerConfig config; config.hostname = "127.0.0.1"; config.port = fixture.serverPort(); config.useSsl = false;
        IrcClient client(1, config);
        QSignalSpy errors(&client, &ChatClient::connectionError);
        client.connectToServer();
        client.m_registrationTimer.start(20);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 2000);
        QVERIFY(!client.isConnected());
        QVERIFY(!client.m_registrationTimer.isActive());
        errors.clear();
        client.m_isRegistered = true;
        client.m_waitingPong = true; client.m_pingClock.start(); client.m_pongTimeoutMs = 0;
        QTest::qWait(1);
        client.onKeepAliveTimeout();
        QCOMPARE(errors.count(), 1);
    }

    void pongDeadlineDoesNotNeedAnotherPeriodicTick() {
        QTcpServer fixture; QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));
        ServerConfig config; config.hostname="127.0.0.1"; config.port=fixture.serverPort(); config.useSsl=false;
        IrcClient client(1,config);
        QSignalSpy errors(&client,&ChatClient::connectionError);
        client.connectToServer();
        QTRY_VERIFY(fixture.hasPendingConnections());
        client.processLine(":fixture CAP * LS :");
        client.processLine(":fixture 001 LoungeCatUser :Welcome");
        QTRY_VERIFY(client.isConnected());
        client.m_keepAliveTimer.stop();
        client.m_pongTimeoutMs=60;
        client.onKeepAliveTimeout();
        QTest::qWait(10);
        client.onKeepAliveTimeout(); // Simulate a periodic check before expiry.
        QCOMPARE(errors.size(),0);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(),1,1000);
        QVERIFY(!client.isConnected());
        QTest::qWait(80);
        QCOMPARE(errors.size(),1);
    }

    void matchingPongAndManualStopRetireTheDeadline() {
        QTcpServer fixture; QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));
        ServerConfig config; config.hostname="127.0.0.1"; config.port=fixture.serverPort(); config.useSsl=false;
        IrcClient client(1,config);
        QSignalSpy errors(&client,&ChatClient::connectionError);
        client.connectToServer();
        QTRY_VERIFY(fixture.hasPendingConnections());
        client.processLine(":fixture CAP * LS :");
        client.processLine(":fixture 001 LoungeCatUser :Welcome");
        QTRY_VERIFY(client.isConnected());
        client.m_keepAliveTimer.stop();
        client.m_pongTimeoutMs=60;
        client.onKeepAliveTimeout();
        client.processLine(":fixture PONG :unrelated-token");
        QVERIFY(client.m_waitingPong);
        client.processLine(":fixture PONG :keepalive");
        QTest::qWait(100);
        QVERIFY(client.isConnected());
        QCOMPARE(errors.size(),0);
        client.onKeepAliveTimeout();
        client.disconnectFromServer();
        QTest::qWait(100);
        QCOMPARE(errors.size(),0);
    }

    void disconnectInvalidatesDelayedSetup() {
        IrcClient client(1, {});
        const auto initial = client.m_generation;
        client.handleWelcomeSequence();
        client.disconnectFromServer();
        QVERIFY(client.m_generation > initial);
        QVERIFY(!client.m_isRegistered);
        QTest::qWait(550);
        QVERIFY(!client.isConnected());
    }

    void uploadFailureCallbackSurvivesCommandStackAndServerRemoval() {
        QTemporaryDir directory;
        PreferencesManager prefs(directory.filePath("prefs.json"));
        QVERIFY(prefs.update([](UserPreferences& p) { p.imgbbApiKey = "local-test-placeholder"; }));
        ConnectionManager manager(nullptr, nullptr, &prefs, nullptr, nullptr);
        const auto id = manager.addServer({}, false);
        auto* client = static_cast<IrcClient*>(manager.client(id));
        client->m_isRegistered = true;
        QSignalSpy failed(&manager, &ConnectionManager::inputFailed);
        QVERIFY(manager.sendUserInput(id, "alice", "/uploadimage /nonexistent/loungecat-fixture.png"));
        QTRY_VERIFY_WITH_TIMEOUT(!failed.isEmpty(), 3000);
        const auto second = manager.addServer({}, false);
        static_cast<IrcClient*>(manager.client(second))->m_isRegistered = true;
        QVERIFY(manager.sendUserInput(second, "alice", "/uploadimage /nonexistent/loungecat-fixture.png"));
        manager.removeServer(second);
        QTest::qWait(100);
        QVERIFY(!manager.connection(second));
    }

    void ownerDestructionJoinsPendingWorkers() {
        QTemporaryDir directory;
        LocalHttpFixture fixture;
        bool networkCompletion = false;
        {
            LinkPreviewService previews;
            previews.fetchPreview("http://example.com/pending", fixture.config(), true,
                [&](const UrlPreview&) { networkCompletion = true; });
            PastebinService pastes;
            pastes.m_nam.setProxy(fixture.proxy());
            pastes.upload("local fixture", [&](bool, const QString&) { networkCompletion = true; });
            UrlShortenerService shortener;
            shortener.m_nam.setProxy(fixture.proxy());
            shortener.shortenUrl("https://example.com/ordinary", [&](const QString&) { networkCompletion = true; });
            ImgBbService uploads;
            uploads.uploadFromFile(directory.filePath("missing.png"), "local-test-placeholder",
                [](bool, const QString&) {});
            SpellChecker checker; checker.setLanguage("pl_PL");
            MessageCache history(directory.filePath("messages"));
            history.loadChannelAsync(1, "#room", 50, [](const CachedChannel&) {});
        }
        QCoreApplication::processEvents();
        QVERIFY(!networkCompletion);
    }

    void spellSnapshotsRemainValidDuringPreferenceChanges() {
        SpellChecker checker;
        const auto snapshot = checker.snapshot();
        auto future = QtConcurrent::run([snapshot]() { return SpellChecker::checkSnapshot(snapshot, "hello world"); });
        checker.setEnabled(false); checker.setLanguage("pl_PL");
        future.waitForFinished();
        QVERIFY(!checker.isEnabled());
        QCOMPARE(checker.language(), QString("pl_PL"));
    }

    void soundBusyWorkerCoalescesWithoutQueuedTasks() {
        SoundService sounds;
        QSemaphore started, release;
        sounds.m_pool.start([&]() { started.release(); release.acquire(); });
        started.acquire();
        for (int i = 0; i < 10; ++i) sounds.play(SoundType::MENTION);
        QCOMPARE(sounds.m_pool.activeThreadCount(), 1);
        release.release();
    }

    void logRotationNeverDeletesExistingHistory() {
        QTemporaryDir directory;
        TextLogService logs(directory.path());
        const auto path = logs.resolveLogFilePath("TestNet", "#room");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        for (int i = 0; i < 4; ++i) {
            QFile file(i == 0 ? path : path + QString(".%1").arg(i));
            QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(TextLogService::MAX_LOG_FILE_BYTES)); file.close();
        }
        IncomingMessage message; message.content = "new line";
        QVERIFY(logs.logMessage("TestNet", "#room", message));
        QVERIFY(!logs.flushSync());
        QCOMPARE(QFileInfo(path).size(), TextLogService::MAX_LOG_FILE_BYTES);
        for (int i = 1; i <= 3; ++i) QVERIFY(QFile::exists(path + QString(".%1").arg(i)));
        QVERIFY(!logs.lastError().isEmpty());
    }

    void ignorePreferencesPreserveUnicodeAndNegotiatedAliasesAcrossReload() {
        QTemporaryDir temp; const auto path = temp.filePath("prefs.json");
        PreferencesManager prefs(path);
        const auto upper = QString::fromUtf8("Émile"), lower = QString::fromUtf8("émile");
        QVERIFY(prefs.addIgnoredUser(upper));
        PreferencesManager loaded(path);
        QVERIFY(loaded.isUserIgnored(upper)); QVERIFY(!loaded.isUserIgnored(lower));
        QVERIFY(loaded.addIgnoredUser(lower)); QCOMPARE(loaded.preferences().ignoredUsers.size(), 2);
        QVERIFY(loaded.removeIgnoredUser(upper)); QVERIFY(loaded.isUserIgnored(lower)); QVERIFY(!loaded.isUserIgnored(upper));
        QVERIFY(loaded.addIgnoredUser("[Nick]", "rfc1459"));
        QVERIFY(loaded.isUserIgnored("{NICK}", "rfc1459")); QVERIFY(!loaded.isUserIgnored("{NICK}", "ascii"));
        QVERIFY(loaded.removeIgnoredUser("{nick}", "rfc1459")); QVERIFY(!loaded.isUserIgnored("[Nick]"));
    }

    void ignoredUsersCannotPublishTypingOrReactionsAndUnignoreRestoresThem() {
        QTemporaryDir temp; PreferencesManager prefs(temp.filePath("prefs.json"));
        QVERIFY(prefs.update([](auto& p) { p.notificationsEnabled=false; p.soundAlertsEnabled=false; p.loggingEnabled=false; }));
        QVERIFY(prefs.addIgnoredUser("[Nick]", "rfc1459"));
        ConnectionManager manager(nullptr, nullptr, &prefs, nullptr, nullptr);
        ServerConfig cfg; cfg.hostname="unused.invalid"; cfg.nickname="tester";
        const auto id=manager.addServer(cfg, false); auto* client=manager.client(id);
        QVERIFY(manager.openQuery(id,"peer"));
        IncomingMessage parent; parent.id="parent"; parent.target="peer"; parent.sender="other"; parent.content="hello";
        Q_EMIT client->messageReceived(id,parent,false);
        Q_EMIT client->typingUserUpdated(id,"peer","{NICK}","active");
        Q_EMIT client->reactionReceived(id,"peer","parent","{NICK}","like");
        { const auto snapshot=manager.connection(id); const auto* channel=snapshot->findChannel("peer");
          QVERIFY(channel->currentTypingUsers.isEmpty()); for(const auto& m:channel->messages) QVERIFY(m.reactions.isEmpty()); }
        QVERIFY(prefs.removeIgnoredUser("{nick}","rfc1459"));
        Q_EMIT client->typingUserUpdated(id,"peer","{NICK}","active");
        Q_EMIT client->reactionReceived(id,"peer","parent","{NICK}","like");
        { const auto snapshot=manager.connection(id); const auto* channel=snapshot->findChannel("peer");
          QCOMPARE(channel->currentTypingUsers,QStringList{"{NICK}"});
          bool found=false; for(const auto& m:channel->messages) if(m.id=="parent") found=m.reactions.value("like").contains("{NICK}"); QVERIFY(found); }
    }

    void settingsDialogPreservesIgnoreIdentityAndKeepsFailedEditsForRetry() {
        QTemporaryDir temp; const auto directory=temp.filePath("settings"); QVERIFY(QDir().mkdir(directory));
        PreferencesManager prefs(directory+"/prefs.json"); ThemeManager themes(temp.filePath("themes"));
        const auto original=prefs.preferences().toJson(); SettingsDialog dialog(&prefs,&themes,nullptr);
        dialog.m_ignoredUsersEdit->setText(QString::fromUtf8("Émile, émile"));
        dialog.m_fontSizeCombo->setCurrentIndex(static_cast<int>(FontSizePreference::LARGE));
        dialog.show(); QSignalSpy accepted(&dialog,&QDialog::accepted);
        QVERIFY(QFile::setPermissions(directory,QFileDevice::ReadOwner|QFileDevice::ExeOwner));
        dialog.saveAndAccept();
        QVERIFY(QFile::setPermissions(directory,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        QVERIFY(accepted.isEmpty()); QVERIFY(dialog.isVisible()); QVERIFY(dialog.m_saveErrorLabel->isVisible());
        QVERIFY(!dialog.m_saveErrorLabel->text().isEmpty()); QCOMPARE(prefs.preferences().toJson(),original);
        QCOMPARE(dialog.m_ignoredUsersEdit->text(),QString::fromUtf8("Émile, émile"));
        dialog.saveAndAccept(); QCOMPARE(accepted.size(),1);
        PreferencesManager reloaded(directory+"/prefs.json");
        QCOMPARE(reloaded.preferences().fontSize,FontSizePreference::LARGE);
        QCOMPARE(reloaded.preferences().ignoredUsers,(QSet<QString>{QString::fromUtf8("Émile"),QString::fromUtf8("émile")}));
    }

    void settingsDoNotClampUneditedValidValues() {
        for (const auto& key : QStringList{"inputHistorySize", "urlShorteningThreshold"}) {
            const QList<int> values=key=="inputHistorySize"?QList<int>{0,1,9,10,500}:QList<int>{50,2000,2001,5000};
            for (const auto value : values) {
                QTemporaryDir temp; PreferencesManager prefs(temp.filePath("prefs.json")); ThemeManager themes(temp.filePath("themes"));
                QVERIFY(prefs.update([&](auto& p) { if(key=="inputHistorySize")p.inputHistorySize=value;else p.urlShorteningThreshold=value; }));
                themes.setActiveTheme(prefs.preferences().theme);
                auto expected=prefs.preferences().toJson(); expected["fontSize"]="LARGE";
                SettingsDialog dialog(&prefs,&themes,nullptr);
                dialog.m_fontSizeCombo->setCurrentIndex(static_cast<int>(FontSizePreference::LARGE));
                dialog.saveAndAccept();
                QCOMPARE(dialog.result(),int(QDialog::Accepted));
                PreferencesManager reloaded(temp.filePath("prefs.json"));
                QCOMPARE(reloaded.preferences().toJson(),expected);
            }
        }
    }

    void recoveredTextLoggingReportsASecondFailure() {
        QTemporaryDir directory;
        TextLogService logs(directory.path());
        const auto path = logs.resolveLogFilePath("Net", "#room");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("old\n"), qint64(4)); }
        QSignalSpy errors(&logs, &TextLogService::storageError);
        IncomingMessage message; message.content = "recovered";
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
        QVERIFY(logs.logMessage("Net", "#room", message));
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!logs.lastError().isEmpty());
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(logs.logMessage("Net", "#room", message));
        QTRY_VERIFY(logs.lastError().isEmpty());
        QVERIFY(logs.flushSync());
        { QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto text = file.readAll();
          QVERIFY(text.startsWith("old\n")); QVERIFY(text.contains("recovered")); }
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
        QVERIFY(logs.logMessage("Net", "#room", message));
        QTRY_COMPARE(errors.size(), 2);
        QVERIFY(!logs.lastError().isEmpty());
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    }

    void legacyLogCollisionPreservesBothCopies() {
        QTemporaryDir directory;
        QVERIFY(QDir().mkpath(directory.filePath("Test_")));
        QVERIFY(QDir().mkpath(directory.filePath("Test")));
        QFile old(directory.filePath("Test_/#room.log")); QVERIFY(old.open(QIODevice::WriteOnly)); old.write("old\n"); old.close();
        QFile current(directory.filePath("Test/#room.log")); QVERIFY(current.open(QIODevice::WriteOnly)); current.write("new\n"); current.close();
        TextLogService logs(directory.path());
        QVERIFY(!logs.migrateLegacyFolders());
        QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(), QByteArray("old\n"));
        QVERIFY(current.open(QIODevice::ReadOnly)); QCOMPARE(current.readAll(), QByteArray("new\n"));
    }

    void imageDiskQuotaRunsInOwnedWorker() {
        QTemporaryDir directory;
        const auto bytes = tinyPng();
        ImageCache cache; cache.m_diskCacheDir = directory.path();
        const QString target("https://example.com/cached.png");
        for (int i = 0; i <= ImageCache::MAX_DISK_IMAGES; ++i) {
            const auto url = i == ImageCache::MAX_DISK_IMAGES ? target : QString("https://example.com/cache-%1.png").arg(i);
            QFile file(cache.diskCachePath(url)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(bytes);
        }
        QSignalSpy loaded(&cache, &ImageCache::imageLoaded);
        QVERIFY(cache.getImage(target).isNull());
        QTRY_COMPARE_WITH_TIMEOUT(loaded.size(), 1, 3000);
        QVERIFY(QDir(directory.path()).entryList(QDir::Files).size() <= ImageCache::MAX_DISK_IMAGES);
    }

    void localSendEchoUsesOnePendingMessageAndAuthoritativeId() {
        QTcpServer fixture; QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));
        ServerConfig config; config.hostname = "127.0.0.1"; config.port = fixture.serverPort();
        config.useSsl = false; config.nickname = "tester";
        IrcClient client(1, config);
        client.connectToServer();
        QTRY_VERIFY_WITH_TIMEOUT(fixture.hasPendingConnections(), 2000);
        auto* peer = fixture.nextPendingConnection(); peer->setParent(&fixture);
        QTRY_COMPARE(client.m_socket->state(), QAbstractSocket::ConnectedState);
        client.processLine(":irc.local 001 tester :Welcome");
        client.m_enabledCaps.insert("echo-message");
        QSignalSpy received(&client, &ChatClient::messageReceived);
        QSignalSpy updated(&client, &ChatClient::messageUpdated);
        QVERIFY(client.sendMessage("#room", "Hello"));
        QCOMPARE(received.size(), 1);
        const auto pending = received.first()[1].value<IncomingMessage>();
        QCOMPARE(pending.deliveryStatus, QString("pending"));
        client.processLine("@msgid=server-id :tester!u@h PRIVMSG #room :Hello");
        QCOMPARE(received.size(), 1); QCOMPARE(updated.size(), 1);
        QCOMPARE(updated.first()[1].toString(), pending.id);
        const auto delivered = updated.first()[2].value<IncomingMessage>();
        QCOMPARE(delivered.id, QString("server-id"));
        QCOMPARE(delivered.deliveryStatus, QString("delivered"));
        QVERIFY(client.m_pendingMessages.isEmpty());
        client.m_enabledCaps.remove("echo-message");
        QVERIFY(client.sendMessage("#room", "Second"));
        QCOMPARE(received.last()[1].value<IncomingMessage>().deliveryStatus, QString("submitted"));
        client.disconnectFromServer();
        QVERIFY(!client.sendMessage("#room", "Retained offline text"));
    }

    void reconnectCancellationDoesNotOpenAnotherSocket() {
        QTcpServer fixture; QVERIFY(fixture.listen(QHostAddress::LocalHost, 0));
        ServerConfig config; config.hostname = "127.0.0.1"; config.port = fixture.serverPort(); config.useSsl = false;
        ConnectionManager manager(nullptr, nullptr, nullptr, nullptr, nullptr);
        const auto id = manager.addServer(config, false);
        manager.reconnectServer(id);
        manager.disconnectServer(id);
        QTest::qWait(350);
        QVERIFY(!fixture.hasPendingConnections());
        QCOMPARE(manager.connection(id)->state, ConnectionState::DISCONNECTED);
    }

    void retiredCacheReleasesTimersWithoutDeletingHistory() {
        QTemporaryDir directory;
        MessageCache cache(directory.path());
        IncomingMessage message; message.content = "persistent";
        cache.cacheMessage(1, "#room", message);
        const auto key = cache.channelKey(1, "#room");
        QVERIFY(cache.m_saveTimers.contains(key));
        cache.retireChannel(1, "#room");
        QTRY_VERIFY_WITH_TIMEOUT(!cache.m_channels.contains(key), 3000);
        QVERIFY(!cache.m_saveTimers.contains(key));
        QVERIFY(cache.m_pendingWrites.isEmpty());
        QCOMPARE(cache.getCachedMessages(1, "#room").size(), 1);
    }

    void uploadPreparationExecutesAwayFromUiThread() {
        ImgBbService service;
        std::atomic_bool worker{false};
        bool completed = false;
        service.m_activeUploads = 1;
        service.prepare([&]() {
            worker.store(QThread::currentThread() != qApp->thread());
            return QByteArray{};
        }, "local-test-placeholder", [&](bool ok, const QString&) { QVERIFY(!ok); completed = true; });
        QTRY_VERIFY_WITH_TIMEOUT(completed, 3000);
        QVERIFY(worker.load());
        QCOMPARE(service.m_activeUploads, 0);
    }

    void animatedPngCannotBeSilentlyFlattened() {
        QTemporaryDir directory;
        const auto apng = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAACAAAAAQCAIAAAD4YuoOAAAACGFjVEwAAAACAAAAAPONk3AAAAAaZmNUTAAAAAAAAAAgAAAAEAAAAAAAAAAAAGQD6AAAv4HRJgAAACBJREFUeJxjfKaswUBLwERT00ctGLVg1IJRC0YtoBIAAJ7lAVHk7jBlAAAAGmZjVEwAAAABAAAAIAAAABAAAAAAAAAAAABkA+gAACTyO/IAAAAkZmRBVAAAAAJ4nGOUM3/GQEvARFPTRy0YtWDUglELRi2gEgAA2esBWzKGzHwAAAAASUVORK5CYII=");
        const auto poster = QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAACAAAAAQCAIAAAD4YuoOAAAACGFjVEwAAAABAAAAALQt6aAAAAAgSURBVHicY3ymrMFAS8BEU9NHLRi1YNSCUQtGLaASAACe5QFR5O4wZQAAABpmY1RMAAAAAAAAACAAAAAQAAAAAAAAAAAAZAPoAAC/gdEmAAAAJGZkQVQAAAABeJxjlDN/xkBLwERT00ctGLVg1IJRC0YtoBIAANnrAVtpkX1pAAAAAElFTkSuQmCC");
        LocalHttpFixture proxy;
        proxy.respond("200 OK", "text/plain", "fixture-only");
        ImgBbService service;
        service.m_nam.setProxy(proxy.proxy());
        for (const auto& input : QList<QPair<QString, QByteArray>>{
                 {"animation.png", apng}, {"animation.jpg", apng}, {"poster.png", poster}}) {
            QFile file(directory.filePath(input.first));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(input.second), static_cast<qint64>(input.second.size()));
            file.close();
            int callbacks = 0;
            bool accepted = true;
            QString detail;
            service.uploadFromFile(file.fileName(), "local-test-placeholder", [&](bool okay, const QString& message) {
                ++callbacks; accepted = okay; detail = message;
            });
            QTRY_COMPARE_WITH_TIMEOUT(callbacks, 1, 3000);
            QVERIFY(!accepted);
            QVERIFY(detail.contains("Animated"));
            QCOMPARE(proxy.requests, 0);
        }
        QImage still(32, 16, QImage::Format_RGB32);
        still.fill(Qt::red);
        still.setText("notes", "acTL is metadata text, not an animation chunk");
        const auto path = directory.filePath("still.png");
        QVERIFY(still.save(path, "PNG"));
        bool completed = false;
        QString detail;
        service.uploadFromFile(path, "local-test-placeholder", [&](bool, const QString& message) {
            completed = true; detail = message;
        });
        QTRY_VERIFY_WITH_TIMEOUT(completed, 3000);
        QCOMPARE(proxy.requests, 1);
        QVERIFY(!detail.contains("Animated"));
    }

    void updateNoticesRequireAnActionableReleaseUrl() {
        const QString page = QStringLiteral("https://github.com/binkiewka/LoungeCat-Desktop/releases/tag/v9.0.0");
        auto metadata = [&](const QString& url, const QString& tag = QStringLiteral("v9.0.0")) {
            return QJsonDocument(QJsonObject{{"tag_name", tag}, {"html_url", url},
                                             {"body", "release notes"}}).toJson();
        };
        const auto valid = UpdateCheckService::parseReleaseMetadata(metadata(page), "1.3.0");
        QVERIFY(valid.has_value());
        QCOMPARE(valid->version, "9.0.0");
        QCOMPARE(valid->releaseUrl, page);
        QCOMPARE(valid->releaseNotes, "release notes");
        for (const auto& url : QStringList{QString(), "/releases/latest", "https://github.com",
                                          "http://github.com/release", "https://example.invalid/release"}) {
            QVERIFY(!UpdateCheckService::parseReleaseMetadata(metadata(url), "1.3.0"));
        }
        QVERIFY(!UpdateCheckService::parseReleaseMetadata(metadata(page, "v1.3.0"), "1.3.0"));
        QVERIFY(!UpdateCheckService::parseReleaseMetadata(metadata(page, "v0.1.0"), "1.3.0"));
        QVERIFY(!UpdateCheckService::parseReleaseMetadata("not-json", "1.3.0"));
    }

    void primarySelectorKeepsGlobalConversationAndDraftStable() {
        QTemporaryDir directory;
        DatabaseService database(directory.filePath("db.sqlite"));
        PreferencesManager prefs(directory.filePath("prefs.json"));
        prefs.update([](auto& value) { value.spellCheckEnabled = false; value.loggingEnabled = false; });
        ThemeManager themes(directory.filePath("themes"));
        ConnectionManager manager(&database, nullptr, &prefs, nullptr, nullptr);
        ServerConfig config; config.serverName = "Saved offline server"; config.autoConnect = false;
        const auto id = manager.addServer(config, false);
        manager.openQuery(id, "alice"); manager.openQuery(id, "bob");
        manager.setActiveChannel(id, "alice");
        MainWindow window(&database, &prefs, &themes, nullptr, nullptr, &manager, true);
        window.show();
        auto* grid = window.findChild<ChatGridContainer*>();
        QVERIFY(grid);
        grid->setGridMode(GridMode::SPLIT_HORIZONTAL);
        QCoreApplication::processEvents();
        auto* primary = grid->primaryPane();
        auto* input = primary->findChild<ChatInputWidget*>();
        QVERIFY(input);
        input->restoreDraft({QString::fromUtf8("Keep ordinary café draft"), std::nullopt, 3, -1});
        auto choose = [&](ChannelPaneWidget* pane, const QString& name) {
            const auto key = QString::number(id) + ':' + name;
            for (auto* combo : pane->findChildren<QComboBox*>()) {
                const auto index = combo->findData(key);
                if (index < 0) continue;
                combo->setCurrentIndex(index);
                QCoreApplication::processEvents();
                return true;
            }
            return false;
        };
        QVERIFY(choose(primary, "bob"));
        QCOMPARE(primary->channelName(), "bob");
        QCOMPARE(manager.activeServerId(), id);
        QCOMPARE(manager.activeChannelName(), "bob");
        config.serverName = "Other inactive saved server";
        QVERIFY(manager.addServer(config, false) > 0);
        QCoreApplication::processEvents();
        QCOMPARE(primary->channelName(), "bob");
        QCOMPARE(manager.activeChannelName(), "bob");
        manager.setActiveChannel(id, "alice");
        QCOMPARE(input->draft().text, QString::fromUtf8("Keep ordinary café draft"));
        QCOMPARE(input->draft().cursor, 3);
        auto* auxiliary = grid->panes()[1];
        auxiliary->setTargetChannel(id, "alice");
        QVERIFY(choose(auxiliary, "bob"));
        QCOMPARE(auxiliary->channelName(), "bob");
        QCOMPARE(primary->channelName(), "alice");
        QCOMPARE(manager.activeChannelName(), "alice");
    }

    void settingsThemePreviewRestoresOnEveryRejectionRoute() {
        for (const QString& route : {QString("cancel"), QString("escape"), QString("close"), QString("save")}) {
            QTemporaryDir directory;
            PreferencesManager prefs(directory.filePath("prefs.json"));
            ThemeManager themes(directory.filePath("themes"));
            QVERIFY(prefs.update([](auto& p) { p.theme = "Dark"; p.spellCheckEnabled = false; }));
            themes.setActiveTheme("Dark");
            const auto original = prefs.preferences().toJson();
            SettingsDialog dialog(&prefs, &themes, nullptr);
            dialog.show();
            QCoreApplication::processEvents();
            QComboBox* selector = nullptr;
            for (auto* combo : dialog.findChildren<QComboBox*>())
                if (combo->findText("Dark") >= 0 && combo->findText("Light") >= 0) selector = combo;
            QVERIFY(selector);
            selector->setCurrentIndex(selector->findText("Light"));
            QCOMPARE(themes.currentTheme().name, "Light");
            auto* buttons = dialog.findChild<QDialogButtonBox*>();
            QVERIFY(buttons);
            if (route == "escape") QTest::keyClick(&dialog, Qt::Key_Escape);
            else if (route == "close") dialog.close();
            else QTest::mouseClick(buttons->button(route == "save" ? QDialogButtonBox::Save : QDialogButtonBox::Cancel), Qt::LeftButton);
            QCoreApplication::processEvents();
            QVERIFY(!dialog.isVisible());
            QCOMPARE(themes.currentTheme().name, route == "save" ? QString("Light") : QString("Dark"));
            auto expected = original;
            if (route == "save") expected["theme"] = "Light";
            PreferencesManager reloaded(directory.filePath("prefs.json"));
            QCOMPARE(prefs.preferences().toJson(), expected);
            QCOMPARE(reloaded.preferences().toJson(), expected);
            QCOMPARE(dialog.result(), route == "save" ? QDialog::Accepted : QDialog::Rejected);
        }
    }

    void settingsDropdownsExposeFieldLabelsAcrossValueChanges() {
        QTemporaryDir directory;
        PreferencesManager prefs(directory.filePath("prefs.json"));
        ThemeManager themes(directory.filePath("themes"));
        SettingsDialog dialog(&prefs, &themes, nullptr);
        dialog.show();
        QCoreApplication::processEvents();
        const QList<QPair<QString, QString>> controls{
            {"Dark", "Theme"}, {"Medium (100%)", "Font Size"},
            {"24-Hour (HH:mm)", "Timestamps"}, {"Show All", "Join/Part/Quit Display"},
            {"Compact Cards (Text Summary)", "Link Preview Mode"}, {"en_US", "Spell Check Language"},
        };
        for (const auto& control : controls) {
            QComboBox* combo = nullptr;
            for (auto* candidate : dialog.findChildren<QComboBox*>())
                if (candidate->findText(control.first) >= 0) combo = candidate;
            QVERIFY(combo);
            auto* accessible = QAccessible::queryAccessibleInterface(combo);
            QVERIFY(accessible);
            const auto labelled = [&]() {
                if (accessible->text(QAccessible::Name).contains(control.second)) return true;
                // Qt6.4 UNIX combos expose their current value as Name and use Label relations.
                for (const auto& relation : accessible->relations(QAccessible::Label | QAccessible::Labelled))
                    if (relation.first && relation.first->text(QAccessible::Name).contains(control.second)) return true;
                return false;
            };
            QVERIFY2(labelled(), qPrintable(control.second));
            const auto originalIndex = combo->currentIndex();
            combo->setCurrentIndex((originalIndex + 1) % combo->count());
            QVERIFY2(labelled(), qPrintable(control.second));
        }
        dialog.reject();
    }

    void searchShortcutFollowsFocusedPaneWithoutChangingPrimary() {
        QTemporaryDir directory;
        DatabaseService database(directory.filePath("db.sqlite"));
        PreferencesManager prefs(directory.filePath("prefs.json"));
        ThemeManager themes(directory.filePath("themes"));
        QVERIFY(prefs.update([](auto& p) { p.spellCheckEnabled = false; }));
        ConnectionManager manager(&database, nullptr, &prefs, nullptr, nullptr);
        ServerConfig config;
        config.hostname = "localhost";
        config.autoConnect = false;
        const auto id = manager.addServer(config, false);
        manager.openQuery(id, "alice");
        manager.openQuery(id, "bob");
        manager.setActiveChannel(id, "alice");
        MainWindow window(&database, &prefs, &themes, nullptr, nullptr, &manager, true);
        window.resize(1600, 1000);
        window.show();
        window.activateWindow();
        QCoreApplication::processEvents();
        auto* grid = window.findChild<ChatGridContainer*>();
        QVERIFY(grid);
        grid->setGridMode(GridMode::GRID_2X2);
        QCoreApplication::processEvents();
        QShortcut* findShortcut = nullptr;
        for (auto* shortcut : window.findChildren<QShortcut*>())
            if (shortcut->key() == QKeySequence::Find) findShortcut = shortcut;
        QVERIFY(findShortcut);
        const auto panes = grid->panes();
        for (int i = 0; i < 4; ++i) {
            panes[i]->setTargetChannel(id, i == 0 ? "alice" : "bob");
            panes[i]->findChild<ChatInputWidget*>()->restoreDraft({QString("Keep pane %1 draft").arg(i), std::nullopt, 3, -1});
        }
        for (auto* pane : panes) {
            auto* editor = pane->findChild<ChatTextEdit*>();
            editor->setFocus();
            QCoreApplication::processEvents();
            QCOMPARE(QApplication::focusWidget(), editor);
            // Exercise the shortcut's app handler independently of the offscreen
            // platform shortcut dispatcher; shown-X11 coverage sends actual Ctrl+F.
            QVERIFY(QMetaObject::invokeMethod(findShortcut, "activated", Qt::DirectConnection));
            QCoreApplication::processEvents();
            for (auto* candidate : panes)
                QCOMPARE(candidate->findChild<SearchOverlayBar*>()->isVisible(), candidate == pane);
            auto* search = pane->findChild<SearchOverlayBar*>()->findChild<QLineEdit*>();
            QTest::keyClick(search, Qt::Key_Escape);
            QCoreApplication::processEvents();
            QCOMPARE(QApplication::focusWidget(), editor);
        }
        auto* sidebar = window.findChild<ServerSidebarWidget*>();
        QVERIFY(sidebar);
        sidebar->setFocus();
        QCoreApplication::processEvents();
        QVERIFY(QMetaObject::invokeMethod(findShortcut, "activated", Qt::DirectConnection));
        QCoreApplication::processEvents();
        QVERIFY(panes[0]->findChild<SearchOverlayBar*>()->isVisible());
        for (int i = 1; i < 4; ++i) QVERIFY(!panes[i]->findChild<SearchOverlayBar*>()->isVisible());
        QCOMPARE(manager.activeChannelName(), QString("alice"));
        for (int i = 0; i < 4; ++i)
            QCOMPARE(panes[i]->findChild<ChatInputWidget*>()->draft().text, QString("Keep pane %1 draft").arg(i));
    }

    void mainWindowRendersWithIsolatedServices() {
        QTemporaryDir directory;
        DatabaseService database(directory.filePath("db.sqlite"));
        PreferencesManager prefs(directory.filePath("prefs.json"));
        ThemeManager themes(directory.filePath("themes"));
        SoundService sound;
        ConnectionManager manager(&database, nullptr, &prefs, nullptr, &sound);
        const auto id = manager.addServer({}, false);
        manager.openQuery(id, "alice");
        MainWindow window(&database, &prefs, &themes, &sound, nullptr, &manager, true);
        window.resize(1000, 700); window.show();
        QTest::qWait(50);
        QImage frame(window.size(), QImage::Format_ARGB32_Premultiplied); frame.fill(Qt::transparent);
        window.render(&frame);
        QVERIFY(!frame.isNull());
        const auto screenshot = qEnvironmentVariable("LOUNGECAT_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(frame.save(screenshot));
    }

};

int main(int argc, char** argv) {
    QTemporaryDir isolated;
    qputenv("LOUNGECAT_DATA_DIR", isolated.filePath("data").toUtf8());
    qputenv("XDG_CACHE_HOME", isolated.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    DefensiveTests test;
    const auto result = QTest::qExec(&test, argc, argv);
    ImageCache::shutdown();
    return result;
}
#include "test_loungecat_repairs.moc"
