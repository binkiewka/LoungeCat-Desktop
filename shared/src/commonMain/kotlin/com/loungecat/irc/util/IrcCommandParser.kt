package com.loungecat.irc.util

sealed class IrcCommand {
    data class Join(val channel: String) : IrcCommand()
    data class Part(val channel: String?, val message: String?) : IrcCommand()
    data class Cycle(val channel: String?, val message: String?) : IrcCommand()
    data class Invite(val nickname: String, val channel: String?) : IrcCommand()
    data class Topic(val topic: String?) : IrcCommand()

    data class Message(val target: String, val message: String) : IrcCommand()
    data class Action(val action: String) : IrcCommand()
    data class Notice(val target: String, val message: String) : IrcCommand()
    data class Query(val nickname: String, val message: String?) : IrcCommand()

    data class Whois(val nickname: String) : IrcCommand()
    data class Nick(val newNick: String) : IrcCommand()
    data class Away(val message: String?) : IrcCommand()
    data object Back : IrcCommand()
    data class Ignore(val nickname: String) : IrcCommand()
    data class Unignore(val nickname: String) : IrcCommand()

    data class Kick(val nickname: String, val reason: String?) : IrcCommand()
    data class Ban(val nickname: String) : IrcCommand()
    data class Unban(val nickname: String) : IrcCommand()
    data class Voice(val nickname: String) : IrcCommand()
    data class Devoice(val nickname: String) : IrcCommand()
    data class Op(val nickname: String) : IrcCommand()
    data class Deop(val nickname: String) : IrcCommand()
    data class Quiet(val nickname: String) : IrcCommand()
    data class Unquiet(val nickname: String) : IrcCommand()
    data class Mode(val modeString: String) : IrcCommand()

    data class List(val filter: String?) : IrcCommand()
    data class Names(val channel: String?) : IrcCommand()
    data class Quit(val message: String?) : IrcCommand()
    data class Raw(val command: String) : IrcCommand()
    data class Ping(val target: String?) : IrcCommand()
    data class Time(val server: String?) : IrcCommand()
    data class Version(val server: String?) : IrcCommand()
    data class Motd(val server: String?) : IrcCommand()
    data class Info(val server: String?) : IrcCommand()
    data class Who(val mask: String) : IrcCommand()

    data object Clear : IrcCommand()
    data object Help : IrcCommand()
    data class Connect(val server: String?) : IrcCommand()
    data class Identify(val args: String) : IrcCommand()

    // Service Aliases
    data class NickServ(val args: String) : IrcCommand()
    data class ChanServ(val args: String) : IrcCommand()
    data class MemoServ(val args: String) : IrcCommand()

    // Admin / Server Commands
    data class Links(val server: String?) : IrcCommand()
    data class Map(val server: String?) : IrcCommand()
    data class Lusers(val mask: String?) : IrcCommand()
    data class Admin(val server: String?) : IrcCommand()
    data class Oper(val args: String) : IrcCommand()
    data class Rehash(val args: String?) : IrcCommand()
    data class Restart(val args: String?) : IrcCommand()
    data class Die(val args: String?) : IrcCommand()
    data class Wallops(val message: String) : IrcCommand()
    data class Saje(val nickname: String, val channel: String) : IrcCommand()

    // IRCOp Bans
    data class Kill(val nickname: String, val reason: String) : IrcCommand()
    data class KLine(val target: String, val reason: String) : IrcCommand()
    data class GLine(val target: String, val reason: String) : IrcCommand()
    data class ZLine(val target: String, val reason: String) : IrcCommand()

    // Composite / Misc
    data class KickBan(val nickname: String, val reason: String?) : IrcCommand()
    data class Ctcp(val target: String, val message: String) : IrcCommand()

    data class SysInfo(val args: kotlin.collections.List<String>) : IrcCommand()

    data class Znc(val args: String) : IrcCommand()

    data object NotACommand : IrcCommand()
    data class Unknown(val command: String, val args: String) : IrcCommand()
}

object IrcCommandParser {

    fun parse(message: String): IrcCommand {
        if (!message.startsWith("/")) return IrcCommand.NotACommand
        if (message.startsWith("//")) return IrcCommand.NotACommand

        val parts = message.substring(1).split(" ", limit = 2)
        val command = parts[0].lowercase()
        val args = if (parts.size > 1) parts[1] else ""

        return when (command) {
            "join", "j" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                val channel = args.trim()
                val formattedChannel = if (channel.startsWith("#")) channel else "#$channel"
                IrcCommand.Join(formattedChannel)
            }
            "part", "leave" -> {
                val partParts = args.split(" ", limit = 2)
                val channel = partParts[0].ifBlank { null }
                val partMessage = if (partParts.size > 1) partParts[1] else null
                IrcCommand.Part(channel, partMessage)
            }
            "cycle", "rejoin" -> {
                val cycleParts = args.split(" ", limit = 2)
                val channel = cycleParts[0].ifBlank { null }
                val cycleMessage = if (cycleParts.size > 1) cycleParts[1] else null
                IrcCommand.Cycle(channel, cycleMessage)
            }
            "invite" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                val inviteParts = args.split(" ", limit = 2)
                val nickname = inviteParts[0]
                val channel = if (inviteParts.size > 1) inviteParts[1].trim() else null
                IrcCommand.Invite(nickname, channel)
            }
            "topic", "t" -> IrcCommand.Topic(args.trim().ifBlank { null })
            "list" -> IrcCommand.List(args.trim().ifBlank { null })
            "names" -> IrcCommand.Names(args.trim().ifBlank { null })
            "msg", "privmsg" -> {
                val msgParts = args.split(" ", limit = 2)
                if (msgParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.Message(msgParts[0], msgParts[1])
            }
            "query", "q" -> {
                val queryParts = args.split(" ", limit = 2)
                if (queryParts.isEmpty() || queryParts[0].isBlank())
                        return IrcCommand.Unknown(command, args)
                IrcCommand.Query(queryParts[0], if (queryParts.size > 1) queryParts[1] else null)
            }
            "me" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Action(args)
            }
            "notice" -> {
                val noticeParts = args.split(" ", limit = 2)
                if (noticeParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.Notice(noticeParts[0], noticeParts[1])
            }
            "whois", "wii" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Whois(args.trim().split(" ")[0])
            }
            "who" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Who(args.trim())
            }
            "nick" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Nick(args.trim().split(" ")[0])
            }
            "away" -> IrcCommand.Away(args.trim().ifBlank { null })
            "back" -> IrcCommand.Back
            "ignore" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Ignore(args.trim().split(" ")[0])
            }
            "unignore" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Unignore(args.trim().split(" ")[0])
            }
            "kick", "k" -> {
                val kickParts = args.split(" ", limit = 2)
                if (kickParts.isEmpty() || kickParts[0].isBlank())
                        return IrcCommand.Unknown(command, args)
                IrcCommand.Kick(kickParts[0], if (kickParts.size > 1) kickParts[1] else null)
            }
            "ban", "b" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Ban(args.trim().split(" ")[0])
            }
            "unban" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Unban(args.trim().split(" ")[0])
            }
            "kb" -> {
                val kbParts = args.split(" ", limit = 2)
                if (kbParts.isEmpty() || kbParts[0].isBlank())
                        return IrcCommand.Unknown(command, args)
                IrcCommand.KickBan(kbParts[0], if (kbParts.size > 1) kbParts[1] else null)
            }
            "voice", "v" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Voice(args.trim().split(" ")[0])
            }
            "devoice" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Devoice(args.trim().split(" ")[0])
            }
            "op" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Op(args.trim().split(" ")[0])
            }
            "deop" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Deop(args.trim().split(" ")[0])
            }
            "quiet", "mute" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Quiet(args.trim().split(" ")[0])
            }
            "unquiet", "unmute" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Unquiet(args.trim().split(" ")[0])
            }
            "mode" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Mode(args)
            }
            "quit", "disconnect", "exit" -> IrcCommand.Quit(args.trim().ifBlank { null })
            "raw", "quote" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Raw(args)
            }
            "ping" -> IrcCommand.Ping(args.trim().ifBlank { null })
            "time" -> IrcCommand.Time(args.trim().ifBlank { null })
            "version" -> IrcCommand.Version(args.trim().ifBlank { null })
            "motd" -> IrcCommand.Motd(args.trim().ifBlank { null })
            "info" -> IrcCommand.Info(args.trim().ifBlank { null })
            "connect", "server" -> IrcCommand.Connect(args.trim().ifBlank { null })
            "identify" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Identify(args.trim())
            }
            "ns", "nickserv" -> IrcCommand.NickServ(args.trim())
            "cs", "chanserv" -> IrcCommand.ChanServ(args.trim())
            "ms", "memoserv" -> IrcCommand.MemoServ(args.trim())
            "links" -> IrcCommand.Links(args.trim().ifBlank { null })
            "map" -> IrcCommand.Map(args.trim().ifBlank { null })
            "lusers" -> IrcCommand.Lusers(args.trim().ifBlank { null })
            "admin" -> IrcCommand.Admin(args.trim().ifBlank { null })
            "oper" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Oper(args.trim())
            }
            "kill" -> {
                val killParts = args.split(" ", limit = 2)
                if (killParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.Kill(killParts[0], killParts[1])
            }
            "kline" -> {
                val kParts = args.split(" ", limit = 2)
                if (kParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.KLine(kParts[0], kParts[1])
            }
            "gline" -> {
                val gParts = args.split(" ", limit = 2)
                if (gParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.GLine(gParts[0], gParts[1])
            }
            "zline" -> {
                val zParts = args.split(" ", limit = 2)
                if (zParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.ZLine(zParts[0], zParts[1])
            }
            "rehash" -> IrcCommand.Rehash(args.trim().ifBlank { null })
            "restart" -> IrcCommand.Restart(args.trim().ifBlank { null })
            "die" -> IrcCommand.Die(args.trim().ifBlank { null })
            "wallops" -> {
                if (args.isBlank()) return IrcCommand.Unknown(command, args)
                IrcCommand.Wallops(args.trim())
            }
            "saje" -> {
                val sajeParts = args.split(" ", limit = 2)
                if (sajeParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.Saje(sajeParts[0], sajeParts[1])
            }
            "ctcp" -> {
                val ctcpParts = args.split(" ", limit = 2)
                if (ctcpParts.size < 2) return IrcCommand.Unknown(command, args)
                IrcCommand.Ctcp(ctcpParts[0], ctcpParts[1])
            }
            "sysinfo" -> {
                val sysParts =
                        if (args.isNotBlank()) args.trim().split("\\s+".toRegex()) else emptyList()
                IrcCommand.SysInfo(sysParts)
            }
            "znc" -> IrcCommand.Znc(args.trim())
            "clear", "cls" -> IrcCommand.Clear
            "help", "commands", "?" -> IrcCommand.Help
            else -> IrcCommand.Unknown(command, args)
        }
    }

    fun getHelpText(): String =
            """
        |=== IRC Commands ===
        |
        |CHANNEL: /join /part /cycle /topic /invite /list /names
        |MESSAGING: /msg /query /me /notice /ctcp
        |USER: /nick /whois /who /away /back /ignore /unignore
        |MODERATION: /kick /ban /unban /kb /voice /devoice /op /deop /quiet /unquiet /mode
        |SERVICES: /identify /ns /cs /ms
        |SERVER: /quit /raw /ping /time /version /motd /info /links /map /lusers /admin
        |IRCOP: /oper /kill /kline /gline /zline /rehash /restart /die /wallops /saje
        |CLIENT: /clear /sysinfo /help /znc
        |
        |TIP: Use // to send a message starting with /
    """.trimMargin()
}
