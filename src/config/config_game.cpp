/*

  Copyright [2024-2025] [Leonardo Julca]

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "config_game.h"
#include "../util.h"

#include <utility>

#define INHERIT(gameConfigKey) this->gameConfigKey = nRootConfig->gameConfigKey;

#define INHERIT_MAP(gameConfigKey, mapDataKey) \
  if ((nMap->mapDataKey).has_value()) { \
    this->gameConfigKey = (nMap->mapDataKey).value(); \
  } else { \
    this->gameConfigKey = nRootConfig->gameConfigKey; \
  }

#define INHERIT_CUSTOM(gameConfigKey, gameSetupKey) \
  if ((nGameSetup->gameSetupKey).has_value()) { \
    this->gameConfigKey = (nGameSetup->gameSetupKey).value(); \
  } else { \
    this->gameConfigKey = nRootConfig->gameConfigKey; \
  }

#define INHERIT_MAP_OR_CUSTOM(gameConfigKey, mapDataKey, gameSetupKey) \
  if ((nGameSetup->gameSetupKey).has_value()) { \
    this->gameConfigKey = (nGameSetup->gameSetupKey).value(); \
  } else if ((nMap->mapDataKey).has_value()) { \
    this->gameConfigKey = (nMap->mapDataKey).value(); \
  } else { \
    this->gameConfigKey = nRootConfig->gameConfigKey; \
  }


#define CHECK_BOUNDED_DEFAULT(MIN_VAL, DEFAULT_VAL, MAX_VAL, MIN_KEY, DEFAULT_KEY, MAX_KEY) \
  do {\
    if (MIN_VAL > MAX_VAL) {\
      Print(\
        "[CONFIG] Error - " + CFG.GetKeyValue(MIN_KEY) + " cannot be larger than " +\
        CFG.GetKeyValue(MAX_KEY, to_string(MAX_VAL))\
      );\
      CFG.SetFailed();\
    }\
    if (DEFAULT_VAL < MIN_VAL) {\
      Print(\
        "[CONFIG] Error - " + CFG.GetKeyValue(DEFAULT_KEY) + " must be larger than " +\
        CFG.GetKeyValue(MIN_KEY, to_string(MIN_VAL))\
      );\
      CFG.SetFailed();\
    }\
    if (DEFAULT_VAL > MAX_VAL) {\
      Print(\
        "[CONFIG] Error - " + CFG.GetKeyValue(DEFAULT_KEY) + " must be smaller than " +\
        CFG.GetKeyValue(MAX_KEY, to_string(MAX_VAL))\
      );\
      CFG.SetFailed();\
    }\
  } while (0)

#define CHECK_BOUNDED_DEFAULT_INHERITED(MIN_VAL, DEFAULT_VAL, MAX_VAL, MIN_KEY, DEFAULT_KEY, MAX_KEY) \
  do {\
    if (MIN_VAL > MAX_VAL) {\
      Print(\
        "[CONFIG] Error - <" + string(MIN_KEY) + "> cannot be larger than <" + string(MAX_KEY) + ">"\
      );\
      m_Valid = false;\
    }\
    if (DEFAULT_VAL < MIN_VAL) {\
      Print(\
        "[CONFIG] Error -<" + string(DEFAULT_KEY) + "> must be larger than <" + string(MIN_KEY) + ">"\
      );\
      m_Valid = false;\
    }\
    if (DEFAULT_VAL > MAX_VAL) {\
      Print(\
        "[CONFIG] Error - <" + string(DEFAULT_KEY) + "> must be smaller than <" + string(MAX_KEY) + ">"\
      );\
      m_Valid = false;\
    }\
  } while (0)

using namespace std;

//
// CGameConfig
//

CGameConfig::CGameConfig(CConfig& CFG)
 : m_Valid(true)
{
  m_VoteKickPercentage                     = CFG.GetUint8("hosting.vote_kick.min_percent", 70);
  m_NumPlayersToStartGameOver              = CFG.GetUint8("hosting.game_over.player_count", 1);
  m_MaxPlayersLoopback                     = CFG.GetUint8("hosting.ip_filter.max_loopback", 8);
  m_MaxPlayersSameIP                       = CFG.GetUint8("hosting.ip_filter.max_same_ip", 8);
  m_PlayersReadyMode                       = CFG.GetEnum<PlayersReadyMode>("hosting.game_ready.mode", TO_ARRAY("fast", "race", "explicit"), PlayersReadyMode::kExpectRace);
  m_AutoStartRequiresBalance               = CFG.GetBool("hosting.autostart.requires_balance", true);
  m_SaveStats                              = CFG.GetBool("db.game_stats.enabled", true);

  m_AutoKickPing                           = CFG.GetUint32("hosting.high_ping.kick_ms", 250);
  m_WarnHighPing                           = CFG.GetUint32("hosting.high_ping.warn_ms", 175);
  m_SafeHighPing                           = CFG.GetUint32("hosting.high_ping.safe_ms", 130);

  m_LobbyTimeoutMode                       = CFG.GetEnum<LobbyTimeoutMode>("hosting.expiry.lobby.mode", TO_ARRAY("never", "empty", "ownerless", "strict"), LobbyTimeoutMode::kOwnerMissing);
  m_LobbyOwnerTimeoutMode                  = CFG.GetEnum<LobbyOwnerTimeoutMode>("hosting.expiry.owner.mode", TO_ARRAY("never", "absent", "strict"), LobbyOwnerTimeoutMode::kAbsent);
  m_LoadingTimeoutMode                     = CFG.GetEnum<GameLoadingTimeoutMode>("hosting.expiry.loading.mode", TO_ARRAY("never", "strict"), GameLoadingTimeoutMode::kStrict);
  m_PlayingTimeoutMode                     = CFG.GetEnum<GamePlayingTimeoutMode>("hosting.expiry.playing.mode", TO_ARRAY("never", "dry", "strict"), GamePlayingTimeoutMode::kStrict);

  m_LobbyTimeout                           = CFG.GetUint32("hosting.expiry.lobby.timeout", 600);
  m_LobbyOwnerTimeout                      = CFG.GetUint32("hosting.expiry.owner.timeout", 120);
  m_LoadingTimeout                         = CFG.GetUint32("hosting.expiry.loading.timeout", 900);
  m_PlayingTimeout                         = CFG.GetUint32("hosting.expiry.playing.timeout", 18000);

  m_PlayingTimeoutWarningShortCountDown    = CFG.GetUint8("hosting.expiry.playing.timeout.soon_warnings", 10);
  m_PlayingTimeoutWarningShortInterval     = CFG.GetUint32("hosting.expiry.playing.timeout.soon_interval", 60);
  m_PlayingTimeoutWarningLargeCountDown    = CFG.GetUint8("hosting.expiry.playing.timeout.eager_warnings", 3);
  m_PlayingTimeoutWarningLargeInterval     = CFG.GetUint32("hosting.expiry.playing.timeout.eager_interval", 1200);

  m_LobbyOwnerReleaseLANLeaver             = CFG.GetBool("hosting.expiry.owner.lan", true);

  m_LobbyCountDownInterval                 = CFG.GetUint32("hosting.game_start.count_down_interval", 500);
  m_LobbyCountDownStartValue               = CFG.GetUint32("hosting.game_start.count_down_ticks", 5);

  m_SaveGameAllowed                        = CFG.GetBool("hosting.save_game.allowed", true);

  m_LatencyDriftMax                        = CFG.GetUint16("hosting.latency.drift.max", 50);

  m_LatencyMin                             = CFG.GetUint16("hosting.latency.min", 10);
  m_LatencyMax                             = CFG.GetUint16("hosting.latency.max", 500);
  m_Latency                                = CFG.GetUint16("hosting.latency.default", 40); // everyone wants "zero latency", so let's not error if they input zero

  CHECK_BOUNDED_DEFAULT(m_LatencyMin, m_Latency, m_LatencyMax, "hosting.latency.min", "hosting.latency.default", "hosting.latency.max");

  m_LagStartMinControllerSyncMilliSeconds      = CFG.GetNonZeroUint32("net.sync.start_lag.players.min_ms", 500);
  m_LagStartMaxControllerSyncMilliSeconds      = CFG.GetNonZeroUint32("net.sync.start_lag.players.max_ms", 0xFFFFFFFF);
  m_LagStartDefaultControllerSyncMilliSeconds  = CFG.GetNonZeroUint32("net.sync.start_lag.players.default_ms", 3200); // 32 frames at 100 latency

  CHECK_BOUNDED_DEFAULT(m_LagStartMinControllerSyncMilliSeconds, m_LagStartDefaultControllerSyncMilliSeconds, m_LagStartMaxControllerSyncMilliSeconds, "net.sync.start_lag.players.min", "net.sync.start_lag.players.default", "net.sync.start_lag.players.max");

  m_LagStopMinControllerSyncMilliSeconds       = CFG.GetNonZeroUint32("net.sync.stop_lag.players.min_ms", 200);
  m_LagStopMaxControllerSyncMilliSeconds       = CFG.GetNonZeroUint32("net.sync.stop_lag.players.max_ms", 0xFFFFFFFF);
  m_LagStopDefaultControllerSyncMilliSeconds   = CFG.GetNonZeroUint32("net.sync.stop_lag.players.default_ms", 800); // 8 frames at 100 latency

  CHECK_BOUNDED_DEFAULT(m_LagStopMinControllerSyncMilliSeconds, m_LagStopDefaultControllerSyncMilliSeconds, m_LagStopMaxControllerSyncMilliSeconds, "net.sync.stop_lag.players.min", "net.sync.stop_lag.players.default", "net.sync.stop_lag.players.max");

  m_LagStartMinObserverSyncMilliSeconds        = CFG.GetNonZeroUint32("net.sync.start_lag.observers.min_ms", 3500);
  m_LagStartMaxObserverSyncMilliSeconds        = CFG.GetNonZeroUint32("net.sync.start_lag.observers.max_ms", 0xFFFFFFFF);
  m_LagStartDefaultObserverSyncMilliSeconds    = CFG.GetNonZeroUint32("net.sync.start_lag.observers.default_ms", 90000); // 900 frames at 100 latency

  CHECK_BOUNDED_DEFAULT(m_LagStartMinObserverSyncMilliSeconds, m_LagStartDefaultObserverSyncMilliSeconds, m_LagStartMaxObserverSyncMilliSeconds, "net.sync.start_lag.observers.min", "net.sync.start_lag.observers.default", "net.sync.start_lag.observers.max");

  m_LagStopMinObserverSyncMilliSeconds         = CFG.GetNonZeroUint32("net.sync.stop_lag.observers.min_ms", 100);
  m_LagStopMaxObserverSyncMilliSeconds         = CFG.GetNonZeroUint32("net.sync.stop_lag.observers.max_ms", 0xFFFFFFFF);
  m_LagStopDefaultObserverSyncMilliSeconds     = CFG.GetNonZeroUint32("net.sync.stop_lag.observers.default_ms", 60000); // 600 frames at 100 latency

  CHECK_BOUNDED_DEFAULT(m_LagStopMinObserverSyncMilliSeconds, m_LagStopDefaultObserverSyncMilliSeconds, m_LagStopMaxObserverSyncMilliSeconds, "net.sync.stop_lag.observers.min", "net.sync.stop_lag.observers.default", "net.sync.stop_lag.observers.max");

  if (m_LagStartDefaultControllerSyncMilliSeconds < m_LagStopDefaultControllerSyncMilliSeconds + m_Latency) {
    Print(
      "[CONFIG] Error - " + CFG.GetKeyValue("net.sync.start_lag.players.default_ms", to_string(m_LagStartDefaultControllerSyncMilliSeconds)) +
      " must be larger than " + CFG.GetKeyValue("net.sync.start_lag.players.default_ms", to_string(m_LagStopDefaultControllerSyncMilliSeconds)) +
      " by at least " + CFG.GetKeyValue("hosting.latency.default", to_string(m_Latency))
    );
    CFG.SetFailed();
  }

  if (m_LagStartDefaultObserverSyncMilliSeconds < m_LagStopDefaultObserverSyncMilliSeconds + m_Latency) {
    Print(
      "[CONFIG] Error - " + CFG.GetKeyValue("net.sync.start_lag.observers.default_ms", to_string(m_LagStartDefaultObserverSyncMilliSeconds)) +
      " must be larger than " + CFG.GetKeyValue("net.sync.start_lag.observers.default_ms", to_string(m_LagStopDefaultObserverSyncMilliSeconds)) +
      " by at least " + CFG.GetKeyValue("hosting.latency.default", to_string(m_Latency))
    );
    CFG.SetFailed();
  }

  m_LatencyEqualizerEnabled                = CFG.GetBool("hosting.latency.equalizer.enabled", false);
  m_LatencyEqualizerMaxDelay               = CFG.GetUint16("hosting.latency.equalizer.max_delay", 320);

  if (m_LatencyEqualizerEnabled && m_LatencyEqualizerMaxDelay < m_Latency) {
    Print(
      "[CONFIG] Error - " + CFG.GetKeyValue("hosting.latency.equalizer.max_delay", to_string(m_LatencyEqualizerMaxDelay)) +
      " cannot be lower than " + CFG.GetKeyValue("hosting.latency.default", to_string(m_Latency))
    );
    CFG.SetFailed();
  }

  m_EnableLagScreen                        = CFG.GetBool("net.lag_screen.enabled", true);
  m_SyncNormalize                          = CFG.GetBool("net.sync.normalization.enabled", true);

  m_PerfThreshold                          = static_cast<int64_t>(CFG.GetUint32("bot.perf_limit", 150));
  m_LacksMapKickDelay                      = CFG.GetUint32("hosting.map.missing.kick_delay", 60); // default: 1 minute
  m_LogDelay                               = CFG.GetUint32("hosting.log_delay", 180); // default: 3 minutes

  m_CheckJoinable                          = CFG.GetBool("monitor.hosting.on_start.check_connectivity", false);
  m_ExtraDiscoveryAddresses                = CFG.GetHostListWithImplicitPort("net.game_discovery.udp.extra_clients.ip_addresses", GAME_DEFAULT_UDP_PORT, ',');
  m_ReconnectionMode                       = RECONNECT_ENABLED_GPROXY_BASIC | RECONNECT_ENABLED_GPROXY_EXTENDED;

  m_PrivateCmdToken                        = CFG.GetString("hosting.commands.trigger", "!");
  if (!m_PrivateCmdToken.empty() && m_PrivateCmdToken[0] == '/') {
    Print("[CONFIG] Error - invalid value provided for <hosting.commands.trigger> - slash (/) is reserved by Battle.net");
    CFG.SetFailed();
  }
  m_BroadcastCmdToken                      = CFG.GetString("hosting.commands.broadcast.trigger");
  if (!m_BroadcastCmdToken.empty() && m_BroadcastCmdToken[0] == '/') {
    Print("[CONFIG] Error - invalid value provided for <hosting.commands.broadcast.trigger> - slash (/) is reserved by Battle.net");
    CFG.SetFailed();
  }
  m_EnableBroadcast                        = CFG.GetBool("hosting.commands.broadcast.enabled", false);

  if (!m_EnableBroadcast)
    m_BroadcastCmdToken.clear();

  m_IndexHostName                          = CFG.GetString("hosting.index.creator_name", 1, 15, "");
  m_LobbyVirtualHostName                   = CFG.GetString("hosting.self.virtual_player.name", 1, 15, "|cFF4080C0Aura");

  m_NotifyJoins                            = CFG.GetBool("ui.notify_joins.enabled", false);
  m_IgnoredNotifyJoinPlayers               = CFG.GetSetSensitive("ui.notify_joins.exceptions", ',', false, false, {}); /* do not trim, because LAN names may have trailing whitespace */
  m_MaxAPM                                 = CFG.GetMaybeUint16("hosting.apm_limiter.max.average");
  m_MaxBurstAPM                            = CFG.GetMaybeUint16("hosting.apm_limiter.max.burst");
  m_HideLobbyNames                         = CFG.GetBool("hosting.nicknames.hide_lobby", false);
  m_HideInGameNames                        = CFG.GetEnum<HideIGNMode>("hosting.nicknames.hide_in_game", TO_ARRAY("never", "host", "always", "auto"), HideIGNMode::kAuto);
  m_LoadInGame                             = CFG.GetBool("hosting.load_in_game.enabled", false);
  m_FakeUsersShareUnitsMode                = CFG.GetEnum<FakeUsersShareUnitsMode>("hosting.fake_users.share_units.mode", TO_ARRAY("never", "auto", "team", "all"), FakeUsersShareUnitsMode::kAuto);
  m_EnableJoinObserversInProgress          = CFG.GetBool("hosting.join_in_progress.observers", false);
  m_EnableJoinPlayersInProgress            = CFG.GetBool("hosting.join_in_progress.players", false);
  m_SpectatorDelay                         = CFG.GetUint32("hosting.spectator_delay", 300); // default: 5 minutes

  m_LoggedWords                            = CFG.GetSet("hosting.log_words", ',', true, false, {});
  m_LogChatTypes                           = CFG.GetBool("hosting.log_non_ascii", false) ? LOG_CHAT_TYPE_NON_ASCII : 0;
  m_LogCommands                            = CFG.GetBool("hosting.log_commands", false);
  m_EnableLobbyChat                        = CFG.GetBool("hosting.chat_lobby.enabled", true);
  m_EnableInGameChat                       = CFG.GetBool("hosting.chat_in_game.enabled", true);
  m_DesyncHandler                          = CFG.GetEnum<OnDesyncHandler>("hosting.desync.handler", TO_ARRAY("none", "notify", "drop"), OnDesyncHandler::kNotify);
  m_IPFloodHandler                         = CFG.GetEnum<OnIPFloodHandler>("hosting.ip_filter.flood_handler", TO_ARRAY("none", "notify", "deny"), OnIPFloodHandler::kDeny);
  m_LeaverHandler                          = CFG.GetEnum<OnPlayerLeaveHandler>("hosting.game_protocol.leaver_handler", TO_ARRAY("none", "native", "share"), OnPlayerLeaveHandler::kNative);
  m_ShareUnitsHandler                      = CFG.GetEnum<OnShareUnitsHandler>("hosting.game_protocol.share_handler", TO_ARRAY("native", "kick", "restrict"), OnShareUnitsHandler::kNative);
  m_UnsafeNameHandler                      = CFG.GetEnum<OnUnsafeNameHandler>("hosting.name_filter.unsafe_handler", TO_ARRAY("none", "censor", "deny"), OnUnsafeNameHandler::kDeny);
  m_BroadcastErrorHandler                  = CFG.GetEnum<OnRealmBroadcastErrorHandler>("hosting.realm_broadcast.error_handler", TO_ARRAY("ignore", "exit_main_error", "exit_empty_main_error", "exit_any_error", "exit_empty_any_error", "exit_max_errors"), OnRealmBroadcastErrorHandler::kExitOnMaxErrors);
  m_PipeConsideredHarmful                  = CFG.GetBool("hosting.name_filter.is_pipe_harmful", true);
  m_UDPEnabled                             = CFG.GetBool("net.game_discovery.udp.enabled", true);

  m_GameIsExpansion                        = static_cast<bool>(CFG.GetStringIndex("hosting.game_versions.expansion.default", {"roc", "tft"}, SELECT_EXPANSION_TFT));
  m_GameVersion                            = CFG.GetMaybeVersion("hosting.game_versions.main");
  CFG.FailIfErrorLast();
  m_CrossPlayMode                          = CFG.GetEnum<CrossPlayMode>("hosting.game_versions.crossplay.mode", TO_ARRAY("none", "conservative", "optimistic", "force"), CrossPlayMode::kConservative);
}

CGameConfig::CGameConfig(CGameConfig* nRootConfig, shared_ptr<CMap> nMap, shared_ptr<CGameSetup> nGameSetup)
 : m_Valid(true)
{
  if (nMap->GetMapHasTargetGameIsExpansion()) {
    // CMap::AcquireGameIsExpansion() takes care of reading CFG in lieu of CGameSetup
    m_GameIsExpansion = nMap->GetMapTargetGameIsExpansion();
  } else {
    m_GameIsExpansion = nRootConfig->m_GameIsExpansion;
  }
  if (nMap->GetMapHasTargetGameVersion()) {
    // CMap::AcquireGameVersion() takes care of reading CFG in lieu of CGameSetup
    m_GameVersion = nMap->GetMapTargetGameVersion();
  } else {
    m_GameVersion = nRootConfig->m_GameVersion;
  }

  INHERIT(m_VoteKickPercentage)

  if (m_VoteKickPercentage > 100)
    m_VoteKickPercentage = 100;

  INHERIT_MAP_OR_CUSTOM(m_NumPlayersToStartGameOver, m_NumPlayersToStartGameOver, m_NumPlayersToStartGameOver)
  INHERIT(m_MaxPlayersLoopback)
  INHERIT(m_MaxPlayersSameIP)
  INHERIT_MAP_OR_CUSTOM(m_PlayersReadyMode, m_PlayersReadyMode, m_PlayersReadyMode)
  INHERIT_MAP_OR_CUSTOM(m_AutoStartRequiresBalance, m_AutoStartRequiresBalance, m_AutoStartRequiresBalance)
  INHERIT(m_SaveStats);

  INHERIT_MAP_OR_CUSTOM(m_AutoKickPing, m_AutoKickPing, m_AutoKickPing)
  INHERIT_MAP_OR_CUSTOM(m_WarnHighPing, m_WarnHighPing, m_WarnHighPing)
  INHERIT_MAP_OR_CUSTOM(m_SafeHighPing, m_SafeHighPing, m_SafeHighPing)

  INHERIT_MAP_OR_CUSTOM(m_LobbyTimeoutMode, m_LobbyTimeoutMode, m_LobbyTimeoutMode);
  INHERIT_MAP_OR_CUSTOM(m_LobbyOwnerTimeoutMode, m_LobbyOwnerTimeoutMode, m_LobbyOwnerTimeoutMode);
  INHERIT_MAP_OR_CUSTOM(m_LoadingTimeoutMode, m_LoadingTimeoutMode, m_LoadingTimeoutMode);
  INHERIT_MAP_OR_CUSTOM(m_PlayingTimeoutMode, m_PlayingTimeoutMode, m_PlayingTimeoutMode);

  INHERIT_MAP_OR_CUSTOM(m_LobbyTimeout, m_LobbyTimeout, m_LobbyTimeout)
  INHERIT_MAP_OR_CUSTOM(m_LobbyOwnerTimeout, m_LobbyOwnerTimeout, m_LobbyOwnerTimeout)
  INHERIT_MAP_OR_CUSTOM(m_LoadingTimeout, m_LoadingTimeout, m_LoadingTimeout)
  INHERIT_MAP_OR_CUSTOM(m_PlayingTimeout, m_PlayingTimeout, m_PlayingTimeout)

  m_LobbyTimeout *= 1000;
  m_LobbyOwnerTimeout *= 1000;
  m_LoadingTimeout *= 1000;
  m_PlayingTimeout *= 1000;

  INHERIT_MAP_OR_CUSTOM(m_PlayingTimeoutWarningShortCountDown, m_PlayingTimeoutWarningShortCountDown, m_PlayingTimeoutWarningShortCountDown)
  INHERIT_MAP_OR_CUSTOM(m_PlayingTimeoutWarningShortInterval, m_PlayingTimeoutWarningShortInterval, m_PlayingTimeoutWarningShortInterval);
  INHERIT_MAP_OR_CUSTOM(m_PlayingTimeoutWarningLargeCountDown, m_PlayingTimeoutWarningLargeCountDown, m_PlayingTimeoutWarningLargeCountDown)
  INHERIT_MAP_OR_CUSTOM(m_PlayingTimeoutWarningLargeInterval, m_PlayingTimeoutWarningLargeInterval, m_PlayingTimeoutWarningLargeInterval);

  INHERIT_MAP_OR_CUSTOM(m_LobbyOwnerReleaseLANLeaver, m_LobbyOwnerReleaseLANLeaver, m_LobbyOwnerReleaseLANLeaver);

  INHERIT_MAP_OR_CUSTOM(m_LobbyCountDownInterval, m_LobbyCountDownInterval, m_LobbyCountDownInterval)
  INHERIT_MAP_OR_CUSTOM(m_LobbyCountDownStartValue, m_LobbyCountDownStartValue, m_LobbyCountDownStartValue)

  INHERIT_MAP_OR_CUSTOM(m_SaveGameAllowed, m_SaveGameAllowed, m_SaveGameAllowed)

  INHERIT(m_LatencyDriftMax)

  INHERIT(m_LatencyMin)
  INHERIT(m_LatencyMax)
  INHERIT_MAP_OR_CUSTOM(m_Latency, m_Latency, m_LatencyAverage)

  INHERIT(m_LagStartMinControllerSyncMilliSeconds)
  INHERIT(m_LagStartMaxControllerSyncMilliSeconds)
  INHERIT_MAP_OR_CUSTOM(m_LagStartDefaultControllerSyncMilliSeconds, m_LagStartDefaultControllerSyncMilliSeconds, m_StartLagPlayersMinMs)

  CHECK_BOUNDED_DEFAULT_INHERITED(m_LagStartMinControllerSyncMilliSeconds, m_LagStartDefaultControllerSyncMilliSeconds, m_LagStartMaxControllerSyncMilliSeconds, "net.sync.start_lag.players.min", "net.sync.start_lag.players.default", "net.sync.start_lag.players.max");

  INHERIT(m_LagStopMinControllerSyncMilliSeconds)
  INHERIT(m_LagStopMaxControllerSyncMilliSeconds)
  INHERIT_MAP_OR_CUSTOM(m_LagStopDefaultControllerSyncMilliSeconds, m_LagStopDefaultControllerSyncMilliSeconds, m_StopLagPlayersMaxMs)

  CHECK_BOUNDED_DEFAULT_INHERITED(m_LagStopMinControllerSyncMilliSeconds, m_LagStopDefaultControllerSyncMilliSeconds, m_LagStopMaxControllerSyncMilliSeconds, "net.sync.stop_lag.players.min", "net.sync.stop_lag.players.default", "net.sync.stop_lag.players.max");

  INHERIT(m_LagStartMinObserverSyncMilliSeconds)
  INHERIT(m_LagStartMaxObserverSyncMilliSeconds)
  INHERIT_MAP_OR_CUSTOM(m_LagStartDefaultObserverSyncMilliSeconds, m_LagStartDefaultObserverSyncMilliSeconds, m_StartLagObserversMinMs)

  CHECK_BOUNDED_DEFAULT_INHERITED(m_LagStartMinObserverSyncMilliSeconds, m_LagStartDefaultObserverSyncMilliSeconds, m_LagStartMaxObserverSyncMilliSeconds, "net.sync.start_lag.observers.min", "net.sync.start_lag.observers.default", "net.sync.start_lag.observers.max");

  INHERIT(m_LagStopMinObserverSyncMilliSeconds)
  INHERIT(m_LagStopMaxObserverSyncMilliSeconds)
  INHERIT_MAP_OR_CUSTOM(m_LagStopDefaultObserverSyncMilliSeconds, m_LagStopDefaultObserverSyncMilliSeconds, m_StopLagObserversMaxMs)

  CHECK_BOUNDED_DEFAULT_INHERITED(m_LagStopMinObserverSyncMilliSeconds, m_LagStopDefaultObserverSyncMilliSeconds, m_LagStopMaxObserverSyncMilliSeconds, "net.sync.stop_lag.observers.min", "net.sync.stop_lag.observers.default", "net.sync.stop_lag.observers.max");

  if (m_LagStartDefaultControllerSyncMilliSeconds < m_LagStopDefaultControllerSyncMilliSeconds + m_Latency) {
    Print(
      "[CONFIG] Error - <net.sync.start_lag.players.default_ms> must be larger than <net.sync.start_lag.players.default_ms>"
      " by at least <hosting.latency.default>"
    );
    m_Valid = false;
  }

  if (m_LagStartDefaultObserverSyncMilliSeconds < m_LagStopDefaultObserverSyncMilliSeconds + m_Latency) {
    Print(
      "[CONFIG] Error - <net.sync.start_lag.observers.default_ms> must be larger than <net.sync.start_lag.observers.default_ms>"
      " by at least <hosting.latency.default>"
    );
    m_Valid = false;
  }

  if (m_LatencyEqualizerEnabled && m_LatencyEqualizerMaxDelay < m_Latency) {
    Print(
      "[CONFIG] Error - <hosting.latency.equalizer.max_delay> cannot be lower than <hosting.latency.default>"
    );
    m_Valid = false;
  }

  INHERIT_MAP_OR_CUSTOM(m_LatencyEqualizerEnabled, m_LatencyEqualizerEnabled, m_LatencyEqualizerEnabled)
  INHERIT_MAP_OR_CUSTOM(m_LatencyEqualizerMaxDelay, m_LatencyEqualizerMaxDelay, m_LatencyEqualizerMaxDelay)

  INHERIT_MAP_OR_CUSTOM(m_EnableLagScreen, m_EnableLagScreen, m_EnableLagScreen)
  INHERIT_CUSTOM(m_SyncNormalize, m_SyncNormalize)

  INHERIT(m_PerfThreshold)
  INHERIT(m_LacksMapKickDelay)
  INHERIT(m_LogDelay)

  m_LacksMapKickDelay *= 1000;
  m_LogDelay *= 1000;

  INHERIT_CUSTOM(m_CheckJoinable, m_CheckJoinable)
  INHERIT(m_ExtraDiscoveryAddresses)
  INHERIT_MAP_OR_CUSTOM(m_ReconnectionMode, m_ReconnectionMode, m_ReconnectionMode)

  INHERIT(m_PrivateCmdToken)
  INHERIT(m_BroadcastCmdToken)
  INHERIT(m_EnableBroadcast)

  INHERIT(m_IndexHostName)
  if (m_IndexHostName.empty()) {
    m_IndexHostName = nGameSetup->m_Creator.GetUser();
  }
  if (m_IndexHostName.empty()) {
    m_IndexHostName = "Aura Bot";
  }

  INHERIT(m_LobbyVirtualHostName)

  INHERIT_CUSTOM(m_NotifyJoins, m_NotifyJoins)
  INHERIT(m_IgnoredNotifyJoinPlayers)

  INHERIT_MAP_OR_CUSTOM(m_MaxAPM, m_MaxAPM, m_MaxAPM)
  INHERIT_MAP_OR_CUSTOM(m_MaxBurstAPM, m_MaxBurstAPM, m_MaxBurstAPM)

  INHERIT_MAP_OR_CUSTOM(m_HideLobbyNames, m_HideLobbyNames, m_HideLobbyNames)
  INHERIT_MAP_OR_CUSTOM(m_HideInGameNames, m_HideInGameNames, m_HideInGameNames)
  INHERIT_MAP_OR_CUSTOM(m_LoadInGame, m_LoadInGame, m_LoadInGame)
  INHERIT_MAP_OR_CUSTOM(m_FakeUsersShareUnitsMode, m_FakeUsersShareUnitsMode, m_FakeUsersShareUnitsMode)
  INHERIT_MAP_OR_CUSTOM(m_EnableJoinObserversInProgress, m_EnableJoinObserversInProgress, m_EnableJoinObserversInProgress)
  INHERIT_MAP_OR_CUSTOM(m_EnableJoinPlayersInProgress, m_EnableJoinPlayersInProgress, m_EnableJoinPlayersInProgress)
  INHERIT(m_SpectatorDelay)

  INHERIT(m_LoggedWords)
  INHERIT(m_LogChatTypes)
  INHERIT_MAP_OR_CUSTOM(m_LogCommands, m_LogCommands, m_LogCommands)
  INHERIT_MAP_OR_CUSTOM(m_EnableLobbyChat, m_EnableLobbyChat, m_EnableLobbyChat)
  INHERIT_MAP_OR_CUSTOM(m_EnableInGameChat, m_EnableInGameChat, m_EnableInGameChat)
  INHERIT(m_DesyncHandler)
  INHERIT_MAP_OR_CUSTOM(m_IPFloodHandler, m_IPFloodHandler, m_IPFloodHandler)
  INHERIT_MAP_OR_CUSTOM(m_LeaverHandler, m_LeaverHandler, m_LeaverHandler)
  INHERIT_MAP_OR_CUSTOM(m_ShareUnitsHandler, m_ShareUnitsHandler, m_ShareUnitsHandler)
  INHERIT_MAP_OR_CUSTOM(m_UnsafeNameHandler, m_UnsafeNameHandler, m_UnsafeNameHandler)
  INHERIT_MAP_OR_CUSTOM(m_BroadcastErrorHandler, m_BroadcastErrorHandler, m_BroadcastErrorHandler)
  INHERIT_MAP(m_PipeConsideredHarmful, m_PipeConsideredHarmful)

  if (nGameSetup->GetIsMirror() && !nGameSetup->GetMirror().GetIsProxyEnabled()) {
    m_UDPEnabled = false;
  } else {
    INHERIT(m_UDPEnabled)
  }

  if (m_LogCommands) {
    m_LogChatTypes |= LOG_CHAT_TYPE_COMMANDS;
  }

  INHERIT_CUSTOM(m_CrossPlayMode, m_CrossPlayMode)
  INHERIT(m_VoteKickPercentage)
}

CGameConfig::~CGameConfig() = default;
