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

#ifndef AURA_CLI_H_
#define AURA_CLI_H_

#include "includes.h"
#include "action.h"
#include "game_host.h"
#include "socket.h"

#include <filesystem>
#include <variant>
#include <CLI11/CLI11.hpp>

enum class CLIAction : uint8_t
{
  kNone = 0u,
  kAbout = 1u,
  kExamples = 2u,
};

// CLIResult

enum class CLIResult : uint8_t
{
  kOk            = 0u,
  kError         = 1u,
  kInfoAndQuit   = 2u,
  kConfigAndQuit = 3u,
  kTest = 4u,
};

//
// CCLI
//

class CCLI
{
public:
  std::optional<std::filesystem::path>        m_CFGAdapterPath;
  std::optional<std::filesystem::path>        m_CFGPath;
  std::optional<std::filesystem::path>        m_HomePath;
  bool                                        m_UseStandardPaths;

  CLIAction                                   m_InfoAction;
  CLIResult                                   m_ParseResult;

  bool                                        m_Verbose;
  std::optional<bool>                         m_LAN;
  std::optional<bool>                         m_BNET;
  std::optional<bool>                         m_IRC;
  std::optional<bool>                         m_Discord;
  std::optional<bool>                         m_ExitOnStandby;
  std::optional<bool>                         m_UseMapCFGCache;
  std::optional<CacheRevalidationMethod>      m_MapCFGCacheRevalidation;
  std::optional<sockaddr_storage>             m_BindAddress;
  std::optional<uint16_t>                     m_HostPort;
  std::optional<UDPDiscoveryMode>             m_UDPDiscoveryMode;
  std::optional<LogLevel>                     m_LogLevel;
  std::optional<bool>                         m_InitSystem;

  std::optional<Version>                      m_War3DataVersion;
  std::optional<std::filesystem::path>        m_War3Path;
  std::optional<std::filesystem::path>        m_MapPath;
  std::optional<std::filesystem::path>        m_MapCFGPath;
  std::optional<std::filesystem::path>        m_MapCachePath;
  std::optional<std::filesystem::path>        m_JASSPath;
  std::optional<std::filesystem::path>        m_GameSavePath;
  std::optional<bool>                         m_CheckJASS;
  std::optional<bool>                         m_ExtractJASS;

  // Host flags
  std::optional<std::string>                  m_SearchTarget;
  std::optional<uint8_t>                      m_SearchType;
  std::optional<std::string>                  m_GameName;
  std::optional<bool>                         m_GameIsExpansion;
  std::optional<Version>                      m_GameVersion;
  std::optional<W3ModLocale>                  m_GameLocaleMod;
  std::optional<uint16_t>                     m_GameLocaleLangID;
  std::optional<bool>                         m_GameTeamsLocked;
  std::optional<bool>                         m_GameTeamsTogether;
  std::optional<bool>                         m_GameAdvancedSharedUnitControl;
  std::optional<bool>                         m_GameRandomRaces;
  std::optional<bool>                         m_GameRandomHeroes;
  std::optional<GameObserversMode>            m_GameObservers;
  std::optional<GameVisibilityMode>           m_GameVisibility;
  std::optional<GameSpeed>                    m_GameSpeed;
  std::optional<std::string>                  m_GameOwner;
  std::optional<bool>                         m_GameOwnerLess;
  std::vector<std::string>                    m_ExcludedRealms;

  std::optional<bool>                                       m_GameMirror;
  std::variant<std::monostate, StringPair, GameHost>        m_GameMirrorSource;
  std::optional<MirrorSourceType>                           m_GameMirrorSourceType;

  std::optional<bool>                         m_GameMirrorWatchLobby;
  std::optional<std::string>                  m_GameMirrorWatchLobbyUser;
  std::optional<uint8_t>                      m_GameMirrorWatchLobbyTeam;
  std::optional<uint32_t>                     m_GameMirrorWatchLobbyTeamRetryInterval;
  std::optional<uint32_t>                     m_GameMirrorWatchLobbyTeamMaxTries;

  std::optional<bool>                         m_GameMirrorProxy;

  std::optional<MirrorTimeoutMode>            m_GameMirrorTimeoutMode;
  std::optional<LobbyTimeoutMode>             m_GameLobbyTimeoutMode;
  std::optional<LobbyOwnerTimeoutMode>        m_GameLobbyOwnerTimeoutMode;
  std::optional<GameLoadingTimeoutMode>       m_GameLoadingTimeoutMode;
  std::optional<GamePlayingTimeoutMode>       m_GamePlayingTimeoutMode;

  std::optional<uint32_t>                     m_GameMirrorTimeout;
  std::optional<uint32_t>                     m_GameLobbyTimeout;
  std::optional<uint32_t>                     m_GameLobbyOwnerTimeout;
  std::optional<uint32_t>                     m_GameLoadingTimeout;
  std::optional<uint32_t>                     m_GamePlayingTimeout;

  std::optional<uint8_t>                      m_GamePlayingTimeoutWarningShortCountDown;
  std::optional<uint32_t>                     m_GamePlayingTimeoutWarningShortInterval;
  std::optional<uint8_t>                      m_GamePlayingTimeoutWarningLargeCountDown;
  std::optional<uint32_t>                     m_GamePlayingTimeoutWarningLargeInterval;

  std::optional<bool>                         m_GameLobbyOwnerReleaseLANLeaver;

  std::optional<uint32_t>                     m_GameLobbyCountDownInterval;
  std::optional<uint32_t>                     m_GameLobbyCountDownStartValue;

  std::optional<uint8_t>                      m_GameAutoStartPlayers;
  std::optional<int64_t>                      m_GameAutoStartSeconds;

  std::optional<bool>                         m_GameEnableLagScreen;
  std::optional<uint16_t>                     m_GameLatencyAverage;
  std::optional<uint16_t>                     m_GameLatencyMaxFrames;
  std::optional<uint16_t>                     m_GameLatencySafeFrames;
  std::optional<bool>                         m_GameLatencyEqualizerEnabled;
  std::optional<uint8_t>                      m_GameLatencyEqualizerFrames;

  std::optional<bool>                         m_GameEnableLobbyChat;
  std::optional<bool>                         m_GameEnableInGameChat;

  std::optional<uint32_t>                     m_GameMapDownloadTimeout;
  std::optional<bool>                         m_GameCheckJoinable;
  std::optional<bool>                         m_GameNotifyJoins;
  std::optional<bool>                         m_GameLobbyReplaceable;
  std::optional<bool>                         m_GameLobbyAutoRehosted;
  std::optional<bool>                         m_GameSaveAllowed;
  std::optional<bool>                         m_GameCheckReservation;
  std::optional<std::string>                  m_GameHCL;
  std::optional<bool>                         m_GameFreeForAll;
  std::optional<uint8_t>                      m_GameNumPlayersToStartGameOver;
  std::optional<PlayersReadyMode>             m_GamePlayersReadyMode;
  std::optional<uint32_t>                     m_GameAutoKickPing;
  std::optional<uint32_t>                     m_GameWarnHighPing;
  std::optional<uint32_t>                     m_GameSafeHighPing;
  std::optional<bool>                         m_GameSyncNormalize;
  std::optional<uint16_t>                     m_GameMaxAPM;
  std::optional<uint16_t>                     m_GameMaxBurstAPM;
  std::vector<std::string>                    m_GameReservations;
  std::optional<bool>                         m_CheckMapVersion;
  std::optional<std::filesystem::path>        m_GameSavedPath;
  std::optional<uint8_t>                      m_GameReconnectionMode;
  std::optional<std::string>                  m_GameMapAlias;
  std::optional<uint8_t>                      m_GameDisplayMode;
  std::optional<OnIPFloodHandler>             m_GameIPFloodHandler;
  std::optional<OnPlayerLeaveHandler>         m_GameLeaverHandler;
  std::optional<OnShareUnitsHandler>          m_GameShareUnitsHandler;
  std::optional<OnUnsafeNameHandler>          m_GameUnsafeNameHandler;
  std::optional<OnRealmBroadcastErrorHandler> m_GameBroadcastErrorHandler;
  std::optional<CrossPlayMode>                m_GameCrossPlayMode;
  std::optional<GameResultSourceSelect>       m_GameResultSource;
  std::optional<bool>                         m_GameHideLobbyNames;
  std::optional<HideIGNMode>                  m_GameHideLoadedNames;
  std::optional<bool>                         m_GameLoadInGame;
  std::optional<FakeUsersShareUnitsMode>      m_GameFakeUsersShareUnitsMode;
  std::optional<bool>                         m_GameEnableJoinObserversInProgress;
  std::optional<bool>                         m_GameEnableJoinPlayersInProgress;
  std::optional<uint32_t>                     m_GameSpectatorDelay;
  std::optional<bool>                         m_GameLogCommands;
  std::optional<bool>                         m_GameAutoStartRequiresBalance;

  // UPnP
  std::optional<bool>                         m_EnableUPnP;
  std::vector<uint16_t>                       m_PortForwardTCP;
  std::vector<uint16_t>                       m_PortForwardUDP;

  // Command queue
  std::optional<std::string>                  m_ExecAs;
  CommandAuth                                 m_ExecAuth;
  std::vector<std::string>                    m_ExecCommands;
  bool                                        m_ExecBroadcast;
  bool                                        m_ExecOnline;

  bool                                        m_RunTests;

  CCLI();
  ~CCLI();

  // Parsing stuff
  //CLI::Validator GetIsFullyQualifiedUserValidator();
  CLIResult Parse(const int argc, char** argv);

  void ConditionalRequireError(const std::string& gateName, const std::string& subName, bool isConverse = false);
  void ConditionalRequireOppositeError(const std::string& gateName, const std::string& subName, bool isConverse = false);

  template<typename T, typename U>
  void ConditionalRequire(const std::string& gateName, const std::optional<T>& gate, const std::string& subName, const std::optional<U>& sub, bool isBiDi = false);

  template<typename T, typename U>
  void ConditionalRequireOpposite(const std::string& gateName, const std::optional<T>& gate, const std::string& subName, const std::optional<U>& sub, bool isBiDi = false);

  template<typename T, typename U, typename F>
  void MapOpt(const std::string& optName, const std::optional<T>& operand, F&& mapFn, std::optional<U>& result);

  [[nodiscard]] bool RunGameLoadParameters(std::shared_ptr<CGameSetup> nGameSetup) const;

  void RunInfoActions() const;
  void OverrideConfig(CAura* nAura) const;
  bool QueueActions(CAura* nAura) const;
  [[nodiscard]] inline std::optional<bool> GetInitSystem() const { return m_InitSystem; };
};

#endif // AURA_CLI_H_
