#pragma once
#include <QString>
#include <QStringList>
#include <QRegularExpression>

namespace loungecat::ircSecrets {
inline bool message(const QString& target, const QString& body) {
    const auto nick = target.section('@',0,0).toLower();
    if (!(nick.endsWith("serv") || nick == "q" || nick == "x")) return false;
    static const QRegularExpression credential(QStringLiteral(
        R"(^\s*(?:identify|id|register|group|ghost|recover|release|regain|password|authenticate|auth|set\s+(?:password|passwd))\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return credential.match(body).hasMatch();
}
inline bool input(const QString& target, QString text) {
    if (!text.startsWith('/') || text.startsWith("//")) return message(target,text);
    text.remove(0,1);
    auto verb = text.section(' ',0,0).toLower();
    auto rest = text.section(' ',1).trimmed();
    if (verb == "raw" || verb == "quote") {
        verb = rest.section(' ',0,0).toLower(); rest = rest.section(' ',1).trimmed();
    }
    if (verb == "pass" || verb == "authenticate" || verb == "oper" || verb == "identify") return true;
    if (verb == "mode" && rest.section(' ',1,1).contains('k')) return true;
    if ((verb == "join" || verb == "j") && rest.contains(' ')) return true;
    if (verb == "msg" || verb == "m" || verb == "privmsg" || verb == "notice" || verb == "query") {
        auto body = rest.section(' ',1).trimmed(); if(body.startsWith(':')) body.remove(0,1);
        return message(rest.section(' ',0,0),body);
    }
    if (verb == "ns") verb = "nickserv";
    if (verb == "cs") verb = "chanserv";
    if (verb == "hs") verb = "hostserv";
    if (verb == "ms") verb = "memoserv";
    return message(verb,rest);
}
}
