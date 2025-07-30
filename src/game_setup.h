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

#ifndef AURA_GAMESETUP_H_
#define AURA_GAMESETUP_H_

#include "includes.h"
#include "game_host.h"
#include "locations.h"
#include "realm.h"
#include "socket.h"

#include <atomic>
#include <filesystem>
#include <regex>

#ifndef DISABLE_CPR
#include <cpr/cpr.h>
#endif

inline std::vector<std::pair<std::string, int>> ExtractEpicWarMaps(const std::string &s, const int maxCount) {
  std::regex pattern(R"(<a href="/maps/(\d+)/"><b>([^<\n]+)</b></a>)");
  std::vector<std::pair<std::string, int>> output;

  std::sregex_iterator iter(s.begin(), s.end(), pattern);
  std::sregex_iterator end;

  int count = 0;
  while (iter != end && count < maxCount) {
    std::smatch match = *iter;
    output.emplace_back(std::make_pair(match[2], std::stoi(match[1])));
    ++iter;
    ++count;
  }

  return output;
}

//
// CGameExtraOptions
//

class CGameExtraOptions
{
public:
  CGameExtraOptions();
  ~CGameExtraOptions();

  std::optional<bool>                    m_TeamsLocked;
  std::optional<bool>                    m_TeamsTogether;
  std::optional<bool>                    m_AdvancedSharedUnitControl;
  std::optional<bool>                    m_RandomRaces;
  std::optional<bool>                    m_RandomHeroes;
  std::optional<GameVisibilityMode>      m_Visibility;
  std::optional<GameSpeed>               m_Speed;
  std::optional<GameObserversMode>       m_Observers;

  bool ParseMapObservers(const std::string& s);
  bool ParseMapVisibility(const std::string& s);
  bool ParseMapSpeed(const std::string& s);
  bool ParseMapRandomRaces(const std::string& s);
  bool ParseMapRandomHeroes(const std::string& s);

  void AcquireCLI(const CCLI* nCLI);
};

struct GameMirrorSetup
{
  bool                                                      m_IsMirror;
  std::variant<std::monostate, StringPair, GameHost>        m_Source;
  std::weak_ptr<CRealm>                                     m_SourceRealm;
  bool                                                      m_EnableProxy;

  GameMirrorSetup()
   : m_IsMirror(false),
     m_EnableProxy(false) 
  {
  }

  ~GameMirrorSetup() {
  }

  inline void Enable() { m_IsMirror = true; }
  bool SetRawSource(const GameHost& gameHost);
  bool SetRawSource(const sockaddr_storage& sourceAddress, const uint32_t gameIdentifier, const uint32_t entryKey);
  bool SetRawSource(const std::string& nInput);
  bool SetRegistrySource(const std::string& gameName, const std::string& registryName);
  bool SetRegistrySource(const StringPair& registry);
  void SetSourceRealm(std::shared_ptr<CRealm> sourceRealm) { m_SourceRealm = sourceRealm; }
  [[nodiscard]] const GameHost* GetRawSource() const;

  [[nodiscard]] inline bool GetIsEnabled() const { return m_IsMirror; }
  [[nodiscard]] inline bool GetIsProxyEnabled() const { return m_EnableProxy; }
  [[nodiscard]] inline std::shared_ptr<CRealm> GetSourceRealm() const { return m_SourceRealm.lock(); }
  [[nodiscard]] inline bool GetIsSearching() const { return m_Source.index() == 1; }
};

//
// CGameSetup
//

class CGameSetup : public std::enable_shared_from_this<CGameSetup>
{
public:
  CAura*                                          m_Aura;
  std::shared_ptr<CSaveGame>                      m_RestoredGame;
  std::shared_ptr<CMap>                           m_Map;
  std::shared_ptr<CCommandContext>                m_Ctx;

  std::string                                     m_Attribution;
  std::string                                     m_SearchRawTarget;
  uint8_t                                         m_SearchType;
  bool                                            m_AllowPaths;
  bool                                            m_StandardPaths;
  bool                                            m_LuckyMode;
  bool                                            m_Verbose;
  std::pair<std::string, std::string>             m_SearchTarget;

  bool                                            m_FoundSuggestions;
  bool                                            m_IsDownloadable;
  bool                                            m_IsStepDownloading;
  bool                                            m_IsStepDownloaded;
  std::string                                     m_BaseDownloadFileName;
  std::string                                     m_MapDownloadUri;
  uint32_t                                        m_MapDownloadSize;
  std::string                                     m_MapSiteUri;
  std::filesystem::path                           m_DownloadFilePath;
  std::ofstream*                                  m_DownloadFileStream;
#ifndef DISABLE_CPR
  std::future<uint32_t>                           m_DownloadFuture;
#endif
  int32_t                                         m_DownloadTimeout;
  int32_t                                         m_SuggestionsTimeout;
  std::optional<int64_t>                          m_ActiveTicks;
  std::string                                     m_ErrorMessage;
  uint8_t                                         m_AsyncStep;

  bool                                            m_IsMapDownloaded;

  std::filesystem::path                           m_SaveFile;

  std::string                                     m_Name;
  std::string                                     m_BaseName;
  bool                                            m_OwnerLess;
  std::pair<std::string, std::string>             m_Owner;
  std::optional<bool>                             m_ChecksReservation;
  std::vector<std::string>                        m_Reservations;
  std::optional<CrossPlayMode>                    m_CrossPlayMode;
  GameMirrorSetup                                 m_Mirror;
  uint8_t                                         m_RealmsDisplayMode;
  std::set<std::string>                           m_RealmsExcluded;

  bool                                            m_LobbyReplaceable;
  bool                                            m_LobbyAutoRehosted;
  uint16_t                                        m_CreationCounter;
  ServiceUser                                     m_Creator;

  std::optional<LobbyTimeoutMode>                 m_LobbyTimeoutMode;
  std::optional<LobbyOwnerTimeoutMode>            m_LobbyOwnerTimeoutMode;
  std::optional<GameLoadingTimeoutMode>           m_LoadingTimeoutMode;
  std::optional<GamePlayingTimeoutMode>           m_PlayingTimeoutMode;

  std::optional<uint32_t>                         m_LobbyTimeout;
  std::optional<uint32_t>                         m_LobbyOwnerTimeout;
  std::optional<uint32_t>                         m_LoadingTimeout;
  std::optional<uint32_t>                         m_PlayingTimeout;

  std::optional<uint8_t>                          m_PlayingTimeoutWarningShortCountDown;
  std::optional<uint32_t>                         m_PlayingTimeoutWarningShortInterval;
  std::optional<uint8_t>                          m_PlayingTimeoutWarningLargeCountDown;
  std::optional<uint32_t>                         m_PlayingTimeoutWarningLargeInterval;

  std::optional<bool>                             m_LobbyOwnerReleaseLANLeaver;

  std::optional<uint32_t>                         m_LobbyCountDownInterval;
  std::optional<uint32_t>                         m_LobbyCountDownStartValue;

  std::optional<bool>                             m_SaveGameAllowed;
  std::optional<bool>                             m_PauseGameAllowed;

  std::optional<uint8_t>                          m_AutoStartPlayers;
  std::optional<int64_t>                          m_AutoStartSeconds;
  std::optional<uint8_t>                          m_ReconnectionMode;
  std::optional<bool>                             m_EnableLobbyChat;
  std::optional<bool>                             m_EnableInGameChat;
  std::optional<OnIPFloodHandler>                 m_IPFloodHandler;
  std::optional<OnPlayerLeaveHandler>             m_LeaverHandler;
  std::optional<OnShareUnitsHandler>              m_ShareUnitsHandler;
  std::optional<OnUnsafeNameHandler>              m_UnsafeNameHandler;
  std::optional<OnRealmBroadcastErrorHandler>     m_BroadcastErrorHandler;

  std::optional<bool>                             m_EnableLagScreen;
  std::optional<uint16_t>                         m_LatencyAverage;
  std::optional<uint32_t>                         m_StartLagPlayersMinMs;
  std::optional<uint32_t>                         m_StopLagPlayersMaxMs;
  std::optional<uint32_t>                         m_StartLagObserversMinMs;
  std::optional<uint32_t>                         m_StopLagObserversMaxMs;
  std::optional<bool>                             m_LatencyEqualizerEnabled;
  std::optional<uint16_t>                         m_LatencyEqualizerMaxDelay;
  std::optional<std::string>                      m_HCL;
  std::optional<uint8_t>                          m_CustomLayout;
  std::optional<bool>                             m_CheckJoinable;
  std::optional<bool>                             m_NotifyJoins;
  std::optional<GameResultSourceSelect>           m_ResultSource;
  std::optional<bool>                             m_HideLobbyNames;
  std::optional<HideIGNMode>                      m_HideInGameNames;


  std::optional<bool>                             m_GameIsExpansion;
  std::optional<Version>                          m_GameVersion;
  std::optional<W3ModLocale>                      m_GameLocaleMod;
  std::optional<uint16_t>                         m_GameLocaleLangID;
  std::optional<bool>                             m_LoadInGame;
  std::optional<FakeUsersShareUnitsMode>          m_FakeUsersShareUnitsMode;
  std::optional<bool>                             m_EnableJoinObserversInProgress;
  std::optional<bool>                             m_EnableJoinPlayersInProgress;
  std::optional<uint32_t>                         m_SpectatorDelay;
  std::optional<bool>                             m_LogCommands;
  std::optional<uint8_t>                          m_NumPlayersToStartGameOver;
  std::optional<PlayersReadyMode>                 m_PlayersReadyMode;
  std::optional<bool>                             m_AutoStartRequiresBalance;
  std::optional<uint32_t>                         m_AutoKickPing;
  std::optional<uint32_t>                         m_WarnHighPing;
  std::optional<uint32_t>                         m_SafeHighPing;
  std::optional<bool>                             m_SyncNormalize;
  std::optional<uint16_t>                         m_MaxAPM;
  std::optional<uint16_t>                         m_MaxBurstAPM;

  CGameExtraOptions*                              m_MapExtraOptions;
  uint8_t                                         m_MapReadyCallbackAction;
  std::string                                     m_MapReadyCallbackData;

  std::atomic<bool>                               m_ExitingSoon;
  bool                                            m_DeleteMe;

  CGameSetup(CAura* nAura, std::shared_ptr<CCommandContext> nCtx, CConfig* mapCFG);
  CGameSetup(CAura* nAura, std::shared_ptr<CCommandContext> nCtx, const std::string nSearchRawTarget, const uint8_t nSearchType, const bool nAllowPaths, const bool nUseStandardPaths, const bool nUseLuckyMode);
  ~CGameSetup();

  [[nodiscard]] std::string GetInspectName() const;
  [[nodiscard]] bool GetDeleteMe() const { return m_DeleteMe; }
  [[nodiscard]] bool GetIsStale() const;

  void ParseInputLocal();
  void ParseInput();
  [[nodiscard]] std::pair<uint8_t, std::filesystem::path> SearchInputStandard();
  [[nodiscard]] std::pair<uint8_t, std::filesystem::path> SearchInputAlias();
  [[nodiscard]] std::pair<uint8_t, std::filesystem::path> SearchInputLocalExact();
  [[nodiscard]] std::pair<uint8_t, std::filesystem::path> SearchInputLocalTryExtensions();
  [[nodiscard]] std::pair<uint8_t, std::filesystem::path> SearchInputLocalFuzzy(std::vector<std::string>& fuzzyMatches);
#ifndef DISABLE_CPR
  void SearchInputRemoteFuzzy(std::vector<std::string>& fuzzyMatches);
#endif
  [[nodiscard]] std::pair<uint8_t, std::filesystem::path> SearchInputLocal(std::vector<std::string>& fuzzyMatches);
  [[nodiscard]] std::pair<uint8_t, std::filesystem::path> SearchInput();
  inline std::shared_ptr<CMap> GetMap() const { return m_Map; }
  [[nodiscard]] std::shared_ptr<CMap> GetBaseMapFromConfig(CConfig* mapCFG, const bool silent);
  [[nodiscard]] std::shared_ptr<CMap> GetBaseMapFromConfigFile(const std::filesystem::path& filePath, const bool isCache, const bool silent);
  [[nodiscard]] std::shared_ptr<CMap> GetBaseMapFromMapFile(const std::filesystem::path& filePath, const bool silent);
  [[nodiscard]] std::shared_ptr<CMap> GetBaseMapFromMapFileOrCache(const std::filesystem::path& mapPath, const bool silent);
  bool ApplyMapModifiers(CGameExtraOptions* extraOptions);
#ifndef DISABLE_CPR
  [[nodiscard]] uint32_t ResolveMapRepositoryTask();
  void RunResolveMapRepository();
  [[nodiscard]] uint32_t RunResolveMapRepositorySync();
  void SetDownloadFilePath(std::filesystem::path&& filePath);
  [[nodiscard]] bool PrepareDownloadMap();
  void RunDownloadMap();
  [[nodiscard]] uint32_t DownloadMapTask();
  [[nodiscard]] uint32_t RunDownloadMapSync();
  void OnResolveMapSuccess();
  void OnDownloadMapSuccess();
  void OnFetchSuggestionsEnd();
  [[nodiscard]] std::vector<std::pair<std::string, std::string>> GetMapRepositorySuggestions(const std::string & pattern, const uint8_t maxCount);


#endif
  [[nodiscard]] bool GetMapLoaded() const;
  void LoadMap();
  [[nodiscard]] bool LoadMapSync();
  void OnLoadMapSuccess();
  void OnLoadMapError();
  [[nodiscard]] bool GetIsActive();
  void SetActive();
  [[nodiscard]] bool RestoreFromSaveFile();
  bool RunHost();

  [[nodiscard]] uint32_t GetGameIdentifier() const;
  [[nodiscard]] uint32_t GetEntryKey() const;
  [[nodiscard]] const sockaddr_storage* GetGameAddress() const;

  [[nodiscard]] inline bool GetIsMirror() const { return InspectMirror().GetIsEnabled(); }
  [[nodiscard]] inline GameMirrorSetup& GetMirror() { return m_Mirror; }
  [[nodiscard]] inline const GameMirrorSetup& InspectMirror() const { return m_Mirror; }
  [[nodiscard]] inline bool GetIsDownloading() const { return m_IsStepDownloading; }
  [[nodiscard]] inline bool GetHasBeenHosted() const { return m_CreationCounter > 0; }
  [[nodiscard]] inline bool GetHasGameVersion() const { return m_GameVersion.has_value(); }

  void AddIgnoredRealm(std::shared_ptr<const CRealm> nRealm);
  void RemoveIgnoredRealm(std::shared_ptr<const CRealm> nRealm);
  inline void SetDisplayMode(const uint8_t nDisplayMode) { m_RealmsDisplayMode = nDisplayMode; };
  void SetOwner(const std::string& nOwner, std::shared_ptr<const CRealm> nRealm);
  void SetOwnerLess(const bool nValue) { m_OwnerLess = nValue; }

  // Game creator stuff
  void RemoveCreator();

  void SetCreator(const ServiceType serviceType, const std::string& nCreator);
  void AcquireCreator();

  inline ServiceType GetCreatedFromType() const { return m_Creator.GetServiceType(); }
  inline bool GetCreatedFromIsExpired() const { return m_Creator.GetIsExpired(); }
  template <typename T>
  [[nodiscard]] std::shared_ptr<T> GetCreatedFrom() const;

  [[nodiscard]] bool MatchesCreatedFrom(const ServiceType fromType) const;
  [[nodiscard]] bool MatchesCreatedFrom(const ServiceType fromType, std::shared_ptr<const void> fromThing) const;
  [[nodiscard]] bool MatchesCreatedFromGame(std::shared_ptr<const CGame> nGame) const;
  [[nodiscard]] bool MatchesCreatedFromRealm(std::shared_ptr<const CRealm> nRealm) const;
  [[nodiscard]] bool MatchesCreatedFromIRC() const;
  [[nodiscard]] bool MatchesCreatedFromDiscord() const;

  inline void SetName(const std::string& nName) { m_Name = nName; }
  inline void SetBaseName(const std::string& nName) {
    m_Name = nName;
    m_BaseName = nName;
  }
  inline void SetOwner(const std::string& ownerName, const std::string& ownerRealm) {
    m_Owner = std::make_pair(ownerName, ownerRealm);
  }

  inline void SetLobbyTimeoutMode(const LobbyTimeoutMode nMode) { m_LobbyTimeoutMode = nMode; }
  inline void SetLobbyOwnerTimeoutMode(const LobbyOwnerTimeoutMode nMode) { m_LobbyOwnerTimeoutMode = nMode; }
  inline void SetLoadingTimeoutMode(const GameLoadingTimeoutMode nMode) { m_LoadingTimeoutMode = nMode; }
  inline void SetPlayingTimeoutMode(const GamePlayingTimeoutMode nMode) { m_PlayingTimeoutMode = nMode; }

  inline void SetLobbyTimeout(const uint32_t nTimeout) { m_LobbyTimeout = nTimeout; }
  inline void SetLobbyOwnerTimeout(const uint32_t nTimeout) { m_LobbyOwnerTimeout = nTimeout; }
  inline void SetLoadingTimeout(const uint32_t nTimeout) { m_LoadingTimeout = nTimeout; }
  inline void SetPlayingTimeout(const uint32_t nTimeout) { m_PlayingTimeout = nTimeout; }

  inline void SetPlayingTimeoutWarningShortCountDown(const uint8_t nMode) { m_PlayingTimeoutWarningShortCountDown = nMode; }
  inline void SetPlayingTimeoutWarningShortInterval(const uint32_t nTimeout) { m_PlayingTimeoutWarningShortInterval = nTimeout; }
  inline void SetPlayingTimeoutWarningLargeCountDown(const uint8_t nMode) { m_PlayingTimeoutWarningLargeCountDown = nMode; }
  inline void SetPlayingTimeoutWarningLargeInterval(const uint32_t nTimeout) { m_PlayingTimeoutWarningLargeInterval = nTimeout; }

  inline void SetLobbyOwnerReleaseLANLeaver(const bool nRelease) { m_LobbyOwnerReleaseLANLeaver = nRelease; }

  inline void SetLobbyCountDownInterval(const uint32_t nValue) { m_LobbyCountDownInterval = nValue; }
  inline void SetLobbyCountDownStartValue(const uint32_t nValue) { m_LobbyCountDownStartValue = nValue; }

  inline void SetIsCheckJoinable(const bool nCheckJoinable) { m_CheckJoinable = nCheckJoinable; }
  inline void SetNotifyJoins(const bool nNotifyJoins) { m_NotifyJoins = nNotifyJoins; }
  inline void SetLobbyReplaceable(const bool nReplaceable) { m_LobbyReplaceable = nReplaceable; }
  inline void SetLobbyAutoRehosted(const bool nRehosted) { m_LobbyAutoRehosted = nRehosted; }

  inline void SetDownloadTimeout(const uint32_t nTimeout) { m_DownloadTimeout = nTimeout; }

  inline void SetSaveGameAllowed(const bool nSave) { m_SaveGameAllowed = nSave; }

  inline void SetResultSource(const GameResultSourceSelect nResultSource) { m_ResultSource = nResultSource; }
  inline void SetVerbose(const bool nVerbose) { m_Verbose = nVerbose; }
  inline void SetContext(std::shared_ptr<CCommandContext> nCtx) { m_Ctx = nCtx; }
  inline void SetMapReadyCallback(const uint8_t action, const std::string& data) {
    m_MapReadyCallbackAction = action;
    m_MapReadyCallbackData = data;
  }
  inline void SetMapExtraOptions(CGameExtraOptions* opts) { m_MapExtraOptions = opts; }
  inline void SetGameSavedFile(const std::filesystem::path& filePath);
  inline void SetCheckReservation(const bool nChecksReservation) { m_ChecksReservation = nChecksReservation; }
  inline void SetReservations(const std::vector<std::string>& nReservations) { m_Reservations = nReservations; }
  inline void SetCrossPlayMode(const CrossPlayMode nValue) { m_CrossPlayMode = nValue; }
  inline void SetAutoStartPlayers(const uint8_t nValue) { m_AutoStartPlayers = nValue; }
  inline void SetAutoStartSeconds(const int64_t nValue) { m_AutoStartSeconds = nValue; }
  inline void SetReconnectionMode(const uint8_t nValue) { m_ReconnectionMode = nValue;}
  inline void SetEnableLobbyChat(const bool nValue) { m_EnableLobbyChat = nValue;}
  inline void SetEnableInGameChat(const bool nValue) { m_EnableInGameChat = nValue;}
  inline void SetIPFloodHandler(const OnIPFloodHandler nValue) { m_IPFloodHandler = nValue;}
  inline void SetLeaverHandler(const OnPlayerLeaveHandler nValue) { m_LeaverHandler = nValue;}
  inline void SetShareUnitsHandler(const OnShareUnitsHandler nValue) { m_ShareUnitsHandler = nValue; }
  inline void SetUnsafeNameHandler(const OnUnsafeNameHandler nValue) { m_UnsafeNameHandler = nValue;}
  inline void SetBroadcastErrorHandler(const OnRealmBroadcastErrorHandler nValue) { m_BroadcastErrorHandler = nValue;}
  inline void SetEnableLagScreen(const bool nValue) { m_EnableLagScreen = nValue; }
  inline void SetLatencyAverage(const uint16_t nValue) { m_LatencyAverage = nValue; }
  inline void SetLatencyEqualizerEnabled(const bool nValue) { m_LatencyEqualizerEnabled = nValue; }
  inline void SetHCL(const std::string& nHCL) { m_HCL = nHCL; }
  inline void SetCustomLayout(const uint8_t nLayout) { m_CustomLayout = nLayout; }

  inline void SetNumPlayersToStartGameOver(const uint8_t nNumPlayersToStartGameOver) { m_NumPlayersToStartGameOver = nNumPlayersToStartGameOver; }
  inline void SetAutoKickPing(const uint32_t nAutoKickPing) { m_AutoKickPing = nAutoKickPing; }
  inline void SetWarnHighPing(const uint32_t nWarnHighPing) { m_WarnHighPing = nWarnHighPing; }
  inline void SetSafeHighPing(const uint32_t nSafeHighPing) { m_SafeHighPing = nSafeHighPing; }
  inline void SetSyncNormalize(const bool nSyncNormalize) { m_SyncNormalize = nSyncNormalize; }
  inline void SetMaxAPM(const uint16_t nMaxAPM) { m_MaxAPM = nMaxAPM; }
  inline void SetMaxBurstAPM(const uint16_t nMaxBurstAPM) { m_MaxBurstAPM = nMaxBurstAPM; }
  inline void SetHideLobbyNames(const bool nHideLobbyNames) { m_HideLobbyNames = nHideLobbyNames; }
  inline void SetHideInGameNames(const HideIGNMode nHideInGameNames) { m_HideInGameNames = nHideInGameNames; }
  inline void SetGameIsExpansion(const bool nGameIsExpansion) { m_GameIsExpansion = nGameIsExpansion; }
  inline void SetGameVersion(const Version& nGameVersion) { m_GameVersion = nGameVersion; }
  inline void SetGameLocaleMod(const W3ModLocale nGameLocaleMod) { m_GameLocaleMod = nGameLocaleMod; }
  inline void SetGameLocaleLangID(const uint16_t nGameLocaleLangID) { m_GameLocaleLangID = nGameLocaleLangID; }
  inline void SetLoadInGame(const bool nGameLoadInGame) { m_LoadInGame = nGameLoadInGame; }
  inline void SetFakeUsersShareUnitsMode(const FakeUsersShareUnitsMode nShareUnitsMode) { m_FakeUsersShareUnitsMode = nShareUnitsMode; }
  inline void SetEnableJoinObserversInProgress(const bool nGameEnableJoinObserversInProgress) { m_EnableJoinObserversInProgress = nGameEnableJoinObserversInProgress; }
  inline void SetEnableJoinPlayersInProgress(const bool nGameEnableJoinPlayersInProgress) { m_EnableJoinPlayersInProgress = nGameEnableJoinPlayersInProgress; }

  inline void SetLogCommands(const bool nLogCommands) { m_LogCommands = nLogCommands; }
  inline void SetAutoStartRequiresBalance(const bool nRequiresBalance) { m_AutoStartRequiresBalance = nRequiresBalance; }

  void ClearExtraOptions();
  void ExportTemporaryToMap(CConfig* MapCFG);
  bool AcquireCLIEarly(const CCLI* nCLI);
  void AcquireHost(const CCLI* nCLI, const std::optional<std::string>& mpName);
  void AcquireCLISimple(const CCLI* nCLI);
  bool AcquireCLIMirror(const CCLI* nCLI);
  void static DeleteTemporaryFromMap(CConfig* MapCFG);
  std::string static NormalizeGameName(const std::string& gameName);

  void OnGameCreate();
  [[nodiscard]] bool Update();
  void AwaitSettled();
};

#endif // AURA_GAMESETUP_H_
