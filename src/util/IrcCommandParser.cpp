#include "util/IrcCommandParser.h"
#include <QRegularExpression>
#include <QHash>

namespace loungecat {

bool IrcCommandParser::isCommand(const QString& input) {
    return input.startsWith('/') && !input.startsWith(QStringLiteral("//"));
}

QStringList IrcCommandParser::availableCommands() {
    return {
        QStringLiteral("/join"), QStringLiteral("/part"), QStringLiteral("/close"),
        QStringLiteral("/quit"), QStringLiteral("/msg"), QStringLiteral("/query"),
        QStringLiteral("/me"), QStringLiteral("/notice"), QStringLiteral("/nick"),
        QStringLiteral("/whois"), QStringLiteral("/whowas"), QStringLiteral("/who"),
        QStringLiteral("/topic"), QStringLiteral("/kick"), QStringLiteral("/ban"),
        QStringLiteral("/unban"), QStringLiteral("/kickban"), QStringLiteral("/voice"),
        QStringLiteral("/devoice"), QStringLiteral("/op"), QStringLiteral("/deop"),
        QStringLiteral("/halfop"), QStringLiteral("/dehalfop"), QStringLiteral("/quiet"),
        QStringLiteral("/unquiet"), QStringLiteral("/mode"), QStringLiteral("/invite"),
        QStringLiteral("/away"), QStringLiteral("/back"), QStringLiteral("/raw"),
        QStringLiteral("/quote"), QStringLiteral("/list"), QStringLiteral("/names"),
        QStringLiteral("/ignore"), QStringLiteral("/unignore"), QStringLiteral("/ignorelist"),
        QStringLiteral("/clear"), QStringLiteral("/clearall"), QStringLiteral("/cycle"),
        QStringLiteral("/ping"), QStringLiteral("/time"), QStringLiteral("/version"),
        QStringLiteral("/motd"), QStringLiteral("/lusers"), QStringLiteral("/links"),
        QStringLiteral("/stats"), QStringLiteral("/trace"), QStringLiteral("/admin"),
        QStringLiteral("/info"), QStringLiteral("/servlist"), QStringLiteral("/connect"),
        QStringLiteral("/reconnect"), QStringLiteral("/ns"), QStringLiteral("/cs"),
        QStringLiteral("/ms"), QStringLiteral("/os"), QStringLiteral("/hs"),
        QStringLiteral("/bs"), QStringLiteral("/slap"), QStringLiteral("/shrug"),
        QStringLiteral("/tableflip"), QStringLiteral("/unflip"), QStringLiteral("/lenny"),
        QStringLiteral("/sysinfo"), QStringLiteral("/uploadimage"), QStringLiteral("/help")
    };
}

std::optional<IrcCommand> IrcCommandParser::parse(const QString& input) {
    const QString trimmed = input.trimmed();
    if (!isCommand(trimmed)) {
        return std::nullopt;
    }

    const QString withoutSlash = trimmed.mid(1);
    const int spaceIdx = withoutSlash.indexOf(' ');
    const QString cmdName = (spaceIdx == -1 ? withoutSlash : withoutSlash.left(spaceIdx)).toLower();
    const QString rest = (spaceIdx == -1 ? QString() : withoutSlash.mid(spaceIdx + 1).trimmed());

    auto splitFirstWord = [](const QString& s) -> std::pair<QString, QString> {
        int idx = s.indexOf(' ');
        if (idx == -1) return {s, QString()};
        return {s.left(idx), s.mid(idx + 1).trimmed()};
    };

    auto ensureChannelPrefix = [](const QString& ch) -> QString {
        if (ch.isEmpty()) return ch;
        if (ch.startsWith('#') || ch.startsWith('&') || ch.startsWith('+') || ch.startsWith('!'))
            return ch;
        return QStringLiteral("#") + ch;
    };

    if (cmdName == QStringLiteral("join") || cmdName == QStringLiteral("j")) {
        auto [ch, key] = splitFirstWord(rest);
        if (ch.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Join{ensureChannelPrefix(ch), key.isEmpty() ? std::nullopt : std::make_optional(key)};
    }
    if (cmdName == QStringLiteral("part") || cmdName == QStringLiteral("leave") || cmdName == QStringLiteral("p")) {
        if (rest.isEmpty()) return cmd::Part{std::nullopt, std::nullopt};
        auto [first, rem] = splitFirstWord(rest);
        if (first.startsWith('#') || first.startsWith('&')) {
            return cmd::Part{first, rem.isEmpty() ? std::nullopt : std::make_optional(rem)};
        }
        return cmd::Part{std::nullopt, rest};
    }
    if (cmdName == QStringLiteral("close") || cmdName == QStringLiteral("wc")) {
        return cmd::Close{rest.isEmpty() ? std::nullopt : std::make_optional(rest)};
    }
    if (cmdName == QStringLiteral("quit") || cmdName == QStringLiteral("disconnect") || cmdName == QStringLiteral("exit")) {
        return cmd::Quit{rest.isEmpty() ? std::nullopt : std::make_optional(rest)};
    }
    if (cmdName == QStringLiteral("msg") || cmdName == QStringLiteral("privmsg") ||
        cmdName == QStringLiteral("m") || cmdName == QStringLiteral("q")) {
        auto [target, msg] = splitFirstWord(rest);
        if (target.isEmpty() || msg.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Message{target, msg};
    }
    if (cmdName == QStringLiteral("query") || cmdName == QStringLiteral("pm")) {
        auto [nick, msg] = splitFirstWord(rest);
        if (nick.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Query{nick, msg.isEmpty() ? std::nullopt : std::make_optional(msg)};
    }
    if (cmdName == QStringLiteral("me") || cmdName == QStringLiteral("action")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Action{rest};
    }
    if (cmdName == QStringLiteral("notice") || cmdName == QStringLiteral("n")) {
        auto [target, msg] = splitFirstWord(rest);
        if (target.isEmpty() || msg.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Notice{target, msg};
    }
    if (cmdName == QStringLiteral("nick")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Nick{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("whois") || cmdName == QStringLiteral("wi") || cmdName == QStringLiteral("wii")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Whois{splitFirstWord(rest).first, cmdName == QStringLiteral("wii")};
    }
    if (cmdName == QStringLiteral("whowas") || cmdName == QStringLiteral("ww")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Whowas{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("who")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Who{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("topic") || cmdName == QStringLiteral("t")) {
        if (rest.isEmpty()) return cmd::Topic{std::nullopt, std::nullopt};
        auto [first, rem] = splitFirstWord(rest);
        if (first.startsWith('#') || first.startsWith('&')) {
            return cmd::Topic{first, rem.isEmpty() ? std::nullopt : std::make_optional(rem)};
        }
        return cmd::Topic{std::nullopt, rest};
    }
    if (cmdName == QStringLiteral("kick") || cmdName == QStringLiteral("k")) {
        auto [nick, reason] = splitFirstWord(rest);
        if (nick.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Kick{nick, reason.isEmpty() ? std::nullopt : std::make_optional(reason)};
    }
    if (cmdName == QStringLiteral("ban") || cmdName == QStringLiteral("b")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Ban{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("unban")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Unban{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("kickban") || cmdName == QStringLiteral("kb")) {
        auto [nick, reason] = splitFirstWord(rest);
        if (nick.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Kickban{nick, reason.isEmpty() ? std::nullopt : std::make_optional(reason)};
    }
    if (cmdName == QStringLiteral("voice") || cmdName == QStringLiteral("v")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Voice{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("devoice") || cmdName == QStringLiteral("dv")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Devoice{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("op")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Op{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("deop") || cmdName == QStringLiteral("dop")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Deop{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("halfop") || cmdName == QStringLiteral("hop")) {
        if (rest.isEmpty() && cmdName == QStringLiteral("hop")) {
            return cmd::Cycle{};
        }
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Halfop{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("dehalfop") || cmdName == QStringLiteral("dhop")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Dehalfop{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("quiet") || cmdName == QStringLiteral("mute")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Quiet{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("unquiet") || cmdName == QStringLiteral("unmute")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Unquiet{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("mode")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        auto [first, rem] = splitFirstWord(rest);
        if (first.startsWith('+') || first.startsWith('-')) {
            return cmd::Mode{std::nullopt, rest};
        }
        return cmd::Mode{first, rem};
    }
    if (cmdName == QStringLiteral("invite") || cmdName == QStringLiteral("inv")) {
        auto [nick, ch] = splitFirstWord(rest);
        if (nick.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Invite{nick, ch.isEmpty() ? std::nullopt : std::make_optional(ensureChannelPrefix(ch))};
    }
    if (cmdName == QStringLiteral("away")) {
        return cmd::Away{rest.isEmpty() ? std::nullopt : std::make_optional(rest)};
    }
    if (cmdName == QStringLiteral("back")) {
        return cmd::Back{};
    }
    if (cmdName == QStringLiteral("raw") || cmdName == QStringLiteral("quote")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Raw{rest};
    }
    if (cmdName == QStringLiteral("list")) {
        return cmd::List{rest.isEmpty() ? std::nullopt : std::make_optional(rest)};
    }
    if (cmdName == QStringLiteral("names")) {
        return cmd::Names{rest.isEmpty() ? std::nullopt : std::make_optional(ensureChannelPrefix(rest))};
    }
    if (cmdName == QStringLiteral("ignore")) {
        if (rest.isEmpty()) return cmd::IgnoreList{};
        return cmd::Ignore{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("unignore")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Unignore{splitFirstWord(rest).first};
    }
    if (cmdName == QStringLiteral("ignorelist") || cmdName == QStringLiteral("ignores")) {
        return cmd::IgnoreList{};
    }
    if (cmdName == QStringLiteral("clear") || cmdName == QStringLiteral("cls")) {
        return cmd::Clear{};
    }
    if (cmdName == QStringLiteral("clearall")) {
        return cmd::ClearAll{};
    }
    if (cmdName == QStringLiteral("cycle") || cmdName == QStringLiteral("rejoin")) {
        return cmd::Cycle{};
    }
    if (cmdName == QStringLiteral("ping") || cmdName == QStringLiteral("lag")) {
        return cmd::Ping{};
    }
    if (cmdName == QStringLiteral("time")) {
        return cmd::Time{};
    }
    if (cmdName == QStringLiteral("version") || cmdName == QStringLiteral("ver")) {
        return cmd::Version{rest.isEmpty() ? std::nullopt : std::make_optional(splitFirstWord(rest).first)};
    }
    if (cmdName == QStringLiteral("motd")) return cmd::Motd{};
    if (cmdName == QStringLiteral("lusers")) return cmd::Lusers{};
    if (cmdName == QStringLiteral("links")) return cmd::Links{};
    if (cmdName == QStringLiteral("stats")) {
        return cmd::Stats{rest.isEmpty() ? std::nullopt : std::make_optional(rest)};
    }
    if (cmdName == QStringLiteral("trace")) {
        return cmd::Trace{rest.isEmpty() ? std::nullopt : std::make_optional(rest)};
    }
    if (cmdName == QStringLiteral("admin")) return cmd::Admin{};
    if (cmdName == QStringLiteral("info")) return cmd::Info{};
    if (cmdName == QStringLiteral("servlist")) return cmd::Servlist{};
    if (cmdName == QStringLiteral("connect") || cmdName == QStringLiteral("server")) {
        const QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) return cmd::Unknown{cmdName, rest};
        QString host = parts[0];
        int port = 6697;
        bool ssl = true;
        if (parts.size() >= 2) {
            QString pStr = parts[1];
            if (pStr.startsWith('+')) {
                ssl = true;
                port = pStr.mid(1).toInt();
            } else {
                port = pStr.toInt();
            }
            if (port <= 0) port = 6697;
        }
        return cmd::Connect{host, port, ssl};
    }
    if (cmdName == QStringLiteral("reconnect")) return cmd::Reconnect{};
    if (cmdName == QStringLiteral("ns") || cmdName == QStringLiteral("nickserv")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::ServiceMessage{QStringLiteral("NickServ"), rest};
    }
    if (cmdName == QStringLiteral("cs") || cmdName == QStringLiteral("chanserv")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::ServiceMessage{QStringLiteral("ChanServ"), rest};
    }
    if (cmdName == QStringLiteral("ms") || cmdName == QStringLiteral("memoserv")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::ServiceMessage{QStringLiteral("MemoServ"), rest};
    }
    if (cmdName == QStringLiteral("os") || cmdName == QStringLiteral("operserv")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::ServiceMessage{QStringLiteral("OperServ"), rest};
    }
    if (cmdName == QStringLiteral("hs") || cmdName == QStringLiteral("hostserv")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::ServiceMessage{QStringLiteral("HostServ"), rest};
    }
    if (cmdName == QStringLiteral("bs") || cmdName == QStringLiteral("botserv")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::ServiceMessage{QStringLiteral("BotServ"), rest};
    }
    if (cmdName == QStringLiteral("slap")) {
        QString target = rest.isEmpty() ? QStringLiteral("someone") : splitFirstWord(rest).first;
        return cmd::Slap{target};
    }
    if (cmdName == QStringLiteral("shrug")) {
        QString s = QStringLiteral("¯\\_(ツ)_/¯");
        return cmd::EmoteText{rest.isEmpty() ? s : (rest + QStringLiteral(" ") + s)};
    }
    if (cmdName == QStringLiteral("tableflip")) {
        QString s = QStringLiteral("(╯°□°）╯︵ ┻━┻");
        return cmd::EmoteText{rest.isEmpty() ? s : (rest + QStringLiteral(" ") + s)};
    }
    if (cmdName == QStringLiteral("unflip")) {
        QString s = QStringLiteral("┬─┬ ノ( ゜-゜ノ)");
        return cmd::EmoteText{rest.isEmpty() ? s : (rest + QStringLiteral(" ") + s)};
    }
    if (cmdName == QStringLiteral("lenny")) {
        QString s = QStringLiteral("( ͡° ͜ʖ ͡°)");
        return cmd::EmoteText{rest.isEmpty() ? s : (rest + QStringLiteral(" ") + s)};
    }
    if (cmdName == QStringLiteral("sysinfo")) {
        return cmd::Sysinfo{};
    }
    if (cmdName == QStringLiteral("uploadimage") || cmdName == QStringLiteral("img")) {
        return cmd::UploadImage{rest.isEmpty() ? std::nullopt : std::make_optional(rest)};
    }
    if (cmdName == QStringLiteral("chathistory") || cmdName == QStringLiteral("history")) {
        int limit = rest.isEmpty() ? 50 : rest.toInt();
        if (limit <= 0) limit = 50;
        return cmd::Chathistory{limit};
    }
    if (cmdName == QStringLiteral("react")) {
        auto [ref, emoji] = splitFirstWord(rest);
        if (ref.isEmpty() || emoji.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::React{ref, emoji};
    }
    if (cmdName == QStringLiteral("setname")) {
        if (rest.isEmpty()) return cmd::Unknown{cmdName, rest};
        return cmd::Setname{rest};
    }
    if (cmdName == QStringLiteral("help") || cmdName == QStringLiteral("commands") || cmdName == QStringLiteral("?")) {
        return cmd::Help{rest.isEmpty() ? std::nullopt : std::make_optional(splitFirstWord(rest).first)};
    }

    return cmd::Unknown{cmdName, rest};
}

QString IrcCommandParser::commandError(const QString& command) {
    // Never echo arguments: service commands can carry credentials.
    static const QHash<QString, QString> requiredArguments{
        {QStringLiteral("join"), QStringLiteral("<channel[,channel]> [key[,key]]")},
        {QStringLiteral("j"), QStringLiteral("<channel[,channel]> [key[,key]]")},
        {QStringLiteral("msg"), QStringLiteral("<target> <message>")},
        {QStringLiteral("privmsg"), QStringLiteral("<target> <message>")},
        {QStringLiteral("query"), QStringLiteral("<nick> [message]")},
        {QStringLiteral("me"), QStringLiteral("<action>")},
        {QStringLiteral("notice"), QStringLiteral("<target> <message>")},
        {QStringLiteral("nick"), QStringLiteral("<nick>")},
        {QStringLiteral("whois"), QStringLiteral("<nick>")},
        {QStringLiteral("who"), QStringLiteral("<mask>")},
        {QStringLiteral("unban"), QStringLiteral("<nick>")},
        {QStringLiteral("unignore"), QStringLiteral("<nick>")},
        {QStringLiteral("voice"), QStringLiteral("<nick>")},
        {QStringLiteral("v"), QStringLiteral("<nick>")},
        {QStringLiteral("devoice"), QStringLiteral("<nick>")},
        {QStringLiteral("op"), QStringLiteral("<nick>")},
        {QStringLiteral("deop"), QStringLiteral("<nick>")},
        {QStringLiteral("quiet"), QStringLiteral("<nick>")},
        {QStringLiteral("mute"), QStringLiteral("<nick>")},
        {QStringLiteral("unquiet"), QStringLiteral("<nick>")},
        {QStringLiteral("unmute"), QStringLiteral("<nick>")},
        {QStringLiteral("ban"), QStringLiteral("<nick>")},
        {QStringLiteral("b"), QStringLiteral("<nick>")},
        {QStringLiteral("kick"), QStringLiteral("<nick> [reason]")},
        {QStringLiteral("k"), QStringLiteral("<nick> [reason]")},
        {QStringLiteral("kb"), QStringLiteral("<nick> [reason]")},
        {QStringLiteral("mode"), QStringLiteral("[target] <modes> [parameters]")},
        {QStringLiteral("invite"), QStringLiteral("<nick> [channel]")},
        {QStringLiteral("raw"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("quote"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("m"), QStringLiteral("<target> <message>")},
        {QStringLiteral("q"), QStringLiteral("<target> <message>")},
        {QStringLiteral("wi"), QStringLiteral("<nick>")},
        {QStringLiteral("wii"), QStringLiteral("<nick>")},
        {QStringLiteral("whowas"), QStringLiteral("<nick>")},
        {QStringLiteral("ww"), QStringLiteral("<nick>")},
        {QStringLiteral("dv"), QStringLiteral("<nick>")},
        {QStringLiteral("dop"), QStringLiteral("<nick>")},
        {QStringLiteral("halfop"), QStringLiteral("<nick>")},
        {QStringLiteral("dehalfop"), QStringLiteral("<nick>")},
        {QStringLiteral("dhop"), QStringLiteral("<nick>")},
        {QStringLiteral("kickban"), QStringLiteral("<nick> [reason]")},
        {QStringLiteral("inv"), QStringLiteral("<nick> [channel]")},
        {QStringLiteral("connect"), QStringLiteral("<host> [port]")},
        {QStringLiteral("server"), QStringLiteral("<host> [port]")},
        {QStringLiteral("ns"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("nickserv"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("cs"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("chanserv"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("ms"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("memoserv"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("os"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("operserv"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("hs"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("hostserv"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("bs"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("botserv"), QStringLiteral("<command> [parameters]")},
        {QStringLiteral("react"), QStringLiteral("<message-id-or-nick> <emoji>")},
        {QStringLiteral("setname"), QStringLiteral("<real name>")},
    };
    const auto it = requiredArguments.constFind(command);
    if (it != requiredArguments.cend())
        return QStringLiteral("Usage: /%1 %2").arg(command, it.value());
    return QStringLiteral("Unknown command /%1. Type /help for available commands.").arg(command);
}

QString IrcCommandParser::helpText(const std::optional<QString>& command) {
    if (command && !command->isEmpty()) {
        return QStringLiteral("Usage for /%1 — see /help dialog for full reference.").arg(*command);
    }
    return QStringLiteral(
        "Available commands: /join, /part, /close, /quit, /msg, /query, /me, /notice, /nick, "
        "/whois, /whowas, /who, /topic, /kick, /ban, /unban, /kickban, /voice, /devoice, /op, "
        "/deop, /halfop, /dehalfop, /quiet, /unquiet, /mode, /invite, /away, /back, /raw, "
        "/list, /names, /ignore, /unignore, /ignorelist, /clear, /clearall, /cycle, /ping, "
        "/time, /version, /motd, /lusers, /ns, /cs, /slap, /shrug, /tableflip, /unflip, "
        "/lenny, /sysinfo, /uploadimage, /help"
    );
}

} // namespace loungecat
