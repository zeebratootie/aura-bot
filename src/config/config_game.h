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

#ifndef AURA_CONFIG_GAME_H_
#define AURA_CONFIG_GAME_H_

#include "../includes.h"
#include "config.h"
#include "../game_setup.h"
#include "../map.h"

//
// CGameConfig
//

// Override order
// Default game config
// Default map config (*) map.flags_locked = yes/no
// Game setup

struct CGameConfig
{
  bool                             m_Valid;
  uint8_t                          m_VoteKickPercentage;         // percentage of players required to vote yes for a votekick to pass
  uint8_t                          m_NumPlayersToStartGameOver;  // when this player count is reached, the game over timer will start
  uint8_t                          m_MaxPlayersLoopback;
  uint8_t                          m_MaxPlayersSameIP;
  PlayersReadyMode                 m_PlayersReadyMode;
  bool                             m_AutoStartRequiresBalance;
  bool                             m_SaveStats;
  
  uint32_t                         m_AutoKickPing;               // auto kick players with ping higher than this
  uint32_t                         m_WarnHighPing;               // announce on chat when players have a ping higher than this value
  uint32_t                         m_SafeHighPing;               // when players ping drops below this value, announce they no longer have high ping

  LobbyTimeoutMode                 m_LobbyTimeoutMode;
  LobbyOwnerTimeoutMode            m_LobbyOwnerTimeoutMode;
  GameLoadingTimeoutMode           m_LoadingTimeoutMode;
  GamePlayingTimeoutMode           m_PlayingTimeoutMode;

  uint32_t                         m_LobbyTimeout;               // auto close the game lobby after this many minutes without any owner
  uint32_t                         m_LobbyOwnerTimeout;          // relinquish game ownership after this many minutes
  uint32_t                         m_LoadingTimeout;             // relinquish game ownership after this many minutes
  uint32_t                         m_PlayingTimeout;             // relinquish game ownership after this many minutes

  uint8_t                          m_PlayingTimeoutWarningShortCountDown;
  uint32_t                         m_PlayingTimeoutWarningShortInterval;
  uint8_t                          m_PlayingTimeoutWarningLargeCountDown;
  uint32_t                         m_PlayingTimeoutWarningLargeInterval;

  bool                             m_LobbyOwnerReleaseLANLeaver;

  uint32_t                         m_LobbyCountDownInterval;     // ms between each number count down when !start is issued
  uint32_t                         m_LobbyCountDownStartValue;   // number at which !start count down begins

  bool                             m_SaveGameAllowed;
  bool                             m_PauseGameAllowed;

  uint16_t                         m_LatencyDriftMax;                           // the maximum allowed frame drift in ms

  uint16_t                         m_LatencyMin;                                // the minimum configurable game refresh latency
  uint16_t                         m_LatencyMax;                                // the maximum configurable game refresh latency
  uint16_t                         m_Latency;                    // the game refresh latency (by default)

  uint32_t                         m_LagStartMinControllerSyncMilliSeconds;     // constraint
  uint32_t                         m_LagStartMaxControllerSyncMilliSeconds;     // constraint
  uint32_t                         m_LagStartDefaultControllerSyncMilliSeconds; // editable with !latency, CMap, CGameSetup

  uint32_t                         m_LagStopMinControllerSyncMilliSeconds;      // constraint
  uint32_t                         m_LagStopMaxControllerSyncMilliSeconds;      // constraint
  uint32_t                         m_LagStopDefaultControllerSyncMilliSeconds;  // editable with !latency, CMap, CGameSetup

  uint32_t                         m_LagStartMinObserverSyncMilliSeconds;       // constraint
  uint32_t                         m_LagStartMaxObserverSyncMilliSeconds;       // constraint
  uint32_t                         m_LagStartDefaultObserverSyncMilliSeconds;   // editable with !latency, CMap, CGameSetup

  uint32_t                         m_LagStopMinObserverSyncMilliSeconds;        // constraint
  uint32_t                         m_LagStopMaxObserverSyncMilliSeconds;        // constraint
  uint32_t                         m_LagStopDefaultObserverSyncMilliSeconds;    // editable with !latency, CMap, CGameSetup

  bool                             m_LatencyEqualizerEnabled;    // whether to add a minimum delay proportional to m_Latency to all actions sent by players
  uint16_t                         m_LatencyEqualizerMaxDelay;   // how many ms can we add to players' latencies

  bool                             m_EnableLagScreen;            // whether to pause the game with a lag screen whenever a player falls behind
  bool                             m_SyncNormalize;              // before 3-minute mark, try to keep players in the game

  int64_t                          m_PerfThreshold;              // the max expected delay between updates - if exceeded it means performance is suffering
  uint32_t                         m_LacksMapKickDelay;
  uint32_t                         m_LogDelay;

  bool                             m_CheckJoinable;
  std::vector<sockaddr_storage>    m_ExtraDiscoveryAddresses;    // list of addresses Aura announces hosted games to through UDP unicast.
  std::map<std::string, Version>   m_GameVersionsByLANPlayerNames;
  uint8_t                          m_ReconnectionMode;

  std::string                      m_PrivateCmdToken;            // a symbol prefix to identify commands and send a private reply
  std::string                      m_BroadcastCmdToken;          // a symbol prefix to identify commands and send the reply to everyone
  bool                             m_EnableBroadcast;
  std::string                      m_IndexHostName;       // index virtual host name
  std::string                      m_LobbyVirtualHostName;       // lobby virtual host name
  bool                             m_NotifyJoins;                // whether the bot should beep when a user joins a hosted game
  std::set<std::string>            m_IgnoredNotifyJoinPlayers;
  std::optional<uint16_t>          m_MaxAPM;
  std::optional<uint16_t>          m_MaxBurstAPM;
  bool                             m_HideLobbyNames;
  HideIGNMode                      m_HideInGameNames;
  bool                             m_LoadInGame;
  FakeUsersShareUnitsMode          m_FakeUsersShareUnitsMode;
  bool                             m_EnableJoinObserversInProgress;
  bool                             m_EnableJoinPlayersInProgress;
  uint32_t                         m_SpectatorDelay;

  std::set<std::string>            m_LoggedWords;
  uint8_t                          m_LogChatTypes;
  bool                             m_LogCommands;
  bool                             m_EnableLobbyChat;
  bool                             m_EnableInGameChat;
  OnDesyncHandler                  m_DesyncHandler;
  OnIPFloodHandler                 m_IPFloodHandler;
  OnPlayerLeaveHandler             m_LeaverHandler;
  OnShareUnitsHandler              m_ShareUnitsHandler;
  OnUnsafeNameHandler              m_UnsafeNameHandler;      // whether to mutilate user names when they contain unsafe characters, or deny entry, or not to care
  OnRealmBroadcastErrorHandler     m_BroadcastErrorHandler;  // under which circumstances should a game lobby be closed given that we failed to announce the game in one or more realms

  bool                             m_PipeConsideredHarmful;

  bool                             m_UDPEnabled;                 // whether this game should be listed in "Local Area Network"
  bool                             m_GameIsExpansion;
  std::optional<Version>           m_GameVersion;
  CrossPlayMode                    m_CrossPlayMode;

  explicit CGameConfig(CConfig& CFG);
  explicit CGameConfig(CGameConfig* nRootConfig, std::shared_ptr<CMap> nMap, std::shared_ptr<CGameSetup> nGameSetup);
  ~CGameConfig();
};

#endif
