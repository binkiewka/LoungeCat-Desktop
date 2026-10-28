#pragma once

namespace loungecat::IrcLimits {
// Support large public channels without allowing unbounded live or pending rosters.
inline constexpr int UsersPerChannel = 10000;
inline constexpr int UsersPerNetwork = 20000;
inline constexpr int ChannelsPerNetwork = 128;
}
