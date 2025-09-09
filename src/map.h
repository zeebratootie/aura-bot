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

#ifndef AURA_MAP_H_
#define AURA_MAP_H_

#include <sha1/sha1.h>

#include "includes.h"
#include "file_util.h"
#include "game_result.h"
#include "game_slot.h"
#include "util.h"

#include <iterator>
#include <cctype>

#pragma once

#define ROTL(x, n) ((x) << (n)) | ((x) >> (32 - (n))) // this won't work with signed types
#define ROTR(x, n) ((x) >> (n)) | ((x) << (32 - (n))) // this won't work with signed types

//
// MapCrypto
//

struct MapCrypto
{
  bool errored;
  CSHA1 sha1;
  uint32_t blizz;

  MapCrypto()
   : errored(false),
     blizz(0)
  {
  }

  ~MapCrypto() = default;
};

//
// MapFragmentHashes
//

struct MapFragmentHashes
{
  std::optional<std::array<uint8_t, 4>> blizz;
  std::optional<std::array<uint8_t, 20>> sha1;
};

//
// MapEssentials
//

struct MapEssentials
{
  bool melee;
  uint8_t dataSet;
  uint8_t numPlayers;
  uint8_t numDisabled;
  uint8_t numTeams;
  Version minCompatibleGameVersion;
  Version minSuggestedGameVersion;
  bool isExpansion;
  bool isLua;
  uint32_t editorVersion;
  uint32_t options;
  uint32_t previewImgSize;
  //uint32_t loadingImgSize;
  //uint32_t prologueImgSize;
  std::string prologueImgPath;
  std::string loadingImgPath;
  std::string name;
  std::string author;
  std::string desc;
  std::optional<std::array<uint8_t, 2>> width;
  std::optional<std::array<uint8_t, 2>> height;
  std::map<Version, MapFragmentHashes> fragmentHashes;
  std::vector<CGameSlot> slots;

  MapEssentials()
   : melee(false),
     numPlayers(0),
     numDisabled(0),
     numTeams(0),
     minCompatibleGameVersion(GAMEVER(1u, 0u)),
     minSuggestedGameVersion(GAMEVER(1u, 0u)),
     isExpansion(false),
     isLua(false),
     editorVersion(0),
     options(0),
     previewImgSize(0)/*,
     loadingImgSize(0),
     prologueImgSize(0)*/
  {
  }
  ~MapEssentials() = default;
};

struct HCLConfig
{
  bool supported;
  bool aboutVirtualPlayers;
  uint8_t toggle;
  std::string defaultValue;

  HCLConfig()
   : supported(false),
     aboutVirtualPlayers(false),
     toggle(MAP_FEATURE_TOGGLE_DISABLED)
  {
  }
  ~HCLConfig() = default;
};

struct W3MMDConfig
{
  bool supported;
  bool enabled;
  bool aboutComputers;
  bool emitSkipsVirtualPlayers;
  bool emitPrioritizePlayers;
  bool useGameOver;
  uint8_t type;

  W3MMDConfig()
   : supported(false),
     enabled(false),
     aboutComputers(false),
     emitSkipsVirtualPlayers(false),
     emitPrioritizePlayers(false),
     useGameOver(false),
     type(MMD_TYPE_STANDARD)
  {
  }
  ~W3MMDConfig() = default;
};

struct W3HMCConfig
{
  bool supported;
  uint8_t toggle;
  uint32_t dwordA;
  uint32_t dwordB;
  uint8_t slot;
  std::string playerName;
  std::string fileName;
  std::string secret;

  W3HMCConfig()
   : supported(false),
     toggle(MAP_FEATURE_TOGGLE_DISABLED),
     dwordA(0),
     dwordB(0),
     slot(0xFF)
  {
  }
  ~W3HMCConfig() = default;
};

struct AHCLConfig
{
  bool supported;
  uint8_t toggle;
  uint8_t slot;
  std::string playerName;
  std::string fileName;
  std::string mission;
  std::string charset;

  AHCLConfig()
   : supported(false),
     toggle(MAP_FEATURE_TOGGLE_DISABLED),
     slot(0xFF)
  {
  }
  ~AHCLConfig() = default;
};

struct MapTransfer
{
  bool started;
  bool finished;
  uint8_t status;
  int64_t startedTicks;
  int64_t finishedTicks;
  uint32_t lastSentOffsetEnd;
  uint32_t lastAck;
  uint32_t crc32;

  MapTransfer()
   : started(false),
     finished(false),
     status(0xFF),
     startedTicks(0),
     finishedTicks(0),
     lastSentOffsetEnd(0),
     lastAck(0),
     crc32(0)
  {
  }
  ~MapTransfer() = default;

  inline bool GetStarted() const { return started; }
  inline int64_t GetStartedTicks() const { return startedTicks; }
  inline void SetStarted() { started = true; }
  inline void SetStartedTicks(const int64_t ticks) { startedTicks = ticks; }

  inline bool GetFinished() const { return finished; }
  inline int64_t GetFinishedTicks() const { return finishedTicks; }
  inline void SetFinished() { finished = true; }
  inline void SetFinishedTicks(const int64_t ticks) { finishedTicks = ticks; }

  inline uint8_t GetStatus() const { return status; }
  inline void SetStatus(uint8_t nStatus) { status = nStatus; }

  inline uint32_t GetLastSentOffsetEnd() const { return lastSentOffsetEnd; }
  inline void SetLastSentOffsetEnd(const uint32_t offset) { lastSentOffsetEnd = offset; }

  inline uint32_t GetLastAck() const { return lastAck; }
  inline void SetLastAck(uint32_t nLastAck) { lastAck = nLastAck; }

  inline uint32_t GetLastCRC32() const { return crc32; }
  inline void SetLastCRC32(const uint32_t nCRC32) { crc32 = nCRC32; }

  inline void Start() {
    SetStarted();
    SetStartedTicks(GetTicks());
  }
  inline void Finish() {
    SetFinished();
    SetFinishedTicks(GetTicks());
  }
  inline bool GetIsInProgress() const { return GetStarted() && !GetFinished(); }
};

//
// CMap
//

class CMap
{
public:
  CAura* m_Aura;

  std::optional<bool>                             m_MapTargetGameIsExpansion;
  std::optional<Version>                          m_MapTargetGameVersion;
  std::optional<uint8_t>                          m_NumPlayersToStartGameOver;
  std::optional<PlayersReadyMode>                 m_PlayersReadyMode;
  std::optional<bool>                             m_AutoStartRequiresBalance;
  std::optional<bool>                             m_EnableLagScreen;
  std::optional<uint32_t>                         m_LagStartDefaultControllerSyncMilliSeconds;
  std::optional<uint32_t>                         m_LagStopDefaultControllerSyncMilliSeconds;
  std::optional<uint32_t>                         m_LagStartDefaultObserverSyncMilliSeconds;
  std::optional<uint32_t>                         m_LagStopDefaultObserverSyncMilliSeconds;
  std::optional<uint32_t>                         m_AutoKickPing;
  std::optional<uint32_t>                         m_WarnHighPing;
  std::optional<uint32_t>                         m_SafeHighPing;

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

  std::optional<uint16_t>                         m_Latency;
  std::optional<bool>                             m_LatencyEqualizerEnabled;
  std::optional<uint16_t>                         m_LatencyEqualizerMaxDelay;

  std::optional<int64_t>                          m_AutoStartSeconds;
  std::optional<uint8_t>                          m_AutoStartPlayers;
  std::optional<uint16_t>                         m_MaxAPM;
  std::optional<uint16_t>                         m_MaxBurstAPM;
  std::optional<bool>                             m_HideLobbyNames;
  std::optional<HideIGNMode>                      m_HideInGameNames;
  std::optional<bool>                             m_LoadInGame;
  std::optional<FakeUsersShareUnitsMode>          m_FakeUsersShareUnitsMode;
  std::optional<bool>                             m_EnableJoinObserversInProgress;
  std::optional<bool>                             m_EnableJoinPlayersInProgress;
  std::optional<uint32_t>                         m_SpectatorDelay;

  std::optional<bool>                             m_LogCommands;
  std::optional<uint8_t>                          m_ReconnectionMode;
  std::optional<bool>                             m_EnableLobbyChat;
  std::optional<bool>                             m_EnableInGameChat;
  std::optional<OnIPFloodHandler>                 m_IPFloodHandler;
  std::optional<OnPlayerLeaveHandler>             m_LeaverHandler;
  std::optional<OnShareUnitsHandler>              m_ShareUnitsHandler;
  std::optional<OnUnsafeNameHandler>              m_UnsafeNameHandler;
  std::optional<OnRealmBroadcastErrorHandler>     m_BroadcastErrorHandler;
  std::optional<bool>                             m_PipeConsideredHarmful;

  GameResultConstraints                           m_GameResultConstraints;
  HCLConfig                                       m_HCL;
  W3MMDConfig                                     m_MMD;
  W3HMCConfig                                     m_HMC;
  AHCLConfig                                      m_AHCL;

private:
  uint32_t                                     m_MapSize;                // config value: <map.size> (4 bytes)
  std::array<uint8_t, 2>                       m_MapWidth;               // config value: <map.width> (2 bytes)
  std::array<uint8_t, 2>                       m_MapHeight;              // config value: <map.height> (2 bytes)
  std::array<uint8_t, 4>                       m_MapCRC32;               // config value: <map.file_hash.crc32> (4 bytes) -> this is the real full CRC
  std::array<uint8_t, 20>                      m_MapSHA1;                // config value: <map.file_hash.sha1> (20 bytes) -> this is the real full SHA1
  std::map<Version, std::array<uint8_t, 20>>   m_MapScriptsSHA1;         // config value: <map.scripts_hash.sha1> (20 bytes)
  std::map<Version, std::array<uint8_t, 4>>    m_MapScriptsBlizzHash;        // config value: <map.scripts_hash.crc32> (4 bytes) -> this is not the real CRC, it's the "xoro" value
  std::vector<CGameSlot>                       m_Slots;
  std::vector<std::pair<std::string, std::string>> m_InitCommands;
  std::string                     m_CFGName;
  std::string                     m_ClientMapPath;       // config value: map path
  std::string                     m_MapType;       // config value: map type (for stats class)
  uint8_t                         m_MapAIType;     // config value
  std::filesystem::path           m_MapServerPath;  // config value: map local path
  SharedByteArray                 m_MapFileContents;       // the map data itself, for sending the map to players
  bool                            m_MapFileIsValid;
  bool                            m_MapLoaderIsPartial;
  std::optional<W3ModLocale>      m_GameLocaleMod;
  uint16_t                        m_GameLocaleLangID;
  uint32_t                        m_MapOptions;
  uint32_t                        m_MapEditorVersion;
  uint8_t                         m_MapDataSet;
  bool                            m_MapRequiresExpansion;
  bool                            m_MapIsLua;
  bool                            m_MapIsMelee;
  bool                            m_EnableModernPlayers;
  bool                            m_EnableModernColors;
  bool                            m_EnableModernTeams;
  Version                         m_MapMinGameVersion;
  Version                         m_MapMinSuggestedGameVersion;
  uint8_t                         m_MapNumControllers; // config value: max map number of players
  uint8_t                         m_MapNumDisabled; // config value: slots that cannot be used - not even by observers
  uint8_t                         m_MapNumTeams;   // config value: max map number of teams
  uint8_t                         m_MapCustomizableObserverTeam;
  uint8_t                         m_MapVersionMaxSlots;
  GameSpeed                       m_GameSpeed;
  GameVisibilityMode              m_GameVisibility;
  GameObserversMode               m_GameObservers;
  uint8_t                         m_GameFlags;
  uint8_t                         m_MapFilterMaker;
  uint8_t                         m_MapFilterType;
  uint8_t                         m_MapFilterSize;
  uint8_t                         m_MapFilterObs;
  std::array<uint8_t, 5>          m_MapContentMismatch;
  uint32_t                        m_MapPreviewImageSize;
  uint8_t                         m_MapPreviewImagePathType;
  std::string                     m_MapPreviewImagePath;
  std::string                     m_MapPreviewImageMimeType;
  /*
  uint32_t                        m_MapPrologueImageSize;
  std::string                     m_MapPrologueImagePath;
  std::string                     m_MapPrologueImageMimeType;
  uint32_t                        m_MapLoadingImageSize;
  std::string                     m_MapLoadingImagePath;
  std::string                     m_MapLoadingImageMimeType;
  */
  std::string                     m_MapTitle;
  std::string                     m_MapAuthor;
  std::string                     m_MapDescription;
  std::string                     m_MapURL;
  std::string                     m_MapSiteURL;
  std::string                     m_MapShortDesc;
  void*                           m_MapMPQ;
  std::optional<bool>             m_MapMPQResult;
  bool                            m_UseStandardPaths;
  bool                            m_Valid;
  bool                            m_JASSValid;
  std::string                     m_ErrorMessage;
  std::string                     m_JASSErrorMessage;

public:
  CMap(CAura* nAura, CConfig* CFG);
  ~CMap();

  [[nodiscard]] inline bool                              GetValid() const { return m_Valid; }
  [[nodiscard]] inline bool                              HasMismatch() const { return m_MapContentMismatch[0] != 0 || m_MapContentMismatch[1] != 0 || m_MapContentMismatch[2] != 0 || m_MapContentMismatch[3] != 0 || m_MapContentMismatch[4] != 0; }
  [[nodiscard]] inline bool                              GetMPQSucceeded() const { return m_MapMPQResult.has_value() && m_MapMPQResult.value(); }
  [[nodiscard]] inline bool                              GetMPQErrored() const { return m_MapMPQResult.has_value() && !m_MapMPQResult.value(); }
  [[nodiscard]] inline const std::string&                GetConfigName() const { return m_CFGName; }
  [[nodiscard]] std::string_view                         GetClientPath() const { return m_ClientMapPath; }
  [[nodiscard]] inline uint32_t                          GetMapSize() const { return m_MapSize; }
  [[nodiscard]] inline float                             GetMapSizeMB() const { return (float)m_MapSize / (float)(1024. * 1024.); }
  [[nodiscard]] uint32_t                                 GetMapSizeClamped(const Version& version) const;
  [[nodiscard]] bool                                     GetMapSizeIsNativeSupported(const Version& version) const;
  [[nodiscard]] inline const std::array<uint8_t, 4>&     GetMapCRC32() const { return m_MapCRC32; } // <map.file_hash.crc32>, but also legacy <map_hash> and <map.crc32>
  [[nodiscard]] inline const std::array<uint8_t, 20>&    GetMapSHA1() const { return m_MapSHA1; } // <map.file_hash.sha1>
  [[nodiscard]] const std::array<uint8_t, 4>&            GetMapScriptsBlizzHash(const Version& nVersion) const; // <map.scripts_hash.blizz>, but also legacy <map_crc>, <map.weak_hash>
  [[nodiscard]] const std::array<uint8_t, 20>&           GetMapScriptsSHA1(const Version& nVersion) const; // <map.scripts_hash.sha1>, but also legacy <map.sha1>
  [[nodiscard]] std::optional<bool>                      MatchMapScriptsBlizz(const Version& nVersion, const std::array<uint8_t, 4>& cmpHash) const;
  [[nodiscard]] std::optional<bool>                      MatchMapScriptsSHA1(const Version& nVersion, const std::array<uint8_t, 20>& cmpHash) const;
  [[nodiscard]] bool                                     GetMapIsGameVersionSupported(const Version& nVersion) const;
  [[nodiscard]] inline GameVisibilityMode                GetGameVisibility() const { return m_GameVisibility; }
  [[nodiscard]] inline GameSpeed                         GetGameSpeed() const { return m_GameSpeed; }
  [[nodiscard]] inline GameObserversMode                 GetGameObservers() const { return m_GameObservers; }
  [[nodiscard]] inline uint8_t                    GetMapFlags() const { return m_GameFlags; }
  [[nodiscard]] uint32_t                          GetGameConvertedFlags() const;
  [[nodiscard]] uint32_t                          GetMapGameType() const;
  [[nodiscard]] inline bool                       GetMapHasTargetGameVersion() const { return m_MapTargetGameVersion.has_value(); }
  [[nodiscard]] inline Version                    GetMapTargetGameVersion() const { return m_MapTargetGameVersion.value(); }
  [[nodiscard]] inline bool                       GetMapHasTargetGameIsExpansion() const { return m_MapTargetGameIsExpansion.has_value(); }
  [[nodiscard]] inline bool                       GetMapTargetGameIsExpansion() const { return m_MapTargetGameIsExpansion.value(); }
  [[nodiscard]] inline std::optional<W3ModLocale> GetGameLocaleMod() const { return m_GameLocaleMod; }
  [[nodiscard]] inline uint16_t                   GetGameLocaleLangID() const { return m_GameLocaleLangID; }
  [[nodiscard]] inline uint32_t                   GetMapOptions() const { return m_MapOptions; }
  [[nodiscard]] inline uint8_t                    GetMapDataSet() const { return m_MapDataSet; }
  [[nodiscard]] inline bool                       GetMapRequiresExpansion() const { return m_MapRequiresExpansion; }
  [[nodiscard]] inline bool                       GetMapScriptIsLua() const { return m_MapIsLua; }
  [[nodiscard]] inline bool                       GetMapIsMelee() const { return m_MapIsMelee; }
  [[nodiscard]] inline const Version&             GetMapMinGameVersion() const { return m_MapMinGameVersion; }
  [[nodiscard]] inline const Version&             GetMapMinSuggestedGameVersion() const { return m_MapMinSuggestedGameVersion; }
  [[nodiscard]] uint8_t                           GetMapLayoutStyle() const;
  [[nodiscard]] inline std::array<uint8_t, 2>     GetMapWidth() const { return m_MapWidth; }
  [[nodiscard]] inline std::array<uint8_t, 2>     GetMapHeight() const { return m_MapHeight; }
  [[nodiscard]] inline std::string                GetMapType() const { return m_MapType; }
  [[nodiscard]] inline uint8_t                    GetMapAIType() const { return m_MapAIType; }
  [[nodiscard]] inline const std::filesystem::path&     GetServerPath() const { return m_MapServerPath; }
  [[nodiscard]] std::filesystem::path             GetResolvedServerPath() const;
  [[nodiscard]] inline bool                       HasServerPath() const { return !m_MapServerPath.empty(); }
  [[nodiscard]] std::string                       GetServerFileName() const;
  [[nodiscard]] std::string                       GetClientFileName() const;
  /*
  [[nodiscard]] inline uint32_t                   GetMapPrologueImageSize() const { return m_MapPrologueImageSize; }
  [[nodiscard]] inline std::string                GetMapPrologueImagePath() const { return m_MapPrologueImagePath; }
  [[nodiscard]] inline std::string                GetMapPrologueImageMimeType() const { return m_MapPrologueImageMimeType; }
  [[nodiscard]] inline uint32_t                   GetMapLoadingImageSize() const { return m_MapLoadingImageSize; }
  [[nodiscard]] inline std::string                GetMapLoadingImagePath() const { return m_MapLoadingImagePath; }
  [[nodiscard]] inline std::string                GetMapLoadingImageMimeType() const { return m_MapLoadingImageMimeType; }
  */
  [[nodiscard]] inline uint32_t                   GetMapPreviewImageSize() const { return m_MapPreviewImageSize; }
  [[nodiscard]] inline uint8_t                    GetMapPreviewImagePathType() const { return m_MapPreviewImagePathType; }
  [[nodiscard]] inline std::string                GetMapPreviewImagePath() const { return m_MapPreviewImagePath; }
  [[nodiscard]] inline std::string                GetMapPreviewImageMimeType() const { return m_MapPreviewImageMimeType; }
  [[nodiscard]] SharedByteArray                   GetMapPreviewContents();
  [[nodiscard]] inline std::string                GetMapTitle() const { return m_MapTitle.empty() ? "Just another Warcraft 3 Map" : m_MapTitle; }
  [[nodiscard]] inline std::string                GetMapAuthor() const { return m_MapAuthor.empty() ? "Unknown" : m_MapAuthor; }
  [[nodiscard]] inline std::string                GetMapDescription() const { return m_MapDescription.empty() ? "Nondescript" : m_MapDescription; }
  [[nodiscard]] inline std::string                GetMapURL() const { return m_MapURL; }
  [[nodiscard]] inline std::string                GetMapSiteURL() const { return m_MapSiteURL; }
  [[nodiscard]] inline std::string                GetMapShortDesc() const { return m_MapShortDesc; }
  [[nodiscard]] inline bool                       GetMapFileIsValid() const { return m_MapFileIsValid; }
  [[nodiscard]] inline const SharedByteArray&     GetMapFileContents() { return m_MapFileContents; }
  [[nodiscard]] inline bool                       HasMapFileContents() const { return m_MapFileContents != nullptr && !m_MapFileContents->empty(); }
  [[nodiscard]] bool                              GetMapFileIsFromManagedFolder() const;
  [[nodiscard]] inline uint8_t                    GetMapNumDisabled() const { return m_MapNumDisabled; }
  [[nodiscard]] inline uint8_t                    GetMapNumControllers() const { return m_MapNumControllers; }
  [[nodiscard]] inline uint8_t                    GetMapNumTeams() const { return m_MapNumTeams; }
  [[nodiscard]] inline uint8_t                    GetMapCustomizableObserverTeam() const { return m_MapCustomizableObserverTeam; }
  [[nodiscard]] inline uint8_t                    GetVersionMaxSlots() const { return m_MapVersionMaxSlots; }
  [[nodiscard]] inline bool                       GetModernPlayersEnabled() const { return m_EnableModernPlayers; }
  [[nodiscard]] inline bool                       GetModernColorsEnabled() const { return m_EnableModernColors; }
  [[nodiscard]] inline bool                       GetModernTeamsEnabled() const { return m_EnableModernTeams; }
  [[nodiscard]] inline std::vector<CGameSlot>     GetSlots() const { return m_Slots; }
  [[nodiscard]] inline const std::vector<CGameSlot>&     InspectSlots() const { return m_Slots; }
  [[nodiscard]] inline const std::vector<std::pair<std::string, std::string>>&   GetInitCommands() const { return m_InitCommands; }
  [[nodiscard]] uint8_t                           GetLobbyRace(const CGameSlot* slot) const;
  [[nodiscard]] bool                              GetUseStandardPaths() const { return m_UseStandardPaths; }
  void                                            ClearMapFileContents() { m_MapFileContents.reset(); }
  bool                                            SetGameConvertedFlags(const uint32_t nFlags);
  bool                                            SetGameTeamsLocked(const bool nEnable);
  bool                                            SetGameTeamsTogether(const bool nEnable);
  bool                                            SetGameAdvancedSharedUnitControl(const bool nEnable);
  bool                                            SetGameRandomRaces(const bool nEnable);
  bool                                            SetGameRandomHeroes(const bool nEnable);
  bool                                            SetGameVisibility(const GameVisibilityMode nGameVisibility);
  bool                                            SetGameSpeed(const GameSpeed nGameSpeed);
  bool                                            SetGameObservers(const GameObserversMode nGameObservers);
  void                                            SetUseStandardPaths(const bool nValue) { m_UseStandardPaths = nValue; }
  [[nodiscard]] inline std::string                GetErrorString() { return m_ErrorMessage; }

  void                                            UpdateCryptoModule(std::map<Version, MapCrypto>::iterator& versionCrypto, const std::string& fileContents) const; // common.j, blizzard.j
  void                                            UpdateCryptoEndModules(std::map<Version, MapCrypto>::iterator& versionCrypto) const; // Padding sequence between blizzard.j and war3map.j
  void                                            UpdateCryptoScripts(std::map<Version, MapCrypto>& cryptos, const Version& version, const std::string& commonJ, const std::string& blizzardJ, const std::string& war3mapJ) const; // war3map.j
  void                                            UpdateCryptoNonScripts(std::map<Version, MapCrypto>& cryptos, const Version& version, const std::string& fileContents) const; // other files

  void                                            OnLoadMPQSubFile(std::optional<MapEssentials>& mapEssentials, std::map<Version, MapCrypto>& cryptos, const std::vector<Version>& supportedVersionHeads, const std::string& fileContents, const bool isMapScript);

  void                                            ReadFileFromArchiveExact(std::vector<uint8_t>& container, const std::string& fileSubPath) const;
  void                                            ReadFileFromArchiveExact(std::string& container, const std::string& fileSubPath) const;
  void                                            ReadFileFromArchive(std::vector<uint8_t>& container, const std::string& fileSubPath) const;
  void                                            ReadFileFromArchive(std::string& container, const std::string& fileSubPath) const;
  std::optional<uint32_t>                         GetFileSizeFromArchive(const std::string& fileSubPath) const;
  void                                            ReplaceTriggerStrings(std::string& container, std::vector<std::string*>& maybeWTSRefs) const;
  std::optional<MapEssentials>                    ParseMPQFromPath(const std::filesystem::path& filePath);
  std::optional<MapEssentials>                    ParseMPQ();
  bool AcquireGameIsExpansion(CConfig* CFG);
  bool AcquireGameVersion(CConfig* CFG);
  void Load(CConfig* CFG);
  void LoadGameConfigOverrides(CConfig& CFG);
  void LoadMapSpecificConfig(CConfig& CFG);

  [[nodiscard]] bool                              TryLoadMapFilePersistent(std::optional<uint32_t>& fileSize, std::optional<uint32_t>& crc32);
  [[nodiscard]] bool                              TryLoadMapFileChunked(std::optional<uint32_t>& fileSize, std::optional<uint32_t>& crc32, std::optional<std::array<uint8_t, 20>>& sha1);
  [[nodiscard]] bool                              CheckMapFileIntegrity();
  void                                            InvalidateMapFile() { m_MapFileIsValid = false; }
  [[nodiscard]] FileChunkTransient                GetMapFileChunk(size_t start);
  [[nodiscard]] std::pair<bool, uint32_t>         ProcessMapChunked(const std::filesystem::path& filePath, std::function<void(FileChunkTransient, size_t, size_t)> processChunk);
  bool                                            UnlinkFile();
  [[nodiscard]] std::string                       CheckProblems();

  [[nodiscard]] inline const GameResultConstraints &     GetGameResultConstraints() const { return m_GameResultConstraints; }
  [[nodiscard]] inline GameResultSourceSelect            GetGameResultSourceOfTruth() const { return m_GameResultConstraints.GetSourceOfTruth(); }

  // HCL
  [[nodiscard]] inline bool                              GetHCLSupported() const { return m_HCL.supported; }
  [[nodiscard]] inline bool                              GetHCLEnabled() const { return m_HCL.toggle != MAP_FEATURE_TOGGLE_DISABLED; }
  [[nodiscard]] inline bool                              GetHCLRequired() const { return m_HCL.toggle == MAP_FEATURE_TOGGLE_REQUIRED; }
  [[nodiscard]] inline bool                              GetHCLAboutVirtualPlayers() const { return m_HCL.aboutVirtualPlayers; }
  [[nodiscard]] inline size_t                            GetHCLRawStatesPerPlayer() const { return m_HCL.aboutVirtualPlayers ? 40u : 41u; }
  [[nodiscard]] inline size_t                            GetHCLRawBitsPerPlayer() const { return 5u; }
  [[nodiscard]] inline size_t                            GetHCLConfigurableStatesPerPlayer() const { return m_HCL.aboutVirtualPlayers ? 20u : 41u; }
  [[nodiscard]] inline size_t                            GetHCLConfigurableBitsPerPlayer() const { return m_HCL.aboutVirtualPlayers ? 4u : 5u; }
  [[nodiscard]] inline const std::string&                GetHCLDefaultValue() const { return m_HCL.defaultValue; }

  // W3MMD
  [[nodiscard]] inline bool                              GetMMDSupported() const { return m_MMD.supported; }
  [[nodiscard]] inline bool                              GetMMDEnabled() const { return m_MMD.enabled; }
  [[nodiscard]] inline bool                              GetMMDAboutComputers() const { return m_MMD.aboutComputers; }
  [[nodiscard]] inline bool                              GetMMDSupportsVirtualPlayers() const { return m_MMD.emitSkipsVirtualPlayers; }
  [[nodiscard]] inline bool                              GetMMDPrioritizePlayers() const { return m_MMD.emitPrioritizePlayers; }
  [[nodiscard]] inline bool                              GetMMDUseGameOver() const { return m_MMD.useGameOver; }
  [[nodiscard]] inline uint8_t                           GetMMDType() const { return m_MMD.type; }

  // W3HMC
  [[nodiscard]] inline bool                              GetHMCEnabled() const { return m_HMC.toggle != MAP_FEATURE_TOGGLE_DISABLED; }
  [[nodiscard]] inline bool                              GetHMCRequired() const { return m_HMC.toggle == MAP_FEATURE_TOGGLE_REQUIRED; }
  [[nodiscard]] inline uint8_t                           GetHMCMode() const { return m_HMC.toggle; }
  [[nodiscard]] inline uint8_t                           GetHMCSlot() const { return m_HMC.slot; }
  [[nodiscard]] inline const std::string&                GetHMCPlayerName() const { return m_HMC.playerName; }
  [[nodiscard]] inline std::array<uint8_t, 8>            GetHMCTrigger() const { return CreateFixedByteArray64<Endianness::kLittle>((uint64_t) m_HMC.dwordA | ((uint64_t)(m_HMC.dwordB) << 32u)); }
  [[nodiscard]] inline const std::string&                GetHMCFileName() const { return m_HMC.fileName; }
  [[nodiscard]] inline const std::string&                GetHMCSecret() const { return m_HMC.secret; }

  // AHCL
  [[nodiscard]] inline bool                              GetAHCLEnabled() const { return m_AHCL.toggle != MAP_FEATURE_TOGGLE_DISABLED; }
  [[nodiscard]] inline bool                              GetAHCLRequired() const { return m_AHCL.toggle == MAP_FEATURE_TOGGLE_REQUIRED; }
  [[nodiscard]] inline uint8_t                           GetAHCLMode() const { return m_AHCL.toggle; }
  [[nodiscard]] inline uint8_t                           GetAHCLSlot() const { return m_AHCL.slot; }
  [[nodiscard]] inline const std::string&                GetAHCLPlayerName() const { return m_AHCL.playerName; }
  [[nodiscard]] inline const std::string&                GetAHCLFileName() const { return m_AHCL.fileName; }
  [[nodiscard]] inline const std::string&                GetAHCLMission() const { return m_AHCL.mission; }
  [[nodiscard]] inline const std::string&                GetAHCLCharset() const { return m_AHCL.charset; }

  [[nodiscard]] inline static std::optional<uint32_t> GetTrigStrNum(const std::string& text)
  {
    std::optional<uint32_t> result;
    if (text.size() < 9 || text.substr(0, 8) != "TRIGSTR_") {
      return result;
    }
    std::string encodedNum = text.substr(8);
    int64_t num;
    try {
      num = std::stol(encodedNum);
    } catch (...) {
      return result;
    }
    if (num < 0 || num > 0xFFFFFFFF) {
      return result;
    }
    result = signed_cast_lossy<uint32_t>(num);
    return result;
  }

  [[nodiscard]] static uint16_t GetLocaleInt(const W3ModLocale locale);
  [[nodiscard]] static std::string GetLocalizedInMPQPath(const W3ModLocale locale, const std::string& baseName);
  [[nodiscard]] static std::string SanitizeTrigStr(const std::string& input);
  [[nodiscard]] static std::optional<std::string> GetTrigStr(const std::string& fileContents, const uint32_t targetNum);
  [[nodiscard]] static std::map<uint32_t, std::string> GetTrigStrMulti(const std::string& fileContents, const std::set<uint32_t> captureTargets);
};

[[nodiscard]] inline bool GetMPQPathIsMapScript(const std::string& fileName)
{
  if (fileName == "war3map.j") return true;
  if (fileName == R"(scripts\war3map.j)") return true;
  if (fileName == "war3map.lua") return true;
  if (fileName == R"(scripts\war3map.lua)") return true;
  return false;
}

[[nodiscard]] inline Version GetNextVersion(const Version& version)
{
  if (version.first == 1 && version.second == 36) {
    // v1.36 .. v2.0
    return GAMEVER(2u, 0u);
  } else {
    return GAMEVER(version.first, version.second + 1);
  }
}

[[nodiscard]] inline Version GetScriptsVersionRangeHead(const Version& version)
{
  if (version.first != 1) return version;
  if (19 <= version.second && version.second <= 21) {
    return GAMEVER(1u, 19u);
  }
  if (24 <= version.second && version.second <= 28) {
    return GAMEVER(1u, 24u);
  }
  return version;
}

[[nodiscard]] inline std::string GetScriptsVersionRangeHeadString(const Version& version)
{
  if (version.first != 1) return ToVersionString(version);
  if (19 <= version.second && version.second <= 21) {
    return ToVersionString(GAMEVER(1u, 19u));
  }
  if (24 <= version.second && version.second <= 28) {
    return ToVersionString(GAMEVER(1u, 24u));
  }
  return ToVersionString(version);
}

[[nodiscard]] inline uint32_t XORRotateLeft(const uint8_t* data, const size_t length)
{
  // a big thank you to Strilanc for figuring this out

  uint32_t i   = 0;
  uint32_t Val = 0;

  if (length > 3) {
    while (i < length - 3) {
      Val = ROTL(Val ^ ((uint32_t)data[i] + (uint32_t)(data[i + 1] << 8) + (uint32_t)(data[i + 2] << 16) + (uint32_t)(data[i + 3] << 24)), 3);
      i += 4;
    }
  }

  while (i < length) {
    Val = ROTL(Val ^ data[i], 3);
    ++i;
  }

  return Val;
}

[[nodiscard]] inline uint32_t ChunkedChecksum(const uint8_t* data, const size_t length, uint32_t checksum)
{
  size_t cursor = 0;
  size_t t = length - 0x400;
  while (cursor <= t) {
    checksum = ROTL(checksum ^ XORRotateLeft(data + cursor, 0x400), 3);
    cursor += 0x400;
  }
  return checksum;
}

[[nodiscard]] inline Version Get24PlayersMinGameVersion()
{
  return GAMEVER(1u, 29u);
}

[[nodiscard]] inline bool GetIs24PlayersGameVersion(const Version& version)
{
  return version >= Get24PlayersMinGameVersion();
}

#undef ROTL
#undef ROTR

#endif // AURA_MAP_H_
