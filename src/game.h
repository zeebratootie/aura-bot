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

/*

   Copyright [2010] [Josko Nikolic]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT

 */

#ifndef AURA_GAME_H_
#define AURA_GAME_H_

#include "includes.h"
#include "list.h"
#include "flat_map.h"
#include "async_observer.h"
#include "locations.h"
#include "map.h"
#include "game_result.h"
#include "game_seeker.h"
#include "game_slot.h"
#include "game_setup.h"
#include "game_virtual_user.h"
#include "save_game.h"
#include "socket.h"
#include "config/config_game.h"

//
// CGame
//

/*
 Nomenclature notes:
 User: GameUser::CGameUser instance, representing a remote game client that successfully joined the game.
 Fake user: (UID, SID) comboes that may occupy game slots. Stored as 16 bits. Higher = SID, Lower = UID.
            WC3 game client cannot distinguish them from actual users.
            Aura never treats them as "users".
 Player: User that does not occupy an observer slot.
 Controller: Any of user, fake user, or AI, that does not occupy an observer slot.
 */

class CGame : public std::enable_shared_from_this<CGame>
{
public:
  CAura* m_Aura;
  CGameConfig m_Config;

private:
  friend class CCommandContext;

protected:
  bool                                                   m_Verbose;
  std::shared_ptr<CTCPServer>                            m_Socket;                        // listening socket
  FlatMap<uint8_t, GameDiscoveryInterface>               m_NetInterfaces;
  CDBBan*                                                m_LastLeaverBannable;            // last ban for the !banlast command - this is a pointer to one of the items in m_Bannables
  std::vector<CDBBan*>                                   m_Bannables;                     // std::vector of potential ban data for the database
  std::vector<CDBBan*>                                   m_ScopeBans;                     // it must be a different vector from m_Bannables, because m_Bannables has unique name data, while m_ScopeBans has unique (name, server) data
  CW3MMD*                                                m_CustomStats;
  Dota::CDotaStats*                                      m_DotaStats;                     // class to keep track of game stats such as kills/deaths/assists in dota
  CGameInteractiveHost*                                  m_GameInteractiveHost;
  std::shared_ptr<CSaveGame>                             m_RestoredGame;
  std::vector<CGameSlot>                                 m_Slots;                         // std::vector of slots
  std::vector<CGameController*>                          m_GameControllers;               // std::vector of potential gameuser data for the database
  UserList                                               m_Users;                         // std::vector of players
  CircleDoubleLinkedList<CQueuedActionsFrame>            m_Actions;            // actions to be sent
  QueuedActionsFrameNode*                                m_CurrentActionsFrame;
  std::vector<std::string>                               m_Reserved;                      // std::vector of player names with reserved slots (from the !hold command)
  std::set<std::string>                                  m_ReportedJoinFailNames;         // set of player names to NOT print ban messages for when joining because they've already been printed
  std::map<std::string, std::set<Version>>               m_VersionErrors;
  std::vector<CGameVirtualUser>                          m_FakeUsers;                     // the fake player's UIDs (lower 8 bits) and SIDs (higher 8 bits) (if present)
  std::shared_ptr<CMap>                                  m_Map;                           // map data
  uint32_t                                               m_GameFlags;
  GameUser::CGameUser*                                   m_PauseUser;
  std::string                                            m_GameName;                      // game name
  uint16_t                                               m_CreationCounter;
  uint64_t                                               m_PersistentId;
  std::string                                            m_LastOwner;                     // name of the player who was owner last time the owner was released
  bool                                                   m_FromAutoReHost;
  bool                                                   m_OwnerLess;
  std::string                                            m_OwnerName;                     // name of the player who owns this game (should be considered an admin)
  std::string                                            m_OwnerRealm;                    // self-identified realm of the player who owns the game (spoofable)
  ServiceUser                                            m_Creator;
  std::string                                            m_CreatorText;                   // who created this game
  std::set<std::string>                                  m_RealmsExcluded;                // battle.net servers where the mirrored game is not to be broadcasted
  std::string                                            m_PlayedBy;
  std::string                                            m_KickVotePlayer;                // the player to be kicked with the currently running kick vote
  std::string                                            m_HCLCommandString;              // the "HostBot Command Library" command string, used to pass a limited amount of data to specially designed maps
  std::string                                            m_MapPath;                       // store the map path to save in the database on game end
  std::string                                            m_MapSiteURL;
  int64_t                                                m_CreationTime;                  // when the game was created
  int64_t                                                m_LastPingTicks;                 // when the last ping was sent
  int64_t                                                m_LastCheckActionsTicks;
  int64_t                                                m_LastRefreshTime;               // when the last game refresh was sent
  int64_t                                                m_LastDownloadCounterResetTicks; // when the download counter was last reset
  int64_t                                                m_LastCountDownTicks;            // when the last countdown message was sent
  int64_t                                                m_StartedLoadingTicks;           // when the game started loading
  int64_t                                                m_FinishedLoadingTicks;          // when the game finished loading
  long                                                   m_MapGameStartTime;              // for W3HMC
  int64_t                                                m_EffectiveTicks;                // ingame ticks excluding paused time
  int64_t                                                m_LatencyTicks;                  // ticks between last update and next
  int64_t                                                m_NextLatencyTicks;              // ticks between last update and next
  int64_t                                                m_LastActionSentTicks;           // when the last action packet was sent
  int64_t                                                m_LastActionLateBy;              // the number of ticks we were late sending the last action packet by
  int64_t                                                m_LastPausedTicks;               // when the game was last paused
  int64_t                                                m_PausedTicksDeltaSum;           // Sum of GetTicks deltas for every game pause
  int64_t                                                m_StartedLaggingTime;            // when the last lag screen started
  int64_t                                                m_LastLagScreenTime;             // when the last lag screen was active (continuously updated)
  int64_t                                                m_LastLagStartCheckTime;
  uint32_t                                               m_PingReportedSinceLagTimes;     // How many times we have sent players' pings since we started lagging
  size_t                                                 m_LagStartMinPlayersFrames;      // the minimum number of packets a player falling out of sync will start the lag screen
  size_t                                                 m_LagStopMaxPlayersFrames;       // the maximum number of packets a player behind sync will stop the lag screen
  size_t                                                 m_LagStartMinObserversFrames;    // the minimum number of packets an observer falling out of sync will start the lag screen
  size_t                                                 m_LagStopMaxObserversFrames;     // the minimum number of packets an observer falling out of sync will start the lag screen
  int64_t                                                m_LastUserSeenTicks;                  // when any user was last seen in the lobby
  int64_t                                                m_LastOwnerSeenTicks;                 // when the game owner was last seen in the lobby
  int64_t                                                m_LastOwnerAssignedTicks;             // when the game owner was assigned
  int64_t                                                m_StartedKickVoteTime;           // when the kick vote was started
  int64_t                                                m_LastStatsUpdateTime;
  int64_t                                                m_LastDynamicLatencyTicks;
  uint8_t                                                m_GameOver;
  std::optional<int64_t>                                 m_GameOverTime;                  // when the game was over
  std::optional<int64_t>                                 m_GameOverTolerance;
  std::optional<int64_t>                                 m_LastPlayerLeaveTicks;          // when the most recent player left the game
  int64_t                                                m_LastLagScreenResetTime;        // when the "lag" screen was last reset
  uint32_t                                               m_RandomSeed;                    // the random seed sent to the Warcraft III clients
  uint32_t                                               m_HostCounter;                   // a unique game number
  uint32_t                                               m_EntryKey;                      // random entry key for LAN, used to prove that a player is actually joining from LAN
  size_t                                                 m_SyncCounter;                   // the number of actions sent so far (for determining if anyone is lagging)
  size_t                                                 m_SyncCounterChecked;            // the number of verified keepalive packets
  uint8_t                                                m_PingEqualizerMaxFrames;
  uint8_t                                                m_PingEqualizerActiveDelayFrames;
  int64_t                                                m_LastPingEqualizerGameTicks;    // m_EffectiveTicks when ping equalizer was last run

  uint32_t                                               m_DownloadCounter;               // # of map bytes downloaded in the last second
  uint32_t                                               m_CountDownCounter;              // the countdown is finished when this reaches zero
  uint8_t                                                m_StartPlayers;                  // number of players when the game started
  std::vector<std::pair<uint8_t, int64_t>>               m_AutoStartRequirements;
  bool                                                   m_ControllersBalanced;
  uint8_t                                                m_ControllersReadyCount;
  uint8_t                                                m_ControllersNotReadyCount;
  uint8_t                                                m_ControllersWithMap;
  uint8_t                                                m_CustomLayout;
  std::pair<uint8_t, uint8_t>                            m_CustomLayoutData;
  uint16_t                                               m_HostPort;                      // the port to host games on
  bool                                                   m_PublicHostOverride;            // whether to use own m_PublicHostAddress, m_PublicHostPort instead of CRealm's (disables hosting on CRealm mirror instances)
  std::array<uint8_t, 4>                                 m_PublicHostAddress;
  uint16_t                                               m_PublicHostPort;
  uint8_t                                                m_DisplayMode;                   // game state, public or private
  bool                                                   m_IsAutoVirtualPlayers;          // if we should try to add the virtual host as a second (fake) player in single-player games
  uint8_t                                                m_VirtualHostUID;                // virtual host's UID - note that they don't get a SID
  uint8_t                                                m_GProxyEmptyActions;            // empty actions used for gproxy protocol
  bool                                                   m_Exiting;                       // set to true and this instance will be deleted next update
  bool                                                   m_ExitingSoon;                   // set to true and this instance will be deleted when no players remain
  uint8_t                                                m_SlotInfoChanged;               // if the slot info has changed and hasn't been sent to the players yet (optimization)
  uint8_t                                                m_JoinedVirtualHosts;
  uint8_t                                                m_ReconnectProtocols;
  bool                                                   m_Replaceable;                   // whether this game can be destroyed when !host command is used inside
  bool                                                   m_Replacing;
  bool                                                   m_PublicStart;                   // if the game owner is the only one allowed to run game commands or not
  bool                                                   m_Locked;                        // if the game owner is the only one allowed to run game commands or not
  bool                                                   m_ChatOnly;                      // if we should ignore game start commands
  bool                                                   m_MuteAll;                       // if we should stop forwarding ingame chat messages targeted for all players or not
  bool                                                   m_ChatEnabled;                   // if we should forward chat messages
  bool                                                   m_IsMirror;                      // if we aren't actually hosting the game, but just broadcasting it
  bool                                                   m_IsMirrorProxy;
  bool                                                   m_CountDownStarted;              // if the game start countdown has started or not
  bool                                                   m_CountDownFast;
  bool                                                   m_CountDownUserInitiated;
  bool                                                   m_GameLoading;                   // if the game is currently loading or not
  bool                                                   m_GameLoaded;                    // if the game has loaded or not
  bool                                                   m_LobbyLoading;                  // if the lobby is being setup asynchronously
  bool                                                   m_IsLagging;                       // if the lag screen is active or not
  bool                                                   m_IsPaused;                        // if the game is paused or not
  bool                                                   m_IsDraftMode;                   // if players are forbidden to choose their own teams (if so, let team captains use !team, !ffa, !vsall, !vsai, !teams)
  bool                                                   m_IsHiddenPlayerNames;           // if players names are to be obfuscated in most circumstances
  bool                                                   m_HadLeaver;                     // if the game had a leaver after it started
  bool                                                   m_CheckReservation;
  bool                                                   m_UsesCustomReferees;
  bool                                                   m_SentPriorityWhois;
  bool                                                   m_Remaking;
  bool                                                   m_Remade;
  uint8_t                                                m_SaveOnLeave;
  GameResultSourceSelect                                 m_GameResultSourceOfTruth;
  bool                                                   m_IsSinglePlayer;
  bool                                                   m_Rated;
  std::string                                            m_UnratedReason;
  bool                                                   m_HMCEnabled;
  uint8_t                                                m_BufferingEnabled;
  uint32_t                                               m_BeforePlayingEmptyActions;     // counter for game-start empty actions. Used for load-in-game feature.

  bool                                                   m_APMTrainerPaused;
  uint32_t                                               m_APMTrainerTicks;

  SharedByteArray                                        m_LoadedMapChunk;
  std::shared_ptr<GameHistory>                           m_GameHistory;
  std::optional<GameResults>                             m_GameResults;
  GameResultSource                                       m_GameResultsSource;

  std::bitset<128>                                       m_SupportedGameVersions;
  Version                                                m_SupportedGameVersionsMin;
  Version                                                m_SupportedGameVersionsMax;

  bool                                                   m_GameDiscoveryActive;
  uint8_t                                                m_GameDiscoveryInfoChanged;
  std::vector<uint8_t>                                   m_GameDiscoveryInfo;
  uint16_t                                               m_GameDiscoveryInfoVersionOffset;
  uint16_t                                               m_GameDiscoveryInfoDynamicOffset;
  std::map<const GameUser::CGameUser*, UserList>         m_SyncPlayers;     //

  std::queue<CGameLogRecord*>                            m_PendingLogs;
  
  std::optional<CGameVirtualUserReference>               m_HMCVirtualUser; // sends ACTION_CHAT_TRIGGER actions for HMC system
  std::optional<CGameVirtualUserReference>               m_AHCLVirtualUser; // sends ACTION_GAME_CACHE_INT actions for AHCL system
  std::optional<CGameVirtualUserReference>               m_InertVirtualUser; // all interactions with this virtual user are forbidden, except maybe chat
  std::optional<CGameVirtualUserReference>               m_JoinInProgressVirtualUser; // must never send actions, otherwise CAsyncObserver desyncs

public:
  CGame(CAura* nAura, std::shared_ptr<CGameSetup> nGameSetup);
  ~CGame();
  CGame(CGame&) = delete;

  bool                                                   GetExiting() const { return m_Exiting; }
  inline QueuedActionsFrameNode*                         GetFirstActionFrameNode() { return m_CurrentActionsFrame; }
  inline QueuedActionsFrameNode*                         GetLastActionFrameNode() { return m_CurrentActionsFrame->prev; }
  inline CQueuedActionsFrame&                            GetFirstActionFrame();
  inline CQueuedActionsFrame&                            GetLastActionFrame();
  std::vector<QueuedActionsFrameNode*>                   GetFrameNodesInRangeInclusive(const uint8_t startOffset, const uint8_t endOffset);
  std::vector<QueuedActionsFrameNode*>                   GetAllFrameNodes();
  void                                                   MergeFrameNodes(std::vector<QueuedActionsFrameNode*>& frameNodes);
  void                                                   ResetUserPingEqualizerDelays();
  bool                                                   CheckUpdatePingEqualizer();
  uint8_t                                                UpdatePingEqualizer();
  void                                                   UpdateDynamicLatency();
  std::vector<std::pair<GameUser::CGameUser*, uint32_t>> GetDescendingSortedRTT() const;
  inline std::shared_ptr<CMap>                           GetMap() const { return m_Map; }
  inline uint32_t                                        GetEntryKey() const { return m_EntryKey; }
  inline uint16_t                                        GetHostPort() const { return m_HostPort; }
  uint16_t                                               GetDiscoveryPort(const uint8_t protocol) const;
  bool                                                   GetIsStageAcceptingJoins() const;
  bool                                                   GetUDPEnabled() const;
  inline bool                                            GetPublicHostOverride() const { return m_PublicHostOverride; }
  inline std::array<uint8_t, 4>                          GetPublicHostAddress() const { return m_PublicHostAddress; }
  inline uint16_t                                        GetPublicHostPort() const { return m_PublicHostPort; }
  inline uint8_t                                         GetDisplayMode() const { return m_DisplayMode; }
  inline uint8_t                                         GetGProxyEmptyActions() const { return m_GProxyEmptyActions; }
  inline std::string                                     GetGameName() const { return m_GameName; }
  inline uint16_t                                        GetCreationCounter() const { return m_CreationCounter; }
  std::string                                            GetCustomCreationCounterText(std::shared_ptr<const CRealm> realm, char counter) const;
  std::string                                            GetCreationCounterText(std::shared_ptr<const CRealm> realm) const;
  std::string                                            GetNextCreationCounterText(std::shared_ptr<const CRealm> realm) const;
  inline uint64_t                                        GetGameID() const { return m_PersistentId; }
  inline uint8_t                                         GetNumSlots() const { return static_cast<uint8_t>(m_Slots.size()); }
  std::string                                            GetIndexHostName() const;
  std::string                                            GetLobbyVirtualHostName() const;
  std::string                                            GetCustomGameNameTemplate(std::shared_ptr<const CRealm> realm = nullptr, bool forceLobby = false) const;
  std::string                                            GetCustomGameName(std::shared_ptr<const CRealm> realm = nullptr, bool forceLobby = false) const;
  std::string                                            GetNextCustomGameName(std::shared_ptr<const CRealm> realm = nullptr, bool forceLobby = false) const;
  std::string                                            GetDiscoveryNameLAN() const;
  std::string                                            GetShortNameLAN() const;
  std::string                                            GetAnnounceText(std::shared_ptr<const CRealm> realm = nullptr) const;
  inline bool                                            GetFromAutoReHost() const { return m_FromAutoReHost; }
  inline bool                                            GetLockedOwnerLess() const { return m_OwnerLess; }
  inline std::string                                     GetOwnerName() const { return m_OwnerName; }
  inline std::string                                     GetOwnerRealm() const { return m_OwnerRealm; }

  inline std::string_view                                GetCreatorName() const { return m_Creator.GetUser(); }
  inline ServiceType                                     GetCreatedFromType() const { return m_Creator.GetServiceType(); }
  inline bool                                            GetCreatedFromIsExpired() const { return m_Creator.GetIsExpired(); }
  inline bool                                            GetCanJoinInProgress() const { return m_JoinInProgressVirtualUser.has_value(); }

  template <typename T>
  [[nodiscard]]                                          std::shared_ptr<T> GetCreatedFrom() const;

  [[nodiscard]]                                          bool MatchesCreatedFrom(const ServiceType fromType) const;
  [[nodiscard]]                                          bool MatchesCreatedFrom(const ServiceType fromType, std::shared_ptr<const void> fromThing) const;
  [[nodiscard]]                                          bool MatchesCreatedFromGame(std::shared_ptr<const CGame> nGame) const;
  [[nodiscard]]                                          bool MatchesCreatedFromRealm(std::shared_ptr<const CRealm> nRealm) const;
  [[nodiscard]]                                          bool MatchesCreatedFromIRC() const;
  [[nodiscard]]                                          bool MatchesCreatedFromDiscord() const;

  [[nodiscard]] inline std::shared_ptr<CRealm> GetSourceRealm() const { return MatchesCreatedFrom(ServiceType::kRealm) ? GetCreatedFrom<CRealm>() : nullptr; }

  inline uint32_t                                        GetHostCounter() const { return m_HostCounter; }
  inline int64_t                                         GetLastLagScreenTime() const { return m_LastLagScreenTime; }
  inline bool                                            GetIsReplaceable() const { return m_Replaceable;}
  inline bool                                            GetIsBeingReplaced() const { return m_Replacing;}
  inline bool                                            GetIsPublicStartable() const { return m_PublicStart; }
  inline bool                                            GetLocked() const { return m_Locked; }
  inline bool                                            GetMuteAll() const { return m_MuteAll; }
  inline bool                                            GetCountDownStarted() const { return m_CountDownStarted; }
  inline bool                                            GetCountDownFast() const { return m_CountDownFast; }
  inline bool                                            GetCountDownUserInitiated() const { return m_CountDownUserInitiated; }
  inline bool                                            GetIsMirror() const { return m_IsMirror; }
  inline bool                                            GetIsMirrorProxy() const { return m_IsMirrorProxy; }
  inline bool                                            GetIsDraftMode() const { return m_IsDraftMode; }
  bool                                                   GetIsHiddenPlayerNames() const;
  inline bool                                            GetGameLoading() const { return m_GameLoading; }
  inline bool                                            GetGameLoaded() const { return m_GameLoaded; }
  inline bool                                            GetLobbyLoading() const { return m_LobbyLoading; }
  inline bool                                            GetIsLobbyOrMirror() const { return !m_GameLoading && !m_GameLoaded; }
  inline bool                                            GetIsLobbyStrict() const { return !m_IsMirror && !m_GameLoading && !m_GameLoaded; }
  inline bool                                            GetIsRestored() const { return m_RestoredGame != nullptr; }
  inline size_t                                          GetSyncCounter() const { return m_SyncCounter; }
  uint8_t                                                GetMaxEqualizerDelayFrames() const { return m_PingEqualizerActiveDelayFrames; }
  uint8_t                                                CalcMaxEqualizerDelayFrames() const;
  int64_t                                                GetActiveLatency() const;
  int64_t                                                GetNextLatency(int64_t frameDrift = 0) const;
  int64_t                                                GetLastActionLateBy(int64_t oldLatency) const;
  size_t                                                 GetSyncLimit(bool isObserver) const;
  size_t                                                 GetSyncLimitSafe(bool isObserver) const;
  inline bool                                            GetIsLagging() const { return m_IsLagging; }
  inline bool                                            GetIsPaused() const { return m_IsPaused; }
  inline bool                                            GetIsGameOver() const { return m_GameOver != GAME_ONGOING; }
  inline bool                                            GetIsGameOverTrusted() const { return m_GameOver == GAME_OVER_TRUSTED; }
  uint8_t                                                GetLayout() const;
  uint8_t                                                GetCustomLayout() const { return m_CustomLayout; }
  bool                                                   GetIsCustomForces() const;
  void                                                   UpdateSelectBlockTime(int64_t& usecBlockTime) const;
  uint32_t                                               GetSlotsOccupied() const;
  uint32_t                                               GetSlotsOpen() const;
  bool                                                   HasSlotsOpen() const;
  bool                                                   GetIsSinglePlayerMode() const;
  bool                                                   GetHasAnyFullObservers() const;
  bool                                                   GetHasChatSendHost() const;
  bool                                                   GetHasChatRecvHost() const;
  bool                                                   GetHasChatSendPermaHost() const;
  bool                                                   GetHasChatRecvPermaHost() const;
  uint32_t                                               GetNumJoinedUsers() const;
  uint32_t                                               GetNumJoinedUsersOrFake() const;
  uint8_t                                                GetNumJoinedPlayers() const;
  uint8_t                                                GetNumJoinedPlayersOrFake() const;
  uint8_t                                                GetNumJoinedObservers() const;
  uint8_t                                                GetNumJoinedObserversOrFake() const;
  uint8_t                                                GetNumJoinedPlayersOrFakeUsers() const;
  uint8_t                                                GetNumFakePlayers() const;
  uint8_t                                                GetNumFakeObservers() const;
  size_t                                                 GetNumSpectators() const;
  uint8_t                                                GetNumOccupiedSlots() const;
  uint8_t                                                GetNumPotentialControllers() const;
  uint8_t                                                GetNumControllers() const;
  uint8_t                                                GetNumComputers() const;
  uint8_t                                                GetNumTeams() const;
  uint8_t                                                GetNumTeamControllersOrOpen(const uint8_t team) const;
  std::string                                            GetClientFileName() const;
  std::string                                            GetHCLCommandString() const { return m_HCLCommandString; }
  std::string                                            GetMapSiteURL() const { return m_MapSiteURL; }
  inline long                                            GetMapGameStartTime() const { return m_MapGameStartTime; }
  inline int64_t                                         GetEffectiveTicks() const { return m_EffectiveTicks; }
  inline int64_t                                         GetLatencyTicks() const { return m_LatencyTicks; }
  inline int64_t                                         GetLastPausedTicks() const { return m_LastPausedTicks; }
  inline int64_t                                         GetPausedTicksDeltaSum() const { return m_PausedTicksDeltaSum; }
  inline int64_t                                         GetStartedLaggingTime() const { return m_StartedLaggingTime; }
  inline bool                                            GetChatOnly() const { return m_ChatOnly; }
  inline bool                                            GetAnyUsingGProxy() { return m_ReconnectProtocols > 0; }
  std::string                                            GetGameSpectatorName() const;
  std::string                                            GetStatusDescription() const;
  std::string                                            GetEndDescription(std::shared_ptr<const CRealm> realm) const;
  std::string                                            GetCategory() const;
  uint32_t                                               GetGameType() const;
  inline uint32_t                                        GetGameFlags() const { return m_GameFlags; }
  uint32_t                                               CalcGameFlags() const;
  std::string_view                                       GetSourceFilePath() const;
  std::array<uint8_t, 4>                                 GetSourceFileHashBlizz(const Version& version) const;
  std::array<uint8_t, 20>                                GetMapSHA1(const Version& version) const;
  std::array<uint8_t, 2>                                 GetAnnounceWidth() const;
  std::array<uint8_t, 2>                                 GetAnnounceHeight() const;
  std::string                                            CheckIsValidHCL(const std::string& hcl) const;

  std::string                                            GetLogPrefix() const;
  ImmutableUserList                                      GetPlayers() const;
  ImmutableUserList                                      GetObservers() const;
  ImmutableUserList                                      GetUnreadyPlayers() const;
  ImmutableUserList                                      GetWaitingReconnectPlayers() const;
  std::vector<CAsyncObserver*>                           GetSpectators() const;
  std::optional<Version>                                 GetOverrideLANVersion(const std::string& playerName, const sockaddr_storage* address) const;
  std::optional<Version>                                 GetIncomingPlayerVersion(const CConnection* user, const CIncomingJoinRequest& joinRequest, std::shared_ptr<const CRealm> fromRealm) const;
  Version                                                GuessIncomingPlayerVersion(const CConnection* user, const CIncomingJoinRequest& joinRequest, std::shared_ptr<const CRealm> fromRealm) const;
  bool                                                   GetIsAutoStartDue() const;
  std::string                                            GetAutoStartText() const;
  std::string                                            GetReadyStatusText() const;
  std::string                                            GetCmdToken() const;
  std::shared_ptr<CTCPServer>                            GetSocket() const { return m_Socket; };
  UserList&                                              GetUsers() { return m_Users; }

  uint16_t                                               CalcHostPortFromType(const uint8_t type) const;
  uint16_t                                               GetHostPortFromType(const uint8_t type) const;
  uint16_t                                               GetHostPortFromTargetAddress(const sockaddr_storage* address) const;
  inline bool                                            GetIsRealmExcluded(const std::string& hostName) const { return m_RealmsExcluded.find(hostName) != m_RealmsExcluded.end() ; }
  uint8_t                                                CalcActiveReconnectProtocols() const;
  std::string                                            GetActiveReconnectProtocolsDetails() const;
  bool                                                   CalcAnyUsingGProxy() const;
  bool                                                   CalcAnyUsingGProxyLegacy() const;
  PlayersReadyMode                                       GetPlayersReadyMode() const;

  inline void                                            SetExiting(bool nExiting) { m_Exiting = nExiting; }
  inline void                                            SetMapSiteURL(const std::string& nMapSiteURL) { m_MapSiteURL = nMapSiteURL; }
  inline void                                            SetChatOnly() { m_ChatOnly = true; }
  void                                                   SetUDPEnabled(bool nEnabled);
  bool                                                   GetHasDesyncHandler() const;
  bool                                                   GetAllowsDesync() const;
  OnIPFloodHandler                                       GetIPFloodHandler() const;
  bool                                                   GetAllowsIPFlood() const;
  void                                                   UpdateReadyCounters();
  bool                                                   GetCanDropOwnerMissing() const;
  void                                                   ResetDropVotes();
  void                                                   ResetOwnerSeen();
  [[nodiscard]] inline bool                              GetIsGameDiscoveryActive() const { return m_GameDiscoveryActive; }
  inline void                                            SetGameDiscoveryActive(const bool nActive = true) { m_GameDiscoveryActive = nActive; }
  inline void                                            UpdateGameDiscovery() { m_GameDiscoveryInfoChanged = GAME_DISCOVERY_CHANGED_MAJOR; }

  inline int64_t                                         GetCreationTime() const { return m_CreationTime; }
  [[nodiscard]] uint32_t                                 GetUptime() const;

  // processing functions

  uint32_t                                               SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const;
  void                                                   UpdateJoinable();
  bool                                                   UpdateLobby();
  void                                                   UpdateLoading();
  void                                                   UpdateLoaded();
  bool                                                   Update(fd_set* fd, fd_set* send_fd);
  void                                                   UpdatePost(fd_set* send_fd) const;
  void                                                   CheckLobbyTimeouts();
  void                                                   RunActionsScheduler();
  void                                                   RunActionsSchedulerInner(const int64_t newLatency, const uint8_t maxNewEqualizerOffset, const int64_t oldLatency, const uint8_t maxOldEqualizerOffset, const int64_t actionLateBy);

  // logging
  void                                                   LogApp(const std::string& logText, const uint8_t logTargets) const;
  void                                                   Log(const std::string& logText);
  void                                                   Log(const std::string& logText, int64_t gameTicks);
  void                                                   LogRemote(const std::string& logText) const;
  void                                                   LogRemoteRaw(const std::string& logText) const;
  void                                                   UpdateLogs();
  void                                                   FlushLogs();
  void                                                   LogSlots();

  // generic functions to send packets to players

  void                                                   Send(CConnection* player, const std::vector<uint8_t>& data) const;
  void                                                   Send(uint8_t UID, const std::vector<uint8_t>& data) const;
  void                                                   SendMulti(const std::vector<uint8_t>& UIDs, const std::vector<uint8_t>& data) const;
  void                                                   SendAsChat(CConnection* player, const std::vector<uint8_t>& data) const;
  void                                                   SendAll(const std::vector<uint8_t>& data) const;
  bool                                                   SendAllAsChat(const std::vector<uint8_t>& data) const;
  bool                                                   SendObserversAsChat(const std::vector<uint8_t>& data) const;
 

  // functions to send packets to players

  void                                                   SendChat(uint8_t fromUID, GameUser::CGameUser* user, std::string_view message, const LogLevelExtra logLevel = LogLevelExtra::kInfo) const;
  void                                                   SendChat(uint8_t fromUID, uint8_t toUID, std::string_view message, const LogLevelExtra logLevel = LogLevelExtra::kInfo) const;
  void                                                   SendChat(GameUser::CGameUser* user, std::string_view message, const LogLevelExtra logLevel = LogLevelExtra::kInfo) const;
  void                                                   SendChat(CAsyncObserver* spectator, std::string_view message, const LogLevelExtra logLevel = LogLevelExtra::kInfo) const;
  void                                                   SendChat(uint8_t toUID, std::string_view message, const LogLevelExtra logLevel = LogLevelExtra::kInfo) const;
  bool                                                   SendAllChat(uint8_t fromUID, std::string_view message) const;
  bool                                                   SendAllChat(std::string_view message) const;
  bool                                                   SendObserverChat(uint8_t fromUID, std::string_view message) const;
  bool                                                   SendObserverChat(std::string_view message) const;
  bool                                                   SendSpectatorChat(const CAsyncObserver* excludeSpectator, std::string_view prefix, std::string_view message) const;
  bool                                                   SendSpectatorChat(std::string_view prefix, std::string_view message) const;
  void                                                   SendAllSlotInfo();
  void                                                   SendVirtualHostPlayerInfo(CConnection* user) const;
  void                                                   SendFakeUsersInfo(CConnection* user) const;
  void                                                   SendJoinedPlayersInfo(CConnection* user) const;
  void                                                   SendMapAndVersionCheck(CConnection* user, const Version& gameVersion) const;
  void                                                   SendWelcomeMessage(GameUser::CGameUser* user) const;
  void                                                   SendOwnerCommandsHelp(std::string_view cmdToken, GameUser::CGameUser* user) const;
  void                                                   SendCommandsHelp(std::string_view cmdToken, GameUser::CGameUser* user, const bool isIntro) const;
  void                                                   QueueLeftMessage(GameUser::CGameUser* user) const;
  void                                                   SendLeftMessage(GameUser::CGameUser* user, const bool sendChat) const;
  void                                                   SendChatMessage(const GameUser::CGameUser* user, const CIncomingMessageOrSettingsView& chatMessage) const;

  void                                                   CheckActions();
  void                                                   PauseAPMTrainer();
  void                                                   ResumeAPMTrainer();
  void                                                   RestartAPMTrainer();

  uint8_t                                                GetNumInGameReadyUsers() const;
  void                                                   ResetInGameReadyUsers() const;

  void                                                   SendGProxyEmptyActions();
  void                                                   EventOutgoingAtomicAction(const uint8_t UID, std::string_view action);
  void                                                   SendAllActionsCallback();
  void                                                   SendAllActions();
  void                                                   SendAllAutoStart() const;

  inline bool                                            GetIsExpansion() const { return m_Config.m_GameIsExpansion; }
  inline const Version&                                  GetVersion() const { return m_Config.m_GameVersion.value(); }
  std::vector<uint8_t>                                   GetGameDiscoveryInfo(const Version& gameVersion, const uint16_t hostPort);
  std::vector<uint8_t>*                                  GetGameDiscoveryInfoTemplate();
  std::vector<uint8_t>                                   GetGameDiscoveryInfoTemplateInner(uint16_t* gameVersionOffset, uint16_t* dynamicInfoOffset) const;
  std::vector<uint8_t>                                   GetSlotInfo() const;
  std::vector<uint8_t>                                   GetHandicaps() const;
  std::vector<uint8_t>                                   GetFakeUsersLobbyInfo() const;
  std::vector<uint8_t>                                   GetFakeUsersLoadedInfo() const;
  std::vector<uint8_t>                                   GetJoinedPlayersInfo() const;

  void                                                   AnnounceDecreateToRealms();
  void                                                   AnnounceToAddress(std::string& address, const std::optional<Version>& customGameVersion);
  void                                                   ReplySearch(sockaddr_storage* address, CSocket* socket, const std::optional<Version>& customGameVersion);
  void                                                   SendGameDiscoveryInfo(const Version& gameVersion);
  void                                                   SendGameDiscoveryInfo();
  void                                                   SendGameDiscoveryInfoMDNS() const;
  void                                                   SendGameDiscoveryInfoVLAN(CGameSeeker* gameSeeker) const;
  void                                                   SendGameDiscoveryRefresh() const;
  void                                                   SendGameDiscoveryCreate(const Version& gameVersion) const;
  void                                                   SendGameDiscoveryCreate() const;
  void                                                   SendGameDiscoveryDecreate() const;

  // events
  // note: these are only called while iterating through the m_Potentials or m_Users std::vectors
  // therefore you can't modify those std::vectors and must use the player's m_DeleteMe member to flag for deletion

  void                      ChangeGameName(const std::string& nGameName);
  
  void                      EventUserDeleted(GameUser::CGameUser* user, fd_set* fd, fd_set* send_fd);
  void                      EventLobbyLastPlayerLeaves();
  void                      ReportAllPings() const;
  void                      SetLaggingPlayerAndUpdate(GameUser::CGameUser* user);
  void                      SetEveryoneLagging();
  std::pair<int64_t, int64_t> GetReconnectWaitTicks() const;
  void                      ReportRecoverableDisconnect(GameUser::CGameUser* user);
  void                      OnRecoverableDisconnect(GameUser::CGameUser* user);
  bool                      CheckUserBanned(CConnection* connection, const CIncomingJoinRequest& joinRequest, std::shared_ptr<CRealm> matchingRealm, std::string& hostName);
  bool                      CheckIPBanned(CConnection* connection, const CIncomingJoinRequest& joinRequest, std::shared_ptr<CRealm> matchingRealm, std::string& hostName);
  void                      EventUserDisconnectTimedOut(GameUser::CGameUser* user);
  void                      EventUserDisconnectSocketError(GameUser::CGameUser* user);
  void                      EventUserDisconnectConnectionClosed(GameUser::CGameUser* user);
  void                      EventUserDisconnectGameProtocolError(GameUser::CGameUser* user, bool canRecover);
  void                      EventUserDisconnectGameAbuse(GameUser::CGameUser* user);
  void                      EventUserKickGProxyExtendedTimeout(GameUser::CGameUser* user);
  void                      EventUserKickUnverified(GameUser::CGameUser* user);
  void                      EventUserKickHandleQueued(GameUser::CGameUser* user);
  void                      EventUserAfterDisconnect(GameUser::CGameUser* user, bool fromOpen);
  void                      EventUserCheckStatus(GameUser::CGameUser* user);
  JoinRequestResult         EventRequestJoin(CConnection* connection, const CIncomingJoinRequest& joinRequest);
  void                      EventBeforeJoin(CConnection* connection);
  void                      EventUserLeft(GameUser::CGameUser* user, const uint32_t clientReason);
  void                      EventUserLoaded(GameUser::CGameUser* user);
  bool                      EventUserIncomingAction(GameUser::CGameUser* user, CIncomingAction& action);
  void                      EventUserKeepAlive(GameUser::CGameUser* user);
  void                      EventChatTrigger(GameUser::CGameUser* user, std::string_view message, const uint32_t first, const uint32_t second);
  void                      EventUserChatOrPlayerSettings(GameUser::CGameUser* user, const CIncomingMessageOrSettingsView& incomingChatMessage);
  void                      EventUserChat(GameUser::CGameUser* user, const CIncomingMessageOrSettingsView& incomingChatMessage);
  void                      EventUserRequestTeam(GameUser::CGameUser* user, uint8_t team);
  void                      EventUserRequestColor(GameUser::CGameUser* user, uint8_t colour);
  void                      EventUserRequestRace(GameUser::CGameUser* user, uint8_t race);
  void                      EventUserRequestHandicap(GameUser::CGameUser* user, uint8_t handicap);
  void                      EventUserDropRequest(GameUser::CGameUser* user);
  void                      EventUserMapSize(GameUser::CGameUser* user, const CIncomingMapFileSize& mapSize);
  void                      EventUserPongToHost(GameUser::CGameUser* user);
  void                      EventUserMapReady(GameUser::CGameUser* user);

  // these events are called outside of any iterations

  void                      EventGameStartedLoading();
  void                      EventGameBeforeLoaded();
  void                      EventGameLoaded();
  void                      HandleGameLoadedStats();
  void                      ReleaseMapBusyTimedLock() const;
  void                      StartGameOverTimer(bool isMMD = false);
  void                      ClearActions();
  void                      Reset();
  bool                      GetIsRemakeable();
  void                      RemakeStart();
  void                      Remake();

  void                      AddProvisionalBannableUser(const GameUser::CGameUser* user);
  void                      ClearBannableUsers();
  void                      UpdateBannableUsers();

  void                      UpdateUserMapProgression(CAsyncObserver* user, const double current, const double expected);
  void                      UpdateUserMapProgression(GameUser::CGameUser* user, const double current, const double expected);
  bool                      ResolvePlayerObfuscation() const;
  void                      RunPlayerObfuscation();
  bool                      CheckSmartCommands(GameUser::CGameUser* user, std::string_view message, const uint8_t activeCmd, CCommandConfig* nConfig);

  // other functions

  uint8_t                   GetSIDFromUID(uint8_t UID) const;
  GameUser::CGameUser*      GetUserFromUID(uint8_t UID) const;
  GameUser::CGameUser*      GetUserFromSID(uint8_t SID) const;
  std::string               GetUserNameFromUID(uint8_t UID) const;
  std::string               GetUserNameFromSID(uint8_t SID) const;
  GameUser::CGameUser*      GetUserFromName(std::string name, bool sensitive) const;
  template <CaseSensitive sensitive>
  GameUser::CGameUser*      GetUserFromName(std::string_view name) const;
  GameUser::CGameUser*      GetOwner() const;
  bool                      HasOwnerSet() const;
  bool                      HasOwnerInGame() const;
  GameUserSearchResult      GetUserFromNamePartial(const std::string& name) const;
  GameUserSearchResult      GetUserFromDisplayNamePartial(const std::string& name) const;
  BannableUserSearchResult  GetBannableFromNamePartial(const std::string& name) const;
  GameUser::CGameUser*      GetUserFromColor(uint8_t colour) const;
  uint8_t                   GetColorFromUID(uint8_t UID) const;
  uint8_t                   GetNewUID() const;
  uint8_t                   GetNewTeam() const;
  uint8_t                   GetNewColor() const;
  uint8_t                   GetNewPseudonymUID() const;
  bool                      CheckActorRequirements(const GameUser::CGameUser* user, const uint8_t reqs) const;
  uint8_t                   SimulateActionUID(const uint8_t actionType, GameUser::CGameUser* user, const bool isDisconnect, const uint8_t actorMask);
  void                      ResolveVirtualUsers();
  void                      ResolveBuffering();
  bool                      GetHasAnyActiveTeam() const;
  bool                      GetHasAnyUser() const;
  bool                      GetIsRealPlayerSlot(const uint8_t SID) const;
  bool                      GetIsVirtualPlayerSlot(const uint8_t SID) const;
  bool                      GetHasAnotherPlayer(const uint8_t ExceptSID) const;
  bool                      CheckIPFlood(std::string_view joinName, const sockaddr_storage* sourceAddress) const;
  std::vector<uint8_t>      GetAllChatUIDs() const;
  std::vector<uint8_t>      GetObserverChatUIDs() const;
  std::vector<uint8_t>      GetFilteredChatUIDs(uint8_t fromUID, const std::vector<uint8_t>& toUIDs) const;
  std::vector<uint8_t>      GetFilteredChatObserverUIDs(uint8_t fromUID, const std::vector<uint8_t>& toUIDs) const;
  uint8_t                   GetPublicHostUID() const;
  uint8_t                   GetHiddenHostUID() const;
  uint8_t                   GetHostUID() const;
  uint8_t                   GetEmptySID(bool reserved) const;
  uint8_t                   GetEmptyTeamSID(const uint8_t team) const;
  uint8_t                   GetEmptyPlayerSID() const;
  uint8_t                   GetEmptyObserverSID() const;
  uint8_t                   GetVirtualUserTeamSID(const uint8_t team) const;
  uint8_t                   GetPassiveVirtualUserTeamSID(const uint8_t team) const;
  inline bool               GetHMCEnabled() const { return m_HMCEnabled; }
  void                      SendIncomingPlayerInfo(GameUser::CGameUser* user) const;
  uint8_t                   NextSendMap(CConnection* connection, const uint8_t UID, MapTransfer& mapTransfer);
  GameUser::CGameUser*                JoinPlayer(CConnection* connection, const CIncomingJoinRequest& joinRequest, const uint8_t SID, const uint8_t UID, const uint8_t HostCounterID, const std::string JoinedRealm, const bool IsReserved, const bool IsUnverifiedAdmin);  
  bool                      CreateVirtualHost();
  bool                      DeleteVirtualHost();
  bool                      GetHasPvPGNPlayers() const;
  bool                      GetIsSlotReservedForSystemVirtualUser(const uint8_t SID) const;
  bool                      GetIsSlotAssignedToSystemVirtualUser(const uint8_t SID) const;

  // Observer features
  inline std::shared_ptr<GameHistory> GetGameHistory() { return m_GameHistory; }
  void                                JoinObserver(CConnection* connection, const CIncomingJoinRequest& joinRequest, std::shared_ptr<CRealm> fromRealm);
  void                                EventObserverMapSize(CAsyncObserver* connection, const CIncomingMapFileSize& mapSize);

  // Initialization
  void                                InitSlots();
  bool                                InitNet();
  void                                InitMDNS(GameDiscoveryInterface& interface);


  // Map transfer
  uint8_t                   CheckCanTransferMap(const CConnection* connection, std::shared_ptr<const CRealm> realm, const Version& version, const bool gotPermission);
  inline SharedByteArray    GetLoadedMapChunk() { return m_LoadedMapChunk; }
  void                      SetLoadedMapChunk(SharedByteArray nLoadedMapChunk) { m_LoadedMapChunk = nLoadedMapChunk; }
  void                      ClearLoadedMapChunk() { m_LoadedMapChunk.reset(); }
  FileChunkTransient        GetMapChunk(size_t start);

  // Slot manipulation

  CGameSlot* GetSlot(const uint8_t SID);
  const CGameSlot* InspectSlot(const uint8_t SID) const;
  bool SwapEmptyAllySlot(const uint8_t SID);
  bool SwapSlots(const uint8_t SID1, const uint8_t SID2);
  bool OpenSlot(const uint8_t SID, const bool kick);
  bool CanLockSlotForJoins(uint8_t SID);
  bool CloseSlot(const uint8_t SID, const bool kick);
  bool OpenSlot();
  bool CloseSlot();
  bool ComputerSlotInner(const uint8_t SID, const uint8_t skill, const bool ignoreLayout = false, const bool overrideComputers = false);
  bool ComputerSlot(const uint8_t SID, const uint8_t skill, bool kick);
  bool SetSlotColor(const uint8_t SID, const uint8_t colour, const bool force);
  bool SetSlotTeam(const uint8_t SID, const uint8_t team, const bool force);
  void SetSlotTeamAndColorAuto(const uint8_t SID);

  void OpenObserverSlots();
  void CloseObserverSlots();
  CGameVirtualUser* GetVirtualUserFromSID(const uint8_t SID);
  const CGameVirtualUser* InspectVirtualUserFromSID(const uint8_t SID) const;
  CGameVirtualUser* CreateFakeUserInner(const uint8_t SID, const uint8_t UID, const std::string& name, bool asObserver = false);
  bool CreateFakeUser(const std::optional<std::string> playerName);
  bool CreateFakePlayer(const std::optional<std::string> playerName);
  bool CreateFakeObserver(const std::optional<std::string> playerName);
  bool DeleteFakeUser(uint8_t SID);
  void UnrefFakeUser(uint8_t SID);
  const CGameVirtualUser* InspectVirtualUserFromRef(const CGameVirtualUserReference* ref) const;

  uint8_t FakeAllSlots();
  void DeleteFakeUsersLobby();
  void DeleteFakeUsersLoaded();
  void OpenAllSlots();
  uint8_t GetFirstCloseableSlot();
  bool CloseAllTeamSlots(const uint8_t team);
  bool CloseAllTeamSlots(const std::bitset<MAX_SLOTS_MODERN> occupiedTeams);
  bool CloseAllSlots();
  bool ComputerNSlots(const uint8_t expectedCount, const uint8_t skill, const bool ignoreLayout = false, const bool overrideComputers = false);
  bool ComputerAllSlots(const uint8_t skill);
  void ShuffleSlots();

  void ReportSpoofed(const std::string& server, GameUser::CGameUser* user);
  void AddToRealmVerified(const std::string& server, GameUser::CGameUser* user, bool sendMessage);
  void AddToReserved(const std::string& name);
  void RemoveFromReserved(const std::string& name);
  bool ReserveAll();
  bool RemoveAllReserved();
  bool MatchOwnerName(const std::string& name) const;
  uint8_t GetReservedIndex(const std::string& name) const;

  std::string GetBannableIP(const std::string& name, const std::string& hostName) const;
  bool GetIsScopeBanned(const std::string& name, const std::string& hostName, const std::string& addressLiteral) const;
  bool CheckScopeBanned(const std::string& name, const std::string& hostName, const std::string& addressLiteral);
  bool AddScopeBan(const std::string& name, const std::string& hostName, const std::string& addressLiteral);
  bool RemoveScopeBan(const std::string& name, const std::string& hostName);

  std::vector<size_t> GetPlayersFramesBehind() const;
  std::vector<size_t> GetUsersFramesBehind() const;
  UserList GetLaggingUsers() const;
  uint8_t CountLaggingPlayers() const;
  UserList CalculateNewLaggingPlayers() const;
  void RemoveFromLagScreens(GameUser::CGameUser* user) const;
  void ResetLagScreen();
  [[nodiscard]] std::pair<double, double> GetLagDetectionRangeMilliSeconds(bool isObserver);
  bool TrySetupLatency(uint16_t latency, std::optional<RangeSizeType> playerSyncRange, std::optional<RangeSizeType> observerSyncRange);
  void SetupLatency(uint16_t latency, std::optional<RangeSizeType> playerSyncRange, std::optional<RangeSizeType> observerSyncRange);
  void ResetLatency();
  void NormalizeSyncCounters() const;
  bool GetIsReserved(const std::string& name) const;
  bool GetIsProxyReconnectable() const;
  bool GetIsProxyReconnectableLong() const;
  bool IsDownloading() const;
  void UncacheOwner();
  void SetOwner(std::string_view name, std::string_view realm);
  void ReleaseOwner();
  void ResetDraft();
  void ResetTeams(const bool alsoCaptains);
  void ResetSync();
  void CountKickVotes();
  bool GetCanStartGracefulCountDown() const;
  void StartCountDown(bool fromUser, bool force);
  void StartCountDownFast(bool force);
  void StopCountDown();
  bool SendEveryoneElseLeftAndDisconnect(const std::string& reason) const;
  bool TrySendFakeUsersShareControl();
  void ShowPlayerNamesGameStartLoading();
  void ShowPlayerNamesInGame();
  bool StopPlayers(const std::string& reason);
  void StopLagger(GameUser::CGameUser* user, const std::string& reason);
  void StopLaggers(const std::string& reason);
  void StopDesynchronized(const std::string& reason);
  void StopLoadPending(const std::string& reason);
  void ResetDropVotes() const;
  std::string GetSaveFileName(const uint8_t UID) const;
  bool Save(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect);
  void SaveEnded(const uint8_t exceptUID, CQueuedActionsFrame& actionFrame);
  bool Pause(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect);
  bool Resume(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect);
  bool SendMiniMapSignal(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect, const float x, const float y, const uint32_t duration);

  bool Trade(const uint8_t fromUID, const uint8_t SID, CQueuedActionsFrame& actionFrame, const uint32_t gold, const uint32_t lumber);
  bool Trade(GameUser::CGameUser* user, const uint8_t SID, CQueuedActionsFrame& actionFrame, const bool isDisconnect, const uint32_t gold, const uint32_t lumber);
  bool Trade(GameUser::CGameUser* user, const uint8_t SID, const bool isDisconnect, const uint32_t gold, const uint32_t lumber);
  bool ShareUnits(const uint8_t fromUID, const uint8_t SID, CQueuedActionsFrame& actionFrame);
  bool ShareUnits(GameUser::CGameUser* fromUser, const uint8_t SID, CQueuedActionsFrame& actionFrame, const bool isDisconnect);
  bool ShareUnits(GameUser::CGameUser* fromUser, const uint8_t SID, const bool isDisconnect);

  void TryActionsOnDisconnect(GameUser::CGameUser* user, const bool isVoluntary);
  bool TrySaveOnDisconnect(GameUser::CGameUser* user, const bool isVoluntary);
  bool TryShareUnitsOnDisconnect(GameUser::CGameUser* user, const bool isVoluntary);

  bool Save(GameUser::CGameUser* user, const bool isDisconnect);
  void SaveEnded(const uint8_t exceptUID);
  bool Pause(GameUser::CGameUser* user, const bool isDisconnect);
  bool Resume(GameUser::CGameUser* user, const bool isDisconnect);
  bool SendMiniMapSignal(GameUser::CGameUser* user, const bool isDisconnect, const float x, const float y, const uint32_t duration);

  inline bool GetIsVerbose() { return m_Verbose; }
  bool SendChatTrigger(const uint8_t UID, const std::string& message, const uint32_t firstValue, const uint32_t secondValue);
  bool SendChatTriggerBytes(const uint8_t UID, const std::string& message, const std::array<uint8_t, 8>& triggerBytes);
  bool SendChatTriggerSymmetric(const uint8_t UID, const std::string& message, const uint16_t identifier);
  bool GetIsCheckJoinable() const;
  void SetIsCheckJoinable(const bool nCheckIsJoinable);
  inline bool GetSentPriorityWhois() const { return m_SentPriorityWhois; }
  inline bool GetRemaking() const { return m_Remaking; }
  inline bool GetRemade() const { return m_Remade; }
  inline uint8_t GetSaveOnLeave() const { return m_SaveOnLeave; }
  inline GameResultSourceSelect GetGameResultSourceOfTruth() const { return m_GameResultSourceOfTruth; }
  bool GetHasReferees() const;
  inline bool GetUsesCustomReferees() const { return m_UsesCustomReferees; }
  bool GetIsSupportedGameVersion(const Version& nVersion) const;
  inline void SetSentPriorityWhois(const bool nValue) { m_SentPriorityWhois = nValue; }
  inline void SetCheckReservation(const bool nValue) { m_CheckReservation = nValue; }
  inline void SetUsesCustomReferees(const bool nValue) { m_UsesCustomReferees = nValue; }
  inline void SetSupportedGameVersion(const Version& nVersion);
  inline void SetSaveOnLeave(const uint8_t nValue) { m_SaveOnLeave = nValue; }

  inline void SetIsReplaceable(const bool nValue = true) { m_Replaceable = nValue; }
  inline void SetIsBeingReplaced(const bool nValue = true) { m_Replacing = nValue; }
  inline void SetIsPublicStartable(const bool nValue = true) { m_PublicStart = nValue; }
  inline void SetLocked(const bool nValue = true) { m_Locked = nValue; }
  inline void SetMuteAll(const bool nValue = true) { m_MuteAll = nValue; }

  bool GetIsAutoVirtualPlayers() const { return m_IsAutoVirtualPlayers; }
  void SetAutoVirtualPlayers(const bool nEnableVirtualHostPlayer) { m_IsAutoVirtualPlayers = nEnableVirtualHostPlayer; }
  void RemoveCreator();
  inline void NextCreationCounter() { ++m_CreationCounter; }

  uint8_t GetNumEnabledTeamSlots(const uint8_t team) const;
  std::vector<uint8_t> GetNumFixedComputersByTeam() const;
  std::vector<uint8_t> GetPotentialTeamSizes() const;
  std::pair<uint8_t, uint8_t> GetLargestPotentialTeam() const;
  std::pair<uint8_t, uint8_t> GetSmallestPotentialTeam(const uint8_t minSize, const uint8_t exceptTeam) const;
  std::vector<uint8_t> GetActiveTeamSizes() const;
  uint8_t GetSelectableTeamSlotFront(const uint8_t team, const uint8_t endOccupiedSID,const uint8_t endOpenSID, const bool force) const;
  uint8_t GetSelectableTeamSlotBack(const uint8_t team, const uint8_t endOccupiedSID,const uint8_t endOpenSID, const bool force) const;
  uint8_t GetSelectableTeamSlotBackExceptHumanLike(const uint8_t team, const uint8_t endOccupiedSID,const uint8_t endOpenSID, const bool force) const;
  uint8_t GetSelectableTeamSlotBackExceptComputer(const uint8_t team, const uint8_t endOccupiedSID,const uint8_t endOpenSID, const bool force) const;
  bool FindHumanVsAITeams(const uint8_t humanCount, const uint8_t computerCount, std::pair<uint8_t, uint8_t>& teams) const;
  uint8_t GetOneVsAllTeamAll() const;
  uint8_t GetOneVsAllTeamOne(const uint8_t teamAll) const;

  // These are the main game modes
  inline void SetDraftMode(const bool nIsDraftMode) {
    m_IsDraftMode = nIsDraftMode;
    if (nIsDraftMode) {
      m_CustomLayout |= CUSTOM_LAYOUT_DRAFT;
    } else {
      m_CustomLayout &= ~CUSTOM_LAYOUT_DRAFT;
    }
  }

  void ResetLayout(const bool quiet);
  void ResetLayoutIfNotMatching();
  bool SetLayoutFFA();
  bool SetLayoutOneVsAll(const GameUser::CGameUser* user);
  bool SetLayoutTwoTeams();
  bool SetLayoutHumansVsAI(const uint8_t humanTeam, const uint8_t computerTeam);
  bool SetLayoutCompact();

  [[nodiscard]] GamePlayerResult ResolveUndecidedComputerOrVirtualAuto(CGameController* controllerData, const GameResultConstraints& constraints, const GameResultTeamAnalysis& teamAnalysis);
  [[nodiscard]] GamePlayerResult ResolveUndecidedController(CGameController* controllerData, const GameResultConstraints& constraints, const GameResultTeamAnalysis& teamAnalysis);
  [[nodiscard]] GameResultTeamAnalysis GetGameResultTeamAnalysis() const;
  [[nodiscard]] std::optional<GameResults> GetGameResultsMMD();
  [[nodiscard]] std::optional<GameResults> GetGameResultsLeaveCode();
  [[nodiscard]] GameResultSource TryConfirmResults(std::optional<GameResults>, GameResultSource resultsSource);
  GameResultSource RunGameResults();
  [[nodiscard]] bool GetIsAPrioriCompatibleWithGameResultsConstraints(std::string& reason) const;
  [[nodiscard]] bool CheckGameResults(const GameResults& gameResults) const;

  void                      StoreGameControllers();
  CGameController*          GetGameControllerFromColor(uint8_t colour) const;
  CGameController*          GetGameControllerFromSID(uint8_t SID) const;
  CGameController*          GetGameControllerFromUID(uint8_t UID) const;

  bool InitStats();
  bool InitHMC();
  bool EventGameCacheInteger(const uint8_t UID, std::string_view action);
  bool UpdateStatsQueue() const;
  void FlushStatsQueue() const;
  void TrySaveStats() const;
  void DestroyStats();
  void DestroyHMC();

  void                      RunHCLEncoding();
  bool                      SendHMC(const std::string& message);
  bool                      CreateHMCPlayer();
  uint8_t                   GetHMCSID() const;
  uint8_t                   GetAHCLSID() const;
};

#endif // AURA_GAME_H_
