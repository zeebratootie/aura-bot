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

#ifndef AURA_AURA_H_
#define AURA_AURA_H_

#include "includes.h"
#include "config/config.h"
#include "config/config_bot.h"
#include "config/config_realm.h"
#include "config/config_game.h"
#include "cli.h"
#include "command.h"
#include "game_setup.h"
#include "locations.h"
#include "net.h"
#include "sampler.h"
#include "util.h"
#include "integration/irc.h"
#include "integration/discord.h"

#include <sha1/sha1.h>
#include <random>
#include <filesystem>
#include <thread_pool/BS_thread_pool.hpp>

#ifdef _WIN32
#pragma once
#include <windows.h>
#endif

#define AURA_VERSION "11.3.1-dev"
#define AURA_APP_NAME "Aura 11.3.1-dev"
#define AURA_REPOSITORY_URL "https://gitlab.com/ivojulca/aura-bot"
#define AURA_ISSUES_URL "https://gitlab.com/ivojulca/aura-bot/-/issues"

//
// AppMetrics
//

struct AppMetrics
{
  UniformlySampledTimedData lobbies;
  UniformlySampledTimedData games;

  AppMetrics(int lobbyRate, size_t lobbyCapacity, int gameRate, size_t gameCapacity)
   : lobbies(UniformlySampledTimedData(lobbyRate, lobbyCapacity)),
     games(UniformlySampledTimedData(gameRate, gameCapacity))
  {
  }

  ~AppMetrics() = default;
};

//
// CAura
//

class CAura
{
public:
  bool                                               m_ScriptsExtracted;           // indicates if there's lacking configuration info so we can quit
  bool                                               m_Exiting;                    // set to true to force aura to shutdown next update (used by SignalCatcher)
  bool                                               m_ExitingSoon;                // set to true to let aura gracefully stop all services and network traffic, and shutdown once done
  bool                                               m_Ready;                      // indicates if there's lacking configuration info so we can quit
  bool                                               m_IsFastPolling;
  bool                                               m_AutoReHosted;               // whether our autorehost game setup has been used for one of the active lobbies
  bool                                               m_MetaDataNeedsUpdate;

  LogLevel                                           m_LogLevel;
  int64_t                                            m_LoopTicks;
  int64_t                                            m_LoopTime;
  int64_t                                            m_LastPerformanceWarningTicks;
  int64_t                                            m_StartedFastPollingTicks;
  std::optional<Version>                             m_GameDataVersion;
  bool                                               m_SupportsModernSlots;
  fd_set                                             m_ReadFDs;
  fd_set                                             m_SendFDs;

  OptionalDependencyMode                             m_MDNSDependency;
  OptionalDependencyMode                             m_DPPDependency;
  uint8_t                                            m_FoundDeps;

  uint32_t                                           m_LastServerID;
  uint32_t                                           m_HostCounter;                // the current host counter (a unique number to identify a game, incremented each time a game is created)
  uint32_t                                           m_ReplacingLobbiesCounter;
  uint64_t                                           m_HistoryGameID;
  size_t                                             m_MaxGameNameSize;

  BS::light_thread_pool                              m_ThreadPool;
  CDataBaseConfig*                                   m_DataBaseConfig;
  CGameConfig*                                       m_GameDefaultConfig;  
  CRealmConfig*                                      m_RealmDefaultConfig;
  CCommandConfig*                                    m_CommandDefaultConfig;

  CAuraDB*                                           m_DB;                         // database
  std::shared_ptr<CGameSetup>                        m_GameSetup;                  // the currently loaded map
  std::shared_ptr<CGameSetup>                        m_AutoRehostGameSetup;        // game setup to be rehosted whenever free

  std::shared_ptr<CCommandContext>                   m_ReloadContext;
  std::shared_ptr<CCommandContext>                   m_SudoContext;

  std::optional<int64_t>                             m_LastGameHostedTicks;
  std::optional<int64_t>                             m_LastGameAutoHostedTicks;

  std::string                                        m_SudoAuthTarget;
  std::string                                        m_SudoExecCommand;

  std::string                                        m_Version;                    // Aura version string
  std::string                                        m_RepositoryURL;              // Aura repository URL
  std::string                                        m_IssuesURL;                  // Aura issues URL

  std::vector<std::weak_ptr<CCommandContext>>        m_ActiveContexts;             // declare before command sources, to ensure m_ActiveContexts is destroyed after them

  CSHA1                                              m_SHA;                        // for calculating SHA1's
  CDiscord                                           m_Discord;                    // Discord client
  CIRC                                               m_IRC;                        // IRC client
  CNet                                               m_Net;                        // network manager
  CBotConfig                                         m_Config;
  std::filesystem::path                              m_ConfigPath;
  std::filesystem::path                              m_GameInstallPath;

  std::queue<GenericAppAction>                       m_PendingActions;
  std::vector<std::shared_ptr<CRealm>>               m_Realms;                     // all our battle.net clients (there can be more than one)
  std::vector<std::shared_ptr<CGame>>                m_StartedGames;               // all games after they have started
  std::vector<std::shared_ptr<CGame>>                m_Lobbies;                    // all games before they are started
  std::vector<std::shared_ptr<CGame>>                m_LobbiesPending;             // vector for just-created lobbies before they get into m_Lobbies
  std::vector<std::weak_ptr<CGame>>                  m_JoinInProgressGames;        // started games that can be joined in-progress (either as observer or player)

  std::map<std::filesystem::path, std::string>       m_CFGCacheNamesByMapNames;
  std::map<std::filesystem::path, TimedUint16>       m_MapFilesTimedBusyLocks;
  std::map<std::filesystem::path, FileChunkCached>   m_CachedFileContents;
  std::map<std::string, std::string>                 m_LastMapIdentifiersFromSuggestions;

  std::vector<std::string>                           m_RealmsIdentifiers;
  std::map<uint8_t, std::weak_ptr<CRealm>>           m_RealmsByHostCounter;
  std::map<std::string, std::weak_ptr<CRealm>>       m_RealmsByInputID;

#ifdef PROFILING
  AppMetrics                                         m_PerfMetrics;
#endif

  explicit CAura(CConfig& CFG, const CCLI& nCLI);
  ~CAura();
  CAura(CAura&) = delete;

  [[nodiscard]] std::vector<Version> GetSupportedVersionsCrossPlayRangeHeads() const;
  [[nodiscard]] std::shared_ptr<CGame> GetMostRecentLobby(bool allowPending = false) const;
  [[nodiscard]] std::shared_ptr<CGame> GetMostRecentLobbyFromCreator(const std::string& fromName) const;
  [[nodiscard]] std::shared_ptr<CGame> GetLobbyByHostCounter(uint32_t hostCounter) const;
  [[nodiscard]] std::shared_ptr<CGame> GetLobbyByHostCounterExact(uint32_t hostCounter) const;
  [[nodiscard]] std::shared_ptr<CGame> GetLobbyOrObservableByHostCounter(uint32_t hostCounter) const;
  [[nodiscard]] std::shared_ptr<CGame> GetLobbyOrObservableByHostCounterExact(uint32_t hostCounter) const;
  [[nodiscard]] std::shared_ptr<CGame> GetGameByIdentifier(const uint64_t gameIdentifier) const;
  [[nodiscard]] std::shared_ptr<CGame> GetGameByString(const std::string& targetGame) const;

  [[nodiscard]] std::shared_ptr<CRealm> GetRealmByInputId(const std::string& inputId) const;
  [[nodiscard]] std::shared_ptr<CRealm> GetRealmByHostCounter(const uint8_t hostCounter) const;
  [[nodiscard]] std::shared_ptr<CRealm> GetRealmByHostName(const std::string& hostName) const;
  [[nodiscard]] ServiceType FindServiceFromHostName(const std::string& hostName, void*& location) const;

  [[nodiscard]] std::vector<std::shared_ptr<CGame>> GetAllGames() const;
  [[nodiscard]] std::vector<std::shared_ptr<CGame>> GetJoinableGames() const;

  [[nodiscard]] bool MergePendingLobbies();
  void TrackGameJoinInProgress(std::shared_ptr<CGame> game);
  void UntrackGameJoinInProgress(std::shared_ptr<CGame> game);

  bool QueueConfigReload(std::shared_ptr<CCommandContext> nCtx);

  // identifier generators

  uint32_t NextHostCounter();
  uint64_t NextHistoryGameID();
  uint32_t NextServerID();

  [[nodiscard]] std::string GetSudoAuthTarget(const std::string& target);

  // processing functions

  [[nodiscard]] AppActionStatus HandleAction(const AppAction& action);
  [[nodiscard]] AppActionStatus HandleDeferredCommandContext(const LazyCommandContext& lazyCtx);
  [[nodiscard]] AppActionStatus HandleGenericAction(const GenericAppAction& genAction);
  [[nodiscard]] int64_t GetSelectBlockTime() const;
  bool Update();
  void AwaitSettled();
  [[nodiscard]] inline bool GetReady() const { return m_Ready; }

  [[nodiscard]] bool GetIsSupportedGameVersion(const Version& version) const;
  [[nodiscard]] bool GetNewGameIsInQuota() const;
  [[nodiscard]] bool GetNewGameIsInQuotaReplace() const;
  [[nodiscard]] bool GetNewGameIsInQuotaConservative() const;
  [[nodiscard]] bool GetNewGameIsInQuotaAutoReHost() const;
  bool CreateGame(std::shared_ptr<CGameSetup> gameSetup);
  [[nodiscard]] bool GetIsAutoHostThrottled() const;

  [[nodiscard]] inline bool GetIsAdvertisingGames() { return !m_Lobbies.empty() || !m_JoinInProgressGames.empty(); }
  [[nodiscard]] inline bool GetHasGames() { return !m_StartedGames.empty() || !m_Lobbies.empty(); }

  [[nodiscard]] inline int64_t GetLoopTicks() const { return m_LoopTicks; }
  [[nodiscard]] inline int64_t GetLoopTime() const { return m_LoopTime; }
  [[nodiscard]] inline bool GetTicksIsAfter(int64_t referenceTicks) const { return referenceTicks <= m_LoopTicks; }
  [[nodiscard]] inline bool GetTicksIsAfterDelay(int64_t referenceTicks, int64_t delayTicks) const { return referenceTicks + delayTicks <= m_LoopTicks; }
  [[nodiscard]] inline bool GetTicksIsFirstOrAfterDelay(std::optional<int64_t> referenceTicks, int64_t delayTicks) const { return !referenceTicks.has_value() || referenceTicks.value() + delayTicks <= m_LoopTicks; }
  [[nodiscard]] inline bool GetTimeIsAfter(int64_t referenceTime) const { return referenceTime <= m_LoopTime; }
  [[nodiscard]] inline bool GetTimeIsAfterDelay(int64_t referenceTime, int64_t delayTime) const { return referenceTime + delayTime <= m_LoopTime; }
  [[nodiscard]] inline bool GetTimeIsFirstOrAfterDelay(std::optional<int64_t> referenceTime, int64_t delayTime) const { return !referenceTime.has_value() || referenceTime.value() + delayTime <= m_LoopTime; }

  // events

  void EventBNETGameRefreshSuccess(std::shared_ptr<CRealm> realm);
  void EventBNETGameRefreshError(std::shared_ptr<CRealm> realm);
  void EventGameReset(std::shared_ptr<CGame> game);
  void EventGameDeleted(std::shared_ptr<CGame> game);
  void EventGameRemake(std::shared_ptr<CGame> game);
  void EventGameStarted(std::shared_ptr<CGame> game);
  void EventRealmDeleted(std::shared_ptr<CRealm> realm);

  // other functions

  [[nodiscard]] bool ReloadConfigs();
  void TryReloadConfigs();
  bool LoadDataBaseConfig(CConfig& CFG);
  bool LoadDefaultConfigs(CConfig& CFG, CNetConfig* netConfig);
  bool LoadAllConfigs(CConfig& CFG);
  void OnLoadConfigs();
  bool LoadBNETs(CConfig& CFG, std::bitset<120>& definedConfigs, bool strict = false);

  uint8_t ExtractScripts();
  bool CopyScripts();
  void CheckScripts();
  void ClearAutoRehost();

  bool LoadMapAliases();
  void LoadIPToCountryData(const CConfig& CFG);
  void InitContextMenu();
  void InitPathVariable();
  void InitSystem();
  void UpdateWindowTitle();
  void UpdateMetaData();

  [[nodiscard]] FileChunkTransient ReadFileChunkCacheable(const std::filesystem::path& filePath, const size_t start, const size_t end)/* noexcept*/;
  [[nodiscard]] SharedByteArray ReadFile(const std::filesystem::path& filePath, const size_t maxSize)/* noexcept*/;
  void UpdateCFGCacheEntries();

  void ClearStaleContexts();
  void ClearStaleFileChunks();
  
  inline bool MatchLogLevel(LogLevel logLevel) const { return logLevel <= m_LogLevel; } // 0: emergency ... 8: trace ... 10 trace3
  inline bool MatchLogLevel(LogLevelExtra logLevel) const { return (uint8_t)logLevel <= (uint8_t)(m_LogLevel); } // 0: emergency ... 8: trace ... 10 trace 3 ... 11 extra
#ifdef DEBUG
  inline bool GetIsLoggingTrace() const { return MatchLogLevel(LogLevel::kTrace); }
#else
  inline bool GetIsLoggingTrace() const { return false; }
#endif
  void LogPersistent(const std::string& logText);
  void LogRemoteFile(const std::string& logText);
  void LogPerformanceWarning(const TaskType taskType, const void* taskPtr, const int64_t frameDrift, const int64_t oldInterval, const int64_t adjustedInterval);
  void GracefulExit();
  bool CheckDependencies();
  bool CheckGracefulExit();
};

#endif // AURA_AURA_H_
