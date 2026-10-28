#pragma once

#include <QString>
#include <QStringList>
#include <optional>
#include <variant>

namespace loungecat {

namespace cmd {

struct Join { QString channel; std::optional<QString> key; };
struct Part { std::optional<QString> channel; std::optional<QString> reason; };
struct Close { std::optional<QString> target; };
struct Quit { std::optional<QString> reason; };
struct Message { QString target; QString message; };
struct Query { QString nickname; std::optional<QString> message; };
struct Action { QString action; };
struct Notice { QString target; QString message; };
struct Nick { QString newNickname; };
struct Whois { QString nickname; bool detailed = false; };
struct Whowas { QString nickname; };
struct Who { QString mask; };
struct Topic { std::optional<QString> channel; std::optional<QString> newTopic; };
struct Kick { QString nickname; std::optional<QString> reason; };
struct Ban { QString maskOrNick; };
struct Unban { QString maskOrNick; };
struct Kickban { QString nickname; std::optional<QString> reason; };
struct Voice { QString nickname; };
struct Devoice { QString nickname; };
struct Op { QString nickname; };
struct Deop { QString nickname; };
struct Halfop { QString nickname; };
struct Dehalfop { QString nickname; };
struct Quiet { QString maskOrNick; };
struct Unquiet { QString maskOrNick; };
struct Mode { std::optional<QString> target; QString modes; };
struct Invite { QString nickname; std::optional<QString> channel; };
struct Away { std::optional<QString> message; };
struct Back {};
struct Raw { QString command; };
struct List { std::optional<QString> filter; };
struct Names { std::optional<QString> channel; };
struct Ignore { QString nickname; };
struct Unignore { QString nickname; };
struct IgnoreList {};
struct Clear {};
struct ClearAll {};
struct Cycle {};
struct Ping {};
struct Time {};
struct Version { std::optional<QString> target; };
struct Motd {};
struct Lusers {};
struct Links {};
struct Stats { std::optional<QString> query; };
struct Trace { std::optional<QString> target; };
struct Admin {};
struct Info {};
struct Servlist {};
struct Connect { QString hostname; int port = 6697; bool useSsl = true; };
struct Reconnect {};
struct ServiceMessage { QString service; QString command; };
struct Slap { QString nickname; };
struct EmoteText { QString text; };
struct Sysinfo {};
struct UploadImage { std::optional<QString> filePath; };
struct Chathistory { int limit = 50; };
struct React { QString targetRef; QString emoji; };
struct Setname { QString realname; };
struct Help { std::optional<QString> command; };
struct Unknown { QString command; QString args; };

} // namespace cmd

using IrcCommand = std::variant<
    cmd::Join, cmd::Part, cmd::Close, cmd::Quit, cmd::Message, cmd::Query,
    cmd::Action, cmd::Notice, cmd::Nick, cmd::Whois, cmd::Whowas, cmd::Who,
    cmd::Topic, cmd::Kick, cmd::Ban, cmd::Unban, cmd::Kickban, cmd::Voice,
    cmd::Devoice, cmd::Op, cmd::Deop, cmd::Halfop, cmd::Dehalfop, cmd::Quiet,
    cmd::Unquiet, cmd::Mode, cmd::Invite, cmd::Away, cmd::Back, cmd::Raw,
    cmd::List, cmd::Names, cmd::Ignore, cmd::Unignore, cmd::IgnoreList,
    cmd::Clear, cmd::ClearAll, cmd::Cycle, cmd::Ping, cmd::Time, cmd::Version,
    cmd::Motd, cmd::Lusers, cmd::Links, cmd::Stats, cmd::Trace, cmd::Admin,
    cmd::Info, cmd::Servlist, cmd::Connect, cmd::Reconnect, cmd::ServiceMessage,
    cmd::Slap, cmd::EmoteText, cmd::Sysinfo, cmd::UploadImage,
    cmd::Chathistory, cmd::React, cmd::Setname,
    cmd::Help, cmd::Unknown
>;

class IrcCommandParser {
public:
    static bool isCommand(const QString& input);
    static std::optional<IrcCommand> parse(const QString& input);
    static QStringList availableCommands();
    static QString commandError(const QString& command);
    static QString helpText(const std::optional<QString>& command = std::nullopt);
};

} // namespace loungecat
