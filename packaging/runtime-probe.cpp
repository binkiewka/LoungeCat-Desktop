#include <QApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSvgRenderer>
#include <QPainter>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QWidget>
#include <QDebug>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4) return 2;
    auto fail = [](const QString& message) { qCritical().noquote() << message; return 1; };
    auto db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(":memory:");
    if (!db.open()) return fail(db.lastError().text());
    QSqlQuery query(db);
    if (!query.exec("CREATE TABLE smoke(value TEXT)") || !query.exec("INSERT INTO smoke VALUES ('package-ok')") ||
        !query.exec("SELECT value FROM smoke") || !query.next() || query.value(0).toString() != "package-ok")
        return fail("SQLite plugin round trip failed");
    if (!QSslSocket::supportsSsl()) return fail("No working Qt TLS backend");
    QFile cert(argv[1]);
    if (!cert.open(QIODevice::ReadOnly)) return fail("No fixture certificate");
    auto config = QSslConfiguration::defaultConfiguration();
    config.setCaCertificates(QSslCertificate::fromData(cert.readAll()));
    QSslSocket socket;
    socket.setSslConfiguration(config);
    socket.setPeerVerifyName("localhost");
    socket.connectToHostEncrypted("127.0.0.1", QString::fromLocal8Bit(argv[2]).toUShort());
    if (!socket.waitForEncrypted(10000)) return fail("Verified loopback TLS handshake failed: " + socket.errorString());
    if (!socket.bytesAvailable() && !socket.waitForReadyRead(10000)) return fail("No encrypted fixture response");
    if (socket.readAll() != "package-ok\n") return fail("Incorrect encrypted response");
    socket.disconnectFromHost();
    QSvgRenderer svg(QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32'><rect width='32' height='32' fill='#00ccaa'/></svg>"));
    if (!svg.isValid()) return fail("SVG renderer unavailable");
    QImage image(32,32,QImage::Format_ARGB32); image.fill(Qt::transparent);
    { QPainter painter(&image); svg.render(&painter); }
    if (image.pixelColor(16,16) != QColor("#00ccaa")) return fail("SVG rendering failed");
    QWidget widget; widget.resize(240,120); widget.show(); app.processEvents();
    if (!widget.isVisible() || widget.grab().isNull()) return fail("Qt widget/platform rendering failed");
    QJsonArray mapped;
    QFile maps("/proc/self/maps");
    if (!maps.open(QIODevice::ReadOnly)) return fail("No runtime mapping evidence");
    for (const auto& line : maps.readAll().split('\n')) {
        const auto path = line.mid(line.indexOf('/'));
        if (line.contains("libQt6") || line.contains("libqsqlite") || line.contains("libqopensslbackend") || line.contains("libssl.so"))
            if (!mapped.contains(QString::fromUtf8(path))) mapped.append(QString::fromUtf8(path));
    }
    QFile out(argv[3]);
    if (!out.open(QIODevice::WriteOnly)) return fail("Cannot save probe evidence");
    out.write(QJsonDocument(QJsonObject{{"sqlite",true},{"verifiedTls",true},{"svg",true},{"widgets",true},
        {"qtVersion",qVersion()},{"tlsBackend",QSslSocket::activeBackend()},
        {"tlsRuntime",QSslSocket::sslLibraryVersionString()},{"mappedLibraries",mapped}}).toJson());
    return 0;
}
