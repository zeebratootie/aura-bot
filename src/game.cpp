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

#include <crc32/crc32.h>

#include "game.h"
#include "game_interactive_host.h"
#include "game_result.h"
#include "game_structs.h"
#include "command.h"
#include "aura.h"
#include "util.h"
#include "config/config.h"
#include "config/config_bot.h"
#include "config/config_game.h"
#include "config/config_commands.h"
#include "integration/irc.h"
#include "socket.h"
#include "net.h"
#include "game_controller_data.h"
#include "auradb.h"
#include "realm.h"
#include "map.h"
#include "connection.h"
#include "game_user.h"
#include "game_virtual_user.h"
#include "protocol/game_protocol.h"
#include "protocol/gps_protocol.h"
#include "protocol/vlan_protocol.h"
#include "proxy/gproxy_server.h"
#include "stats/dota.h"
#include "stats/w3mmd.h"
#include "integration/irc.h"
#include "file_util.h"

#include <bitset>
#include <ctime>

using namespace std;

#define LOG_APP_IF(T, U) \
  do {\
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_IF for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      LogApp(U, LOG_C); \
    }\
  } while (0)

#define LOG_APP_IF_CUSTOM(T, U, V) \
  do {\
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_IF_CUSTOM for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      LogApp(U, V); \
    }\
  } while (0)


#ifdef DEBUG
#define DLOG_APP_IF(T, U) \
  do {\
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_IF for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      LogApp(U, LOG_C); \
    }\
  } while (0)

#define DLOG_APP_IF_CUSTOM(T, U, V) \
  do {\
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_IF_CUSTOM for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      LogApp(U, V); \
    }\
  } while (0)
#else
#define DLOG_APP_IF(T, U) do {} while (0)
#define DLOG_APP_IF_CUSTOM(T, U, V) do {} while (0)
#endif

constexpr uint8_t NOT_ACTION_SOURCE_OBSERVER = NOT_TINY(ACTION_SOURCE_OBSERVER);

//
// CGame
//

CGame::CGame(CAura* nAura, shared_ptr<CGameSetup> nGameSetup)
  : m_Aura(nAura),
    m_Config(CGameConfig(nAura->m_GameDefaultConfig, nGameSetup->m_Map, nGameSetup)),
    m_Verbose(nGameSetup->m_Verbose),
    m_Socket(nullptr),
    m_LastLeaverBannable(nullptr),
    m_CustomStats(nullptr),
    m_DotaStats(nullptr),
    m_GameInteractiveHost(nullptr),
    m_RestoredGame(nGameSetup->m_RestoredGame),
    m_CurrentActionsFrame(nullptr),
    m_Map(nGameSetup->m_Map),
    m_GameFlags(0),
    m_PauseUser(nullptr),
    m_GameName(nGameSetup->m_Name),
    m_CreationCounter(nGameSetup->m_CreationCounter),
    m_PersistentId(nAura->NextHistoryGameID()),
    m_FromAutoReHost(nGameSetup->m_LobbyAutoRehosted),
    m_OwnerLessLocked(nGameSetup->m_OwnerLessLocked),
    m_OwnerName(nGameSetup->m_Owner.first),
    m_OwnerRealm(nGameSetup->m_Owner.second),
    m_Creator(nGameSetup->m_Creator),
    m_CreatorText(nGameSetup->m_Attribution),
    m_RealmsExcluded(nGameSetup->m_RealmsExcluded),
    m_MapPath(nGameSetup->m_Map->GetClientPath()),
    m_MapSiteURL(nGameSetup->m_Map->GetMapSiteURL()),
    m_CreationTime(nAura->GetClockTime()),
    m_LastPingTicks(APP_MIN_TICKS),
    m_LastDiscoveryTicks(APP_MIN_TICKS),
    m_LastCheckActionsTicks(APP_MIN_TICKS),
    m_LastRefreshTime(nAura->GetClockTime()),
    m_LastDownloadCounterResetTicks(nAura->GetClockTicks()),
    m_LastCountDownTicks(APP_MIN_TICKS),
    m_StartedLoadingTicks(0),
    m_FinishedLoadingTicks(0),
    m_MapGameStartTime(0),
    m_EffectiveTicks(0),
    m_LatencyTicks(0),
    m_NextLatencyTicks(0),
    m_LastActionSentTicks(0),
    m_LastActionExpectedTicks(0),
    m_LastPausedTicks(0),
    m_PausedTicksDeltaSum(0),
    m_StartedLaggingTime(0),
    m_LastLagScreenTime(0),
    m_LastLagStartCheckTime(APP_MIN_TICKS),
    m_PingReportedSinceLagTimes(0),
    m_LagStartMinPlayersFrames(0),
    m_LagStopMaxPlayersFrames(0),
    m_LagStartMinObserversFrames(0),
    m_LagStopMaxObserversFrames(0),
    m_LastUserSeenTicks(nAura->GetClockTicks()),
    m_LastOwnerSeenTicks(nAura->GetClockTicks()),
    m_StartedKickVoteTime(0),
    m_LastStatsUpdateTime(0),
    m_GameOver(GAME_ONGOING),
    m_LastLagScreenResetTime(0),
    m_RandomSeed(0),
    m_HostCounter(nGameSetup->GetGameIdentifier()),
    m_EntryKey(nGameSetup->GetEntryKey()),
    m_SyncCounter(0),
    m_SyncCounterChecked(0),
    m_PingEqualizerMaxFrames(1),
    m_PingEqualizerActiveDelayFrames(0),
    m_LastPingEqualizerGameTicks(0),
    m_CountDownCounter(0),
    m_StartPlayers(0),
    m_ControllersBalanced(false),
    m_ControllersReadyCount(0),
    m_ControllersNotReadyCount(0),
    m_ControllersWithMap(0),
    m_CustomLayout(nGameSetup->m_CustomLayout.value_or(MAPLAYOUT_ANY)),
    m_CustomLayoutData(make_pair(nGameSetup->m_Map->GetVersionMaxSlots(), nGameSetup->m_Map->GetVersionMaxSlots())),
    m_HostPort(0),
    m_PublicHostOverride(nGameSetup->GetIsMirror()),
    m_RealmsDisplayMode(nGameSetup->m_RealmsDisplayMode),
    m_IsAutoVirtualPlayers(false),
    m_VirtualHostUID(0xFF),
    m_GProxyEmptyActions(0),
    m_Destroying(false),
    m_Exiting(false),
    m_ExitingSoon(false),
    m_SlotInfoChanged(SLOTS_UNCHANGED),
    m_JoinedVirtualHosts(0),
    m_ReconnectProtocols(0),
    m_Replaceable(nGameSetup->m_LobbyReplaceable),
    m_Replacing(false),
    m_PublicStart(false),
    m_Locked(false),
    m_ChatOnly(false),
    m_MuteAll(false),
    m_ChatEnabled(false),
    m_IsMirror(nGameSetup->GetIsMirror()),
    m_IsMirrorProxy(false),
    m_CountDownStarted(false),
    m_CountDownFast(false),
    m_CountDownUserInitiated(false),
    m_GameLoading(false),
    m_GameLoaded(false),
    m_LobbyLoading(false),
    m_IsLagging(false),
    m_IsPaused(false),
    m_IsDraftMode(false),
    m_IsHiddenPlayerNames(false),
    m_HadLeaver(false),
    m_CheckReservation(nGameSetup->m_ChecksReservation.value_or(nGameSetup->m_RestoredGame != nullptr)),
    m_UsesCustomReferees(false),
    m_SentPriorityWhois(false),
    m_Remaking(false),
    m_Remade(false),
    m_SaveOnLeave(SAVE_ON_LEAVE_AUTO),
    m_GameResultSourceOfTruth(nGameSetup->m_ResultSource.has_value() ? nGameSetup->m_ResultSource.value() : nGameSetup->m_Map->GetGameResultSourceOfTruth()),
    m_IsSinglePlayer(false),
    m_Rated(false),
    m_HMCEnabled(false),
    m_BufferingEnabled(BUFFERING_ENABLED_NONE),
    m_BeforePlayingEmptyActions(0),
    m_APMTrainerPaused(false),
    m_APMTrainerTicks(0),
    m_GameHistory(make_shared<GameHistory>()),
    m_GameResultsSource(GameResultSource::kNone),
    m_SupportedGameVersionsMin(GAMEVER(0xFF, 0xFF)),
    m_SupportedGameVersionsMax(GAMEVER(0u, 0u)),
    m_GameDiscoveryActive(false),
    m_GameDiscoveryPending(false),
    m_GameDiscoveryInfoChanged(GAME_DISCOVERY_CHANGED_NEW),
    m_GameDiscoveryInfoVersionOffset(0),
    m_GameDiscoveryInfoDynamicOffset(0)
{
  if (!m_Config.m_Valid) {
    m_Exiting = true;
    return;
  }

  InitGameVersions();
  m_ChatEnabled = m_Config.m_EnableLobbyChat;
  m_IsHiddenPlayerNames = m_Config.m_HideLobbyNames;
  InitHCL(nGameSetup);
  InitGameFlags();
  m_LatencyTicks = m_NextLatencyTicks;

  if (!nGameSetup->GetIsMirror()) {
    for (const auto& userName : nGameSetup->m_Reservations) {
      AddToReserved(userName);
    }

    m_RandomSeed = GetRandomUInt32();

    ResetLatency();

    // wait time of 1 minute  = 0 empty actions required
    // wait time of 2 minutes = 1 empty action required...

    if (m_Aura->m_Net.m_Config.m_ReconnectWaitTicksLegacy > 0) {
      m_GProxyEmptyActions = signed_cast_lossy<uint8_t>(m_Aura->m_Net.m_Config.m_ReconnectWaitTicksLegacy / 60000 - 1);
    }

    // start listening for connections
    if (!InitNet()) {
      m_Exiting = true;
    }

    // Only maps in <bot.maps_path>
    if (m_Map->GetMapFileIsFromManagedFolder()) {
      auto it = m_Aura->m_MapFilesTimedBusyLocks.find(m_Map->GetServerPath());
      if (it == m_Aura->m_MapFilesTimedBusyLocks.end()) {
        m_Aura->m_MapFilesTimedBusyLocks[m_Map->GetServerPath()] = make_pair<int64_t, uint16_t>(m_Aura->GetClockTicks(), (uint16_t)0u);
      } else {
        it->second.first = m_Aura->GetClockTicks();
        it->second.second++;
      }
    }
  } else {
    const sockaddr_storage* address = nGameSetup->GetGameAddress();
    if (address) {
      SetIsCheckJoinable(false);
      m_PublicHostAddress = AddressToIPv4Array(address);
      m_PublicHostPort = GetAddressPort(address);
      m_IsMirrorProxy = nGameSetup->GetMirror().GetIsProxyEnabled();
      if (nGameSetup->GetMirror().GetHasEntryKey() && !m_IsMirrorProxy) {
        m_RealmsDisplayMode = GAME_DISPLAY_NONE;
      }
    }
    if (!address || (m_IsMirrorProxy && !InitNet())) {
      m_Exiting = true;
    }
  }

  InitSlots();
  UpdateReadyCounters();

  if (!m_IsMirror) {
    InitAutoStart(nGameSetup);
  }

#ifdef PROFILING
  for (size_t i = 0; i < 30; i++) {
    m_FrameDrifts[i] = 0;
  }
#endif
}

void CGame::InitSlots()
{
  m_SlotsConfig.SetLayout(CalcSlotsLayout());
  m_SlotsConfig.SetObserverSentinel(CalcObserverTeam());

  if (m_RestoredGame) {
    uint8_t i = 0xFF;
    m_SlotsConfig.slots = m_RestoredGame->GetSlots();
    // reset user slots
    for (auto& slot : m_SlotsConfig.slots) {
      if (slot.GetIsPlayerOrFake()) {
        slot.SetUID(++i);
        slot.SetDownloadStatus(100);
        slot.SetSlotStatus(SLOTSTATUS_OPEN);
      }
    }
    return;
  }

  // Done at the CGame level rather than CMap,
  // so that Aura is able to deal with outdated/bugged map configs.

  m_SlotsConfig.slots = m_Map->GetSlots();

  const bool useObservers = m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat || m_Map->GetGameObservers() == GameObserversMode::kReferees;

  // Match actual observer slots to set map flags.
  if (!useObservers) {
    CloseObserverSlots();
  }

  const bool customForces = m_Map->GetMapOptions() & MAPOPT_CUSTOMFORCES;
  const bool fixedPlayers = m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS;
  bitset<MAX_SLOTS_MODERN> usedColors;
  for (auto& slot : m_SlotsConfig.slots) {
    slot.SetUID(0);
    slot.SetDownloadStatus(SLOTPROG_RST);

    if (!fixedPlayers) {
      slot.SetType(SLOTTYPE_USER);
    } else switch (slot.GetType()) {
      case SLOTTYPE_USER:
        break;
      case SLOTTYPE_COMP:
        slot.SetComputer(SLOTCOMP_YES);
        break;
      default:
        // Treat every other value as SLOTTYPE_AUTO
        // CMap should never set SLOTTYPE_NONE
        // I bet that we don't need to set SLOTTYPE_NEUTRAL nor SLOTTYPE_RESCUEABLE either,
        // since we already got <map.num_disabled>
        if (slot.GetIsComputer()) {
          slot.SetType(SLOTTYPE_COMP);
        } else {
          slot.SetType(SLOTTYPE_USER);
        }
        break;
    }

    if (slot.GetComputer() > 0) {
      // The way WC3 client treats computer slots defined from WorldEdit depends on the
      // Fixed Player Settings flag:
      //  - OFF: Any computer slots are ignored, and they are treated as Open slots instead.
      //  - ON: Computer slots are enforced. They cannot be removed, or edited in any way.
      //
      // For Aura, enforcing computer slots with Fixed Player Settings ON is a must.
      // However, we can support default editable computer slots when it's OFF, through mapcfg files.
      //
      // All this also means that when Fixed Player Settings is off, there are no unselectable slots.
      slot.SetComputer(SLOTCOMP_YES);
      slot.SetSlotStatus(SLOTSTATUS_OCCUPIED);
    } else {
      //slot.SetComputer(SLOTCOMP_NO);
      slot.SetSlotStatus(slot.GetSlotStatus() & SLOTSTATUS_VALID_INITIAL_NON_COMPUTER);
    }

    if (!slot.GetIsSelectable()) {
      // There is no way to define default handicaps/difficulty using WorldEdit,
      // and unselectable cannot be changed in the game lobby.
      slot.SetHandicap(100);
      slot.SetComputerType(SLOTCOMP_NORMAL);
    } else {
      // Handicap valid engine values are 50, 60, 70, 80, 90, 100
      // The other 250 uint8 values may be set on-the-fly by Aura,
      // and are used by maps that implement HCL.
      //
      // Aura supports default handicaps through mapcfg files.
      uint8_t handicap = slot.GetHandicap() / 10;
      if (handicap < 5) handicap = 5;
      if (handicap > 10) handicap = 10;
      slot.SetHandicap(handicap * 10);
      slot.SetComputerType(slot.GetComputerType() & SLOTCOMP_VALID);
    }

    if (!customForces) {
      // default user-customizable slot is always observer
      // only when users join do we assign them a team
      // (if they leave, the slots are reset to observers)
      slot.SetTeam(GetObserverTeam());
    }

    // Ensure colors are unique for each playable slot.
    // Observers must have color 12 or 24, according to game version.
    if (slot.GetTeam() == GetObserverTeam()) {
      slot.SetColor(GetObserverColor());
    } else {
      const uint8_t originalColor = slot.GetColor();
      if (usedColors.test(originalColor)) {
        uint8_t testColor = FindNextAvailableBit(usedColors, originalColor, GetObserverColor());
        slot.SetColor(testColor);
        usedColors.set(testColor);
      } else {
        usedColors.set(originalColor);
      }
    }

    // When Fixed Player Settings is enabled, GAMEFLAG_RANDOMRACES cannot be turned on.
    if (!fixedPlayers && (m_Map->GetMapFlags() & GAMEFLAG_RANDOMRACES)) {
      slot.SetRace(SLOTRACE_RANDOM);
    } else {
      // Ensure race is unambiguous. It's defined as a bitfield,
      // so we gotta unset contradictory bits.
      bitset<8> slotRace(slot.GetRace());
      slotRace.reset(7);
      if (fixedPlayers) {
        // disable SLOTRACE_SELECTABLE
        slotRace.reset(6);
      } else {
        // enable SLOTRACE_SELECTABLE
        slotRace.set(6);
      }
      slotRace.reset(4);
      uint8_t chosenRaceBit = 5; // SLOTRACE_RANDOM
      bool foundRace = false;
      while (chosenRaceBit--) {
        // Iterate backwards so that SLOTRACE_RANDOM is preferred
        // Why? Because if someone edited the mapcfg with an ambiguous race,
        // it's likely they don't know what they are doing.
        if (foundRace) {
          slotRace.reset(chosenRaceBit);
        } else {
          foundRace = slotRace.test(chosenRaceBit);
        }
      }
      if (!foundRace) { // Slot is missing a default race.
        chosenRaceBit = 5; // SLOTRACE_RANDOM
        slotRace.set(chosenRaceBit);
        while (chosenRaceBit--) slotRace.reset(chosenRaceBit);
      }
      slot.SetRace(static_cast<uint8_t>(slotRace.to_ulong()));
    }
  }

  if (useObservers) {
    OpenObserverSlots();
  }

  if (m_Map->GetHMCEnabled()) {
    CreateHMCPlayer();
  }
}

bool CGame::InitNet()
{
  uint16_t hostPort = m_Aura->m_Net.NextHostPort();
  m_Socket = m_Aura->m_Net.GetOrCreateTCPServer(hostPort, Concat("Game <<", GetShortNameLAN(), ">>"));

  if (!m_Socket) {
    return false;
  }

  m_HostPort = m_Socket->GetPort();
  vector<pair<uint8_t, GameDiscoveryInterface>> interfaces;
  interfaces.resize(3);
  {
    uint8_t type = GAME_DISCOVERY_INTERFACE_LOOPBACK;
    interfaces[0].first = type;
    interfaces[0].second.SetType(type);
    interfaces[0].second.SetPort(CalcHostPortFromType(type));
  }
  {
    uint8_t type = GAME_DISCOVERY_INTERFACE_IPV4;
    interfaces[1].first = type;
    interfaces[1].second.SetType(type);
    interfaces[1].second.SetPort(CalcHostPortFromType(type));
  }
  {
    uint8_t type = GAME_DISCOVERY_INTERFACE_IPV6;
    interfaces[2].first = type;
    interfaces[2].second.SetType(type);
    interfaces[2].second.SetPort(CalcHostPortFromType(type));
  }

  for (auto& entry : interfaces) {
    // MDNS doesn't distinguish between IPv4 and IPv6.
    GameDiscoveryInterface& interface = entry.second;
    if (interface.GetType() == GAME_DISCOVERY_INTERFACE_IPV6) continue;
    InitMDNS(interface);
  }

  m_NetInterfaces = FlatMap<uint8_t, GameDiscoveryInterface>(move(interfaces));
  return true;
}

void CGame::InitMDNS(GameDiscoveryInterface& interface)
{
  Version version = std::max(m_SupportedGameVersionsMin, GAMEVER(1u, 30u));
  while (version <= m_SupportedGameVersionsMax) {
    if (!GetIsSupportedGameVersion(version)) continue;
    // called from the constructor, so can't use shared_from_this
    interface.AddMDNS(m_Aura, this, version);
    version = GetNextVersion(version);
  }
}

void CGame::ClearActions()
{
  m_Actions.reset();
}

void CGame::Reset()
{
  m_PauseUser = nullptr;
  m_HMCVirtualUser.reset();
  m_AHCLVirtualUser.reset();
  m_InertVirtualUser.reset();
  m_JoinInProgressVirtualUser.reset();
  m_FakeUsers.clear();
  m_PendingChatMessages.clear();
  if (!m_GameHistory->GetIsFinished()) {
    m_GameHistory->SetFinishedTicks(m_Aura->GetClockTicks());
    m_GameHistory->UpdateSpectatorActions((int64_t)m_Config.m_SpectatorDelay);
  }
  m_GameHistory.reset();
  m_GameResults.reset();

  for (auto& entry : m_SyncPlayers) {
    entry.second.clear();
  }
  m_SyncPlayers.clear();

  ClearActions();

  if (m_GameLoaded) {
    RunGameResults();
  }

  if (m_GameLoaded && m_Config.m_SaveStats) {
    TrySaveStats();
  }

  for (auto& controllerData : m_GameControllers) {
    delete controllerData;
  }
  m_GameControllers.clear();

  ClearBannableUsers();

  DestroyStats();
  DestroyHMC();

  for (auto& realm : m_Aura->m_Realms) {
    realm->ResetGameChatAnnouncement();
  }
  ResetLatency();
}

CGameController* CGame::GetGameControllerFromColor(uint8_t color) const
{
  if (color == GetObserverColor()) {
    // observer color is ambiguous
    return nullptr;
  }
  for (const auto& controllerData : m_GameControllers) {
    if (controllerData && controllerData->GetColor() == color) {
      return controllerData;
    }
  }
  return nullptr;
}

void CGame::StoreGameControllers()
{
  m_GameControllers.reserve(m_SlotsConfig.GetCount());
  for (uint8_t SID = 0, slotCount = static_cast<uint8_t>(m_SlotsConfig.GetCount()); SID < slotCount; ++SID) {
    // Do not exclude observers yet, so that they can be searched in commands.
    const CGameSlot* slot = InspectSlot(SID);
    if (slot->GetSlotStatus() != SLOTSTATUS_OCCUPIED) {
      continue;
    }
    const IndexedGameSlot idxSlot = IndexedGameSlot(SID, slot);
    if (!slot->GetIsPlayerOrFake()) {
      m_GameControllers.push_back(new CGameController(m_Map->GetMapAIType(), idxSlot));
      continue;
    }
    const GameUser::CGameUser* user = GetUserFromSID(SID);
    if (user) {
      m_GameControllers.push_back(new CGameController(user, idxSlot));
      continue;
    }
    const CGameVirtualUser* virtualUser = GetVirtualUserFromSID(SID);
    if (virtualUser) {
      m_GameControllers.push_back(new CGameController(virtualUser, idxSlot));
      continue;
    }
    m_GameControllers.push_back(nullptr);
  }
}

CGameController* CGame::GetGameControllerFromSID(uint8_t SID) const
{
  if (SID >= static_cast<uint8_t>(m_GameControllers.size())) {
    return nullptr;
  }
  return m_GameControllers[SID];
}

CGameController* CGame::GetGameControllerFromUID(uint8_t UID) const
{
  for (const auto& controllerData : m_GameControllers) {
    if (controllerData && controllerData->GetUID() == UID) {
      return controllerData;
    }
  }
  return nullptr;
}

bool CGame::InitStats()
{
  if (!m_Map->GetMMDEnabled()) {
    return false;
  }
  if (m_Map->GetMMDType() == MMD_TYPE_DOTA) {
    m_DotaStats = new Dota::CDotaStats(shared_from_this());
  } else {
    m_CustomStats = new CW3MMD(shared_from_this());
  }
  return true;
}

bool CGame::InitHMC()
{
  if (m_Map->GetHMCEnabled()) {
    const uint8_t SID = m_Map->GetHMCSlot();
    const CGameSlot* slot = InspectSlot(SID);
    if (slot && slot->GetIsPlayerOrFake() && !GetUserFromSID(SID)) {
      const CGameVirtualUser* virtualUserMatch = InspectVirtualUserFromSID(SID);
      if (virtualUserMatch && !virtualUserMatch->GetIsObserver()) {
        m_HMCEnabled = true;
      }
    }
  }

  if (!m_HMCEnabled) return false;

  m_GameInteractiveHost = new CGameInteractiveHost(shared_from_this(), m_Map->GetHMCFileName());
  return true;
}

bool CGame::EventGameCacheInteger(const uint8_t UID, string_view actionDetails)
{
  // ACTION_GAME_CACHE_INT
  if (!m_CustomStats && !m_DotaStats && !m_GameInteractiveHost) return false;

  string_view cacheFileName = ExtractStringView<OOBPolicy::kCheck, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(actionDetails, 0, 0);
  if (cacheFileName.empty()) {
    return false;
  }
  actionDetails.remove_prefix(cacheFileName.size() + 1);
  string_view missionKey = ExtractStringView<OOBPolicy::kCheck, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(actionDetails, 0, 0);
  if (missionKey.empty()) {
    return false;
  }
  actionDetails.remove_prefix(missionKey.size() + 1);
  string_view key = ExtractStringView<OOBPolicy::kCheck, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(actionDetails, 0, 0);
  if (key.empty()) {
    return false;
  }
  actionDetails.remove_prefix(key.size() + 1);
  if (actionDetails.size() != 4) {
    return false;
  }
  uint32_t value = ByteArrayToUInt32LE(actionDetails, 0);

  if (m_CustomStats) {
    if (!m_CustomStats->EventGameCacheInteger(UID, cacheFileName, missionKey, key, value)) {
      DestroyStats();
    }
  } else if (m_DotaStats) {
    if (!m_DotaStats->EventGameCacheInteger(UID, cacheFileName, missionKey, key, value)) {
      DestroyStats();
    }
  }

  if (m_GameInteractiveHost) {
    if (!m_GameInteractiveHost->EventGameCacheInteger(UID, cacheFileName, missionKey, key, value)) {
      DestroyHMC();
    }
  }

  return true;
}

bool CGame::UpdateStatsQueue() const
{
  // return false if game over was detected
  if (m_CustomStats) {
    return m_CustomStats->UpdateQueue();
  } else if (m_DotaStats) {
    return m_DotaStats->UpdateQueue();
  }
  return true;
}

void CGame::FlushStatsQueue() const
{
  if (m_CustomStats) m_CustomStats->FlushQueue();
  else if (m_DotaStats) m_DotaStats->FlushQueue();
}

void CGame::TrySaveStats() const
{
  // store the CDBGamePlayers in the database
  // add non-dota stats
  if (!m_GameControllers.empty()) {
    const int64_t hiResTicks = GetTicks();
    LOG_APP_IF(LogLevel::kDebug, "[STATS] saving game end player data to database");
    if (m_Aura->m_DB->Begin()) {
      const uint64_t gameTime = signed_cast<uint64_t>(m_EffectiveTicks / 1000);
      // FIXME: Max game time should be ensured elsewhere.
      assert(gameTime <= integer_cast<uint64_t>(numeric_limits<uint32_t>::max()) && "Game time limited to 1193 hours");
      for (auto& controllerData : m_GameControllers) {
        m_Aura->m_DB->UpdateGamePlayerOnEnd(m_PersistentId, controllerData, integer_cast_lossy<uint32_t>(gameTime));
      }
      if (!m_Aura->m_DB->Commit()) {
        LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to commit game end player data");
      } else {
        LOG_APP_IF(LogLevel::kDebug, Concat("[STATS] commited game end player data in ", to_string(GetTicks() - hiResTicks), " ms"));
      }
    } else {
      LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to begin transaction game end player data");
    }
  }

  if (m_DotaStats) {
    m_Aura->m_DB->SaveDotAStats(m_DotaStats);
  }
}

void CGame::DestroyStats()
{
  if (m_CustomStats) {
    delete m_CustomStats;
    m_CustomStats = nullptr;
  } else if (m_DotaStats) {
    delete m_DotaStats;
    m_DotaStats = nullptr;
  }
}

void CGame::DestroyHMC()
{
  if (m_GameInteractiveHost) {
    delete m_GameInteractiveHost;
    m_GameInteractiveHost = nullptr;
  }
}

void CGame::ReleaseMapBusyTimedLock() const
{
  // check whether the map is in <bot.maps_path>
  if (!m_Map->GetMapFileIsFromManagedFolder()) {
    return;
  }

  auto it = m_Aura->m_MapFilesTimedBusyLocks.find(m_Map->GetServerPath());
  if (it == m_Aura->m_MapFilesTimedBusyLocks.end()) {
    return;
  }

  it->second.first = m_Aura->GetClockTicks();
  if (--it->second.second > 0) {
    return;
  }

  const bool deleteTooLarge = (
    m_Aura->m_Config.m_EnableDeleteOversizedMaps &&
    (m_Map->GetMapSize() > m_Aura->m_Config.m_MaxSavedMapSize * 1024) &&
    // Ensure the mapcache ini file has been created before trying to delete from disk
    m_Aura->m_CFGCacheNamesByMapNames.find(m_Map->GetServerPath()) != m_Aura->m_CFGCacheNamesByMapNames.end()
  );

  if (deleteTooLarge) {
    // Release from disk
    m_Map->UnlinkFile();
  }
}

void CGame::StartGameOverTimer(bool isMMD)
{
  m_ExitingSoon = true;
  m_GameOver = isMMD ? GAME_OVER_MMD : GAME_OVER_TRUSTED;
  m_GameOverTime = m_Aura->GetClockTime();
  if (isMMD) {
    m_GameOverTolerance = 300;
  } else {
    m_GameOverTolerance = 60;
  }
  if (!m_GameHistory->GetIsFinished()) {
    m_GameHistory->SetFinishedTicks(m_Aura->GetClockTicks());
    m_GameHistory->UpdateSpectatorActions((int64_t)m_Config.m_SpectatorDelay);
  }

  if (GetNumJoinedUsers() > 0) {
    SendAllChat(Concat("Gameover timer started (disconnecting in ", to_string(m_GameOverTolerance.value_or(60)), " seconds...)"));
  }

  if (GetIsLobbyOrMirror()) {
    if (m_GameDiscoveryActive) {
      SendGameDiscoveryDecreate();
      m_GameDiscoveryActive = false;
    }
    if (m_RealmsDisplayMode != GAME_DISPLAY_NONE) {
      AnnounceDecreateToRealms(); // STOPADV @ ResetGameBroadcastData(), SEND_ENTERCHAT
    }
    m_ChatOnly = true;
    StopCountDown();
  }

  m_Aura->UntrackGameJoinInProgress(shared_from_this());
}

void CGame::LogFrameDrifts()
{
#ifdef PROFILING
  size_t maxBucketIndex = 0;
  uint64_t sum = 0;
  for (size_t i = 0; i < 30; i++) {
    if (m_FrameDrifts[i] > 0) {
      sum += m_FrameDrifts[i];
      maxBucketIndex = i;
    }
  }
  if (sum == 0) return;
  vector<string> frameDriftReport;
  frameDriftReport.reserve(maxBucketIndex + 1);
  vector<double> percents;
  percents.reserve(maxBucketIndex + 1);
  for (size_t i = 0; i <= maxBucketIndex; i++) {
    percents.push_back(static_cast<double>(100.) * static_cast<double>(m_FrameDrifts[i]) / static_cast<double>(sum));
  }
  uint64_t minRange = 0, maxRange = 5;
  frameDriftReport.push_back(Concat("[0-5>,", to_string(m_FrameDrifts[0]), ",", ToFormattedString(percents[0]), "%"));
  for (size_t i = 1; i <= maxBucketIndex; i++) {
    if (i >= 20) {
      minRange += 20;
      maxRange += 20;
    } else if (i >= 10) {
      minRange += 10;
      maxRange += 10;
    } else {
      minRange += 5;
      maxRange += 5;
    }
    if (i == 10) minRange -= 5;
    if (i == 20) minRange -= 10;
    frameDriftReport.push_back(Concat("[", to_string(minRange), "-", to_string(maxRange), ">,", to_string(m_FrameDrifts[i]), ",", ToFormattedString(percents[i]), "%"));
  }
  for (const auto& line : frameDriftReport) {
    //LogApp(line, LOG_C | LOG_P);
    Print(line);
  }
#endif
}

CGame::~CGame()
{
  m_Destroying = true;
  LogFrameDrifts();
  Reset();
  ReleaseMapBusyTimedLock();

  m_Socket.reset();
  m_Aura->m_Net.ClearStaleServers();

  for (auto& user : m_Users) {
    delete user;
  }

  if (GetIsBeingReplaced()) {
    --m_Aura->m_ReplacingLobbiesCounter;
  }
}

template <typename T>
shared_ptr<T> CGame::GetCreatedFrom() const
{
  return m_Creator.GetService<T>();
}

template shared_ptr<CGame> CGame::GetCreatedFrom() const;
template shared_ptr<CRealm> CGame::GetCreatedFrom() const;

bool CGame::MatchesCreatedFrom(const ServiceType fromType) const
{
  return m_Creator.GetServiceType() == fromType;
}

bool CGame::MatchesCreatedFrom(const ServiceType fromType, shared_ptr<const void> fromThing) const
{
  if (m_Creator.GetServiceType() != fromType) return false;
  switch (fromType) {
    case ServiceType::kGame:
      return static_pointer_cast<const CGame>(fromThing) == GetCreatedFrom<const CGame>();
    case ServiceType::kRealm:
      return static_pointer_cast<const CRealm>(fromThing) == GetCreatedFrom<const CRealm>();
    default:
      return true;
  }
}

bool CGame::MatchesCreatedFromGame(shared_ptr<const CGame> nGame) const
{
  return MatchesCreatedFrom(ServiceType::kGame, static_pointer_cast<const void>(nGame));
}

bool CGame::MatchesCreatedFromRealm(shared_ptr<const CRealm> nRealm) const
{
  return MatchesCreatedFrom(ServiceType::kRealm, static_pointer_cast<const void>(nRealm));
}

bool CGame::MatchesCreatedFromIRC() const
{
  return MatchesCreatedFrom(ServiceType::kIRC);
}

bool CGame::MatchesCreatedFromDiscord() const
{
  return MatchesCreatedFrom(ServiceType::kDiscord);
}

uint8_t CGame::CalcSlotsLayout() const
{
  if (m_RestoredGame) return MAPLAYOUT_FIXED_PLAYERS;
  return GetMap()->GetMapLayoutStyle();
}

bool CGame::GetIsCustomForces() const
{
  if (m_RestoredGame) return true;
  return GetMap()->GetMapLayoutStyle() != MAPLAYOUT_ANY;
}

template <int64_t factor>
void CGame::UpdateSelectBlockTime(int64_t& blockTime) const
{
  // return the number of ticks (ms) until the next "timed action", which for our purposes is the next game update
  // the main Aura loop will make sure the next loop update happens at or before this value
  // note: this function COULD take into account other timers in this game, but they're far less critical
  // note: this function MUST take into account when actions are not being sent (e.g. during loading or lagging)

  if (!m_GameLoaded || m_IsLagging || blockTime == 0) {
    return;
  }

  const int64_t ticksSinceLastUpdateExpected = m_Aura->GetClockTicks() - m_LastActionExpectedTicks;

  if (ticksSinceLastUpdateExpected > m_LatencyTicks) {
    blockTime = 0;
    return;
  }

  int64_t maybeBlockTime = (m_LatencyTicks - ticksSinceLastUpdateExpected) * factor;
  if (maybeBlockTime < blockTime) {
    blockTime = maybeBlockTime;
  }
}

template void CGame::UpdateSelectBlockTime<1000>(int64_t& blockTime) const; // microseconds
template void CGame::UpdateSelectBlockTime<1>(int64_t& blockTime) const; // milliseconds

uint8_t CGame::CalcObserverTeam() const
{
  return GetMaxPlayersForGameVersion(GetVersion());
}

uint8_t CGame::CalcObserverColor() const
{
  return GetMaxPlayersForGameVersion(GetVersion());
}

uint8_t CGame::GetObserverTeam() const
{
  return m_SlotsConfig.GetObserverSentinel();
}

uint8_t CGame::GetObserverColor() const
{
  return m_SlotsConfig.GetObserverSentinel();
}

uint8_t CGame::GetMinControllerInvalidColor() const
{
  if (m_Map->GetModernColorsEnabled()) {
    return MAX_SLOTS_MODERN;
  } else {
    return MAX_SLOTS_LEGACY;
  }
}

uint32_t CGame::GetNumSlotsOccupied() const
{
  return m_SlotsConfig.GetOccupiedCount();
}

uint32_t CGame::GetNumSlotsOpen() const
{
  return m_SlotsConfig.GetOpenCount();
}

bool CGame::HasSlotsOpen() const
{
  return m_SlotsConfig.GetIsAnyOpen();
}

bool CGame::GetArePlayersSameVersion() const
{
  if (m_Users.empty()) return true;
  Version gameVersion = m_Users[0]->GetGameVersion();
  for (const auto& user : m_Users) {
    if (user->GetGameVersion() != gameVersion) {
      return false;
    }
  }
  return true;
}

bool CGame::GetArePlayersSameVersionRange() const
{
  if (m_Users.empty()) return true;
  Version gameVersion = GetScriptsVersionRangeHead(m_Users[0]->GetGameVersion());
  for (const auto& user : m_Users) {
    if (GetScriptsVersionRangeHead(user->GetGameVersion()) != gameVersion) {
      return false;
    }
  }
  return true;
}

bool CGame::GetArePlayersSameSlotsProtocol() const
{
  if (m_Users.empty()) return true;
  bool supports24 = GetIs24PlayersGameVersion(m_Users[0]->GetGameVersion());
  for (const auto& user : m_Users) {
    if (GetIs24PlayersGameVersion(user->GetGameVersion()) != supports24) {
      return false;
    }
  }
  return true;
}

bool CGame::GetIsSinglePlayerMode() const
{
  return GetNumJoinedUsersOrFake() < 2;
}

bool CGame::GetHasAnyFullObservers() const
{
  return m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat && GetNumJoinedObservers() >= 1;
}

bool CGame::GetHasChatSendHost() const
{
  if (GetHasChatSendPermaHost()) return true;
  if (GetHasAnyFullObservers()) {
    return GetNumJoinedPlayersOrFake() >= 2;
  } else {
    return GetNumJoinedPlayersOrFakeUsers() >= 2;
  }
}

bool CGame::GetHasChatRecvHost() const
{
  if (GetHasChatRecvPermaHost()) return true;
  if (m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat && GetNumJoinedObservers() == 1) return false;
  return GetNumJoinedPlayersOrFakeUsers() >= 2;
}

bool CGame::GetHasChatSendPermaHost() const
{
  return GetNumFakePlayers() > 0 || (m_Map->GetGameObservers() == GameObserversMode::kReferees && GetNumFakeObservers() > 0);
}

bool CGame::GetHasChatRecvPermaHost() const
{
  if (GetNumFakeObservers() > 0) return true;
  return GetNumFakePlayers() > 0 && !GetHasAnyFullObservers();
}

uint32_t CGame::GetNumJoinedUsers() const
{
  uint32_t counter = 0;

  for (const auto& user : m_Users) {
    if (user->GetDeleteMe() || user->GetDisconnectedUnrecoverably())
      continue;

    ++counter;
  }

  return counter;
}

uint32_t CGame::GetNumJoinedUsersOrFake() const
{
  uint32_t counter = static_cast<uint8_t>(m_FakeUsers.size());

  for (const auto& user : m_Users) {
    if (user->GetDeleteMe() || user->GetDisconnectedUnrecoverably())
      continue;

    ++counter;
  }

  return counter;
}

uint8_t CGame::GetNumJoinedPlayers() const
{
  uint8_t counter = 0;

  for (const auto& user : m_Users) {
    if (user->GetDeleteMe() || user->GetDisconnectedUnrecoverably())
      continue;
    if (user->GetIsObserver())
      continue;

    ++counter;
  }

  return counter;
}

uint8_t CGame::GetNumJoinedObservers() const
{
  uint8_t counter = 0;

  for (const auto& user : m_Users) {
    if (user->GetDeleteMe() || user->GetDisconnectedUnrecoverably())
      continue;
    if (!user->GetIsObserver())
      continue;

    ++counter;
  }

  return counter;
}

uint8_t CGame::GetNumFakePlayers() const
{
  uint8_t counter = 0;
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    if (!fakeUser.GetIsObserver()) {
      ++counter;
    }
  }
  return counter;
}

uint8_t CGame::GetNumFakeObservers() const
{
  uint8_t counter = 0;
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    if (fakeUser.GetIsObserver()) {
      ++counter;
    }
  }
  return counter;
}

size_t CGame::GetNumSpectators() const
{
  size_t counter = 0;
  for (const auto& serverConnections : m_Aura->m_Net.m_GameObservers) {
    // std::pair<uint16_t, vector<CAsyncObserver*>>
    for (const auto& spectator : serverConnections.second) {
      if (!spectator->GetFinishedLoading()) continue;
      if (spectator->GetGame() == shared_from_this()) {
        ++counter;
      }
    }
  }
  return counter;
}

uint8_t CGame::GetNumJoinedPlayersOrFake() const
{
  return PLUS_TINY(GetNumJoinedPlayers(), GetNumFakePlayers());
}

uint8_t CGame::GetNumJoinedObserversOrFake() const
{
  return PLUS_TINY(GetNumJoinedObservers(), GetNumFakeObservers());
}

uint8_t CGame::GetNumJoinedPlayersOrFakeUsers() const
{
  uint8_t counter = static_cast<uint8_t>(m_FakeUsers.size());

  for (const auto& user : m_Users) {
    if (user->GetDeleteMe() || user->GetDisconnectedUnrecoverably())
      continue;
    if (user->GetIsObserver())
      continue;

    ++counter;
  }

  return counter;
}

uint8_t CGame::GetNumPotentialControllers() const
{
  uint8_t count = 0;
  for (const auto& slot : m_SlotsConfig.slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
      ++count;
    }
  }
  if (count > m_Map->GetMapNumControllers()) {
    return m_Map->GetMapNumControllers();
  }
  return count;
}

uint8_t CGame::GetNumControllers() const
{
  return m_SlotsConfig.GetOccupiedControllersCount();
}

uint8_t CGame::GetNumComputers() const
{
  return m_SlotsConfig.GetComputersCount();
}

uint8_t CGame::GetNumTeams() const
{
  return m_SlotsConfig.GetOccupiedTeamsCount();
}

uint8_t CGame::GetNumTeamControllersOrOpen(const uint8_t team) const
{
  uint8_t count = 0;
  for (const auto& slot : m_SlotsConfig.InspectAll()) {
    if (slot.GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    if (slot.GetTeam() == team) {
      ++count;
    }
  }
  return count;
}

string CGame::GetClientFileName() const
{
  size_t lastSlash = m_MapPath.rfind('\\');
  if (lastSlash == string::npos) {
    return m_MapPath;
  }
  return m_MapPath.substr(lastSlash + 1);
}

string CGame::GetGameSpectatorName() const
{
  if (!GetMap()->GetMapIsMelee()) {
    return m_GameName;
  }
  vector<const CGameController*> startingControllers;
  for (const auto& controllerData : m_GameControllers) {
    if (controllerData == nullptr || controllerData->GetIsObserver()) continue;
    startingControllers.push_back(controllerData);
  }
  string candidateName;
  const size_t numControllers = startingControllers.size();
  if (numControllers == 2) {
    candidateName = Concat(startingControllers[0]->GetName(), " vs ", startingControllers[1]->GetName());
  }
  if (!candidateName.empty() && candidateName.size() <= m_Aura->m_MaxGameNameSize && IsASCII(candidateName)) {
    return candidateName;
  }
  if (numControllers == 2) {
    vector<string> controllers;
    controllers.push_back(startingControllers[0]->GetShortName());
    controllers.push_back(startingControllers[1]->GetShortName());
    candidateName = JoinStrings(controllers, " vs ");
    if (candidateName.size() > m_Aura->m_MaxGameNameSize) {
      candidateName = "Melee VS";
    }
  } else if (m_CustomLayout == CUSTOM_LAYOUT_FFA) {
    candidateName = Concat("FFA ", to_string(numControllers), "P");
  }
  if (!candidateName.empty() && candidateName.size() <= m_Aura->m_MaxGameNameSize && IsASCII(candidateName)) {
    return candidateName;
  }
  return m_GameName;
}

string CGame::GetStatusDescription() const
{
  string gameName = GetShortNameLAN();
  if (m_IsMirror) {
     return Concat(EnsureWrapUTF8(GetMap()->GetMapTitle()), " (Mirror) \"", EnsureUTF8(gameName), "\"");
  }

  string description = Concat(
    EnsureWrapUTF8(GetMap()->GetMapTitle()), " \"", EnsureUTF8(gameName), "\" - ", EnsureUTF8(m_OwnerName), " - ",
    ToDecString(GetNumJoinedPlayersOrFake()),
    "/",
    ToDecString(m_GameLoading || m_GameLoaded ? m_ControllersWithMap : static_cast<uint8_t>(GetNumSlots()))
  );

  if (m_GameLoading || m_GameLoaded)
    description += Concat(" : ", to_string((m_EffectiveTicks / 1000) / 60), "min");
  else
    description += Concat(" : ", to_string((m_Aura->GetClockTime() - m_CreationTime) / 60), "min");

  return description;
}

string CGame::GetEndDescription(shared_ptr<const CRealm> realm) const
{
  if (m_IsMirror)
     return Concat("[", GetMap()->GetMapTitle(), "] (Mirror) \"", GetCustomGameName(realm, true), "\"");

  string winnersFragment;

  if (m_GameResults.has_value()) {
    vector<string> winnerNames = m_GameResults->GetWinnersNames();
    if (winnerNames.size() > 2) {
      winnersFragment = Concat("Winners: [", winnerNames[0], "], and others");
    } else if (winnerNames.size() == 2) {
      winnersFragment = Concat("Winners: [", winnerNames[0], "] and [", winnerNames[1], "]");
    } else if (winnerNames.size() == 1) {
      winnersFragment = Concat("Winner: [", winnerNames[0], "]");
    }
  }

  string description = Concat(
    "[", GetMap()->GetMapTitle(), "] \"", GetCustomGameName(realm, true), "\". ", winnersFragment
  );

  if (m_GameLoading || m_GameLoaded)
    description += Concat(" : ", to_string((m_EffectiveTicks / 1000) / 60), "min");
  else
    description += Concat(" : ", to_string((m_Aura->GetClockTime() - m_CreationTime) / 60), "min");

  return description;
}

string CGame::GetCategory() const
{
  if (m_GameLoading || m_GameLoaded)
    return "GAME";

  return "LOBBY";
}

string CGame::GetLogPrefix() const
{
  string MinString = to_string((m_EffectiveTicks / 1000) / 60);
  string SecString = to_string((m_EffectiveTicks / 1000) % 60);

  if (MinString.size() == 1)
    MinString.insert(0, "0");

  if (SecString.size() == 1)
    SecString.insert(0, "0");

  if (m_GameLoaded && m_Aura->GetIsLoggingTrace()) {
    return Concat("[", GetCategory(), ": ", GetShortNameLAN(), " | Frame ", to_string(m_SyncCounter), "] ");
  } else {
    return Concat("[", GetCategory(), ": ", GetShortNameLAN(), "] ");
  }
}

ImmutableUserList CGame::GetPlayers() const
{
  ImmutableUserList players;
  for (const auto& user : m_Users) {
    if (!user->GetLeftMessageSent() && !user->GetIsObserver()) {
      // Check GetLeftMessageSent instead of GetDeleteMe for debugging purposes
      players.push_back(user);
    }
  }
  return players;
}

ImmutableUserList CGame::GetObservers() const
{
  ImmutableUserList observers;
  for (const auto& user : m_Users) {
    if (!user->GetLeftMessageSent() && user->GetIsObserver()) {
      // Check GetLeftMessageSent instead of GetDeleteMe for debugging purposes
      observers.push_back(user);
    }
  }
  return observers;
}

ImmutableUserList CGame::GetUnreadyPlayers() const
{
  ImmutableUserList players;
  for (const auto& user : m_Users) {
    if (!user->GetLeftMessageSent() && !user->GetIsObserver()) {
      if (!user->GetIsReady()) {
        players.push_back(user);
      }
    }
  }
  return players;
}

ImmutableUserList CGame::GetWaitingReconnectPlayers() const
{
  ImmutableUserList players;
  for (const auto& user : m_Users) {
    if (!user->GetLeftMessageSent() && user->GetDisconnected() && user->GetCanReconnect()) {
      players.push_back(user);
    }
  }
  return players;
}

vector<CAsyncObserver*> CGame::GetSpectators() const
{
  vector<CAsyncObserver*> spectators;
  for (const auto& serverConnections : m_Aura->m_Net.m_GameObservers) {
    // std::pair<uint16_t, vector<CAsyncObserver*>>
    for (const auto& spectator : serverConnections.second) {
      if (!spectator->GetFinishedLoading()) continue;
      if (spectator->GetGame() == shared_from_this()) {
        spectators.push_back(spectator);
      }
    }
  }
  return spectators;
}

uint32_t CGame::GetUptime() const
{
  const int64_t loopTime = m_Aura->GetClockTime();
  if (loopTime < m_CreationTime) return 0;
  return (uint32_t)(loopTime - m_CreationTime);
}

size_t CGame::GetFrameDriftBucket(int64_t actionLateBy) const
{
  uint64_t clamped = signed_cast<uint64_t>(clamp<int64_t>(actionLateBy, 0, 349));
  if (clamped < 50) return integer_cast_lossy<size_t>(clamped / 5);
  if (clamped < 150) return integer_cast_lossy<size_t>(10 + ((clamped - 50) / 10));
  return integer_cast_lossy<size_t>(20 + ((clamped - 150) / 20));
}

uint32_t CGame::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const
{
  uint32_t NumFDs = 0;

  for (auto& user : m_Users) {
    if (user->GetDisconnected()) continue;
    user->GetSocket()->SetFD(fd, send_fd, nfds);
    ++NumFDs;
  }

  return NumFDs;
}

void CGame::UpdateJoinable()
{
  // refresh metadata every 10 seconds

  if (m_Aura->GetTimeIsAfterDelay(m_LastRefreshTime, 10)) {
    // send a game refresh packet to each battle.net connection

    if (m_Aura->m_StartedGames.empty()) {
      // This is a lobby. Take the chance to update the detailed console title
      m_Aura->UpdateMetaData();
    }

    m_LastRefreshTime = m_Aura->GetClockTime();
  }

  if (m_IsMirror) {
    return;
  }

  // update map download progression indicators

  if (m_Aura->GetTicksIsAfterDelay(m_LastDownloadCounterResetTicks, 1000)) {
    // hackhack: another timer hijack is in progress here
    // since the download counter is reset once per second it's a great place to update the slot info if necessary

    if (m_SlotInfoChanged & SLOTS_DOWNLOAD_PROGRESS_CHANGED) {
      SendAllSlotInfo();
      //UpdateReadyCounters();
      UNSET_TINY(m_SlotInfoChanged, SLOTS_DOWNLOAD_PROGRESS_CHANGED);
    }

    m_LastDownloadCounterResetTicks = m_Aura->GetClockTicks();
  }
}

bool CGame::UpdateLobby()
{
  if (m_SlotInfoChanged & SLOTS_ALIGNMENT_CHANGED) {
    SendAllSlotInfo();
    UpdateReadyCounters();
    UNSET_TINY(m_SlotInfoChanged, SLOTS_ALIGNMENT_CHANGED);
  }

  if (GetIsAutoStartDue()) {
    SendAllChat("Game automatically starting in. . .");
    StartCountDown(false, true);
  }

  if (!m_Users.empty()) {
    m_LastUserSeenTicks = m_Aura->GetClockTicks();
    if (HasOwnerInGame()) {
      m_LastOwnerSeenTicks = m_LastUserSeenTicks;
    }
  }

  // countdown every m_LobbyCountDownInterval ms (default 500 ms)

  if (m_CountDownStarted && m_Aura->GetTicksIsAfterDelay(m_LastCountDownTicks, m_Config.m_LobbyCountDownInterval)) {
    bool shouldStartLoading = false;
    if (m_CountDownCounter > 0) {
      // we use a countdown counter rather than a "finish countdown time" here because it might alternately round up or down the count
      // this sometimes resulted in a countdown of e.g. "6 5 3 2 1" during my testing which looks pretty dumb
      // doing it this way ensures it's always "5 4 3 2 1" but each interval might not be *exactly* the same length

      SendAllChat(Concat(to_string(m_CountDownCounter--), ". . ."));
    } else if (GetNumJoinedUsers() >= 1) { // allow observing AI vs AI matches
      shouldStartLoading = true;
    } else {
      // Some operations may remove fake users during countdown.
      // Ensure that the game doesn't start if there are neither real nor fake users.
      // (If a user leaves or joins, the countdown is stopped elsewhere.)
      LOG_APP_IF(LogLevel::kDebug, "countdown stopped - lobby is empty.");
      StopCountDown();
    }

    m_LastCountDownTicks = m_Aura->GetClockTicks();
    if (shouldStartLoading) {
      EventGameStartedLoading();
      return true;
    }
  }

  // release abandoned lobbies, so other users can take ownership
  CheckLobbyTimeouts();

  if (m_Exiting) {
    return true;
  }

  // last action of CGame::UpdateLobby
  // try to create the virtual host user, if there are slots available
  //
  // ensures that all pending users' leave messages have already been sent
  // either at CGame::EventUserDeleted or at CGame::EventRequestJoin (reserve system kicks)
  if (!m_GameLoading && !GetHasVirtualHost() && HasSlotsOpen()) {
    CreateVirtualHost();
  }

  return false;
}

void CGame::UpdateLoading()
{
  bool finishedLoading = true;
  bool anyLoaded = false;
  for (auto& user : m_Users) {
    if (user->GetFinishedLoading()) {
      anyLoaded = true;
    } else if (!user->GetDisconnected()) {
      finishedLoading = false;
      break;
    }
  }

  if (finishedLoading) {
    if (anyLoaded) {
      EventGameBeforeLoaded();
      EventGameLoaded();
    } else {
      // Flush leaver queue to allow players and the game itself to be destroyed.
      SendAllActionsCallback();
    }
  } else {
    if (m_Config.m_LoadingTimeoutMode == GameLoadingTimeoutMode::kStrict) {
      if (m_Aura->GetTicksIsAfterDelay(m_StartedLoadingTicks, (int64_t)m_Config.m_LoadingTimeout)) {
        StopLoadPending(Concat("was automatically dropped after ", to_string(m_Config.m_LoadingTimeout / 1000), " seconds"));
      }
    }

    // Warcraft III disconnects if it doesn't receive an action packet for more than ~65 seconds
    if (m_Config.m_LoadInGame && anyLoaded && m_Aura->GetTimeIsAfterDelay(m_LastLagScreenResetTime, 60)) {
      ResetLagScreen();
    }
  }
}

void CGame::UpdateLoaded()
{
  const int64_t hiResTicks = GetTicks();

  // check if anyone has started lagging
  // we consider a user to have started lagging if they're more than m_SyncLimit keepalives behind

  if (!m_IsLagging) {
    if (m_Config.m_EnableLagScreen && m_Aura->GetTicksIsAfterDelay(m_LastLagStartCheckTime, 1000)) {
      string LaggingString;
      bool startedLagging = false;
      vector<size_t> framesBehind = GetUsersFramesBehind();
      uint8_t i = static_cast<uint8_t>(m_Users.size());
      while (i--) {
        if (framesBehind[i] > GetSyncLimit(m_Users[i]->GetIsObserver()) && !m_Users[i]->GetDisconnectedUnrecoverably()) {
          startedLagging = true;
          break;
        }
      }
      if (startedLagging) {
        uint8_t worstLaggerIndex = 0;
        uint8_t bestLaggerIndex = 0;
        size_t worstLaggerFrames = 0;
        size_t bestLaggerFrames = numeric_limits<size_t>::max();
        UserList laggingPlayers;
        i = static_cast<uint8_t>(m_Users.size());
        while (i--) {
          if (framesBehind[i] > GetSyncLimitSafe(m_Users[i]->GetIsObserver()) && !m_Users[i]->GetDisconnectedUnrecoverably()) {
            m_Users[i]->SetLagging(true);
            m_Users[i]->SetStartedLaggingTicks(m_Aura->GetClockTicks());
            m_Users[i]->ClearStalePings();
            laggingPlayers.push_back(m_Users[i]);
            if (framesBehind[i] > worstLaggerFrames) {
              worstLaggerIndex = i;
              worstLaggerFrames = framesBehind[i];
            }
            if (framesBehind[i] < bestLaggerFrames) {
              bestLaggerIndex = i;
              bestLaggerFrames = framesBehind[i];
            }
          }
        }
        if (laggingPlayers.size() == m_Users.size()) {
          // Avoid showing everyone as lagging
          m_Users[bestLaggerIndex]->SetLagging(false);
          m_Users[bestLaggerIndex]->ClearStartedLaggingTicks();
          laggingPlayers.erase(laggingPlayers.begin() + static_cast<ptrdiff_t>(m_Users.size() - 1 - bestLaggerIndex));
        }

        if (!laggingPlayers.empty()) {
          // start the lag screen
          DLOG_APP_IF(LogLevel::kTrace, Concat("global lagger update (+", ToNameListSentence(laggingPlayers), ")"));
          SendAll(GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, m_Aura->GetClockTicks()));
          ResetDropVotes();

          m_IsLagging = true;
          m_StartedLaggingTime = m_Aura->GetClockTime();
          m_LastLagScreenResetTime = m_Aura->GetClockTime();

          // print debug information
          double worstLaggerSeconds = static_cast<double>(worstLaggerFrames) * static_cast<double>(m_LatencyTicks) / static_cast<double>(1000.);
          if (m_Aura->MatchLogLevel(LogLevel::kInfo)) {
            LogApp(Concat("started lagging on ", ToNameListSentence(laggingPlayers, true), "."), LOG_ALL);
            LogApp(Concat("worst lagger is [", m_Users[worstLaggerIndex]->GetName(), "] (", ToFormattedString(worstLaggerSeconds), " seconds behind)"), LOG_C);
          }
        }
      }
      m_LastLagStartCheckTime = m_Aura->GetClockTicks();
    }
  } else if (!m_Users.empty()) { // m_IsLagging == true (context: CGame::UpdateLoaded())
    pair<int64_t, int64_t> waitTicks = GetReconnectWaitTicks();
    UserList droppedUsers;
    for (auto& user : m_Users) {
      if (!user->GetIsLagging()) {
        continue;
      }
      bool timeExceeded = false;
      if (user->GetDisconnected() && user->GetGProxy()->GetIsExtended()) {
        timeExceeded = m_Aura->GetTicksIsAfterDelay(user->GetStartedLaggingTicks(), waitTicks.second);
      } else if (user->GetDisconnected() && user->GetCanReconnect()) {
        timeExceeded = m_Aura->GetTicksIsAfterDelay(user->GetStartedLaggingTicks(), waitTicks.first);
      } else {
        timeExceeded = m_Aura->GetTicksIsAfterDelay(user->GetStartedLaggingTicks(), 60000);
      }
      if (timeExceeded) {
        if (user->GetDisconnected()) {
          StopLagger(user, Concat("failed to reconnect within ", to_string((m_Aura->GetClockTicks() - user->GetStartedLaggingTicks()) / 1000), " seconds"));
        } else {
          StopLagger(user, Concat("was automatically dropped after ", to_string((m_Aura->GetClockTicks() - user->GetStartedLaggingTicks()) / 1000), " seconds"));
        }
        droppedUsers.push_back(user);
      }
    }
    if (!droppedUsers.empty()) {
      bool saved = false;
      for (const auto& user : droppedUsers) {
        TryShareUnitsOnDisconnect(user, false);
        if (!saved) saved = TrySaveOnDisconnect(user, false);
      }
      ResetDropVotes();
    }

    // Warcraft III disconnects if it doesn't receive an action packet for more than ~65 seconds
    if (m_Aura->GetTimeIsAfterDelay(m_LastLagScreenResetTime, 60)) {
      ResetLagScreen();
    }

    // check if anyone has stopped lagging normally
    // we consider a user to have stopped lagging if they're less than m_SyncLimitSafe keepalives behind

    uint8_t playersLaggingCounter = 0;
    for (auto& user : m_Users) {
      if (!user->GetIsLagging()) {
        continue;
      }

      if (user->GetDisconnectNoticeSent()) {
        ++playersLaggingCounter;
        ReportRecoverableDisconnect(user);
        continue;
      }

      if (user->GetDisconnectedUnrecoverably()) {
        user->SetLagging(false);
        user->ClearStartedLaggingTicks();
        DLOG_APP_IF(LogLevel::kTrace, Concat("global lagger update (-", user->GetName(), ")"));
        SendAll(GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetClockTicks()));
        LOG_APP_IF(LogLevel::kInfo, Concat("lagging user disconnected [", user->GetName(), "]"));
      } else if (!user->GetIsSyncCounterStopLag()) {
        ++playersLaggingCounter;
      } else {
        DLOG_APP_IF(LogLevel::kTrace, Concat("global lagger update (-", user->GetName(), ")"));
        SendAll(GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetClockTicks()));
        user->SetLagging(false);
        user->ClearStartedLaggingTicks();
        LOG_APP_IF(LogLevel::kInfo, Concat("user no longer lagging [", user->GetName(), "] (", user->GetDelayText(true), ")"));
      }
    }

    if (playersLaggingCounter == 0) {
      m_IsLagging = false;
      m_LastActionExpectedTicks = (
        (hiResTicks - m_LatencyTicks) - /* Let CGame::Update() immediately send first few pending actions */
        (m_LastActionSentTicks - m_LastActionExpectedTicks) /* CPU stalling correction term - e.g. if in Windows CMD, some text was selected */
      );
      m_PingReportedSinceLagTimes = 0;
      LOG_APP_IF(LogLevel::kInfo, Concat("stopped lagging after ", ToFormattedString(static_cast<double>(m_Aura->GetClockTime() - m_StartedLaggingTime)), " seconds"));
    }
  }

  if (m_IsLagging) {
    // keep track of the last lag screen time so we can avoid timing out users
    m_LastLagScreenTime = m_Aura->GetClockTime();

    // every 17 seconds, report most recent lag data
    if (m_Aura->GetTimeIsAfterDelay(m_StartedLaggingTime, m_PingReportedSinceLagTimes * 17)) {
      ReportAllPings();
      ++m_PingReportedSinceLagTimes;
    }

    if (m_Config.m_SyncNormalize) {
      if (m_PingReportedSinceLagTimes == 2 && !m_Aura->GetTicksIsAfterDelay(m_FinishedLoadingTicks, 60000)) {
        NormalizeSyncCounters();
      } else if (m_PingReportedSinceLagTimes == 3 && !m_Aura->GetTicksIsAfterDelay(m_FinishedLoadingTicks, 180000)) {
        NormalizeSyncCounters();
      }
    }
  }

  switch (m_Config.m_PlayingTimeoutMode) {
    case GamePlayingTimeoutMode::kNever:
      break;
    case GamePlayingTimeoutMode::kDry:
    case GamePlayingTimeoutMode::kStrict:
      if (m_Aura->GetTicksIsAfterDelay(m_FinishedLoadingTicks, (int64_t)m_Config.m_PlayingTimeout)) {
        if (m_Config.m_PlayingTimeoutMode == GamePlayingTimeoutMode::kStrict) {
          m_GameOverTolerance = 0;
          StartGameOverTimer();
        } else {
          Log(Concat("game timed out after ", to_string(m_Config.m_PlayingTimeout / 1000), " seconds"));
          m_Config.m_PlayingTimeoutMode = GamePlayingTimeoutMode::kNever;
        }
      }
      break;
    default:
      // impossible path
      break;
  }

  // TODO: Implement game timeout warnings
  // m_PlayingTimeoutWarningShortCountDown    = CFG.GetUint8("hosting.expiry.playing.timeout.soon_warnings", 10);
  // m_PlayingTimeoutWarningShortInterval     = CFG.GetUint32("hosting.expiry.playing.timeout.soon_interval", 60);
  // m_PlayingTimeoutWarningLargeCountDown    = CFG.GetUint8("hosting.expiry.playing.timeout.eager_warnings", 3);
  // m_PlayingTimeoutWarningLargeInterval     = CFG.GetUint32("hosting.expiry.playing.timeout.eager_interval", 1200);

  // TODO: Implement game pause timeout (and also warnings)
  // On timeout:
  // Warnings are needed in order for other players to unpause if so they wish.
  /*
  // Must disconnect, because unpausing as m_PauseUser would desync them anyway
  m_PauseUser->SetLeftReason("pause time limit exceeded");
  m_PauseUser->SetLeftCode(PLAYERLEAVE_DISCONNECT);
  m_PauseUser->DisableReconnect();
  m_PauseUser->CloseConnection();
  if (!user->GetIsEndingOrEnded()) {
    Resume(user, user->GetPingEqualizerFrame(), true);
    QueueLeftMessage(m_PauseUser);
  }
  */
}

void CGame::UpdateLoadedOrLoadInGame()
{
  m_LastInGameChatFlushTicks = m_Aura->GetClockTicks();
  if (!m_PendingChatMessages.empty()) {
    for (const CTargetedInGameChatMessage& chatMessage : m_PendingChatMessages) {
      GameProtocol::PacketWrapper packetWrapper = chatMessage.GetPacket();
      for (const uint8_t targetUID : chatMessage.GetToUIDs()) {
        if (m_JoinInProgressVirtualUser.has_value() && targetUID == m_JoinInProgressVirtualUser->GetUID()) {
          if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
            GameFrame& frame = m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_CHAT_PUBLIC);
            frame.m_Bytes = vector<uint8_t>(begin(packetWrapper.data), begin(packetWrapper.data) + static_cast<ptrdiff_t>(packetWrapper.data.size()));
          }
        } else {
          GameUser::CGameUser* targetUser = GetUserFromUID(targetUID);
          if (!targetUser) continue;
          if (targetUser->GetFinishedLoading()) {
            targetUser->Send(packetWrapper);
          } else {
            targetUser->m_OnLoadChatMessages.Push(move(packetWrapper));
          }
        }
      }
    }
    m_PendingChatMessages.clear();
  }
}

bool CGame::Update(fd_set* fd, fd_set* send_fd)
{
  const int64_t loopTicks = m_Aura->GetClockTicks();
  const int64_t hiResTicks = GetTicks();

  if (!m_Users.empty() && !m_LobbyLoading && m_Aura->GetTicksIsAfterDelay(m_LastPingTicks, 5000)) {
    // ping every 5 seconds
    // changed this to ping during game loading as well to hopefully fix some problems with people disconnecting during loading
    // changed this to ping during the game as well
    // we must send pings to users who are downloading the map because
    // Warcraft III disconnects from the lobby if it doesn't receive a ping every ~90 seconds
    // so if the user takes longer than 90 seconds to download the map they would be disconnected unless we keep sending pings
    SendAllConnected(GameProtocol::SEND_W3GS_PING_FROM_HOST(loopTicks));
    m_LastPingTicks = loopTicks;
  }

  if (m_GameLoaded && (m_EffectiveTicks >= m_LastCheckActionsTicks + 5000)) {
    CheckActions();

    if (!m_APMTrainerPaused) {
      ++m_APMTrainerTicks;
      for (auto& user : m_Users) {
        if (!user->GetHasAPMTrainer()) {
          continue;
        }
        double recentAPM = m_APMTrainerTicks < 3 ? user->GetMostRecentAPM() : user->GetRecentAPM();
        if (recentAPM < user->GetAPMTrainerTarget()) {
          SendChat(user, Concat("[APM] Recent: ", to_string(static_cast<size_t>(round(recentAPM))), " - Average: ", to_string(static_cast<size_t>(round(user->GetAPM())))));
        }
      }
    }
  }

  // update users

  for (auto i = begin(m_Users); i != end(m_Users);) {
    if ((*i)->Update(fd, (*i)->GetCanReconnect() ? GAME_USER_TIMEOUT_RECONNECTABLE : GAME_USER_TIMEOUT_VANILLA)) {
      EventUserDeleted(*i, fd, send_fd);
      m_Aura->m_Net.OnUserKicked(*i);
      delete *i;
      i = m_Users.erase(i);
    } else {
      ++i;
    }
  }

  if (m_Remaking) {
    if (!m_Users.empty()) {
      for (auto& user : m_Users) {
        user->SetDeleteMe(true);
      }
      return false;
    }

    FlushStatsQueue();

    if (m_Aura->GetNewGameIsInQuota()) {
      Remake();
    } else {
      // Cannot remake
      m_Remaking = false;
      m_Exiting = true;
    }
    return true;
  }

  // keep track of the largest sync counter (the number of keepalive packets received by each user)
  // if anyone falls behind by more than m_SyncLimit keepalives we start the lag screen

  if (m_GameLoaded) {
    UpdateLoaded();
  }

  // send actions every m_Latency milliseconds
  // actions are at the heart of every Warcraft 3 game but luckily we don't need to know their contents to relay them
  // we queue user actions in EventUserIncomingAction then just resend them in batches to all users here

  if (m_GameLoaded && !m_IsLagging && hiResTicks - m_LastActionExpectedTicks >= m_LatencyTicks) {
    UpdateLoadedOrLoadInGame();
    SendAllActions();
  } else if ((m_GameLoaded || (m_GameLoading && m_Config.m_LoadInGame)) && m_Aura->GetTicksIsFirstOrAfterDelay(m_LastInGameChatFlushTicks, 300)) {
    UpdateLoadedOrLoadInGame();
  }

  UpdateLogs();

  // end the game if there aren't any users left
  if (m_Users.empty() && (m_GameLoading || m_GameLoaded || m_ExitingSoon)) {
    if (!m_Exiting) {
      //FlushStatsQueue();
      LOG_APP_IF(LogLevel::kInfo, "is over (no users left)");
      m_Exiting = true;
    }
    return m_Exiting;
  }

  if (m_GameLoading) {
    UpdateLoading();
  }

  // expire the votekick
  if (!m_KickVotePlayer.empty() && m_Aura->GetTimeIsAfterDelay(m_StartedKickVoteTime, 60)) {
    LOG_APP_IF(LogLevel::kDebug, Concat("votekick against user [", m_KickVotePlayer, "] expired"));
    SendAllChat(Concat("A votekick against user [", m_KickVotePlayer, "] has expired"));
    m_KickVotePlayer.clear();
    m_StartedKickVoteTime = 0;
  }

  // start the gameover timer if there's only a configured number of players left
  // do not count observers, but fake users are counted regardless
  if (m_GameLoading || m_GameLoaded) {
    uint8_t remainingPlayers = MINUS_TINY(GetNumJoinedPlayersOrFakeUsers(), m_JoinedVirtualHosts);
    if (remainingPlayers != m_StartPlayers && !GetIsGameOverTrusted()) {
      if (remainingPlayers == 0) {
        LOG_APP_IF(LogLevel::kInfo, Concat("gameover timer started: 0 p | ", ToDecString(GetNumJoinedObservers()), " obs | 0 fake"));
        StartGameOverTimer();
      } else if (remainingPlayers <= m_Config.m_NumPlayersToStartGameOver) {
        LOG_APP_IF(LogLevel::kInfo, Concat("gameover timer started: ", ToDecString(GetNumJoinedPlayers()), " p | ", ToDecString(GetNumComputers()), " comp | ", ToDecString(GetNumJoinedObservers()), " obs | ", to_string(m_FakeUsers.size() - m_JoinedVirtualHosts), " fake | ", ToDecString(m_JoinedVirtualHosts), " vhost"));
        StartGameOverTimer();
      }
  }}

  // finish the gameover timer
  if (GetIsGameOver() && m_Aura->GetTimeIsAfterDelay(m_GameOverTime.value(), m_GameOverTolerance.value_or(60))) {
    // Disconnect the user socket, destroy it, but do not send W3GS_PLAYERLEAVE
    // Sending it would force them to actually quit the game, and go to the scorescreen.
    if (m_GameLoading || m_GameLoaded) {
      SendEveryoneElseLeftAndDisconnect("was disconnected (gameover timer finished)");
    } else {
      StopPlayers("was disconnected (gameover timer finished)");
    }
  }

  if (m_Aura->GetTimeIsAfterDelay(m_LastStatsUpdateTime, 30)) {
    if (!UpdateStatsQueue() && !GetIsGameOver() && m_Map->GetMMDUseGameOver()) {
      Log("gameover timer started (stats reported game over)");
      StartGameOverTimer(true);
    }
    m_LastStatsUpdateTime = m_Aura->GetClockTime();
  }

  if (GetIsStageAcceptingJoins()) {
    // Also updates mirror games.
    UpdateJoinable();
  }


  if (!m_LobbyLoading && m_Aura->GetTicksIsAfterDelay(m_LastDiscoveryTicks, m_GameDiscoveryPending ? 2000 : 5000)) {
    // send UDP refresh every 5 seconds
    // this used to be sent using the same interval as pings
    // however, if we are broadcasting to a VPN network, this operation can take around 10 ms,
    // so we want more fine-grained control of this operation
    if (GetUDPEnabled() && GetIsStageAcceptingJoins()) {
      if (!m_Aura->m_Net.m_Config.m_UDPBroadcastStrictMode || m_GameDiscoveryPending) {
        SendGameDiscoveryInfo();
      } else {
        SendGameDiscoveryRefresh();
      }
      m_GameDiscoveryActive = true;
      m_GameDiscoveryPending = false;
    }

    if (m_GameDiscoveryInfoChanged & GAME_DISCOVERY_CHANGED_SLOTS) {
#ifdef PROFILING
      int64_t t = GetTicks();
#endif
      SendGameDiscoveryInfoMDNS();
#ifdef PROFILING
      int64_t dt = GetTicks() - t;
      if (dt > 1) Print(Concat("SendGameDiscoveryInfoMDNS() took ", to_string(dt), " ms"));
#endif
      UNSET_TINY(m_GameDiscoveryInfoChanged, GAME_DISCOVERY_CHANGED_SLOTS);
    }

    m_LastDiscoveryTicks = loopTicks;
  }

  if (GetIsLobbyStrict()) {
    if (UpdateLobby()) {
      // EventGameStartedLoading or m_Exiting
      return true;
    }
  }

  return m_Exiting;
}

void CGame::UpdatePost(fd_set* send_fd) const
{
  // we need to manually call DoSend on each user now because GameUser::CGameUser::Update doesn't do it
  // this is in case user 2 generates a packet for user 1 during the update but it doesn't get sent because user 1 already finished updating
  // in reality since we're queueing actions it might not make a big difference but oh well

  for (const auto& user : m_Users) {
    if (user->GetDisconnected()) continue;
    user->GetSocket()->DoSend(send_fd);
  }
}

void CGame::CheckLobbyTimeouts()
{
  if (HasOwnerSet()) {
    switch (m_Config.m_LobbyOwnerTimeoutMode) {
      case LobbyOwnerTimeoutMode::kNever:
        break;
      case LobbyOwnerTimeoutMode::kAbsent:
        if (m_Aura->GetTicksIsAfterDelay(m_LastOwnerSeenTicks, static_cast<int64_t>(m_Config.m_LobbyOwnerTimeout))) {
          ReleaseOwner();
        }
        break;
      case LobbyOwnerTimeoutMode::kStrict:
        if (m_Aura->GetTicksIsAfterDelay(m_LastOwnerAssignedTicks, static_cast<int64_t>(m_Config.m_LobbyOwnerTimeout))) {
          ReleaseOwner();
        }
        break;
      IGNORE_ENUM_LAST(LobbyOwnerTimeoutMode)
    }
  }

  if (!m_Aura->m_Net.m_HealthCheckInProgress && (!m_IsMirror || m_Config.m_LobbyTimeoutMode == LobbyTimeoutMode::kStrict)) {
    bool timedOut = false;
    switch (m_Config.m_LobbyTimeoutMode) {
      case LobbyTimeoutMode::kNever:
        break;
      case LobbyTimeoutMode::kEmpty:
        timedOut = m_Aura->GetTicksIsAfterDelay(m_LastUserSeenTicks, static_cast<int64_t>(m_Config.m_LobbyTimeout));
        break;
      case LobbyTimeoutMode::kOwnerMissing:
        timedOut = m_Aura->GetTicksIsAfterDelay(m_LastOwnerSeenTicks, static_cast<int64_t>(m_Config.m_LobbyTimeout));
        break;
      case LobbyTimeoutMode::kStrict:
        timedOut = m_Aura->GetTimeIsAfterDelay(m_CreationTime, static_cast<int64_t>(m_Config.m_LobbyTimeout) / 1000);
        break;
      IGNORE_ENUM_LAST(LobbyTimeoutMode)
    }
    if (timedOut) {
      Log("is over (lobby time limit hit)");
      m_Exiting = true;
    }
  }
}

void CGame::RunActionsScheduler()
{
  const int64_t oldLatency = GetActiveLatency();
  const int64_t actionLateBy = GetLastActionLateBy();
#ifdef PROFILING
  const size_t i = GetFrameDriftBucket(actionLateBy);
  ++m_FrameDrifts[i];
#endif
  const int64_t newLatency = GetNextLatency(actionLateBy);
  if (newLatency != oldLatency) {
    m_LatencyTicks = newLatency;
    if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
      vector<uint8_t> storedLatency = CreateByteArrayLE(static_cast<uint16_t>(newLatency));
      m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_LATENCY, storedLatency);
      m_GameHistory->SetActiveLatency(newLatency);
      m_GameHistory->UpdateSpectatorActions((int64_t)m_Config.m_SpectatorDelay);
    }
  }

  uint8_t maxOldEqualizerOffset = m_PingEqualizerActiveDelayFrames;
  if (CheckUpdatePingEqualizer()) {
    m_PingEqualizerActiveDelayFrames = UpdatePingEqualizer();
  }

  RunActionsSchedulerInner(newLatency, m_PingEqualizerActiveDelayFrames, oldLatency, maxOldEqualizerOffset, actionLateBy);
}

void CGame::RunActionsSchedulerInner(const int64_t newLatency, const uint8_t maxNewEqualizerOffset, const int64_t oldLatency, const uint8_t maxOldEqualizerOffset, const int64_t actionLateBy)
{
  if (actionLateBy > m_Config.m_PerfThreshold && !m_IsSinglePlayer) {
    m_Aura->LogPerformanceWarning(TaskType::kGameFrame, this, actionLateBy, oldLatency, newLatency);
  }

  if (maxNewEqualizerOffset < maxOldEqualizerOffset) {
    // No longer are that many frames needed.
    vector<QueuedActionsFrameNode*> mergeableNodes = GetFrameNodesInRangeInclusive(maxNewEqualizerOffset, maxOldEqualizerOffset);
    MergeFrameNodes(mergeableNodes);
  }

  m_CurrentActionsFrame = m_CurrentActionsFrame->next;
  for (auto& user : m_Users) {
    user->AdvanceActiveGameFrame();
  }
}

void CGame::LogApp(const string& logText, const uint8_t logTargets) const
{
  if (logTargets & LOG_C) {
    Print(Concat(GetLogPrefix(), logText));
  }
  if (logTargets & LOG_P) {
    m_Aura->LogPersistent(Concat(GetLogPrefix(), logText));
  }
  if (logTargets & LOG_R) {
    LogRemote(logText);
  }
}

void CGame::Log(const string& text)
{
  if (m_GameLoaded) {
    Log(text, m_EffectiveTicks);
  } else {
    string logText = Concat(GetLogPrefix(), text);
    Print(logText);
    LogRemote(text);
  }
}

void CGame::Log(const string& logText, int64_t gameTicks)
{
  m_PendingLogs.push(new CGameLogRecord(gameTicks, logText));
}

void CGame::LogRemote(const string& text) const
{
  LogRemoteRaw(Concat(GetLogPrefix(), text));
}

void CGame::LogRemoteRaw(const string& text) const
{
  if (m_Aura->m_Config.m_LogRemoteMode == LOG_REMOTE_MODE_FILE || m_Aura->m_Config.m_LogRemoteMode == LOG_REMOTE_MODE_MIXED) {
    m_Aura->LogRemoteFile(text);
  }
  if (m_Aura->m_Config.m_LogRemoteMode == LOG_REMOTE_MODE_NETWORK || m_Aura->m_Config.m_LogRemoteMode == LOG_REMOTE_MODE_MIXED) {
    if (m_Aura->m_IRC.m_Config.m_LogGames) {
      m_Aura->m_IRC.SendAllChannels(text);
    }
#ifndef DISABLE_DPP
    if (m_Aura->m_Discord.m_Config.m_LogGames) {
      m_Aura->m_Discord.SendAllChannels(text);
    }
#endif
  }
}

void CGame::UpdateLogs()
{
  int64_t ticks = m_EffectiveTicks;
  while (!m_PendingLogs.empty()) {
    CGameLogRecord* record = m_PendingLogs.front();
    if (ticks + static_cast<int64_t>(m_Config.m_LogDelay) < record->GetTicks()) {
      break;
    }
    string logText = Concat(GetLogPrefix(), record->ToString());
    Print(logText);
    LogRemoteRaw(logText);
    delete record;
    m_PendingLogs.pop();
  }
}

void CGame::FlushLogs()
{
  while (!m_PendingLogs.empty()) {
    CGameLogRecord* record = m_PendingLogs.front();
    string logText = Concat(GetLogPrefix(), record->ToString());
    Print(logText);
    LogRemoteRaw(logText);
    delete record;
    m_PendingLogs.pop();
  }
}

/*
 * LogSlots() - for debugging purposes
 *
 * Not used anywhere.
 */
void CGame::LogSlots()
{
  uint8_t i = 0, slotsNum = GetNumSlots();
  while (i < slotsNum) {
    LogApp(Concat("slot_", ToDecString(i), " = <", ByteArrayToHexString(m_SlotsConfig.Inspect(i).GetProtocolArray()), ">"), LOG_C);
    ++i;
  }
}

void CGame::Send(CConnection* user, const std::vector<uint8_t>& data) const
{
  if (user)
    user->Send(data);
}

void CGame::Send(uint8_t UID, const std::vector<uint8_t>& data) const
{
  GameUser::CGameUser* user = GetUserFromUID(UID);
  Send(user, data);
}

void CGame::SendAll(const std::vector<uint8_t>& data) const
{
  for (auto& user : m_Users) {
    user->Send(data);
  }
}

void CGame::SendAllVariant(
  const function<bool(const GameUser::CGameUser*)>& choicePredicateIsFirst,
  const function<vector<uint8_t>(const GameUser::CGameUser*)>& buildFirst,
  const function<vector<uint8_t>(const GameUser::CGameUser*)>& buildSecond
) const
{
  LazyVariantBytesStorage store;
  for (auto& user : m_Users) {
    if (choicePredicateIsFirst(user)) {
      if (!store.first.has_value()) {
        store.first = buildFirst(user);
      }
      user->Send(store.first.value());
    } else {
      if (!store.second.has_value()) {
        store.second = buildSecond(user);
      }
      user->Send(store.second.value());
    }
  }
}

void CGame::SendAllVariant(
  const function<bool(const GameUser::CGameUser*)>& choicePredicate,
  const function<vector<uint8_t>(const GameUser::CGameUser*)>& dataGenerator
) const
{
  LazyVariantBytesStorage store;
  for (auto& user : m_Users) {
    if (choicePredicate(user)) {
      if (!store.first.has_value()) {
        store.first = dataGenerator(user);
      }
      user->Send(store.first.value());
    } else {
      if (!store.second.has_value()) {
        store.second = dataGenerator(user);
      }
      user->Send(store.second.value());
    }
  }
}

void CGame::SendAllConnected(const std::vector<uint8_t>& data) const
{
  // Note: Abuse of this function may desync GProxy-reconnected players
  // But it's safe for pings and chat.
  for (auto& user : m_Users) {
    if (user->GetDisconnected()) continue;
    user->Send(data);
  }
}

void CGame::SendLobbyChat(vector<uint8_t> toUIDs, uint8_t fromUID, string_view message) const
{
  auto packet = GameProtocol::SENDWRAP_W3GS_CHAT_FROM_HOST_LOBBY(fromUID, toUIDs, GameProtocol::Magic::ChatType::CHAT_LOBBY, string_view(), message);
  for (auto& targetUID : toUIDs) {
    GameUser::CGameUser* targetUser = GetUserFromUID(targetUID);
    if (!targetUser || targetUser->GetLeftMessageSent()) {
      continue;
    }
    targetUser->Send(packet);
  }
}

void CGame::SendLobbyChatSingle(GameUser::CGameUser* targetUser, uint8_t fromUID, string_view message) const
{
  if (targetUser->GetLeftMessageSent()) {
    return;
  }
  targetUser->Send(GameProtocol::SENDWRAP_W3GS_CHAT_FROM_HOST_LOBBY(fromUID, CreateByteArray(targetUser->GetUID()), GameProtocol::Magic::ChatType::CHAT_LOBBY, string_view(), message));
}

void CGame::SendLobbyChatAll(uint8_t fromUID, string_view message) const
{
  vector<uint8_t> toUIDs = GetAllUIDs();
  if (toUIDs.empty()) {
    return;
  }
  auto packet = GameProtocol::SENDWRAP_W3GS_CHAT_FROM_HOST_LOBBY(fromUID, toUIDs, GameProtocol::Magic::ChatType::CHAT_LOBBY, string_view(), message);
  for (auto& targetUser : m_Users) {
    if (targetUser->GetLeftMessageSent()) {
      continue;
    }
    targetUser->Send(packet);
  }
}

void CGame::SendInGameChat(vector<uint8_t> toUIDs, uint8_t fromUID, uint8_t inGameChannel, string_view message)
{
  if (toUIDs.empty()) {
    return;
  }
  m_PendingChatMessages.emplace_back(fromUID, toUIDs, inGameChannel, message);
}

void CGame::SendInGameChatSingle(GameUser::CGameUser* targetUser, uint8_t fromUID, string_view message)
{
  SendInGameChat(CreateByteArray(targetUser->GetUID()), fromUID, targetUser->GetChatChannel(true), message);
}

void CGame::SendInGameChatAll(uint8_t fromUID, string_view message)
{
  SendInGameChat(GetAllUIDs(), fromUID, CHAT_RECV_ALL, message);
}

void CGame::SendInGameChatObservers(uint8_t fromUID, string_view message)
{
  // send a public message to all known observers - it'll be marked [Observers] or [Referees] in Warcraft 3
  SendInGameChat(GetObserverChatUIDs(), fromUID, CHAT_RECV_OBS, message);
}

void CGame::SendChat(uint8_t fromUID, GameUser::CGameUser* user, string_view message, const LogLevelExtra logLevel)
{
  // send a private message to one user - it'll be marked [Private] in Warcraft 3

  if (message.empty() || !user || user->GetIsInLoadingScreen()) {
    return;
  }

#ifdef DEBUG
  if (m_Aura->MatchLogLevel(logLevel)) {
    const GameUser::CGameUser* fromUser = GetUserFromUID(fromUID);
    if (fromUser) {
      LogApp(Concat("sent as [", fromUser->GetName(), "] -> [", user->GetName(), " (UID:", ToDecString(user->GetUID()), ")] <<", message, ">>"), LOG_C);
    } else if (fromUID == m_VirtualHostUID) {
      LogApp(Concat("sent as Virtual Host -> [", user->GetName(), " (UID:", ToDecString(user->GetUID()), ")] <<", message, ">>"), LOG_C);
    } else {
      LogApp(Concat("sent as [UID:", ToDecString(fromUID), "] -> [", user->GetName(), " (UID:", ToDecString(user->GetUID()), ")] <<", message, ">>"), LOG_C);
    }
  }
#else
  if (m_Aura->MatchLogLevel(logLevel)) {
    LogApp(Concat("sent to [", user->GetName(), "] <<", message, ">>"), LOG_C);
  }
#endif

  
  if (!m_GameLoading && !m_GameLoaded) {
    SendLobbyChatSingle(user, fromUID, message);
  } else {
    SendInGameChatSingle(user, fromUID, message);
  }
}

void CGame::SendChat(uint8_t fromUID, uint8_t toUID, string_view message, const LogLevelExtra logLevel)
{
  SendChat(fromUID, GetUserFromUID(toUID), message, logLevel);
}

void CGame::SendChat(GameUser::CGameUser* user, string_view message, const LogLevelExtra logLevel)
{
  SendChat(GetHostUID(), user, message, logLevel);
}

void CGame::SendChat(uint8_t toUID, string_view message, const LogLevelExtra logLevel)
{
  SendChat(GetHostUID(), toUID, message, logLevel);
}

void CGame::SendChat(CAsyncObserver* spectator, string_view message, const LogLevelExtra /*logLevel*/)
{
  spectator->SendChat(message);
}

void CGame::SendAllChat(uint8_t fromUID, string_view message)
{
  if (m_GameLoading && !m_Config.m_LoadInGame)
    return;

  if (message.empty())
    return;

  if (m_Aura->GetIsLoggingTrace()) {
    const GameUser::CGameUser* fromUser = GetUserFromUID(fromUID);
    if (fromUser) {
      LogApp(Concat("sent as [", fromUser->GetName(), "] <<", message, ">>"), LOG_C);
    } else if (fromUID == m_VirtualHostUID) {
      LogApp(Concat("sent as Virtual Host <<", message, ">>"), LOG_C);
    } else {
      LogApp(Concat("sent as [UID:", ToDecString(fromUID), "] <<", message, ">>"), LOG_C);
    }
  } else {
    LOG_APP_IF(LogLevel::kInfo, Concat("sent <<", message, ">>"));
  }

  // send a public message to all users - it'll be marked [All] in Warcraft 3

  
  if (!m_GameLoading && !m_GameLoaded) {
    SendLobbyChatAll(fromUID, message);
  } else {
    SendInGameChatAll(fromUID, message);
  }
}

void CGame::SendAllChat(string_view message)
{
  SendAllChat(GetHostUID(), message);
}

void CGame::SendObserverChat(uint8_t fromUID, string_view message)
{
  if (!m_GameLoaded)
    return;

  if (message.empty())
    return;

  if (m_Aura->GetIsLoggingTrace()) {
    const GameUser::CGameUser* fromUser = GetUserFromUID(fromUID);
    if (fromUser) {
      LogApp(Concat("sent as [", fromUser->GetName(), "] <<", message, ">>"), LOG_C);
    } else if (fromUID == m_VirtualHostUID) {
      LogApp(Concat("sent as Virtual Host <<", message, ">>"), LOG_C);
    } else {
      LogApp(Concat("sent as [UID:", ToDecString(fromUID), "] <<", message, ">>"), LOG_C);
    }
  } else {
    LOG_APP_IF(LogLevel::kInfo, Concat("sent <<", message, ">>"));
  }

  SendInGameChatObservers(fromUID, message);
}

void CGame::SendObserverChat(string_view message)
{
  SendObserverChat(GetHostUID(), message);
}

bool CGame::SendSpectatorChat(const CAsyncObserver* excludeSpectator, string_view prefix, string_view message) const
{
  if (!m_GameLoaded) return false;
  GameProtocol::MemoizedGameChatMessageBuilder builder(GameProtocol::ChatToHostType::CTH_MESSAGE_INGAME, prefix, message);
  vector<CAsyncObserver*> spectators = GetSpectators(); // excludes those that haven't finished loading
  bool anySent = false;
  for (auto& spectator : spectators) {
    if (spectator == excludeSpectator) continue;
    spectator->Send(builder.To(spectator->GetUID(), spectator->GetChatChannel()));
    anySent = true;
  }
  return anySent;
}

bool CGame::SendSpectatorChat(string_view prefix, string_view message) const
{
  return SendSpectatorChat(nullptr, prefix, message);
}

void CGame::UpdateReadyCounters()
{
  m_ControllersWithMap = 0;
  m_ControllersBalanced = true;
  m_ControllersReadyCount = 0;
  m_ControllersNotReadyCount = 0;
  if (m_Users.empty()) {
    return;
  }
  const size_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> readyControllersByTeam(numTeams, 0);
  for (size_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (!m_SlotsConfig.GetIsOccupied(i) || m_SlotsConfig.GetIsObserver(i)) {
      continue;
    }
    GameUser::CGameUser* player = GetUserFromSID(integer_cast_lossy<uint8_t>(i));
    if (!player) {
      ++m_ControllersWithMap;
      ++m_ControllersReadyCount;
      ++readyControllersByTeam[m_SlotsConfig.Inspect(i).GetTeam()];
    } else if (player->GetMapReady()) {
      ++m_ControllersWithMap;
      if (player->UpdateReady()) {
        ++m_ControllersReadyCount;
        ++readyControllersByTeam[m_SlotsConfig.Inspect(i).GetTeam()];
      } else {
        ++m_ControllersNotReadyCount;
      }
    } else {
      ++m_ControllersNotReadyCount;
    }
  }
  uint8_t refCount = 0;
  size_t i = numTeams;
  while (i--) {
    // allow empty teams
    if (readyControllersByTeam[i] == 0) continue;
    if (refCount == 0) {
      refCount = readyControllersByTeam[i];
    } else if (readyControllersByTeam[i] != refCount) {
      m_ControllersBalanced = false;
      break;
    }
  }
}

vector<uint8_t> CGame::GetSlotInfo(const GameUser::CGameUser* user) const
{
  return GameProtocol::SEND_W3GS_SLOTINFO(m_SlotsConfig, m_RandomSeed, m_Map->GetMapNumControllers(), user->GetGameVersion());
}

vector<uint8_t> CGame::GetSlotInfo(const CAsyncObserver* observer) const
{
  return GameProtocol::SEND_W3GS_SLOTINFO(m_SlotsConfig, m_RandomSeed, m_Map->GetMapNumControllers(), observer->GetGameVersion());
}

vector<uint8_t> CGame::GetHandicaps() const
{
  vector<uint8_t> handicaps;
  for (const auto& slot : m_SlotsConfig.slots) {
    handicaps.push_back(slot.GetHandicap());
  }
  return handicaps;
}

void CGame::SendAllSlotInfo()
{
  if (m_GameLoading || m_GameLoaded)
    return;

  if (!m_Users.empty()) {
    SendAllVariant(
      [this](const GameUser::CGameUser* user) {
        return GetAreSameSlotProtocolGameVersions(user->GetGameVersion(), GetVersion());
      },
      [this](const GameUser::CGameUser* user) {
        return GetSlotInfo(user);
      }
    );
  }

  m_SlotInfoChanged = SLOTS_UNCHANGED;
}

uint8_t CGame::GetNumEnabledTeamSlots(const uint8_t team) const
{
  // Only for Custom Forces
  uint8_t counter = 0;
  for (const auto& slot : m_SlotsConfig.slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    if (slot.GetTeam() == team) {
      ++counter;
    }
  }
  return counter;
}

vector<uint8_t> CGame::GetNumFixedComputersByTeam() const
{
  const uint8_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> fixedComputers(numTeams, 0);
  for (const auto& slot : m_SlotsConfig.slots) {
    if (slot.GetTeam() == GetObserverTeam()) continue;
    if (!slot.GetIsSelectable()) {
      ++fixedComputers[slot.GetTeam()];
    }
  }
  return fixedComputers;
}

vector<uint8_t> CGame::GetPotentialTeamSizes() const
{
  const uint8_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> teamSizes(numTeams, 0);
  for (const auto& slot : m_SlotsConfig.slots) {
    if (slot.GetTeam() == GetObserverTeam()) continue;
    if (slot.GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    ++teamSizes[slot.GetTeam()];
  }
  return teamSizes;
}


pair<uint8_t, uint8_t> CGame::GetLargestPotentialTeam() const
{
  // Only for Custom Forces
  const uint8_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> teamSizes = GetPotentialTeamSizes();
  pair<uint8_t, uint8_t> largestTeam = make_pair(GetObserverTeam(), (uint8_t)0u);
  for (uint8_t team = 0; team < numTeams; ++team) {
    if (teamSizes[team] > largestTeam.second) {
      largestTeam = make_pair(team, teamSizes[team]);
    }
  }
  return largestTeam;
}

pair<uint8_t, uint8_t> CGame::GetSmallestPotentialTeam(const uint8_t minSize, const uint8_t exceptTeam) const
{
  // Only for Custom Forces
  const uint8_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> teamSizes = GetPotentialTeamSizes();
  pair<uint8_t, uint8_t> smallestTeam = make_pair(GetObserverTeam(), GetObserverTeam());
  for (uint8_t team = 0; team < numTeams; ++team) {
    if (team == exceptTeam || teamSizes[team] < minSize) continue;
    if (teamSizes[team] < smallestTeam.second) {
      smallestTeam = make_pair(team, teamSizes[team]);
    }
  }
  return smallestTeam;
}

vector<uint8_t> CGame::GetActiveTeamSizes() const
{
  const uint8_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> teamSizes(numTeams, 0);
  for (const auto& slot : m_SlotsConfig.slots) {
    if (slot.GetTeam() == GetObserverTeam()) continue;
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
      ++teamSizes[slot.GetTeam()];
    }
  }
  return teamSizes;
}

uint8_t CGame::GetSelectableTeamSlotFront(const uint8_t team, const uint8_t endOccupiedSID, const uint8_t endOpenSID, const bool force) const
{
  uint8_t forceResult = 0xFF;
  uint8_t endSID = endOccupiedSID < endOpenSID ? endOpenSID : endOccupiedSID;
  for (uint8_t i = 0; i < endSID; ++i) {
    const CGameSlot& slot = m_SlotsConfig.Inspect(i);
    if (slot.GetTeam() != team) continue;
    if (slot.GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    if (!slot.GetIsSelectable()) continue;
    if (slot.GetSlotStatus() != SLOTSTATUS_OPEN && i < endOccupiedSID) {
      // When force is used, fallback to the highest occupied SID
      forceResult = i;
      continue;
    }
    return i;
  }
  if (force) return forceResult;
  return 0xFF; // Player team change request
}

uint8_t CGame::GetSelectableTeamSlotBack(const uint8_t team, const uint8_t endOccupiedSID, const uint8_t endOpenSID, const bool force) const
{
  uint8_t forceResult = 0xFF;
  uint8_t SID = endOccupiedSID < endOpenSID ? endOpenSID : endOccupiedSID;
  while (SID--) {
    const CGameSlot* slot = InspectSlot(SID);
    if (!slot || slot->GetTeam() != team) continue;
    if (slot->GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    if (!slot->GetIsSelectable()) continue;
    if (slot->GetSlotStatus() != SLOTSTATUS_OPEN && SID < endOccupiedSID) {
      // When force is used, fallback to the highest occupied SID
      if (forceResult == 0xFF) forceResult = SID;
      continue;
    }
    return SID;
  }
  if (force) return forceResult;
  return 0xFF; // Player team change request
}

uint8_t CGame::GetSelectableTeamSlotBackExceptHumanLike(const uint8_t team, const uint8_t endOccupiedSID, const uint8_t endOpenSID, const bool force) const
{
  uint8_t forceResult = 0xFF;
  uint8_t SID = endOccupiedSID < endOpenSID ? endOpenSID : endOccupiedSID;
  while (SID--) {
    const CGameSlot* slot = InspectSlot(SID);
    if (!slot || slot->GetTeam() != team) continue;
    if (slot->GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    if (!slot->GetIsSelectable()) continue;
    if (slot->GetIsPlayerOrFake()) continue;
    if (slot->GetSlotStatus() != SLOTSTATUS_OPEN && SID < endOccupiedSID) {
      // When force is used, fallback to the highest occupied SID
      if (forceResult == 0xFF) forceResult = SID;
      continue;
    }
    return SID;
  }
  if (force) return forceResult;
  return 0xFF; // Player team change request
}

uint8_t CGame::GetSelectableTeamSlotBackExceptComputer(const uint8_t team, const uint8_t endOccupiedSID, const uint8_t endOpenSID, const bool force) const
{
  uint8_t forceResult = 0xFF;
  uint8_t SID = endOccupiedSID < endOpenSID ? endOpenSID : endOccupiedSID;
  while (SID--) {
    const CGameSlot* slot = InspectSlot(SID);
    if (!slot || slot->GetTeam() != team) continue;
    if (slot->GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    if (!slot->GetIsSelectable()) continue;
    if (slot->GetIsComputer()) continue;
    if (slot->GetSlotStatus() != SLOTSTATUS_OPEN && SID < endOccupiedSID) {
      // When force is used, fallback to the highest occupied SID
      if (forceResult == 0xFF) forceResult = SID;
      continue;
    }
    return SID;
  }
  if (force) return forceResult;
  return 0xFF; // Player team change request
}

bool CGame::FindHumanVsAITeams(const uint8_t humanCount, const uint8_t computerCount, pair<uint8_t, uint8_t>& teams) const
{
  if (!GetIsCustomForces()) {
    teams.first = 0;
    teams.second = 1;
    return true;
  } else if (!(m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS)) {
    // MAPOPT_CUSTOMFORCES
    pair<uint8_t, uint8_t> largestTeam = GetLargestPotentialTeam();
    pair<uint8_t, uint8_t> smallestTeam = GetSmallestPotentialTeam(humanCount < computerCount ? humanCount : computerCount, largestTeam.first);
    if (largestTeam.second == 0 || smallestTeam.second == GetObserverTeam()) {
      return false;
    }
    pair<uint8_t, uint8_t>& computerTeam = (computerCount > humanCount ? largestTeam : smallestTeam);
    pair<uint8_t, uint8_t>& humanTeam = (computerCount > humanCount ? smallestTeam : largestTeam);
    if (humanTeam.second < humanCount || computerTeam.second < computerCount) {
      return false;
    }
    teams.first = humanTeam.first;
    teams.second = computerTeam.first;
    return true;
  }

  // Fixed Player Settings

  vector<uint8_t> lockedTeams = GetNumFixedComputersByTeam();
  uint8_t fixedTeamsCounter = 0;
  uint8_t forcedComputerTeam = 0xFF;
  for (uint8_t team = 0; team < lockedTeams.size(); ++team) {
    if (lockedTeams[team] == 0) continue;
    if (++fixedTeamsCounter >= 2) {
      // Bail-out if there are fixed computers in different teams.
      // This is the case in DotA/AoS maps.
      return false;
    }
    forcedComputerTeam = team;
  }
  if (forcedComputerTeam != 0xFF) {
    if (GetNumEnabledTeamSlots(forcedComputerTeam) < computerCount) {
      return false;
    }
  }

  {
    const uint8_t numTeams = m_Map->GetMapNumTeams();
    vector<uint8_t> teamSizes = GetPotentialTeamSizes();
    pair<uint8_t, uint8_t> largestTeam = make_pair(GetObserverTeam(), (uint8_t)0u);
    pair<uint8_t, uint8_t> smallestTeam = make_pair(GetObserverTeam(), GetObserverTeam());
    for (uint8_t team = 0; team < numTeams; ++team) {
      if (team == forcedComputerTeam) continue;
      if (teamSizes[team] > largestTeam.second) {
        largestTeam = make_pair(team, teamSizes[team]);
      }
      if (teamSizes[team] < smallestTeam.second) {
        smallestTeam = make_pair(team, teamSizes[team]);
      }
    }
    if (forcedComputerTeam != 0xFF) {
      if (largestTeam.second < humanCount) {
        return false;
      }
      teams.first = largestTeam.first;
      teams.second = forcedComputerTeam;
    } else {
      // Just like MAPOPT_CUSTOMFORCES
      pair<uint8_t, uint8_t>& computerTeam = (computerCount > humanCount ? largestTeam : smallestTeam);
      pair<uint8_t, uint8_t>& humanTeam = (computerCount > humanCount ? smallestTeam : largestTeam);
      if (humanTeam.second < humanCount || computerTeam.second < computerCount) {
        return false;
      }
      teams.first = humanTeam.first;
      teams.second = humanTeam.second;
    }
    return true;
  }
}

void CGame::ResetLayout(const bool quiet)
{
  if (m_CustomLayout == CUSTOM_LAYOUT_NONE) {
    return;
  }
  m_CustomLayout = CUSTOM_LAYOUT_NONE;
  if (!quiet) {
    SendAllChat("Team restrictions automatically removed.");
  }
}

void CGame::ResetLayoutIfNotMatching()
{
  switch (m_CustomLayout) {
    case CUSTOM_LAYOUT_NONE:
      break;
    case CUSTOM_LAYOUT_ONE_VS_ALL:
    case CUSTOM_LAYOUT_HUMANS_VS_AI: {
      if (
        (GetNumTeamControllersOrOpen(m_CustomLayoutData.first) == 0) ||
        (GetNumTeamControllersOrOpen(m_CustomLayoutData.second) == 0)
      ) {
        ResetLayout(false);
        break;
      }
      bool isNotMatching = false;
      if (m_CustomLayout == CUSTOM_LAYOUT_HUMANS_VS_AI) {
        for (const auto& slot : m_SlotsConfig.slots) {
          if (slot.GetSlotStatus() != SLOTSTATUS_CLOSED) continue;
          if (slot.GetIsComputer()) {
            if (slot.GetTeam() != m_CustomLayoutData.second) {
              isNotMatching = true;
              break;
            }
          } else {
            // Open, human, or fake user
            if (slot.GetTeam() != m_CustomLayoutData.first) {
              isNotMatching = true;
              break;
            }
          }
        }
      } else {
      }
      if (isNotMatching) {
        ResetLayout(false);
      }
      break;
    }
    case CUSTOM_LAYOUT_FFA: {
      if (GetHasAnyActiveTeam()) {
        ResetLayout(false);
      }
      break;
    }
    case CUSTOM_LAYOUT_DRAFT:
    case CUSTOM_LAYOUT_COMPACT:
    case CUSTOM_LAYOUT_ISOPLAYERS:
    default:
      break;
  }
}

bool CGame::SetLayoutCompact()
{
  m_CustomLayout = CUSTOM_LAYOUT_COMPACT;

  if (GetIsCustomForces()) {
    // Unsupported, and not very useful anyway.
    // Typical maps with fixed user settings are NvN, and compacting will just not be useful.
    return false;
  }

  const uint8_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> teamSizes = GetActiveTeamSizes();
  pair<uint8_t, uint8_t> largestTeam = make_pair(GetObserverTeam(), (uint8_t)0u);
  for (uint8_t team = 0; team < numTeams; ++team) {
    if (largestTeam.second < teamSizes[team]) {
      largestTeam = make_pair(team, teamSizes[team]);
    }
  }
  if (largestTeam.second <= 1) {
    return false;
  }

  const uint32_t controllerCount = GetNumControllers();
  if (controllerCount < 2) {
    return false;
  }
  //const uint8_t extraPlayers = controllerCount % largestTeam.second;
  const uint8_t expectedFullTeams = static_cast<uint8_t>(controllerCount / (uint32_t)largestTeam.second);
  if (expectedFullTeams < 2) {
    // Compacting is used for NvNvN...
    return false;
  }
  //const uint8_t expectedMaxTeam = expectedFullTeams - (extraPlayers == 0);
  vector<uint8_t> premadeMappings(numTeams, GetObserverTeam());
  bitset<MAX_SLOTS_MODERN> fullTeams;
  for (uint8_t team = 0; team < numTeams; ++team) {
    if (teamSizes[team] == largestTeam.second) {
      if (!fullTeams.test(team)) {
        premadeMappings[team] = static_cast<uint8_t>(fullTeams.count());
        fullTeams.set(team);
      }
    }
  }

  const uint8_t autoTeamOffset = static_cast<uint8_t>(fullTeams.count());

  for (auto& slot : m_SlotsConfig.slots) {
    uint8_t team = slot.GetTeam();
    if (fullTeams.test(team)) {
      slot.SetTeam(premadeMappings[team]);
    } else {
      slot.SetTeam(autoTeamOffset);
    }
  }

  uint8_t i = numTeams;
  while (i--) {
    if (i < autoTeamOffset) {
      teamSizes[i] = largestTeam.second;
    } else if (i == autoTeamOffset) {
      teamSizes[i] = (uint8_t)(controllerCount - ((uint32_t)largestTeam.second * (uint32_t)autoTeamOffset));
    } else {
      teamSizes[i] = 0u;
    }
  }

  uint8_t fillingTeamNum = autoTeamOffset;
  for (auto& slot : m_SlotsConfig.slots) {
    uint8_t team = slot.GetTeam();
    if (team < autoTeamOffset) continue;
    if (teamSizes[team] > largestTeam.second) {
      if (teamSizes[fillingTeamNum] >= largestTeam.second) {
        ++fillingTeamNum;
      }
      slot.SetTeam(fillingTeamNum);
      --teamSizes[team];
      ++teamSizes[fillingTeamNum];
    }
  }

  return true;
}

bool CGame::SetLayoutTwoTeams()
{
  m_CustomLayout = CUSTOM_LAYOUT_ISOPLAYERS;

  // TODO(IceSandslash): SetLayoutTwoTeams
  if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS) {
    if (m_Map->GetMapNumTeams() != 2) {
      return false;
    }
  }
  //m_CustomLayout = CUSTOM_LAYOUT_ISOPLAYERS;
  return false;
}

bool CGame::SetLayoutHumansVsAI(const uint8_t humanTeam, const uint8_t computerTeam)
{
  m_CustomLayout = CUSTOM_LAYOUT_HUMANS_VS_AI;
  const bool isSwap = GetIsCustomForces();
  if (isSwap) {
    uint8_t SID = GetNumSlots() - 1;
    uint8_t endHumanSID = SID;
    uint8_t endComputerSID = SID;
    while (SID != 0xFF) {
      CGameSlot* slot = GetSlot(SID);
      if (slot->GetSlotStatus() != SLOTSTATUS_OCCUPIED) {
        --SID;
        continue;
      }
      const bool isComputer = slot->GetIsComputer();
      const uint8_t currentTeam = slot->GetTeam();
      const uint8_t targetTeam = isComputer ? computerTeam : humanTeam;
      if (currentTeam == targetTeam) {
        --SID;
        continue;
      }
      uint8_t& selfEndSID = isComputer ? endComputerSID : endHumanSID;
      uint8_t& otherEndSID = isComputer ? endHumanSID : endComputerSID;
      uint8_t swapSID = 0xFF;
      if (isComputer) {
        swapSID = GetSelectableTeamSlotBackExceptComputer(targetTeam, SID, selfEndSID, true);
      } else {
        swapSID = GetSelectableTeamSlotBackExceptHumanLike(targetTeam, SID, selfEndSID, true);
      }
      if (swapSID == 0xFF) {
        return false;
      }
      const bool isTwoWays = InspectSlot(swapSID)->GetSlotStatus() == SLOTSTATUS_OCCUPIED;
      if (!SwapSlots(SID, swapSID)) {
        Print(ByteArrayToDecString(InspectSlot(SID)->GetByteArray()));
        Print(ByteArrayToDecString(InspectSlot(swapSID)->GetByteArray()));
      } else {
        // slot still points to the same SID
        selfEndSID = swapSID;
        if (isTwoWays && SID > otherEndSID) {
          otherEndSID = SID;
        }
      }
      m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
      if (!isTwoWays) --SID;
    }
    CloseAllTeamSlots(computerTeam);
  } else {
    uint8_t remainingSlots = m_Map->GetMapNumControllers() - GetNumControllers();
    for (auto& slot : m_SlotsConfig.slots) {
      if (slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED) continue;
      const uint8_t targetTeam = slot.GetIsComputer() ? computerTeam : humanTeam;
      const uint8_t wasTeam = slot.GetTeam();
      if (wasTeam != targetTeam && (remainingSlots > 0 || wasTeam != GetObserverTeam())) {
        slot.SetTeam(targetTeam);
        m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        if (wasTeam == GetObserverTeam()) {
          if (--remainingSlots == 0) break;
        }
      }
    }
  }
  m_CustomLayoutData = make_pair(humanTeam, computerTeam);
  return true;
}

bool CGame::SetLayoutFFA()
{
  m_CustomLayout = CUSTOM_LAYOUT_FFA;

  uint8_t nextTeam = GetNumControllers(); // Only arrange non-observers
  const bool isSwap = GetIsCustomForces();
  if (isSwap && nextTeam > m_Map->GetMapNumTeams()) {
    return false;
  }

  vector<uint8_t> lockedTeams = GetNumFixedComputersByTeam();
  for (const auto& count : lockedTeams) {
    if (count > 1) {
      return false;
    }
  }

  if (!FindNextMissingElementBack(nextTeam, lockedTeams)) {
    return true; // every team got 1 fixed computer slot
  }
  uint8_t SID = GetNumSlots();
  bitset<MAX_SLOTS_MODERN> occupiedTeams;
  while (SID--) {
    CGameSlot* slot = GetSlot(SID);
    if (slot->GetTeam() == GetObserverTeam()) continue;
    if (slot->GetSlotStatus() != SLOTSTATUS_OCCUPIED) continue;
    if (slot->GetTeam() == nextTeam) {
      // Slot already has the right team. Skip both team and slot.
      occupiedTeams.set(nextTeam);
      if (!FindNextMissingElementBack(nextTeam, lockedTeams)) {
        break;
      }
      continue;
    }
    if (isSwap) {
      uint8_t swapSID = GetSelectableTeamSlotBack(nextTeam, SID, GetNumSlots(), true);
      if (swapSID == 0xFF) {
        return false;
      }
      if (!SwapSlots(SID, swapSID)) {
        return false;
      }
      m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
      if (!FindNextMissingElementBack(nextTeam, lockedTeams)) {
        break;
      }
      occupiedTeams.set(nextTeam);
    } else {
      slot->SetTeam(nextTeam);
      m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
      if (!FindNextMissingElementBack(nextTeam, lockedTeams)) {
        break;
      }
      occupiedTeams.set(nextTeam);
    }
  }
  if (isSwap) {
    CloseAllTeamSlots(occupiedTeams);
  }
  return true;
}

uint8_t CGame::GetOneVsAllTeamAll() const
{
  if (!GetIsCustomForces()) {
    return 1;
  }

  const uint8_t mapNumTeams = m_Map->GetMapNumTeams();
  const uint8_t expectedTeamSize = GetNumPotentialControllers() - 1;
  vector<uint8_t> lockedTeams = GetNumFixedComputersByTeam();

  // Make sure GetOneVsAllTeamAll() yields the team with the fixed computer slots.
  // Fixed computer slots in different forces are not allowed in OneVsAll mode.
  uint8_t resultTeam = 0xFF;
  uint8_t fixedTeamsCounter = 0;
  for (uint8_t team = 0; team < lockedTeams.size(); ++team) {
    if (lockedTeams[team] == 0) continue;
    if (++fixedTeamsCounter >= 2) {
      return 0xFF;
    }
    resultTeam = team;
  }

  vector<uint8_t> teamSizes = GetPotentialTeamSizes();
  if (resultTeam == 0xFF) {
    pair<uint8_t, uint8_t> largestTeam = make_pair(GetObserverTeam(), (uint8_t)0u);
    for (uint8_t team = 0; team < mapNumTeams; ++team) {
      if (teamSizes[team] > largestTeam.second) {
        largestTeam = make_pair(team, teamSizes[team]);
      }
    }
    resultTeam = largestTeam.first;
  }
  if (expectedTeamSize > teamSizes[resultTeam]) {
    return 0xFF;
  } else {
    return resultTeam;
  }
}

uint8_t CGame::GetOneVsAllTeamOne(const uint8_t teamAll) const
{
  if (!GetIsCustomForces()) {
    return 0;
  }

  const uint8_t mapNumTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> teamSizes = GetPotentialTeamSizes();
  pair<uint8_t, uint8_t> smallestTeam = make_pair(GetObserverTeam(), GetObserverTeam());
  for (uint8_t team = 0; team < mapNumTeams; ++team) {
    if (team == teamAll) continue;
    if (teamSizes[team] < smallestTeam.second) {
      smallestTeam = make_pair(team, teamSizes[team]);
    }
  }
  // We can be sure that smallestTeam does not contain fixed computer slots, because:
  // - TeamAll contains the only allowed computer slot.
  // - CMap validator ensures that there are at least two well-defined teams in the game.
  return smallestTeam.first;
}

bool CGame::SetLayoutOneVsAll(const GameUser::CGameUser* targetPlayer)
{
  m_CustomLayout = CUSTOM_LAYOUT_COMPACT;

  const bool isSwap = GetMap()->GetMapOptions() & MAPOPT_CUSTOMFORCES;
  uint8_t targetSID = GetSIDFromUID(targetPlayer->GetUID());
  //uint8_t targetTeam = m_SlotsConfig.Inspect(targetSID).GetTeam();

  const uint8_t teamAll = GetOneVsAllTeamAll();
  if (teamAll == 0xFF) return false;
  const uint8_t teamOne = GetOneVsAllTeamOne(teamAll);

  // Move the alone user to its own team.
  if (isSwap) {
    const uint8_t swapSID = GetSelectableTeamSlotBack(teamOne, GetNumSlots(), GetNumSlots(), true);
    if (swapSID == 0xFF) {
      return false;
    }
    SwapSlots(targetSID, swapSID);
    targetSID = swapSID; // Sync slot index on swap.
  } else {
    CGameSlot* slot = GetSlot(targetSID);
    slot->SetTeam(teamOne);
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  }

  // Move the rest of users.
  if (isSwap) {
    uint8_t endObserverSID = GetNumSlots();
    uint8_t endAllSID = endObserverSID;
    uint8_t SID = GetNumSlots() - 1;
    while (SID != 0xFF) {
      if (SID == targetSID || m_SlotsConfig.Inspect(SID).GetTeam() == teamAll || !m_SlotsConfig.GetIsOccupied(SID)) {
        --SID;
        continue;
      }

      uint8_t swapSID = GetSelectableTeamSlotBack(teamAll, SID, endAllSID, true);
      bool toObservers = swapSID == 0xFF; // Alliance team is full.
      if (toObservers) {
        if (m_SlotsConfig.GetIsComputer(SID)) {
          return false;
        }
        swapSID = GetSelectableTeamSlotBack(GetObserverTeam(), SID, endObserverSID, true);
        if (swapSID == 0xFF) {
          return false;
        }
      }
      if (!SwapSlots(SID, swapSID)) {
        Print(ByteArrayToDecString(InspectSlot(SID)->GetByteArray()));
        Print(ByteArrayToDecString(InspectSlot(swapSID)->GetByteArray()));
        return false;
      } else if (toObservers) {
        endObserverSID = swapSID;
      } else {
        endAllSID = swapSID;
      }
      CloseAllTeamSlots(teamOne);
      m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
      --SID;
    }
  } else {
    uint8_t remainingSlots = m_Map->GetMapNumControllers() - GetNumControllers();
    if (remainingSlots > 0) {
      uint8_t SID = GetNumSlots();
      while (SID--) {
        if (SID == targetSID) continue;
        uint8_t wasTeam = m_SlotsConfig.Inspect(SID).GetTeam();
        m_SlotsConfig.Get(SID).SetTeam(teamAll);
        m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        if (wasTeam == GetObserverTeam()) {
          if (--remainingSlots == 0) break;
        }
      }
    }
  }
  m_CustomLayout = CUSTOM_LAYOUT_ONE_VS_ALL;
  m_CustomLayoutData = make_pair(teamOne, teamAll);
  return true;
}

optional<Version> CGame::GetOverrideLANVersion(const string& playerName, const sockaddr_storage* /*address*/) const
{
  auto match = m_Config.m_GameVersionsByLANPlayerNames.find(playerName);
  if (match == m_Config.m_GameVersionsByLANPlayerNames.end()) {
    return nullopt;
  }
  return optional<Version>(match->second);
}

optional<Version> CGame::GetIncomingPlayerVersion(const CConnection* user, const CIncomingJoinRequest& joinRequest, shared_ptr<const CRealm> fromRealm) const
{
  optional<Version> result;
  if (user->GetIsGameSeeker()) {
    const CGameSeeker* seeker = reinterpret_cast<const CGameSeeker*>(user);
    optional<Version> maybeVersion = seeker->GetMaybeGameVersion();
    if (maybeVersion.has_value()) {
      result.swap(maybeVersion);
      return result;
    }
  }

  if (fromRealm) {
    optional<Version> maybeVersion = fromRealm->GetExpectedGameVersion();
    if (maybeVersion.has_value()) {
      result.swap(maybeVersion);
      return result;
    }
  }

  {
    string playerName = TrimString(joinRequest.GetLowerName());
    optional<Version> maybeVersion = GetOverrideLANVersion(playerName, user->GetRemoteAddress());
    if (maybeVersion.has_value()) {
      result.swap(maybeVersion);
      return result;
    }
  }

  if (m_SupportedGameVersionsMin == m_SupportedGameVersionsMax) {
    result = GetVersion();
  }

  return result;
}

Version CGame::GuessIncomingPlayerVersion(const CConnection* user, const CIncomingJoinRequest& joinRequest, shared_ptr<const CRealm> /*fromRealm*/) const
{
  string lowerName = joinRequest.GetLowerName();
  auto versionErrors = m_VersionErrors.find(lowerName);
  if (versionErrors == m_VersionErrors.end() || versionErrors->second.size() == m_SupportedGameVersions.count()) {
    optional<Version> lastKnownVersion = m_Aura->m_Net.GetMaybeCachedGameVersion(user->GetRemoteAddress());
    if (lastKnownVersion.has_value()) {
      DLOG_APP_IF(LogLevel::kTrace, Concat("guessed game version for ", SanitizeWrapUTF8(joinRequest.GetName()), " as ", ToVersionString(lastKnownVersion.value()), " (fetched from game discovery cache)"));
    } else {
      DLOG_APP_IF(LogLevel::kTrace, Concat("guessed game version for ", SanitizeWrapUTF8(joinRequest.GetName()), " as ", ToVersionString(GetVersion()), " (default)"));
    }
    return lastKnownVersion.value_or(GetVersion());
  }

  bool onlyRangeHeads = true;
  for (uint8_t i = 0; i < 2; ++i) {
    Version version = m_SupportedGameVersionsMin;
    while (version != m_SupportedGameVersionsMax) {
      // First check m_SupportedGameVersionsMin
      // Afterwards, check range heads in ascending order
      // Finally, check all remaining versions in ascending order
      if (onlyRangeHeads && version != m_SupportedGameVersionsMin && GetScriptsVersionRangeHead(version) != version) {
        version = GetNextVersion(version);
        continue;
      }
      if (versionErrors->second.find(version) != versionErrors->second.end()) {
        version = GetNextVersion(version);
        continue;
      }
      DLOG_APP_IF(LogLevel::kTrace, Concat("guessed user game version for ", SanitizeWrapUTF8(joinRequest.GetName()), " as ", ToVersionString(version), " (next)"));
      return version;
    }
    onlyRangeHeads = false; 
  }

  // Fallback
  {
    optional<Version> lastKnownVersion = m_Aura->m_Net.GetMaybeCachedGameVersion(user->GetRemoteAddress());
    if (lastKnownVersion.has_value()) {
      DLOG_APP_IF(LogLevel::kTrace, Concat("guessed user game version for ", SanitizeWrapUTF8(joinRequest.GetName()), " as ", ToVersionString(lastKnownVersion.value()), " (fallback to game discovery cache)"));
    } else {
      DLOG_APP_IF(LogLevel::kTrace, Concat("guessed user game version for ", SanitizeWrapUTF8(joinRequest.GetName()), " as ", ToVersionString(GetVersion()), " (fallback)"));
    }
    return lastKnownVersion.value_or(GetVersion());
  }
}

bool CGame::GetIsAutoStartDue() const
{
  if (m_Users.empty() || m_CountDownStarted || m_AutoStartRequirements.empty()) {
    return false;
  }
  if (!m_ControllersBalanced && m_Config.m_AutoStartRequiresBalance) {
    return false;
  }

  for (const auto& requirement : m_AutoStartRequirements) {
    if (requirement.first <= m_ControllersReadyCount && m_Aura->GetTimeIsAfter(requirement.second)) {
      return GetCanStartGracefulCountDown();
    }
  }

  return false;
}

string CGame::GetAutoStartText() const
{
  if (m_AutoStartRequirements.empty()) {
    return "Autostart is not set.";
  }

  vector<string> fragments; 
  for (const auto& requirement : m_AutoStartRequirements) {
    if (requirement.first == 0 && m_Aura->GetTimeIsAfter(requirement.second)) {
      fragments.push_back("now");
    } else if (requirement.first == 0) {
      fragments.push_back(Concat("in ", DurationLeftToString(requirement.second - m_Aura->GetClockTime())));
    } else if (m_Aura->GetTimeIsAfter(requirement.second)) {
      fragments.push_back(Concat("with ", to_string(requirement.first), " players"));
    } else {
      fragments.push_back(Concat("with ", to_string(requirement.first), "+ players after ", DurationLeftToString(requirement.second - m_Aura->GetClockTime())));
    }
  }

  if (fragments.size() == 1) {
    return Concat("Autostarts ", fragments[0], ".");
  }

  return Concat("Autostarts ", JoinStrings(fragments, "or"), ".");
}

string CGame::GetReadyStatusText() const
{
  string notReadyFragment;
  if (m_ControllersNotReadyCount > 0) {
    if (m_Config.m_BroadcastCmdToken.empty()) {
      notReadyFragment = Concat(" Use ", m_Config.m_PrivateCmdToken, "ready when you are.");
    } else {
      notReadyFragment = Concat(" Use ", m_Config.m_BroadcastCmdToken, "ready when you are.");
    }
  }
  if (m_ControllersReadyCount == 0) {
    return Concat("No players ready yet.", notReadyFragment);
  }

  if (m_ControllersReadyCount == 1) {
    return Concat("One player is ready.", notReadyFragment);
  }

  return Concat(to_string(m_ControllersReadyCount), " players are ready.", notReadyFragment);
}

string CGame::GetPlayingTimeoutWelcomeText() const
{
  switch (m_Config.m_PlayingTimeoutMode) {
    case GamePlayingTimeoutMode::kNever:
      return string();
    case GamePlayingTimeoutMode::kDry:
      return Concat("Game would be over after ", ToDurationString(static_cast<uint64_t>(m_Config.m_PlayingTimeout / 1000)), ".");
    case GamePlayingTimeoutMode::kStrict:
      return Concat("Game will be over after ", ToDurationString(static_cast<uint64_t>(m_Config.m_PlayingTimeout / 1000)), ".");
    default:
      // should not be possible
      return string();
  }
}

string CGame::GetCmdToken() const
{
  return m_Config.m_BroadcastCmdToken.empty() ? m_Config.m_PrivateCmdToken : m_Config.m_BroadcastCmdToken;
}

void CGame::SendAllAutoStart()
{
  SendAllChat(GetAutoStartText());
}

uint32_t CGame::GetGameType() const
{
  uint32_t mapGameType = 0;
  if (m_RealmsDisplayMode == GAME_DISPLAY_PRIVATE) mapGameType |= MAPGAMETYPE_PRIVATEGAME;
  if (m_RestoredGame) {
    mapGameType |= MAPGAMETYPE_SAVEDGAME;
  } else {
    mapGameType |= MAPGAMETYPE_UNKNOWN0;
    mapGameType |= m_Map->GetMapGameType();
  }
  return mapGameType;
}

void CGame::InitGameVersions()
{
  SetSupportedGameVersion(GetVersion());
  bool canCrossPlay = !(
    (m_Config.m_CrossPlayMode == CrossPlayMode::kNone) ||
    (m_Config.m_CrossPlayMode == CrossPlayMode::kConservative && m_Map->GetMapDataSet() == MAP_DATASET_MELEE)
  );
  if (canCrossPlay) {
    Version headVersion = GetScriptsVersionRangeHead(GetVersion());
    for (const auto& version : m_Aura->m_Config.m_SupportedGameVersions) {
      switch (m_Config.m_CrossPlayMode) {
        case CrossPlayMode::kNone:
          UNREACHABLE();
          break;
        case CrossPlayMode::kConservative:
        case CrossPlayMode::kOptimistic:
          if (GetScriptsVersionRangeHead(version) != headVersion) {
            continue;
          }
          break;
        case CrossPlayMode::kForce:
          break;
        IGNORE_ENUM_LAST(CrossPlayMode)
      }
      if (!m_Map->GetMapIsGameVersionSupported(version)) {
        // map is too recent,
        // or we failed to calculate hashes for this game version
        continue;
      }
      SetSupportedGameVersion(version);
    }
  }
}

void CGame::InitGameFlags()
{
  m_GameFlags = m_Map->GetGameConvertedFlags();
}

void CGame::InitHCL(shared_ptr<const CGameSetup> gameSetup)
{
  if (gameSetup->m_HCL.has_value()) {
    m_HCLCommandString = gameSetup->m_HCL.value();
  } else if (gameSetup->m_Map->GetHCLEnabled()) {
    m_HCLCommandString = gameSetup->m_Map->GetHCLDefaultValue();
  }
}

void CGame::InitAutoStart(shared_ptr<const CGameSetup> gameSetup)
{
  if (gameSetup->m_AutoStartSeconds.has_value() || gameSetup->m_AutoStartPlayers.has_value()) {
    uint8_t autoStartPlayers = gameSetup->m_AutoStartPlayers.value_or(0);
    int64_t autoStartSeconds = (int64_t)gameSetup->m_AutoStartSeconds.value_or(0);
    if (!gameSetup->m_AutoStartPlayers.has_value() || autoStartPlayers > m_ControllersReadyCount) {
      m_AutoStartRequirements.push_back(make_pair(
        autoStartPlayers,
        m_CreationTime + autoStartSeconds
      ));
    }
  } else if (m_Map->m_AutoStartSeconds.has_value() || m_Map->m_AutoStartPlayers.has_value()) {
    uint8_t autoStartPlayers = m_Map->m_AutoStartPlayers.value_or(0);
    int64_t autoStartSeconds = (int64_t)m_Map->m_AutoStartSeconds.value_or(0);
    if (m_Map->m_AutoStartPlayers.has_value() || autoStartPlayers > m_ControllersReadyCount) {
      m_AutoStartRequirements.push_back(make_pair(
        autoStartPlayers,
        m_CreationTime + autoStartSeconds
      ));
    }
  }
}

string_view CGame::GetSourceFilePath() const {
  if (m_RestoredGame) {
    return m_RestoredGame->GetClientPath();
  } else {
    return m_Map->GetClientPath();
  }
}

array<uint8_t, 4> CGame::GetSourceFileHashBlizz(const Version& version) const
{
  if (m_RestoredGame) {
    return m_RestoredGame->GetSaveHash();
  } else {
    return m_Map->GetMapScriptsBlizzHash(version);
  }
}

array<uint8_t, 20> CGame::GetMapSHA1(const Version& version) const
{
  if (version >= GAMEVER(1u, 30u)) {
    return m_Map->GetMapSHA1();
  } else {
    return m_Map->GetMapScriptsSHA1(version);
  }
}

array<uint8_t, 2> CGame::GetAnnounceWidth(shared_ptr<const CRealm> realm) const
{
  if (GetIsProxyReconnectable() && !(m_IsMirrorProxy && !realm)) {
    // use an invalid map width/height to indicate reconnectable games
    // TODO: Support reconnection (GProxy) when using --mirror-proxy
    return GPSProtocol::SEND_GPSS_DIMENSIONS();
  }
  if (m_RestoredGame) return {0, 0};
  return m_Map->GetMapWidth();
}

array<uint8_t, 2> CGame::GetAnnounceHeight(shared_ptr<const CRealm> realm) const
{
  if (GetIsProxyReconnectable() && !(m_IsMirrorProxy && !realm)) {
    // use an invalid map width/height to indicate reconnectable games
    // TODO: Support reconnection (GProxy) when using --mirror-proxy
    return GPSProtocol::SEND_GPSS_DIMENSIONS();
  }
  if (m_RestoredGame) return {0, 0};
  return m_Map->GetMapHeight();
}

string CGame::CheckIsValidHCL(const string& hcl) const
{
  if (m_Map->GetHCLAboutVirtualPlayers()) {
    return CheckIsValidHCLSmall(hcl);
  } else {
    return CheckIsValidHCLStandard(hcl);
  }
}

void CGame::SendVirtualHostPlayerInfo(CConnection* user) const
{
  if (!GetHasVirtualHost()) {
    return;
  }

  Send(user, GameProtocol::SEND_W3GS_PLAYERINFO_EXCLUDE_IP(GetVersion(), m_VirtualHostUID, GetLobbyVirtualHostName()));
}

vector<uint8_t> CGame::GetFakeUsersLobbyInfo() const
{
  optional<bool> dontOverride;
  vector<uint8_t> info;
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    if (m_JoinInProgressVirtualUser.has_value() && fakeUser.GetUID() == m_JoinInProgressVirtualUser->GetUID()) {
      continue;
    }
    vector<uint8_t> playerInfo = fakeUser.GetPlayerInfoBytes(dontOverride);
    AppendContainer(info, playerInfo);
  }
  return info;
}

vector<uint8_t> CGame::GetFakeUsersLoadedInfo() const
{
  optional<bool> overrideLoaded(true);
  vector<uint8_t> info;
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    if (m_JoinInProgressVirtualUser.has_value() && fakeUser.GetUID() == m_JoinInProgressVirtualUser->GetUID()) {
      continue;
    }
    vector<uint8_t> playerInfo = fakeUser.GetPlayerInfoBytes(overrideLoaded);
    AppendContainer(info, playerInfo);
  }
  return info;
}

vector<uint8_t> CGame::GetJoinedPlayersInfo() const
{
  vector<uint8_t> info;
  for (auto& otherPlayer : m_Users) {
    if (otherPlayer->GetDeleteMe()) {
      continue;
    }
    AppendContainer(info,
      GameProtocol::SEND_W3GS_PLAYERINFO_EXCLUDE_IP(GetVersion(), otherPlayer->GetUID(), otherPlayer->GetDisplayName()/*, otherPlayer->GetIPv4(), otherPlayer->GetIPv4Internal()*/)
    );
  }
  return info;
}

void CGame::SendFakeUsersInfo(CConnection* user) const
{
  if (!m_FakeUsers.empty()) {
    Send(user, GetFakeUsersLoadedInfo());
  }
}

void CGame::SendJoinedPlayersInfo(CConnection* connection) const
{
  vector<uint8_t> info = GetJoinedPlayersInfo();
  if (!info.empty()) {
    Send(connection, info);
  }
}

void CGame::SendMapAndVersionCheck(CConnection* user, const Version& version) const
{
  // When the game client receives MAPCHECK packet, it remains if the map is OK.
  // Otherwise, they immediately leave the lobby.
  const uint32_t clampedMapSize = m_Map->GetMapSizeClamped(version);
  if (clampedMapSize < m_Map->GetMapSize()) {
    DLOG_APP_IF(LogLevel::kTrace, Concat("map requires bypass for v", ToVersionString(version), " - size ", ToFormattedString((float)clampedMapSize / (float)(1024. * 1024.)), " MB"));
  }
  optional<array<uint8_t, 20>> maybeSHA1;
  if (version >= GAMEVER(1u, 23u)) {
    maybeSHA1 = GetMapSHA1(version);
  }
  user->Send(GameProtocol::SEND_W3GS_MAPCHECK(m_MapPath, clampedMapSize, m_Map->GetMapCRC32(), m_Map->GetMapScriptsBlizzHash(version), maybeSHA1));
}

void CGame::SendIncomingPlayerInfo(GameUser::CGameUser* user) const
{
  for (auto& otherPlayer : m_Users) {
    if (otherPlayer == user)
      continue;
    if (otherPlayer->GetDeleteMe())
      break;
    otherPlayer->Send(
      GameProtocol::SEND_W3GS_PLAYERINFO_EXCLUDE_IP(GetVersion(), user->GetUID(), user->GetDisplayName()/*, user->GetIPv4(), user->GetIPv4Internal()*/)
    );
  }
}

MapTransferStatus CGame::NextSendMap(CConnection* user, const uint8_t UID, MapTransfer& mapTransfer)
{
  if (!mapTransfer.GetIsInProgress()) {
    return MapTransferStatus::kNone;
  }

  // send up to 100 pieces of the map at once so that the download goes faster
  // if we wait for each MAPPART packet to be acknowledged by the client it'll take a long time to download
  // this is because we would have to wait the round trip time (the ping time) between sending every 1442 bytes of map data
  // doing it this way allows us to send at least 1.4 MB in each round trip interval which is much more reasonable
  // the theoretical throughput is [1.4 MB * 1000 / ping] in KB/sec so someone with 100 ping (round trip ping, not LC ping) could download at 1400 KB/sec
  // note: this creates a queue of map data which clogs up the connection when the client is on a slower connection (e.g. dialup)
  // in this case any changes to the lobby are delayed by the amount of time it takes to send the queued data (i.e. 140 KB, which could be 30 seconds or more)
  // for example, users joining and leaving, slot changes, chat messages would all appear to happen much later for the low bandwidth user
  // note: the throughput is also limited by the number of times this code is executed each second
  // e.g. if we send the maximum amount (1.4 MB) 10 times per second the theoretical throughput is 1400 KB/sec
  // therefore the maximum throughput is 14 MB/sec, and this value slowly diminishes as the user's ping increases
  // in addition to this, the throughput is limited by the configuration value bot_maxdownloadspeed
  // in summary: the actual throughput is MIN( 1.4 * 1000 / ping, 1400, bot_maxdownloadspeed ) in KB/sec assuming only one user is downloading the map

  const uint32_t mapSize = m_Map->GetMapSize();

  if (mapTransfer.GetLastSentOffsetEnd() == 0 && (
      mapTransfer.GetLastSentOffsetEnd() < mapTransfer.GetLastAck() + 1442 * m_Aura->m_Net.m_Config.m_MaxParallelMapPackets &&
      mapTransfer.GetLastSentOffsetEnd() < mapSize &&
      !(m_Aura->m_Net.m_Config.m_MaxUploadSpeed > 0 && m_Aura->m_Net.m_TransferredMapBytesThisUpdate > m_Aura->m_Net.m_Config.m_MaxUploadSpeed * 100)
    )
  ) {
    // overwrite the "started download ticks" since this is the first time we've sent any map data to the user
    // prior to this we've only determined if the user needs to download the map but it's possible we could have delayed sending any data due to download limits

    mapTransfer.Start();
  }

  while (
    mapTransfer.GetLastSentOffsetEnd() < mapTransfer.GetLastAck() + 1442 * m_Aura->m_Net.m_Config.m_MaxParallelMapPackets &&
    mapTransfer.GetLastSentOffsetEnd() < mapSize &&

    // limit the download speed if we're sending too much data
    // the download counter is the # of map bytes downloaded in the last second (it's reset once per second)
    !(m_Aura->m_Net.m_Config.m_MaxUploadSpeed > 0 && m_Aura->m_Net.m_TransferredMapBytesThisUpdate > m_Aura->m_Net.m_Config.m_MaxUploadSpeed * 100)
  ) {
    uint32_t lastOffsetEnd = mapTransfer.GetLastSentOffsetEnd();
    const FileChunkTransient cachedChunk = GetMapChunk(lastOffsetEnd);
    if (!cachedChunk.bytes) {
      return MapTransferStatus::kMissing;
    }

    const vector<uint8_t> packet = GameProtocol::SEND_W3GS_MAPPART(GetHostUID(), UID, lastOffsetEnd, cachedChunk);
    uint32_t chunkSendSize = integer_cast_lossy<uint32_t>(packet.size() - 18);
    mapTransfer.SetLastSentOffsetEnd(lastOffsetEnd + chunkSendSize);

    // Update CRC32 for map parts sent to this user
    mapTransfer.SetLastCRC32(CRC32::CalculateCRC(
      cachedChunk.GetDataAtCursor(lastOffsetEnd),
      chunkSendSize,
      mapTransfer.GetLastCRC32()
    ));

    bool fullySent = mapTransfer.GetLastSentOffsetEnd() == mapSize;
    m_Aura->m_Net.m_TransferredMapBytesThisUpdate += chunkSendSize;
    Send(user, packet);

    if (fullySent) {
      mapTransfer.SetFinished();
      if (mapTransfer.GetLastCRC32() == ByteArrayToUInt32LE(m_Map->GetMapCRC32())) {
        return MapTransferStatus::kDone;
      } else {
        return MapTransferStatus::kInvalid;
      }
    }
  }

  return MapTransferStatus::kInProgress;
}

vector<string> CGame::GetWelcomeMessageLines(GameUser::CGameUser* user) const
{
  vector<pair<uint64_t, function<string()>>> textFuncs;
  vector<pair<uint64_t, function<bool()>>> boolFuncs;
  vector<pair<uint64_t, bool>> boolValues;
  boolFuncs.reserve(9); // 9 excluding LAN, NAMERISK, CHECKLASTOWNER
  boolValues.reserve(3);
  //boolFuncs.emplace_back(HashCode("LAN"),    [this, user] () { return !user->GetRealm(false); });
  boolValues.emplace_back(HashCode("LAN"), !user->GetRealm(false));
  boolValues.emplace_back(HashCode("NAMERISK"), user->GetIsNameCensored());
  boolFuncs.emplace_back(HashCode("URL"),    [this] () { return !this->GetMapSiteURL().empty(); });
  boolFuncs.emplace_back(HashCode("OWNER"), [this]() { return !this->m_OwnerName.empty(); });
  boolFuncs.emplace_back(HashCode("CREATOR"), [this]() { return !this->m_CreatorText.empty(); });
  boolFuncs.emplace_back(HashCode("FILENAME"), [this]() {
    size_t lastSlashPos = this->m_MapPath.rfind('\\');
    return lastSlashPos != string::npos && lastSlashPos <= this->m_MapPath.length() - 6;
  });
  //boolFuncs.emplace_back(HashCode("NAMERISK"), [this, user]() { return user->GetIsNameCensored(); });
  boolFuncs.emplace_back(HashCode("AUTOSTART"), [this]() { return !this->m_AutoStartRequirements.empty(); });
  boolFuncs.emplace_back(HashCode("OWNERLESS"), [this]() { return this->m_OwnerLessLocked; });
  boolFuncs.emplace_back(HashCode("SHORTDESC"), [this]() { return !this->m_Map->GetMapShortDesc().empty(); });
  boolFuncs.emplace_back(HashCode("REPLACEABLE"), [this]() { return this->m_Replaceable; });
  //boolFuncs.emplace_back(HashCode("CHECKLASTOWNER"), [this, user]() { return m_OwnerName != user->GetName() && m_LastOwner == user->GetName(); });
  boolFuncs.emplace_back(HashCode("GAMETIMEOUTANY"), [this]() { return this->m_Config.m_PlayingTimeoutMode != GamePlayingTimeoutMode::kNever; });
  boolFuncs.emplace_back(HashCode("GAMETIMEOUTSHORT"), [this]() { return this->m_Config.m_PlayingTimeoutMode != GamePlayingTimeoutMode::kNever && this->m_Config.m_PlayingTimeout <= 3600000u; });
  boolValues.emplace_back(HashCode("CHECKLASTOWNER"), m_OwnerName != user->GetName() && m_LastOwner == user->GetName());

  //static_assert(HashCode("LAN") < HashCode("URL"), "Hash for LAN is not before URL");
  static_assert(HashCode("URL") < HashCode("OWNER"), "Hash for URL is not before OWNER");
  static_assert(HashCode("OWNER") < HashCode("CREATOR"), "Hash for OWNER is not before CREATOR");
  static_assert(HashCode("CREATOR") < HashCode("FILENAME"), "Hash for CREATOR is not before FILENAME");
  //static_assert(HashCode("FILENAME") < HashCode("NAMERISK"), "Hash for FILENAME is not before NAMERISK");
  //static_assert(HashCode("NAMERISK") < HashCode("AUTOSTART"), "Hash for NAMERISK is not before AUTOSTART");
  static_assert(HashCode("FILENAME") < HashCode("AUTOSTART"), "Hash for FILENAME is not before AUTOSTART");
  static_assert(HashCode("AUTOSTART") < HashCode("OWNERLESS"), "Hash for AUTOSTART is not before OWNERLESS");
  static_assert(HashCode("OWNERLESS") < HashCode("SHORTDESC"), "Hash for OWNERLESS is not before SHORTDESC");
  static_assert(HashCode("SHORTDESC") < HashCode("REPLACEABLE"), "Hash for SHORTDESC is not before REPLACEABLE");
  static_assert(HashCode("REPLACEABLE") < HashCode("GAMETIMEOUTANY"), "Hash for REPLACEABLE is not before GAMETIMEOUTANY");
  static_assert(HashCode("GAMETIMEOUTANY") < HashCode("GAMETIMEOUTSHORT"), "Hash for GAMETIMEOUTANY is not before GAMETIMEOUTSHORT");

  static_assert(HashCode("LAN") < HashCode("NAMERISK"), "Hash for LAN is not before NAMERISK");
  static_assert(HashCode("NAMERISK") < HashCode("CHECKLASTOWNER"), "Hash for NAMERISK is not before CHECKLASTOWNER");

  textFuncs.emplace_back(HashCode("URL"), [this]() { return string(EnsureUTF8(this->GetMapSiteURL())); });
  textFuncs.emplace_back(HashCode("OWNER"), [this]() { return this->m_OwnerName; });
  textFuncs.emplace_back(HashCode("CREATOR"), [this]() { return this->m_CreatorText; });
  textFuncs.emplace_back(HashCode("FILENAME"), [this]() { return string(EnsureUTF8(this->GetClientFileName())); });
  textFuncs.emplace_back(HashCode("AUTOSTART"), [this]() { return this->GetAutoStartText(); });
  textFuncs.emplace_back(HashCode("HOSTREALM"), [this]() {
    switch (this->m_Creator.GetServiceType()) {
      case ServiceType::kRealm: {
        if (this->m_Creator.GetIsExpired()) {
          return string("@unknown.battle.net");
        } else {
          return Concat("@", this->GetCreatedFrom<const CRealm>()->GetCanonicalDisplayName());
        }
        break;
      }
      case ServiceType::kIRC:
        return Concat("@", this->m_Aura->m_IRC.m_Config.m_HostName);
      case ServiceType::kDiscord:
        // FIXME: {HOSTREALM} may need to display the Discord guild
        return string("@users.discord.com");
      default:
        return Concat("@", ToFormattedRealm());
    }
  });
  textFuncs.emplace_back(HashCode("SHORTDESC"), [this]() { return this->m_Map->GetMapShortDesc(); });
  textFuncs.emplace_back(HashCode("OWNERREALM"), [this]() { return Concat("@", ToFormattedRealm(this->m_OwnerRealm)); });
  textFuncs.emplace_back(HashCode("GAMETIMEOUT"), [this]() { return this->GetPlayingTimeoutWelcomeText(); });
  textFuncs.emplace_back(HashCode("READYSTATUS"), [this]() { return this->GetReadyStatusText(); });
  textFuncs.emplace_back(HashCode("TRIGGER_PREFER_BROADCAST"), [this]() { return this->m_Config.m_BroadcastCmdToken.empty() ? this->m_Config.m_PrivateCmdToken : this->m_Config.m_BroadcastCmdToken; });
  textFuncs.emplace_back(HashCode("TRIGGER_PREFER_PRIVATE"), [this]() { return this->m_Config.m_PrivateCmdToken.empty() ? this->m_Config.m_BroadcastCmdToken : this->m_Config.m_PrivateCmdToken; });
  textFuncs.emplace_back(HashCode("TRIGGER_BROADCAST"), [this]() { return this->m_Config.m_BroadcastCmdToken; });
  textFuncs.emplace_back(HashCode("TRIGGER_PRIVATE"), [this]() { return this->m_Config.m_PrivateCmdToken; });

  static_assert(HashCode("URL") < HashCode("OWNER"), "Hash for URL is not before OWNER");
  static_assert(HashCode("OWNER") < HashCode("CREATOR"), "Hash for OWNER is not before CREATOR");
  static_assert(HashCode("CREATOR") < HashCode("FILENAME"), "Hash for CREATOR is not before FILENAME");
  static_assert(HashCode("FILENAME") < HashCode("AUTOSTART"), "Hash for FILENAME is not before AUTOSTART");
  static_assert(HashCode("AUTOSTART") < HashCode("HOSTREALM"), "Hash for AUTOSTART is not before HOSTREALM");
  static_assert(HashCode("HOSTREALM") < HashCode("SHORTDESC"), "Hash for HOSTREALM is not before SHORTDESC");
  static_assert(HashCode("SHORTDESC") < HashCode("OWNERREALM"), "Hash for SHORTDESC is not before OWNERREALM");
  static_assert(HashCode("OWNERREALM") < HashCode("GAMETIMEOUT"), "Hash for OWNERREALM is not before GAMETIMEOUT");
  static_assert(HashCode("GAMETIMEOUT") < HashCode("READYSTATUS"), "Hash for GAMETIMEOUT is not before READYSTATUS");
  static_assert(HashCode("READYSTATUS") < HashCode("TRIGGER_PREFER_BROADCAST"), "Hash for READYSTATUS is not before TRIGGER_PREFER_BROADCAST");
  static_assert(HashCode("TRIGGER_PREFER_BROADCAST") < HashCode("TRIGGER_PREFER_PRIVATE"), "Hash for TRIGGER_PREFER_BROADCAST is not before TRIGGER_PREFER_PRIVATE");
  static_assert(HashCode("TRIGGER_PREFER_PRIVATE") < HashCode("TRIGGER_BROADCAST"), "Hash for TRIGGER_PREFER_PRIVATE is not before TRIGGER_BROADCAST");
  static_assert(HashCode("TRIGGER_BROADCAST") < HashCode("TRIGGER_PRIVATE"), "Hash for TRIGGER_BROADCAST is not before TRIGGER_PRIVATE");

  const FlatMap<uint64_t, function<string()>> textFuncMap(move(textFuncs));
  const FlatMap<uint64_t, function<bool()>> boolFuncMap(move(boolFuncs));
  const FlatMap<uint64_t, bool> boolCache(move(boolValues));

  vector<string> lines;
  lines.reserve(m_Aura->m_Config.m_Greeting.size());
  for (const auto& templateLine : m_Aura->m_Config.m_Greeting) {
    string replaced = TrimString(RemoveDuplicateWhiteSpace(ReplaceTemplate(templateLine, &boolCache, nullptr, &boolFuncMap, &textFuncMap, true)));
    if (!replaced.empty()) {
      lines.push_back(replaced);
    }
  }
  return lines;
}

void CGame::SendWelcomeMessage(GameUser::CGameUser* user)
{
  // TODO: Name censored warning
  const vector<string> lines = GetWelcomeMessageLines(user);
  for (const auto& line : lines) {
    SendChat(user, line, LogLevelExtra::kTrace);
  }
}

void CGame::SendOwnerCommandsHelp(string_view cmdToken, GameUser::CGameUser* user)
{
  SendChat(user, Concat(cmdToken, "open [NUMBER] - opens a slot"), LogLevelExtra::kTrace);
  SendChat(user, Concat(cmdToken, "close [NUMBER] - closes a slot"), LogLevelExtra::kTrace);
  SendChat(user, Concat(cmdToken, "fill [DIFFICULTY] - adds computers"), LogLevelExtra::kTrace);
  if (m_Map->GetMapNumTeams() > 2) {
    SendChat(user, Concat(cmdToken, "ffa - sets free for all game mode"), LogLevelExtra::kTrace);
  }
  SendChat(user, Concat(cmdToken, "vsall - sets one vs all game mode"), LogLevelExtra::kTrace);
  SendChat(user, Concat(cmdToken, "terminator - sets humans vs computers"), LogLevelExtra::kTrace);
}

void CGame::SendCommandsHelp(string_view cmdToken, GameUser::CGameUser* user, const bool isIntro)
{
  if (isIntro) {
    SendChat(user, Concat("Welcome, ", user->GetName(), ". Please use ", cmdToken, GetTokenName(cmdToken), " for commands."), LogLevelExtra::kTrace);
  } else {
    SendChat(user, Concat("Use ", cmdToken, GetTokenName(cmdToken), " for commands."), LogLevelExtra::kTrace);
  }
  if (!isIntro) return;
  SendChat(user, Concat(cmdToken, "ping - view your latency"), LogLevelExtra::kTrace);
  SendChat(user, Concat(cmdToken, "go - starts the game"), LogLevelExtra::kTrace);
  if (!m_OwnerLessLocked && m_OwnerName.empty()) {
    SendChat(user, Concat(cmdToken, "owner - acquire permissions over this game"), LogLevelExtra::kTrace);
  }
  if (MatchOwnerName(user->GetName())) {
    SendOwnerCommandsHelp(cmdToken, user);
  }
  user->GetCommandHistory()->SetSentAutoCommandsHelp(true);
}

void CGame::EventOutgoingAtomicAction(const uint8_t UID, string_view action)
{
  const uint8_t actionType = GetByteAt(action, 0);

  if (actionType == ACTION_SAVE) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user) {
      LOG_APP_IF(LogLevel::kInfo, Concat("[", user->GetName(), "] is saving the game"));
      SendAllChat(Concat("[", user->GetDisplayName(), "] is saving the game"));
      if (user->GetIsNativeReferee() && !user->GetCanSave()) {
        SendChat(user, "NOTE: You have now reached the maximum allowed saves for this game.");
      }
    } else {
      CGameVirtualUser* virtualUserMatch = GetVirtualUserFromSID(GetSIDFromUID(UID));
      if (virtualUserMatch) {
        LOG_APP_IF(LogLevel::kInfo, Concat("Virtual user [", virtualUserMatch->GetName(), "] is saving the game"));
        SendAllChat(Concat("Virtual user [", virtualUserMatch->GetDisplayName(), "] is saving the game"));
      }
    }
  }

  if (actionType == ACTION_SAVE_ENDED) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user) {
      LOG_APP_IF(LogLevel::kInfo, Concat("[", user->GetName(), "] finished saving the game"));
    }
  }

  if (actionType == ACTION_PAUSE) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user) {
      LOG_APP_IF(LogLevel::kInfo, Concat("[", user->GetName(), "] paused the game"));
      if (user->GetIsNativeReferee() && !user->GetCanPause()) {
        SendChat(user, "NOTE: You have now reached the maximum allowed pauses for this game.");
      }
    } else {
      CGameVirtualUser* virtualUserMatch = GetVirtualUserFromSID(GetSIDFromUID(UID));
      if (virtualUserMatch) {
        LOG_APP_IF(LogLevel::kInfo, Concat("Virtual user [", virtualUserMatch->GetName(), "] paused the game"));
      }
    }
  }

  if (actionType == ACTION_RESUME) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user) {
      LOG_APP_IF(LogLevel::kInfo, Concat("[", user->GetName(), "] resumed the game"));
    } else {
      CGameVirtualUser* virtualUserMatch = GetVirtualUserFromSID(GetSIDFromUID(UID));
      if (virtualUserMatch) {
        LOG_APP_IF(LogLevel::kInfo, Concat("Virtual user [", virtualUserMatch->GetName(), "] resumed the game"));
      }
    }
  }

  if (actionType == ACTION_CHAT_TRIGGER && action.size() >= 10) {
    string_view chatMessage = ExtractStringView<OOBPolicy::kUnsafe, NullTerminatorPolicy::kRequired, StringEncoding::kNone>(action, 9, 0);
    if (!chatMessage.empty()) {
      GameUser::CGameUser* user = GetUserFromUID(UID);
      if (user) {
        EventChatTrigger(user, chatMessage, ByteArrayToUInt32LE(action, 1), ByteArrayToUInt32LE(action, 5));
      }
    }
  }

  if (actionType == ACTION_ALLIANCE_SETTINGS && action.size() >= 6 && GetByteAt(action, 1) < MAX_SLOTS_MODERN) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user) {
      const bool wantsShare = (ByteArrayToUInt32LE(action, 2) & ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY) == ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY;
      const uint8_t targetSID = GetByteAt(action, 1);
      if (user->GetIsSharingUnitsWithSlot(targetSID) != wantsShare) {
        if (wantsShare) {
          LOG_APP_IF(LogLevel::kDebug, Concat("Player [", user->GetName(), "] granted shared unit control to [", GetUserNameFromSID(targetSID), "]"));
        } else {
          LOG_APP_IF(LogLevel::kDebug, Concat("Player [", user->GetName(), "] took away shared unit control from [", GetUserNameFromSID(targetSID), "]"));
        }
        user->SetIsSharingUnitsWithSlot(targetSID, wantsShare);
        GameUser::CGameUser* targetUser = GetUserFromSID(targetSID);
        if (targetUser) {
          if (
            (InspectSlot(targetSID)->GetTeam() == InspectSlot(user->GetSID())->GetTeam()) &&
            (m_Map->GetMapFlags() & GAMEFLAG_FIXEDTEAMS)
          ) {
            targetUser->SetHasControlOverUnitsFromSlot(user->GetSID(), wantsShare);
            if (wantsShare && m_Config.m_ShareUnitsHandler == OnShareUnitsHandler::kRestrictSharee) {
              int64_t timeout = user->GetAntiAbuseTimeout();
              if (!user->GetAntiShareKicked()) {
                user->AddKickReason(GameUser::KickReason::kAntiShare);
                user->KickAtLatest(m_Aura->GetClockTicks() + timeout);
                user->AddAbuseCounter();
              }
              user->SetLeftCode(PLAYERLEAVE_LOST);
              user->SetLeftReason("autokicked - antishare");
              SendChat(user, Concat("[ANTISHARE] You will be kicked out of the game unless you remove Shared Unit Control within ", ToDurationString(timeout / 1000), "."));
              SendChat(targetUser, Concat("[ANTISHARE] You may not perform further actions until [", user->GetDisplayName(), "] removes Shared Unit Control."));
            }
            if (!wantsShare) {
              targetUser->CheckReleaseOnHoldActions();
            }
          }
        }
        if (!wantsShare && user->GetAntiShareKicked() && !user->GetIsSharingUnitsWithAnyAllies()) {
          user->ResetLeftReason();
          user->RemoveKickReason(GameUser::KickReason::kAntiShare);
          user->CheckStillKicked();
        }
      }
    }
  }

  if (actionType == ACTION_MINIMAPSIGNAL) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user && user->GetIsObserver()) {
      SendObserverChat(Concat("[", user->GetName(), "] sent a minimap signal."));
    }
  }

  if (actionType == ACTION_GAME_CACHE_INT && action.size() >= 6) {
    EventGameCacheInteger(UID, action.substr(1));
  }
}

void CGame::SendAllActionsCallback()
{
  CQueuedActionsFrame& frame = GetFirstActionFrame();
  switch (frame.callback) {
    case ON_SEND_ACTIONS_PAUSE:
      m_IsPaused = true;
      m_PauseUser = GetUserFromUID(frame.pauseUID);
      m_LastPausedTicks = m_Aura->GetClockTicks();
      break;
    case ON_SEND_ACTIONS_RESUME:
      m_IsPaused = false;
      m_PauseUser = nullptr;
      break;
    default:
      break;
  }

  for (const ActionQueue& actionQueue : frame.actions) {
    for (const CIncomingAction& action : actionQueue) {
      vector<const uint8_t*> delimiters = action.SplitAtomic();
      for (size_t i = 0, j = 1, l = delimiters.size(); j < l; i++, j++) {
        EventOutgoingAtomicAction(
          action.GetUID(), string_view(
            reinterpret_cast<const char*>(delimiters[i]),
            static_cast<size_t>(delimiters[j] - delimiters[i])
          )
        );
      }
    }
  }

  for (GameUser::CGameUser* user : frame.leavers) {
    DLOG_APP_IF(LogLevel::kTrace, Concat("[", user->GetName(), "] running scheduled deletion"));
    user->SetDeleteMe(true);
  }

  frame.Reset();
}

void CGame::CheckActions()
{
  for (auto& user : m_Users) {
    user->CheckReleaseOnHoldActions();
    user->ShiftRecentActionCounters();
  }
  m_LastCheckActionsTicks = m_EffectiveTicks;
}

void CGame::PauseAPMTrainer()
{
  m_APMTrainerPaused = true;
}

void CGame::ResumeAPMTrainer()
{
  m_APMTrainerPaused = false;
}

void CGame::RestartAPMTrainer()
{
  m_APMTrainerTicks = 0;
  ResumeAPMTrainer();
  CheckActions();
}

uint8_t CGame::GetNumInGameReadyUsers() const
{
  uint8_t count = 0;
  for (auto& user : m_Users) {
    if (user->GetInGameReady()) {
      ++count;
    }
  }
  return count;
}

void CGame::ResetInGameReadyUsers() const
{
  for (auto& user : m_Users) {
    user->SetInGameReady(false);
  }
}

void CGame::SendGProxyEmptyActions()
{
  if (!GetAnyUsingGProxy()) {
    return;
  }

  const vector<uint8_t> emptyActions = GameProtocol::SEND_W3GS_EMPTY_ACTIONS(m_GProxyEmptyActions);

  // GProxy sends these empty actions itself BEFORE every action received.
  // So we need to match it, to avoid desyncs.
  // Note that Warcraft III doesn't respond to empty actions (i.e no keep alive frame).
  for (auto& user : m_Users) {
    if (!user->GetCanReconnect()) {
      Send(user, emptyActions);
    }
  }

  if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
    m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_GPROXY);
  }
}

void CGame::SendAllActions()
{
  const int64_t hiResTicks = GetTicks();
  const int64_t activeLatency = GetActiveLatency();
  if (!m_IsPaused) {
    m_EffectiveTicks += activeLatency;
  } else {
    m_PausedTicksDeltaSum += activeLatency;
  }

  // Note that Warcraft III doesn't respond to empty actions (i.e no keep alive frame).
  // So adding +1 to sync counter is enough.
  ++m_SyncCounter;

  SendGProxyEmptyActions();
  vector<uint8_t> actions = GetFirstActionFrame().GetBytes((uint16_t)activeLatency);
  SendAll(actions);

  if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
    m_GameHistory->m_PlayingBuffer.emplace_back(m_IsPaused ? GAME_FRAME_TYPE_PAUSED : GAME_FRAME_TYPE_ACTIONS, actions);
    m_GameHistory->EventActionFramePushed();
    m_GameHistory->UpdateSpectatorActions((int64_t)m_Config.m_SpectatorDelay);
  }

  m_LastActionSentTicks = hiResTicks;
  m_LastActionExpectedTicks += activeLatency;

  SendAllActionsCallback();

  RunActionsScheduler();
}

std::string CGame::GetCustomGameNameTemplate(shared_ptr<const CRealm> realm, bool forceLobby) const
{
  const bool isSpectator = !forceLobby && (m_GameLoading || m_GameLoaded);
  if (realm) {
    if (isSpectator) {
      return realm->GetWatchableNameTemplate();
    } else {
      return realm->GetLobbyNameTemplate();
    }
  } else {
    if (isSpectator) {
      return m_Aura->m_Config.m_LANWatchableNameTemplate;
    } else {
      return m_Aura->m_Config.m_LANLobbyNameTemplate;
    }
  }
}

std::string CGame::GetCustomGameName(shared_ptr<const CRealm> realm, bool forceLobby) const
{
  string nameTemplate = GetCustomGameNameTemplate(realm, forceLobby);

  const FlatMap<uint64_t, string> textCache;

  vector<pair<uint64_t, function<string()>>> textFuncs;
  textFuncs.reserve(3);
  textFuncs.emplace_back(HashCode("MODE"),    [this] () { return this->GetHCLCommandString(); });
  textFuncs.emplace_back(HashCode("NAME"),    [this] () { return this->GetGameName(); });
  textFuncs.emplace_back(HashCode("COUNTER"), [this, realm]() { return this->GetCreationCounterText(realm); });
  static_assert(HashCode("MODE") < HashCode("NAME"), "Hash for MODE is not before NAME");
  static_assert(HashCode("NAME") < HashCode("COUNTER"), "Hash for NAME is not before COUNTER");
  const FlatMap<uint64_t, function<string()>> textFuncMap(move(textFuncs));

  string replaced = ReplaceTemplate(nameTemplate, nullptr, &textCache, nullptr, &textFuncMap);
  return TrimString(RemoveDuplicateWhiteSpace(replaced));
}

std::string CGame::GetNextCustomGameName(shared_ptr<const CRealm> realm, bool forceLobby) const
{
  string nameTemplate = GetCustomGameNameTemplate(realm, forceLobby);

  const FlatMap<uint64_t, string> textCache;

  vector<pair<uint64_t, function<string()>>> textFuncs;
  textFuncs.reserve(3);
  textFuncs.emplace_back(HashCode("MODE"),    [this] () { return this->GetHCLCommandString(); });
  textFuncs.emplace_back(HashCode("NAME"),    [this] () { return this->GetGameName(); });
  textFuncs.emplace_back(HashCode("COUNTER"), [this, realm]() { return this->GetNextCreationCounterText(realm); });
  static_assert(HashCode("MODE") < HashCode("NAME"), "Hash for MODE is not before NAME");
  static_assert(HashCode("NAME") < HashCode("COUNTER"), "Hash for NAME is not before COUNTER");
  const FlatMap<uint64_t, function<string()>> textFuncMap(move(textFuncs));

  string replaced = ReplaceTemplate(nameTemplate, nullptr, &textCache, nullptr, &textFuncMap);
  return TrimString(RemoveDuplicateWhiteSpace(replaced));
}

std::string CGame::GetDiscoveryNameLAN() const
{
  return GetCustomGameName(nullptr, false);
}

std::string CGame::GetShortNameLAN() const
{
  return GetCustomGameName(nullptr, true);
}

string CGame::GetAnnounceText(shared_ptr<const CRealm> realm) const
{
  bool isSpectator = !GetIsLobbyOrMirror();
  Version version = GetVersion();
  if (realm) {
    version = realm->GetGameVersion();
  }
  uint32_t mapSize = m_Map->GetMapSize();
  string versionPrefix;
  if (mapSize > 0x20000000 || (version <= GAMEVER(1u, 28u) && mapSize > MAX_MAP_SIZE_1_28) || (version <= GAMEVER(1u, 26u) && mapSize > MAX_MAP_SIZE_1_26) || (version <= GAMEVER(1u, 23u) && mapSize > MAX_MAP_SIZE_1_23)) {
    versionPrefix = Concat("[", ToVersionString(version), ".UnlockMapSize] ");
  } else {
    versionPrefix = Concat("[", ToVersionString(version), "] ");

}
  string startedPhrase;
  if (m_IsMirror || m_RestoredGame || m_OwnerName.empty()) {
    startedPhrase = Concat(". (\"", GetCustomGameName(realm), "\")");
  } else {
    startedPhrase = Concat(". (Started by ", m_OwnerName, ": \"", GetCustomGameName(realm), "\")");
  }

  string typeWord;
  if (m_RestoredGame) {
    typeWord = "Loaded game";
  } else if (m_RealmsDisplayMode == GAME_DISPLAY_PRIVATE) {
    typeWord = "Private game";
  } else {
    typeWord = "Game";
  }

  string capabilityWord;
  if (isSpectator) {
    capabilityWord = " watchable: ";
  } else if (m_IsMirror) {
    capabilityWord = " mirrored: ";
  } else {
    capabilityWord = " hosted: ";
  }

  return Concat(versionPrefix, typeWord, capabilityWord, EnsureUTF8(m_Map->GetServerFileName()), startedPhrase);
}

uint16_t CGame::CalcHostPortFromType(const uint8_t type) const
{
  switch (type) {
    case GAME_DISCOVERY_INTERFACE_IPV4:
      // Uses <net.game_discovery.udp.tcp4_custom_port.value>
      if (m_Aura->m_Net.m_Config.m_UDPEnableCustomPortTCP4) {
        return m_Aura->m_Net.m_Config.m_UDPCustomPortTCP4;
      }
      return m_HostPort;
    case GAME_DISCOVERY_INTERFACE_IPV6:
      // Uses <net.game_discovery.udp.tcp6_custom_port.value>
      if (m_Aura->m_Net.m_Config.m_UDPEnableCustomPortTCP6) {
        return m_Aura->m_Net.m_Config.m_UDPCustomPortTCP6;
      }
      return m_HostPort;
    case GAME_DISCOVERY_INTERFACE_LOOPBACK:
      return m_HostPort;
    default:
      return 0;
  }
}

uint16_t CGame::GetHostPortFromType(const uint8_t type) const
{
  const GameDiscoveryInterface* match = m_NetInterfaces.find(type);
  if (!match) return 0;
  return match->port;
}

uint16_t CGame::GetHostPortFromTargetAddress(const sockaddr_storage* address) const
{
  if (isLoopbackAddress(address)) {
    return GetHostPortFromType(GAME_DISCOVERY_INTERFACE_LOOPBACK);
  }
  switch (GetInnerIPVersion(address)) {
    case AF_INET:
      return GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV4);
    case AF_INET6:
      return GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV6);
    default:
      return 0;
  }
}

uint8_t CGame::CalcActiveReconnectProtocols() const
{
  uint8_t protocols = 0;
  for (const auto& user : m_Users) {
    if (!user->GetCanReconnect()) continue;
    if (user->GetGProxy()->GetIsExtended()) {
      protocols |= RECONNECT_ENABLED_GPROXY_EXTENDED;
      if (protocols != RECONNECT_ENABLED_GPROXY_EXTENDED) break;
    } else {
      protocols |= RECONNECT_ENABLED_GPROXY_BASIC;
      if (protocols != RECONNECT_ENABLED_GPROXY_BASIC) break;
    }
  }
  return protocols;
}

string CGame::GetActiveReconnectProtocolsDetails() const
{
  // Must only be used to print to console, because GetName() is used instead of GetDisplayName()
  vector<string> protocols;
  for (const auto& user : m_Users) {
    if (!user->GetCanReconnect()) {
      protocols.push_back(Concat("[", user->GetName(), ": OFF]"));
    } else if (user->GetGProxy()->GetIsExtended()) {
      protocols.push_back(Concat("[", user->GetName(), ": EXT]"));
    } else {
      protocols.push_back(Concat("[", user->GetName(), ": ON]"));
    }
  }
  return JoinStrings(protocols);
}

bool CGame::CalcAnyUsingGProxy() const
{
  for (const auto& user : m_Users) {
    if (user->GetCanReconnect()) {
      return true;
    }
  }
  return false;
}

bool CGame::CalcAnyUsingGProxyLegacy() const
{
  for (const auto& user : m_Users) {
    if (!user->GetCanReconnect()) continue;
    if (!user->GetGProxy()->GetIsExtended()) {
      return true;
    }
  }
  return false;
}

PlayersReadyMode CGame::GetPlayersReadyMode() const {
  return m_Config.m_PlayersReadyMode;
}

shared_ptr<CGame> CGame::GetCheckedShared() {
  if (m_Destroying) {
    return nullptr;
  }
  return shared_from_this();
}

CQueuedActionsFrame& CGame::GetFirstActionFrame()
{
  return GetFirstActionFrameNode()->data;
}

CQueuedActionsFrame& CGame::GetLastActionFrame()
{
  return GetLastActionFrameNode()->data;
}

vector<QueuedActionsFrameNode*> CGame::GetFrameNodesInRangeInclusive(const uint8_t startOffset, const uint8_t endOffset)
{
  vector<QueuedActionsFrameNode*> frameNodes;
  uint8_t nodeCount = ToBaseOne(MINUS_TINY(endOffset, startOffset));
  frameNodes.reserve(nodeCount);
  QueuedActionsFrameNode* frameNode = GetFirstActionFrameNode();
  uint8_t offset = startOffset;
  while (offset--) {
    frameNode = frameNode->next;
  }
  offset = nodeCount;
  while (offset--) {
    frameNodes.push_back(frameNode);
    frameNode = frameNode->next;
  }
  return frameNodes;
}

vector<QueuedActionsFrameNode*> CGame::GetAllFrameNodes()
{
  vector<QueuedActionsFrameNode*> frameNodes;
  frameNodes.reserve(GetMaxEqualizerDelayFrames());
  QueuedActionsFrameNode* frameNode = GetFirstActionFrameNode();
  if (frameNode == nullptr) return frameNodes;
  QueuedActionsFrameNode* lastFrameNode = GetLastActionFrameNode();
  while (frameNode != lastFrameNode) {
    frameNodes.push_back(frameNode);
    frameNode = frameNode->next;
  }
  return frameNodes;
}

void CGame::MergeFrameNodes(vector<QueuedActionsFrameNode*>& frameNodes)
{
  size_t i = 0, frameCount = frameNodes.size();
  CQueuedActionsFrame& targetFrame = frameNodes[i++]->data;
  while (i < frameCount) {
    CQueuedActionsFrame& obsoleteFrame = frameNodes[i]->data;
    targetFrame.MergeFrame(obsoleteFrame);
    m_Actions.remove(frameNodes[i]);
    // When the node is deleted, data is deleted as well.
    delete frameNodes[i];
    ++i;
  }
}

void CGame::ResetUserPingEqualizerDelays()
{
  for (auto& user : m_Users) {
    user->SetPingEqualizerFrameNode(m_Actions.head);
  }
}

bool CGame::CheckUpdatePingEqualizer()
{
  if (!m_Config.m_LatencyEqualizerEnabled) return false;
  // Use m_EffectiveTicks instead of GetTicks() to ensure we don't drift while lag screen is displayed.
  if (m_EffectiveTicks - m_LastPingEqualizerGameTicks < PING_EQUALIZER_PERIOD_TICKS) {
    return false;
  }
  return true;
  
}

uint8_t CGame::UpdatePingEqualizer()
{
  uint8_t maxEqualizerOffset = 0;
  vector<pair<GameUser::CGameUser*, uint32_t>> descendingRTTs = GetDescendingSortedRTT();
  if (descendingRTTs.empty()) return maxEqualizerOffset;
  const uint32_t maxPing = descendingRTTs[0].second;
  bool addedFrame = false;
  for (const pair<GameUser::CGameUser*, uint32_t>& userPing : descendingRTTs) {
    // How much better ping than the worst player?
    const uint32_t framesAheadNowDiscriminator = (maxPing - userPing.second) / (uint32_t)m_LatencyTicks;
    const uint32_t framesAheadBefore = userPing.first->GetPingEqualizerOffset();
    uint32_t framesAheadNow;
    if (framesAheadNowDiscriminator > framesAheadBefore) {
      framesAheadNow = framesAheadBefore + 1;
      if (!addedFrame && m_PingEqualizerActiveDelayFrames < framesAheadNow && framesAheadNow < m_PingEqualizerMaxFrames) {
        m_Actions.emplaceAfter(GetLastActionFrameNode());
        addedFrame = true;
      }
      userPing.first->AddDelayPingEqualizerFrame();
    } else if (0 < framesAheadBefore && framesAheadNowDiscriminator < framesAheadBefore) {
      framesAheadNow = framesAheadBefore - 1;
      userPing.first->SubDelayPingEqualizerFrame();
    }
    uint8_t nextOffset = userPing.first->GetPingEqualizerOffset();
    if (nextOffset > maxEqualizerOffset) {
      maxEqualizerOffset = nextOffset;
    }
  }
  m_LastPingEqualizerGameTicks = m_EffectiveTicks;
  return maxEqualizerOffset;
}

vector<pair<GameUser::CGameUser*, uint32_t>> CGame::GetDescendingSortedRTT() const
{
  vector<pair<GameUser::CGameUser*, uint32_t>> sortableUserPings;
  for (auto& user : m_Users) {
     if (!user->GetLeftMessageSent() && !user->GetIsObserver()) {
       sortableUserPings.emplace_back(user, user->GetRTT().value_or(0));
     }
  }
  sort(begin(sortableUserPings), end(sortableUserPings), &GameUser::SortUsersByPairedUint32Descending);
  return sortableUserPings;
}

uint16_t CGame::GetDiscoveryPort(const uint8_t protocol) const
{
  return m_Aura->m_Net.GetUDPPort(protocol);
}

vector<uint8_t> CGame::GetGameDiscoveryInfo(const Version& gameVersion, const uint16_t hostPort)
{
  uint32_t slotsOff = static_cast<uint32_t>(GetNumSlots() == GetNumSlotsOpen() ? GetNumSlots() : GetNumSlotsOpen() + 1);
  uint32_t uptime = GetUptime();
  if (m_Config.m_CrossPlayMode != CrossPlayMode::kForce || (GAMEVER(1u, 24u) <= m_SupportedGameVersionsMin && m_SupportedGameVersionsMax <= GAMEVER(1u, 28u))) {
    vector<uint8_t> info = *(GetGameDiscoveryInfoTemplate());
    WriteUint32LE(info, gameVersion.second, m_GameDiscoveryInfoVersionOffset);
    WriteUint32LE(info, slotsOff, m_GameDiscoveryInfoDynamicOffset);
    WriteUint32LE(info, uptime, m_GameDiscoveryInfoDynamicOffset + 4);
    WriteUint16LE(info, hostPort, m_GameDiscoveryInfoDynamicOffset + 8);
    return info;    
  } else {
    vector<uint8_t> info = GameProtocol::SEND_W3GS_GAMEINFO(
      GetIsExpansion(),
      gameVersion,
      GetGameType(),
      GetGameFlags(),
      GetAnnounceWidth(nullptr),
      GetAnnounceHeight(nullptr),
      GetDiscoveryNameLAN(),
      GetIndexHostName(),
      uptime,
      GetSourceFilePath(),
      GetSourceFileHashBlizz(gameVersion),
      GetNumSlots(), // Total Slots
      slotsOff,
      hostPort,
      m_HostCounter,
      m_EntryKey
    );
    return info;
  }
}

vector<uint8_t>* CGame::GetGameDiscoveryInfoTemplate()
{
  if (!(m_GameDiscoveryInfoChanged & GAME_DISCOVERY_CHANGED_MAJOR)) {
    return &m_GameDiscoveryInfo;
  }
  m_GameDiscoveryInfo = GetGameDiscoveryInfoTemplateInner(&m_GameDiscoveryInfoVersionOffset, &m_GameDiscoveryInfoDynamicOffset);
  UNSET_TINY(m_GameDiscoveryInfoChanged, GAME_DISCOVERY_CHANGED_MAJOR);
  return &m_GameDiscoveryInfo;
}

vector<uint8_t> CGame::GetGameDiscoveryInfoTemplateInner(uint16_t* gameVersionOffset, uint16_t* dynamicInfoOffset) const
{
  // we send 12 for SlotsTotal because this determines how many UID's Warcraft 3 allocates
  // we need to make sure Warcraft 3 allocates at least SlotsTotal + 1 but at most 12 UID's
  // this is because we need an extra UID for the virtual host user (but we always delete the virtual host user when the 12th person joins)
  // however, we can't send 13 for SlotsTotal because this causes Warcraft 3 to crash when sharing control of units
  // nor can we send SlotsTotal because then Warcraft 3 crashes when playing maps with less than 12 UID's (because of the virtual host user taking an extra UID)
  // we also send 12 for SlotsOpen because Warcraft 3 assumes there's always at least one user in the game (the host)
  // so if we try to send accurate numbers it'll always be off by one and results in Warcraft 3 assuming the game is full when it still needs one more user
  // the easiest solution is to simply send 12 for both so the game will always show up as (1/12) users

  // note: the PrivateGame flag is not set when broadcasting to LAN (as you might expect)
  // note: we do not use m_Map->GetMapGameType because none of the filters are set when broadcasting to LAN (also as you might expect)

  return GameProtocol::SEND_W3GS_GAMEINFO_TEMPLATE(
    gameVersionOffset, dynamicInfoOffset,
    GetIsExpansion(),
    GetGameType(),
    GetGameFlags(),
    GetAnnounceWidth(nullptr),
    GetAnnounceHeight(nullptr),
    GetDiscoveryNameLAN(),
    GetIndexHostName(),
    GetSourceFilePath(),
    GetSourceFileHashBlizz(GetVersion()),
    GetNumSlots(), // Total Slots
    m_HostCounter,
    m_EntryKey
  );
}

void CGame::AnnounceDecreateToRealms()
{
  for (auto& realm : m_Aura->m_Realms) {
    if (m_IsMirror && realm->GetIsMirror())
      continue;

    if (realm->GetGameBroadcast() == shared_from_this()) {
      realm->ResetGameChatAnnouncement();
      realm->ResetGameBroadcastData(); // STOPADV
      realm->TrySendEnterChat();
    }

    if (realm->GetGameBroadcastPending() == shared_from_this()) {
      realm->ResetGameBroadcastPending();
    }
  }
}

void CGame::AnnounceToAddress(string& addressLiteral, const optional<Version>& customGameVersion)
{
  Version version = GetVersion();
  if (customGameVersion.has_value()) {
    version = customGameVersion.value();
  }
  optional<sockaddr_storage> maybeAddress = CNet::ParseAddress(addressLiteral);
  if (!maybeAddress.has_value())
    return;

  sockaddr_storage* address = &(maybeAddress.value());
  SetAddressPort(address, 6112);
  m_Aura->m_Net.Send(address, GetGameDiscoveryInfo(version, GetHostPortFromTargetAddress(address)));
}

void CGame::ReplySearch(sockaddr_storage* address, CSocket* socket, const optional<Version>& customGameVersion)
{
  Version version = GetVersion();
  if (customGameVersion.has_value()) {
    version = customGameVersion.value();
  }
  socket->SendReply(address, GetGameDiscoveryInfo(version, GetHostPortFromTargetAddress(address)));
}

void CGame::SendGameDiscoveryCreate(const Version& version) const
{
  vector<uint8_t> packet = GameProtocol::SEND_W3GS_CREATEGAME(GetIsExpansion(), version, m_HostCounter);
  m_Aura->m_Net.SendGameDiscovery(packet, m_Config.m_ExtraDiscoveryAddresses);
}

void CGame::SendGameDiscoveryCreate() const
{
  Version version = m_SupportedGameVersionsMin;
  Version maxVersion = std::min(m_SupportedGameVersionsMax, GAMEVER(1u, 29u));
  while (version <= maxVersion) {
    if (GetIsSupportedGameVersion(version)) {
      SendGameDiscoveryCreate(version);
    }
    version = GetNextVersion(version);
  }
}

void CGame::SendGameDiscoveryDecreate() const
{
  vector<uint8_t> packet = GameProtocol::SEND_W3GS_DECREATEGAME(m_HostCounter);
  m_Aura->m_Net.SendGameDiscovery(packet, m_Config.m_ExtraDiscoveryAddresses);
}

void CGame::SendGameDiscoveryRefresh() const
{
  vector<uint8_t> packet = GameProtocol::SEND_W3GS_REFRESHGAME(
    m_HostCounter,
    static_cast<uint32_t>(m_SlotsConfig.GetCount() == GetNumSlotsOpen() ? 1 : m_SlotsConfig.GetCount() - GetNumSlotsOpen()),
    static_cast<uint32_t>(m_SlotsConfig.GetCount())
  );
  m_Aura->m_Net.SendGameDiscovery(packet, m_Config.m_ExtraDiscoveryAddresses);

  // Send to active VLAN connections
  if (m_Aura->m_Net.m_Config.m_VLANEnabled) {
    for (auto& serverConnections : m_Aura->m_Net.m_GameSeekers) {
      for (auto& connection : serverConnections.second) {
        if (connection->GetDeleteMe()) continue;
        if (connection->GetIsVLAN() && connection->HasGameVersion() && GetIsSupportedGameVersion(connection->GetGameVersion())) {
          SendGameDiscoveryInfoVLAN(connection);
        }
      }
    }
  }
}

void CGame::SendGameDiscoveryInfo(const Version& gameVersion)
{
  // See CNet::SendGameDiscovery()
  m_Aura->m_Net.SendBroadcast(GetGameDiscoveryInfo(gameVersion, GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV4)));

  for (auto& address : m_Config.m_ExtraDiscoveryAddresses) {
    if (isLoopbackAddress(&address)) continue; // We already ensure sending loopback packets above.
    bool isIPv6 = GetInnerIPVersion(&address) == AF_INET6;
    if (isIPv6 && !m_Aura->m_Net.m_SupportTCPOverIPv6) {
      continue;
    }
    m_Aura->m_Net.Send(&address, GetGameDiscoveryInfo(gameVersion, GetHostPortFromType(isIPv6 ? GAME_DISCOVERY_INTERFACE_IPV6 : GAME_DISCOVERY_INTERFACE_IPV4)));
  }

  // Send to active UDP in TCP tunnels and VLAN connections
  if (m_Aura->m_Net.m_Config.m_EnableTCPWrapUDP || m_Aura->m_Net.m_Config.m_VLANEnabled) {
    for (auto& serverConnections : m_Aura->m_Net.m_GameSeekers) {
      for (auto& connection : serverConnections.second) {
        if (connection->GetDeleteMe()) continue;
        if (connection->GetIsUDPTunnel()) {
          connection->Send(GetGameDiscoveryInfo(gameVersion, GetHostPortFromType(connection->GetUsingIPv6() ? GAME_DISCOVERY_INTERFACE_IPV6 : GAME_DISCOVERY_INTERFACE_IPV4)));
        }
        if (connection->GetIsVLAN() && connection->HasGameVersion() && GetIsSupportedGameVersion(connection->GetGameVersion())) {
          SendGameDiscoveryInfoVLAN(connection);
        }
      }
    }
  }
}

void CGame::QueueSendGameDiscoveryInfo()
{
  m_GameDiscoveryPending = true;
}

void CGame::SendGameDiscoveryInfoMDNS() const
{
#ifndef DISABLE_MDNS
  for (const auto& intEntry : m_NetInterfaces.get()) {
    const GameDiscoveryInterface& interface = intEntry.second;
    for (const auto& bonEntry : interface.mdns) {
      bonEntry.second->PushRecord(shared_from_this());
    }
  }
#endif
}

void CGame::SendGameDiscoveryInfoVLAN(CGameSeeker* gameSeeker) const
{
  array<uint8_t, 4> IP = {0, 0, 0, 0};
  uint16_t port = GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV4);
  if (m_IsMirror) {
    IP = GetPublicHostAddress();
    port = GetPublicHostPort();
  }
  gameSeeker->Send(
    VLANProtocol::SEND_VLAN_GAMEINFO(
      GetIsExpansion(),
      gameSeeker->GetGameVersion(),
      GetGameType(),
      GetGameFlags(),
      GetAnnounceWidth(nullptr),
      GetAnnounceHeight(nullptr),
      GetDiscoveryNameLAN(),
      GetIndexHostName(),
      GetUptime(), // dynamic
      GetSourceFilePath(),
      GetSourceFileHashBlizz(GetVersion()),
      static_cast<uint32_t>(m_SlotsConfig.GetCount()), // Total Slots
      static_cast<uint32_t>(m_SlotsConfig.GetCount() == GetNumSlotsOpen() ? m_SlotsConfig.GetCount() : GetNumSlotsOpen() + 1),
      IP,
      port,
      m_HostCounter,
      m_EntryKey
    )
  );
}

void CGame::SendGameDiscoveryInfo()
{
  Version version = m_SupportedGameVersionsMin;
  Version maxVersion = std::min(m_SupportedGameVersionsMax, GAMEVER(1u, 29u));
  while (version <= maxVersion) {
    if (GetIsSupportedGameVersion(version)) {
      SendGameDiscoveryInfo(version);
    }
    version = GetNextVersion(version);
  }
}

void CGame::ChangeGameName(const std::string& gameName)
{
  m_GameName = gameName;

  for (auto& realm : m_Aura->m_Realms) {
    if (realm->GetGameBroadcast() == shared_from_this()) {
      realm->SetGameBroadcastWantsRename();
    }
  }
}

/*
 * EventUserDeleted is called when CGame event loop identifies that a CGameUser has the m_DeleteMe flag
 * This flag is set by
 * - SendAllActionsCallback (after the game started, or failed to start)
 * - StopPlayers
 * - EventUserAfterDisconnect (only in the game lobby)
 */
void CGame::EventUserDeleted(GameUser::CGameUser* user, fd_set* /*fd*/, fd_set* send_fd)
{
  if (!user->GetMapChecked()) {
    user->AddLeftReason("map not validated");
  }

  if (m_Exiting) {
    LOG_APP_IF(LogLevel::kDebug, Concat("deleting user [", user->GetName(), "]: ", user->GetLeftReason()));
  } else {
    LOG_APP_IF(LogLevel::kInfo, Concat("deleting user [", user->GetName(), "]: ", user->GetLeftReason()));
  }

  if (!user->GetIsObserver()) {
    m_LastPlayerLeaveTicks = m_Aura->GetClockTicks();
    m_LastPingEqualizerGameTicks = 0;
  }

  // W3GS_PLAYERLEAVE messages may follow ACTION_PAUSE or ACTION_RESUME (or none),
  // so ensure we don't leave m_PauseUser as a dangling pointer.
  if (m_PauseUser == user) {
    m_PauseUser = nullptr;
  }

  if (m_GameLoading || m_GameLoaded) {
    for (auto& otherPlayer : m_SyncPlayers[user]) {
      UserList& BackList = m_SyncPlayers[otherPlayer];
      auto BackIterator = std::find(BackList.begin(), BackList.end(), user);
      if (BackIterator == BackList.end()) {
      } else {
        *BackIterator = std::move(BackList.back());
        BackList.pop_back();
      }
    }
    m_SyncPlayers.erase(user);
    m_HadLeaver = true;
  } else {
    if (!user->GetMapChecked() && !user->GetGameVersionIsExact() && !m_Map->GetMapSizeIsNativeSupported(m_SupportedGameVersionsMin)) {
      // Crossplay enabled, but the map size is too large for some supported versions.
      // There's a chance the user left because they joined a default-1.27 lobby over LAN, but they are running 1.26.
      // Interestingly enough, 1.27 clients have no problem joining a default-1.26 lobby
      // They even send W3GS_MAPSIZE packets reporting 8 MB.
      string lowerName = user->GetLowerName();
      auto match = m_VersionErrors.find(lowerName);
      if (match == m_VersionErrors.end()) {
        m_VersionErrors[lowerName] = set<Version>{user->GetGameVersion()};
      } else if (m_VersionErrors.size() < MAX_GAME_VERSION_ERROR_USERS_STORED) {
        match->second.insert(move(user->GetGameVersion()));
      }
    }
    if (!m_LobbyLoading && m_Config.m_LobbyOwnerReleaseLANLeaver) {
      if (MatchOwnerName(user->GetName()) && m_OwnerRealm == user->GetRealmHostName() && user->GetRealmHostName().empty()) {
        ReleaseOwner();
      }
    }
  }

  // send the left message if we haven't sent it already
  // it may only be prematurely sent if this is a lobby
  if (!user->GetLeftMessageSent()) {
    if (user->GetIsLagging()) {
      DLOG_APP_IF(LogLevel::kTrace, Concat("global lagger update (-", user->GetName(), ")"));
      SendAll(GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetClockTicks()));
    }
    SendLeftMessage(user, (m_GameLoaded && !user->GetIsObserver()) || (!user->GetIsLeaver() && user->GetAnyKicked()));
    if (m_GameLoaded) m_IsSinglePlayer = GetIsSinglePlayerMode();
  }

  // abort the countdown if there was one in progress, but only if the user who left is actually a controller, or otherwise relevant.
  if (m_CountDownStarted && !m_CountDownFast && !m_GameLoading && !m_GameLoaded) {
    if (!user->GetIsObserver() || GetNumSlotsOccupied() < m_HCLCommandString.size()) {
      // Intentionally reveal the name of the lobby leaver (may be trolling.)
      SendAllChat(Concat("Countdown stopped because [", user->GetName(), "] left!"));
      m_CountDownStarted = false;
    } else {
      // Observers that leave during countdown are replaced by fake observers.
      // This ensures the integrity of many things related to game slots.
      // e.g. this allows m_ControllersWithMap to remain unchanged.
      const uint8_t replaceSID = GetEmptyObserverSID();
      const uint8_t replaceUID = GetNewUID();
      CreateFakeUserInner(replaceSID, replaceUID, Concat("User[", ToDecString(ToBaseOne(replaceSID)), "]"), false);
      m_FakeUsers.back().SetIsObserver(true);
      CGameSlot* slot = GetSlot(replaceSID);
      slot->SetTeam(GetObserverTeam());
      slot->SetColor(GetObserverColor());
      LOG_APP_IF(LogLevel::kInfo, Concat("replaced leaving observer by fake user (SID=", ToDecString(replaceSID), "|UID=", ToDecString(replaceUID), ")"));
    }
  }

  // abort the votekick

  if (!m_KickVotePlayer.empty()) {
    SendAllChat(Concat("A votekick against user [", m_KickVotePlayer, "] has been cancelled"));
    m_KickVotePlayer.clear();
    m_StartedKickVoteTime = 0;
  }

  // record everything we need to know about the user for storing in the database later
  // since we haven't stored the game yet (it's not over yet!) we can't link the gameuser to the game
  // see the destructor for where these CDBGamePlayers are stored in the database
  // we could have inserted an incomplete record on creation and updated it later but this makes for a cleaner interface

  if (m_GameLoading || m_GameLoaded) {
    // When a user leaves from an already loaded game, their slot remains unchanged
    const CGameSlot* slot = InspectSlot(GetSIDFromUID(user->GetUID()));
    CGameController* controllerData = GetGameControllerFromColor(slot->GetColor());
    if (controllerData) {
      controllerData->SetServerLeftCode(static_cast<uint8_t>(user->GetLeftCode()));
      // FIXME: Max game time should be ensured elsewhere.
      uint64_t leftGameTime = signed_cast<uint64_t>(m_EffectiveTicks / 1000);
      assert(leftGameTime <= integer_cast<uint64_t>(numeric_limits<uint32_t>::max()) && "Game time limited to 1193 hours");
      controllerData->SetLeftGameTime(integer_cast_lossy<uint32_t>(leftGameTime));
    }

    // keep track of the last user to leave for the !banlast command
    // ignore the last user leaving, as well as the second-to-last (forfeit)
    if (m_Users.size() > 2 && !m_ExitingSoon) {
      for (auto& bannable : m_Bannables) {
        if (bannable->GetName() == user->GetName()) {
          m_LastLeaverBannable = bannable;
        }
      }
    }
  }

  if ((m_GameLoading || m_GameLoaded || m_ExitingSoon) && !user->GetIsObserver()) {
    // end the game if there aren't any players left
    // but only if the user who left isn't an observer
    // this allows parties of 2+ observers to watch AI vs AI
    const uint8_t numJoinedPlayers = GetNumJoinedPlayers();
    if (numJoinedPlayers == 0) {
      LOG_APP_IF(LogLevel::kInfo, "gameover timer started: no players left");
      StartGameOverTimer();
    } else if (!GetIsGameOverTrusted()) {
      if (numJoinedPlayers == 1 && GetNumComputers() == 0) {
        LOG_APP_IF(LogLevel::kInfo, Concat("gameover timer started: remaining 1 p | 0 comp | ", ToDecString(GetNumJoinedObservers()), " obs"));
        StartGameOverTimer();
      }
    }
  }

  // Flush queued data before the socket is destroyed.
  if (!user->GetDisconnected()) {
    user->GetSocket()->DoSend(send_fd);
  }
}

void CGame::EventLobbyLastPlayerLeaves()
{
  if (m_CustomLayout != CUSTOM_LAYOUT_FFA) {
    ResetLayout(false);
  }
}

void CGame::ReportAllPings()
{
  UserList SortedPlayers = m_Users;
  if (SortedPlayers.empty()) return;

  if (m_IsLagging) {
    sort(begin(SortedPlayers), end(SortedPlayers), &GameUser::SortUsersByKeepAlivesAscending);
  } else {
    sort(begin(SortedPlayers), end(SortedPlayers), &GameUser::SortUsersByLatencyDescending);
  }

  vector<string> pingsText;
  for (auto i = begin(SortedPlayers); i != end(SortedPlayers); ++i) {
    pingsText.push_back(Concat((*i)->GetDisplayName(), ": ", (*i)->GetDelayText(false)));
  }
  
  SendAllChat(JoinStrings(pingsText));

  if (m_IsLagging) {
    GameUser::CGameUser* worstLagger = SortedPlayers[0];
    if (worstLagger->GetDisconnected() && worstLagger->GetCanReconnect()) {
      ImmutableUserList waitingReconnectPlayers = GetWaitingReconnectPlayers();
      uint8_t laggerCount = CountLaggingPlayers() - static_cast<uint8_t>(waitingReconnectPlayers.size());
      string laggerText;
      if (laggerCount > 0) {
        laggerText = Concat(" (+", ToDecString(laggerCount), " other laggers)");
      }
      SendAllChat(Concat(ToNameListSentence(waitingReconnectPlayers), " disconnected, but may reconnect", laggerText));
    } else {
      string syncDelayText = worstLagger->GetSyncText();
      if (!syncDelayText.empty()) {
        uint8_t laggerCount = CountLaggingPlayers();
        if (laggerCount > 1) {
          SendAllChat(Concat(ToDecString(laggerCount), " laggers - [", worstLagger->GetDisplayName(), "] is ", syncDelayText));
        } else {
          SendAllChat(Concat("[", worstLagger->GetDisplayName(), "] is ", syncDelayText));
        }
      }
    }
    if (GetCanDropOwnerMissing()) {
      SendAllChat(Concat(GetCmdToken(), "drop command is now freely available"));
    }
  }
}

bool CGame::GetCanDropOwnerMissing() const
{
  GameUser::CGameUser* gameOwner = GetOwner();
  if (gameOwner && !gameOwner->GetIsLagging()) {
    return false;
  }
  if (GetLockedOwnerLess()) {
    return false;
  }
  return m_Aura->GetTimeIsAfterDelay(m_StartedLaggingTime, 20);
}

void CGame::ResetDropVotes()
{
  for (auto& eachPlayer : m_Users) {
    eachPlayer->SetDropVote(false);
  }
}

void CGame::ResetOwnerSeen()
{
  m_LastOwnerSeenTicks = m_Aura->GetClockTicks();
}

void CGame::SetLaggingPlayerAndUpdate(GameUser::CGameUser* user)
{
  const int64_t loopTime = m_Aura->GetClockTime();
  const int64_t loopTicks = m_Aura->GetClockTicks();
  if (!user->GetIsLagging()) {
    ResetDropVotes();

    if (!GetIsLagging()) {
      m_IsLagging = true;
      m_StartedLaggingTime = loopTime;
      m_LastLagScreenResetTime = loopTime;
      m_LastLagScreenTime = loopTime;
    }

    // Report lagging users:
    // - Just disconnected user
    // - Players outside safe sync limit
    // Since the disconnected user has already been flagged with SetDisconnectNoticeSent, they get
    // excluded from the output vector of CalculateNewLaggingPlayers(),
    // So we have to add them afterwards.
    UserList laggingPlayers = CalculateNewLaggingPlayers();
    laggingPlayers.push_back(user);
    for (auto& laggingPlayer : laggingPlayers) {
      laggingPlayer->SetLagging(true);
      laggingPlayer->SetStartedLaggingTicks(loopTicks);
      laggingPlayer->ClearStalePings();
    }
    DLOG_APP_IF(LogLevel::kTrace, Concat("global lagger update (+", ToNameListSentence(laggingPlayers), ")"));
    SendAll(GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, loopTicks));
  }
}

void CGame::SetEveryoneLagging()
{
  if (GetIsLagging()) {
    return;
  }
  const int64_t loopTime = m_Aura->GetClockTime();
  const int64_t loopTicks = m_Aura->GetClockTicks();

  ResetDropVotes();

  m_IsLagging = true;
  m_StartedLaggingTime = loopTime;
  m_LastLagScreenResetTime = loopTime;
  m_LastLagScreenTime = loopTime;

  for (auto& user : m_Users) {
    user->SetLagging(true);
    user->SetStartedLaggingTicks(loopTicks);
    user->ClearStalePings();
  }
}

pair<int64_t, int64_t> CGame::GetReconnectWaitTicks() const
{
  return make_pair(
    static_cast<int64_t>(m_GProxyEmptyActions + 1) * 60000,
    m_Aura->m_Net.m_Config.m_ReconnectWaitTicks
  );
}

void CGame::ReportRecoverableDisconnect(GameUser::CGameUser* user)
{
  if (user->m_LastDisconnectRepeatNoticeTicks.has_value() && !m_Aura->GetTicksIsAfterDelay(user->m_LastDisconnectRepeatNoticeTicks.value(), 20000)) {
    return;
  }

  int64_t timeRemaining = 0;
  pair<int64_t, int64_t> ticksRemaining = GetReconnectWaitTicks();
  if (user->GetGProxy()->GetIsExtended()) {
    timeRemaining = m_Aura->GetClockTicks() - user->GetStartedLaggingTicks() - ticksRemaining.second;
  } else {
    timeRemaining = m_Aura->GetClockTicks() - user->GetStartedLaggingTicks() - ticksRemaining.first;
  }

  timeRemaining /= 1000;
  if (timeRemaining <= 0) {
    return;
  }

  SendAllChat(user->GetUID(), Concat("Please wait for me to reconnect (time limit: ", to_string(timeRemaining), " seconds)"));
  user->m_LastDisconnectRepeatNoticeTicks = m_Aura->GetClockTicks();
}

void CGame::OnRecoverableDisconnect(GameUser::CGameUser* user)
{
  user->GetCommandHistory()->SudoModeEnd(m_Aura, shared_from_this(), user->GetName());

  if (!user->GetIsLagging()) {
    SetLaggingPlayerAndUpdate(user);
  }

  ReportRecoverableDisconnect(user);
}

void CGame::EventUserAfterDisconnect(GameUser::CGameUser* user, bool fromOpen)
{
  if (!m_GameLoading && !m_GameLoaded && !m_CountDownFast) {
    if (!fromOpen) {
      const uint8_t SID = GetSIDFromUID(user->GetUID());
      OpenSlot(SID, true); // kick = true
    }
    user->SetDeleteMe(true);
  } else {
    // Let's avoid sending leave messages during game load.
    // Also, once the game is loaded, ensure all the users' actions will be sent before the leave message is sent.
    Resume(user, user->GetPingEqualizerFrame(), true);
    QueueLeftMessage(user);
  }

  if (m_GameLoading && !user->GetFinishedLoading() && !m_Config.m_LoadInGame) {
    const vector<uint8_t> packet = GameProtocol::SEND_W3GS_GAMELOADED_OTHERS(user->GetUID());
    m_GameHistory->m_LoadingVirtualBuffer.reserve(m_GameHistory->m_LoadingVirtualBuffer.size() + packet.size());
    AppendContainer(m_GameHistory->m_LoadingVirtualBuffer, packet);
    SendAll(packet);
  }
}

void CGame::EventUserDisconnectTimedOut(GameUser::CGameUser* user)
{
  if (user->GetDisconnected()) return;
  if (user->GetCanReconnect() && m_GameLoaded) {
    if (!user->GetDisconnectNoticeSent()) {
      user->UnrefConnection();
      user->SetDisconnectNoticeSent(true);
      if (user->GetGProxy()->GetIsExtended()) {
        SendAllChat(Concat(user->GetDisplayName(), " has disconnected, but is using GProxyDLL and may reconnect"));
      } else {
        SendAllChat(Concat(user->GetDisplayName(), " has disconnected, but is using GProxy++ and may reconnect"));
      }
    }
    OnRecoverableDisconnect(user);
    return;
  }

  // not only do we not do any timeouts if the game is lagging, we allow for an additional grace period of 10 seconds
  // this is because Warcraft 3 stops sending packets during the lag screen
  // so when the lag screen finishes we would immediately disconnect everyone if we didn't give them some extra time

  if (m_Aura->GetTimeIsAfterDelay(m_LastLagScreenTime, 10)) {
    if (!user->HasLeftReason()) {
      user->SetLeftReason("has lost the connection (timed out)");
      user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
    }
    user->CloseConnection(); // automatically sets ended (reconnect already not enabled)
    TryActionsOnDisconnect(user, false);
  }
}

void CGame::EventUserDisconnectSocketError(GameUser::CGameUser* user)
{
  if (user->GetDisconnected()) return;
  if (user->GetCanReconnect() && m_GameLoaded) {
    if (!user->GetDisconnectNoticeSent()) {
      string errorString = user->GetConnectionErrorString();
      user->UnrefConnection();
      user->SetDisconnectNoticeSent(true);
      SendAllChat(Concat(user->GetDisplayName(), " has disconnected (connection error - ", errorString, ") but is using GProxy++ and may reconnect"));
    }

    OnRecoverableDisconnect(user);
    return;
  }

  if (!user->HasLeftReason()) {
    user->SetLeftReason(Concat("has lost the connection (connection error - ", user->GetSocket()->GetErrorString(), ")"));
    user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
  }
  if (user->GetIsLagging()) {
    StopLagger(user, user->GetLeftReason());
  } else {
    user->CloseConnection(); // automatically sets ended (reconnect already not enabled)
  }
  TryActionsOnDisconnect(user, false);
}

void CGame::EventUserDisconnectConnectionClosed(GameUser::CGameUser* user)
{
  if (user->GetDisconnected()) return;
  if (user->GetCanReconnect() && m_GameLoaded) {
    if (!user->GetDisconnectNoticeSent()) {
      user->UnrefConnection();
      user->SetDisconnectNoticeSent(true);
      SendAllChat(Concat(user->GetDisplayName(), " has terminated the connection, but is using GProxy++ and may reconnect"));
    }

    OnRecoverableDisconnect(user);
    return;
  }

  if (!user->HasLeftReason()) {
    user->SetLeftReason("has terminated the connection");
    user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
  }
  if (user->GetIsLagging()) {
    StopLagger(user, user->GetLeftReason());
  } else {
    user->CloseConnection(); // automatically sets ended (reconnect already not enabled)
  }
  TryActionsOnDisconnect(user, false);
}

void CGame::EventUserDisconnectGameProtocolError(GameUser::CGameUser* user, bool canRecover)
{
  if (user->GetDisconnected()) return;
  if (canRecover && user->GetCanReconnect() && m_GameLoaded) {
    if (!user->GetDisconnectNoticeSent()) {
      user->UnrefConnection();
      user->SetDisconnectNoticeSent(true);
      SendAllChat(Concat(user->GetDisplayName(), " has disconnected (protocol error) but is using GProxy++ and may reconnect"));
    }

    OnRecoverableDisconnect(user);
    return;
  }

  if (!user->HasLeftReason()) {
    if (canRecover) {
      user->SetLeftReason("has lost the connection (protocol error)");
    } else {
      user->SetLeftReason("has lost the connection (unrecoverable protocol error)");
    }
    user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
  }
  if (user->GetIsLagging()) {
    StopLagger(user, user->GetLeftReason());
  } else {
    user->DisableReconnect();
    user->CloseConnection(); // automatically sets ended
  }
  TryActionsOnDisconnect(user, false);
}

void CGame::EventUserDisconnectGameAbuse(GameUser::CGameUser* user)
{
  if (user->GetDisconnected()) return;
  if (!user->HasLeftReason()) {
    user->SetLeftReason("was kicked by anti-abuse");
    user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
  }
  user->DisableReconnect();
  user->CloseConnection(); // automatically sets ended
  user->AddKickReason(GameUser::KickReason::kAbuser);
}

void CGame::EventUserKickGProxyExtendedTimeout(GameUser::CGameUser* user)
{
  if (user->GetDeleteMe()) return;
  StopLagger(user, "failed to reconnect in time");
  TryActionsOnDisconnect(user, false);
  ResetDropVotes();
}

void CGame::EventUserKickUnverified(GameUser::CGameUser* user)
{
  if (user->GetDisconnected()) return;
  if (!user->HasLeftReason()) {
    user->SetLeftReason("has been kicked because they are not verified by their realm");
  }
  user->CloseConnection();
  user->AddKickReason(GameUser::KickReason::kSpoofer);
}

void CGame::EventUserKickHandleQueued(GameUser::CGameUser* user)
{
  if (user->GetDisconnected())
    return;

  if (m_CountDownStarted) {
    user->ClearKickByTicks();
    return;
  }

  user->DisableReconnect();
  user->CloseConnection();
  // left reason, left code already assigned when queued
}

void CGame::SendChatMessage(const GameUser::CGameUser* user, const CIncomingMessageOrSettingsView& chatMessage)
{
  if (m_GameLoading && !m_Config.m_LoadInGame) {
    return;
  }

  if (!m_GameLoading && !m_GameLoaded) {
    SendLobbyChat(chatMessage.GetToUIDs(), chatMessage.GetFromUID(), chatMessage.GetText());
    return;
  }

  uint8_t inGameChannel = chatMessage.GetInGameChannel();

  // Never allow referees to send private messages to users.
  // Referee rulings/warnings are expected to be public.
  if (user->GetIsObserver() && m_Map->GetGameObservers() == GameObserversMode::kReferees && inGameChannel != CHAT_RECV_OBS) {
    vector<uint8_t> targetUIDs;
    if (m_UsesCustomReferees && !user->GetIsPowerObserver()) {
      // When custom referees are enabled, only designated referees can use [All] chat.
      // Others use [Ref] exclusively.
      targetUIDs = GetFilteredChatObserverUIDs(chatMessage.GetFromUID(), chatMessage.GetToUIDs());
      inGameChannel = CHAT_RECV_OBS;
    } else {
      targetUIDs = GetFilteredChatUIDs(chatMessage.GetFromUID(), chatMessage.GetToUIDs());
      inGameChannel = CHAT_RECV_ALL;
    }
    SendInGameChat(targetUIDs, chatMessage.GetFromUID(), inGameChannel, chatMessage.GetText());
    return;
  }

  // When observers on defeat (or full observers) are enabled, Aura cannot reliably figure out whether a player became an observer
  // therefore, we can only rely on game clients to properly manage chat visibility
  SendInGameChat(chatMessage.GetToUIDs(), chatMessage.GetFromUID(), inGameChannel, chatMessage.GetText());
}

void CGame::QueueLeftMessage(GameUser::CGameUser* user) const
{
  CQueuedActionsFrame& frame = user->GetPingEqualizerFrame();
  frame.leavers.push_back(user);
  user->TrySetEnding();
  DLOG_APP_IF(LogLevel::kTrace, Concat("[", user->GetName(), "] scheduled for deletion in ", ToDecString(user->GetPingEqualizerOffset()), " frames"));
}

void CGame::SendLeftMessage(GameUser::CGameUser* user, const bool sendChat)
{
  // This function, together with GetLeftMessage and SetLeftMessageSent,
  // controls which UIDs Aura considers available.
  if (sendChat) {
    if (!user->GetIsLeaver()) {
      SendAllChat(Concat(user->GetExtendedName(), " ", user->GetLeftReason(), "."));
    } else if (user->GetRealm(false)) {
      // Note: Not necessarily spoof-checked
      SendAllChat(user->GetUID(), Concat(user->GetLeftReason(), " [", user->GetExtendedName(), "]."));
    } else {
      SendAllChat(user->GetUID(), user->GetLeftReason());
    }
  }
  LogRemote(Concat("[", user->GetExtendedName(), "] ", user->GetLeftReason()));

  OnPlayerLeaveHandler leaverHandler = m_Config.m_LeaverHandler;
  if (!m_GameLoaded) {
    leaverHandler = OnPlayerLeaveHandler::kNative;
  }

  vector<uint8_t> packet = GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(user->GetUID(), GetIsLobbyStrict() ? PLAYERLEAVE_LOBBY : user->GetLeftCode());
  switch (leaverHandler) {
    case OnPlayerLeaveHandler::kNone:
    case OnPlayerLeaveHandler::kShareUnits: {
      // Ensure the disconnected user's game client / GProxy fully exits.
      Send(user, packet);
      // TODO: ON_PLAYER_LEAVE_SHARE_UNITS - Turn into a CGameVirtualUser?
      break;
    }

    case OnPlayerLeaveHandler::kNative: {
      SendAll(packet);
      if (m_GameLoaded && (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING)) {
        m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_LEAVER, packet);
      }
      break;
    }

    IGNORE_ENUM_LAST(OnPlayerLeaveHandler)
  }

  user->SetLeftMessageSent(true);
  user->SetStatus(USERSTATUS_ENDED);

  if (user->GetAntiShareKicked()) {
    for (auto& otherUser : m_Users) {
      if (!otherUser->GetHasControlOverUnitsFromSlot(user->GetSID())) {
        continue;
      }
      otherUser->SetHasControlOverUnitsFromSlot(user->GetSID(), false);
      otherUser->CheckReleaseOnHoldActions();
    }
  }
}

bool CGame::SendEveryoneElseLeftAndDisconnect(const string& reason) const
{
  bool anyStopped = false;
  for (auto& p1 : m_Users) {
    for (auto& p2 : m_Users) {
      if (p1 == p2 || p2->GetLeftMessageSent()) {
        continue;
      }
      Send(p1, GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(p2->GetUID(), PLAYERLEAVE_DISCONNECT));
    }
    for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
      Send(p1, fakeUser.GetGameQuitBytes(PLAYERLEAVE_DISCONNECT));
    }
    p1->DisableReconnect();
    p1->SetLagging(false);
    if (!p1->HasLeftReason()) {
      p1->SetLeftReason(reason);
      p1->SetLeftCode(PLAYERLEAVE_DISCONNECT);
    }
    p1->SetLeftMessageSent(true);
    if (p1->GetCanReconnect()) {
      // Let GProxy know that it should give up at reconnecting.
      Send(p1, GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(p1->GetUID(), PLAYERLEAVE_DISCONNECT));
    }
    if (m_GameLoaded && (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING)) {
      vector<uint8_t> packet = GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(p1->GetUID(), PLAYERLEAVE_DISCONNECT);
      m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_LEAVER, packet);
    }
    p1->CloseConnection();
    p1->SetStatus(USERSTATUS_ENDED);
    if (!p1->GetDisconnected()) {
      anyStopped = true;
    }
  }
  return anyStopped;
}

bool CGame::TrySendFakeUsersShareControl()
{
  bool anyShared = false;
  for (auto& fakeUser : m_FakeUsers) {
    if (!fakeUser.GetCanShare()) continue;
    uint8_t toSID = GetNumSlots();
    while (toSID--) {
      if (!fakeUser.GetCanShare(toSID)) continue;
      if (ShareUnits(fakeUser.GetUID(), toSID, GetLastActionFrame())) {
        anyShared = true;
      }
    }
  }
  return anyShared;
}

bool CGame::GetIsHiddenPlayerNames() const
{
  return m_IsHiddenPlayerNames;
}

void CGame::ShowPlayerNamesGameStartLoading() {
  if (!m_IsHiddenPlayerNames) return;

  m_IsHiddenPlayerNames = false;

  for (auto& p1 : m_Users) {
    for (auto& p2 : m_Users) {
      if (p1 == p2 || p2->GetLeftMessageSent()) {
        continue;
      }
      Send(p1, GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(p2->GetUID(), PLAYERLEAVE_LOBBY));
      Send(p1, GameProtocol::SEND_W3GS_PLAYERINFO_EXCLUDE_IP(GetVersion(), p2->GetUID(), p2->GetDisplayName()/*, user->GetIPv4(), user->GetIPv4Internal()*/));
    }
  }
}

void CGame::ShowPlayerNamesInGame() {
  m_IsHiddenPlayerNames = false;
}

void CGame::EventUserCheckStatus(GameUser::CGameUser* user)
{
  if (user->GetDisconnected())
    return;

  if (m_CountDownStarted) {
    user->SetStatusMessageSent(true);
    return;
  }

  bool hideNames = m_IsHiddenPlayerNames || m_Config.m_HideInGameNames == HideIGNMode::kAlways || m_Config.m_HideInGameNames == HideIGNMode::kHost;
  if (m_Config.m_HideInGameNames == HideIGNMode::kAuto && m_Map->GetMapNumControllers() >= 3) {
    hideNames = true;
  }

  bool IsOwnerName = MatchOwnerName(user->GetName());
  string OwnerFragment;
  if (user->GetIsOwner(nullopt)) {
    OwnerFragment = " (game owner)";
  } else if (IsOwnerName) {
    OwnerFragment = " (unverified game owner, send me a whisper: \"sc\")";
  }

  string GProxyFragment;
  if (m_Aura->m_Net.m_Config.m_AnnounceGProxy && GetIsProxyReconnectable() && !hideNames) {
    if (user->GetGProxy()->GetIsExtended()) {
      GProxyFragment = Concat(" is using GProxyDLL, a Warcraft III plugin to protect against disconnections. See: <", m_Aura->m_Net.m_Config.m_AnnounceGProxySite, ">");
    } else if (user->GetCanReconnect()) {
      if (GetIsProxyReconnectableLong()) {
        GProxyFragment = Concat(" is using an outdated GProxy++. Please upgrade to GProxyDLL at: <", m_Aura->m_Net.m_Config.m_AnnounceGProxySite, ">");
      } else {
        GProxyFragment = Concat(" is using GProxy, a Warcraft III plugin to protect against disconnections. See: <", m_Aura->m_Net.m_Config.m_AnnounceGProxySite, ">");
      }
    }
  }
  
  user->SetStatusMessageSent(true);
  if (OwnerFragment.empty() && GProxyFragment.empty()) {
    if (m_Aura->m_Net.m_Config.m_AnnounceIPv6 && user->GetUsingIPv6() && !hideNames) {
      SendAllChat(Concat(user->GetDisplayName(), " joined the game over IPv6."));
    }
    return;
  }

  if (hideNames) {
    if (m_IsHiddenPlayerNames) {
      SendChat(user, Concat("[", user->GetName(), "]", OwnerFragment, " joined the game as [", user->GetDisplayName(), "]"));
    } else {
      SendChat(user, Concat("[", user->GetName(), "]", OwnerFragment, " joined the game."));
    }
    return;
  }

  string IPv6Fragment;
  if (user->GetUsingIPv6() && !hideNames) {
    IPv6Fragment = ". (Joined over IPv6).";
  }
  if (!OwnerFragment.empty() && !GProxyFragment.empty()) {
    SendAllChat(user->GetDisplayName() + OwnerFragment + GProxyFragment + IPv6Fragment);
  } else if (!OwnerFragment.empty()) {
    if (user->GetUsingIPv6()) {
      SendAllChat(Concat(user->GetDisplayName(), OwnerFragment, " joined the game over IPv6."));
    } else {
      SendAllChat(Concat(user->GetDisplayName(), OwnerFragment, " joined the game."));
    }
  } else {
    SendAllChat(Concat(user->GetDisplayName(), GProxyFragment, IPv6Fragment));
  }
}

GameUser::CGameUser* CGame::JoinPlayer(CConnection* connection, const CIncomingJoinRequest& joinRequest, const uint8_t SID, const uint8_t UID, const uint8_t HostCounterID, const string JoinedRealm, const bool IsReserved, const bool IsUnverifiedAdmin)
{
  // If realms are reloaded, HostCounter may change.
  // However, internal realm IDs maps to constant realm input IDs.
  // Hence, CGamePlayers are created with references to internal realm IDs.
  uint32_t internalRealmId = HostCounterID;
  shared_ptr<CRealm> matchingRealm = nullptr;
  if (HostCounterID >= 0x10) {
    matchingRealm = m_Aura->GetRealmByHostCounter(HostCounterID);
    if (matchingRealm) internalRealmId = matchingRealm->GetInternalID();
  }

  optional<Version> gameVersion = GetIncomingPlayerVersion(connection, joinRequest, matchingRealm);

  GameUser::CGameUser* Player = new GameUser::CGameUser(
    shared_from_this(),
    connection, 
    UID == 0xFF ? GetNewUID() : UID,
    gameVersion.has_value(),
    gameVersion.value_or(GuessIncomingPlayerVersion(connection, joinRequest, matchingRealm)),
    internalRealmId,
    JoinedRealm,
    joinRequest.GetName(),
    joinRequest.GetIPv4Internal(),
    joinRequest.GetIsCensored(),
    IsReserved
  );

  // Now, socket belongs to GameUser::CGameUser. Don't look for it in CConnection.

  //m_Users.push_back(Player);
  connection->SetSocket(nullptr);
  connection->SetDeleteMe(true);

  if (matchingRealm) {
    // Realm admins/moderators skip spoofcheck when the realm opts in via
    // <realm>.unverified_users.trust_admins. Anyone joining under an admin's
    // name from that realm is granted admin powers without verification.
    if (IsUnverifiedAdmin && matchingRealm->GetTrustsAdmins()) {
      Player->SetRealmVerified(true);
    }
    Player->SetWhoisShouldBeSent(
      IsUnverifiedAdmin || MatchOwnerName(Player->GetName()) || !HasOwnerSet() ||
      matchingRealm->GetIsFloodImmune() || matchingRealm->GetHasEnhancedAntiSpoof()
    );
  }

  if (GetIsCustomForces()) {
    m_SlotsConfig.slots[SID] = CGameSlot(m_SlotsConfig.Inspect(SID).GetType(), Player->GetUID(), SLOTPROG_RST, SLOTSTATUS_OCCUPIED, 0, m_SlotsConfig.Inspect(SID).GetTeam(), m_SlotsConfig.Inspect(SID).GetColor(), m_Map->GetLobbyRace(&m_SlotsConfig.Inspect(SID)));
  } else {
    m_SlotsConfig.slots[SID] = CGameSlot(m_SlotsConfig.Inspect(SID).GetType(), Player->GetUID(), SLOTPROG_RST, SLOTSTATUS_OCCUPIED, 0, GetObserverTeam(), GetObserverColor(), m_Map->GetLobbyRace(&m_SlotsConfig.Inspect(SID)));
    SetSlotTeamAndColorAuto(SID);
  }
  Player->SetIsObserver(m_SlotsConfig.GetIsObserver(SID));

  // send slot info to the new user
  // the SLOTINFOJOIN packet also tells the client their assigned UID and that the join was successful.

  Player->Send(GameProtocol::SEND_W3GS_SLOTINFOJOIN(Player->GetUID(), Player->GetSocket()->GetPortLE(), Player->GetIPv4(), m_SlotsConfig, m_RandomSeed, m_Map->GetMapNumControllers(), Player->GetGameVersion()));

  SendIncomingPlayerInfo(Player); // sends info to other players

  // send virtual host info and fake users info (if present) to the new user.

  SendVirtualHostPlayerInfo(Player);
  SendFakeUsersInfo(Player);
  SendJoinedPlayersInfo(Player); // only sends info regarding other players

  // send a map check packet to the new user.
  SendMapAndVersionCheck(Player, Player->GetGameVersion());

  m_Users.push_back(Player);

  // send slot info to everyone, so the new user gets this info twice but everyone else still needs to know the new slot layout.
  SendAllSlotInfo();
  UpdateReadyCounters();

  if (GetIPFloodHandler() == OnIPFloodHandler::kNotify) {
    CheckIPFlood(joinRequest.GetName(), &(Player->GetSocket()->m_RemoteHost));
  }

  // send a welcome message

  if (!m_RestoredGame) {
    SendWelcomeMessage(Player);
  }

  for (const auto& otherPlayer :  m_Users) {
    if (otherPlayer == Player || otherPlayer->GetLeftMessageSent()) {
      continue;
    }
    if (otherPlayer->GetHasPinnedMessage()) {
      SendChat(otherPlayer->GetUID(), Player, otherPlayer->GetPinnedMessage(), LogLevelExtra::kDebug);
    }
  }

  AddProvisionalBannableUser(Player);

  string notifyString = "";
  if (m_Config.m_NotifyJoins && m_Config.m_IgnoredNotifyJoinPlayers.find(joinRequest.GetLowerName()) == m_Config.m_IgnoredNotifyJoinPlayers.end()) {
    notifyString = "\x07";
  }

  size_t observerCount = GetObservers().size();
  if (observerCount > 0) {
    LogRemote(Concat("[", Player->GetExtendedName(), "] joined (", Player->GetGameVersionString(), " - ", ToDecString(GetNumControllers()), " / ", to_string(m_Map->GetMapNumControllers()), "), ", to_string(observerCount), " obs"));
  } else {
    LogRemote(Concat("[", Player->GetExtendedName(), "] joined (", Player->GetGameVersionString(), " - ", ToDecString(GetNumControllers()), " / ", to_string(m_Map->GetMapNumControllers()), ")"));
  }

  if (notifyString.empty()) {
    LOG_APP_IF(LogLevel::kInfo, Concat("user joined (P", ToDecString(ToBaseOne(SID)), "): [", joinRequest.GetName(), "@", Player->GetRealmHostName(), "#", ToDecString(Player->GetUID()), "] ", Player->GetGameVersionString(), " from [", Player->GetIPString(), "] (", Player->GetSocket()->GetName(), ")", notifyString));
  } else {
    LOG_APP_IF(LogLevel::kNotice, Concat("user joined (P", ToDecString(ToBaseOne(SID)), "): [", joinRequest.GetName(), "@", Player->GetRealmHostName(), "#", ToDecString(Player->GetUID()), "] ", Player->GetGameVersionString(), " from [", Player->GetIPString(), "] (", Player->GetSocket()->GetName(), ")", notifyString));
  }
  if (joinRequest.GetIsCensored()) {
    LOG_APP_IF(LogLevel::kNotice, Concat("user ", EnsureWrapUTF8(joinRequest.GetName()), " has censored name - was ", EnsureWrapUTF8(joinRequest.GetOriginalName())));
  }
  if (!GetAreSameSlotProtocolGameVersions(Player->GetGameVersion(), GetVersion())) {
    LOG_APP_IF(LogLevel::kDebug, Concat("user ", EnsureWrapUTF8(joinRequest.GetName()), " joined v", ToVersionString(GetVersion()), " lobby using compatibility mode"));
  }

  return Player;
}

void CGame::JoinObserver(CConnection* connection, const CIncomingJoinRequest& joinRequest, shared_ptr<CRealm> fromRealm)
{
  // This leaves no chance for GProxy handshake

  if (!m_JoinInProgressVirtualUser.has_value()) return;
  const optional<Version> gameVersion = GetIncomingPlayerVersion(connection, joinRequest, fromRealm);

  CAsyncObserver* observer = new CAsyncObserver(
    shared_from_this(),
    connection,
    m_JoinInProgressVirtualUser->GetUID(),
    gameVersion.has_value(),
    gameVersion.value_or(GuessIncomingPlayerVersion(connection, joinRequest, fromRealm)),
    fromRealm,
    joinRequest.GetName()
  );
  m_Aura->m_Net.m_GameObservers[connection->GetPort()].push_back(observer);
  connection->SetSocket(nullptr);
  connection->SetDeleteMe(true);

  Send(observer, GameProtocol::SEND_W3GS_SLOTINFOJOIN(observer->GetUID(), observer->GetSocket()->GetPortLE(), observer->GetIPv4(), m_SlotsConfig, m_RandomSeed, m_Map->GetMapNumControllers(), observer->GetGameVersion()));
  observer->SendOtherPlayersInfo();
  SendMapAndVersionCheck(observer, observer->GetGameVersion());
  Send(observer, GameProtocol::SEND_W3GS_SLOTINFO(m_SlotsConfig, m_RandomSeed, m_Map->GetMapNumControllers(), observer->GetGameVersion()));

  observer->SendChat("This game is in progress. You can join as an spectator.");

  string realmHostName;
  if (fromRealm) realmHostName = fromRealm->GetServer();
  LOG_APP_IF(LogLevel::kInfo, Concat("spectator joined [", joinRequest.GetName(), "@", realmHostName, "#", to_string(observer->GetUID()), "] ", observer->GetGameVersionString() , " from [", observer->GetIPString(), "]"));
  if (!GetAreSameSlotProtocolGameVersions(observer->GetGameVersion(), GetVersion())) {
    LOG_APP_IF(LogLevel::kDebug, Concat("spectator ", EnsureWrapUTF8(joinRequest.GetName()), " joined v", ToVersionString(GetVersion()), " game using compatibility mode"));
  }
}

void CGame::EventObserverMapSize(CAsyncObserver* user, const CIncomingMapFileSize& clientMap)
{
  const bool isFirstCheck = !user->GetMapChecked();

  user->SetMapChecked(true);
  const uint32_t expectedMapSize = m_Map->GetMapSizeClamped(user->GetGameVersion());
  UpdateUserMapProgression(user, clientMap.GetFileSize(), expectedMapSize);

  if (clientMap.GetFlag() != 1 || clientMap.GetFileSize() != expectedMapSize) {
    // observer doesn't have the map
    const MapTransferCheckResult checkResult = CheckCanTransferMap(user, user->GetRealm(), user->GetGameVersion(), false /* cannot start manual download for observers */);
    if (checkResult == MapTransferCheckResult::kAllowed) {
      MapTransfer& mapTransfer = user->GetMapTransfer();
      if (!mapTransfer.GetStarted() && clientMap.GetFlag() == 1) {
        // inform the client that we are willing to send the map

        LOG_APP_IF(LogLevel::kDebug, Concat("map download started for observer [", user->GetName(), "]"));
        Send(user, GameProtocol::SEND_W3GS_STARTDOWNLOAD(GetHostUID()));
        mapTransfer.Start();
      } else {
        mapTransfer.SetLastAck(clientMap.GetFileSize());
      }
    } else if (isFirstCheck) {
      user->SetTimeoutAtLatest(m_Aura->GetClockTicks() + m_Config.m_LacksMapKickDelay);

      if (GetMapSiteURL().empty()) {
        user->SendChat(Concat("Spectator [", user->GetName(), "], please download the map before joining. (Kick in ", to_string(m_Config.m_LacksMapKickDelay / 1000), " seconds...)"));
      } else {
        user->SendChat(Concat("Spectator [", user->GetName(), "], please download the map from <", EnsureUTF8(GetMapSiteURL()), "> before joining. (Kick in ", to_string(m_Config.m_LacksMapKickDelay / 1000), " seconds...)"));
      }

      if (!user->HasLeftReason()) {
        string reason;
        switch (checkResult) {
          case MapTransferCheckResult::kInvalid:
            reason = "invalid";
            break;
          case MapTransferCheckResult::kMissing:
            reason = "missing";
            break;
          case MapTransferCheckResult::kDisabled:
            reason = "disabled";
            break;
          case MapTransferCheckResult::kTooLargeVersion:
            LOG_APP_IF(LogLevel::kDebug, Concat("user [", user->GetName(), "] running v", ToVersionString(user->GetGameVersion()), " cannot download ", ToFormattedString(m_Map->GetMapSizeMB()), " MB map in-game"));
            // falls through
          case MapTransferCheckResult::kTooLargeConfig:
            reason = "too large";
            break;
          case MapTransferCheckResult::kBufferBloat:
            reason = "bufferbloat";
            break;
          // kAllowed checked above
          IGNORE_CASE(MapTransferCheckResult::kAllowed)
        }
        user->SetLeftReason(Concat("autokicked - they don't have the map, and it cannot be transferred (", reason, ")"));
      }
    }
  } else if (user->GetMapTransfer().GetStarted()) {
    // calculate download rate
    const double seconds = static_cast<double>(m_Aura->GetClockTicks() - user->GetMapTransfer().GetStartedTicks()) / 1000.f;
    LOG_APP_IF(LogLevel::kDebug, Concat("map download finished for observer [", user->GetName(), "] in ", ToFormattedString(seconds), " seconds"));
    user->SendChat(Concat("You downloaded the map in ", ToFormattedString(seconds), " seconds"/* (", ToFormattedString(Rate), " KB/sec)"*/));
    user->GetMapTransfer().Finish();
    user->EventMapReady();
  } else {
    user->EventMapReady();
  }
}

bool CGame::CheckIPFlood(string_view joinName, const sockaddr_storage* sourceAddress)
{
  // check for multiple IP usage
  UserList usersSameIP;
  for (auto& otherPlayer : m_Users) {
    if (joinName == otherPlayer->GetName()) {
      continue;
    }
    // In a lobby, all users are always connected, but
    // this is still a safety measure in case we reuse this method for GProxy or whatever.
    if (GetSameAddresses(sourceAddress, &(otherPlayer->GetSocket()->m_RemoteHost))) {
      usersSameIP.push_back(otherPlayer);
    }
  }

  if (usersSameIP.empty()) {
    return true;
  }

  uint8_t maxPlayersFromSameIp = isLoopbackAddress(sourceAddress) ? m_Config.m_MaxPlayersLoopback : m_Config.m_MaxPlayersSameIP;
  if (static_cast<uint8_t>(usersSameIP.size()) >= maxPlayersFromSameIp) {
    if (GetIPFloodHandler() == OnIPFloodHandler::kNotify) {
      SendAllChat(Concat("Player [", joinName, "] has the same IP address as: ", ToNameListSentence(usersSameIP)));
    }
    return false;
  }
  return true;
}

JoinRequestResult CGame::EventRequestJoin(CConnection* connection, const CIncomingJoinRequest& joinRequest)
{
  if (!GetIsStageAcceptingJoins()) {
    DLOG_APP_IF(LogLevel::kTrace, Concat("user ", EnsureWrapUTF8(joinRequest.GetName()), " failed to join (not accepting joins)"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_STARTED));
    return JoinRequestResult::kFail;
  }
  if (
    joinRequest.GetName().empty() || joinRequest.GetName().size() > MAX_PLAYER_NAME_SIZE ||
    (joinRequest.GetIsCensored() && m_Config.m_UnsafeNameHandler == OnUnsafeNameHandler::kDeny)
  ) {
    DLOG_APP_IF(LogLevel::kTrace, Concat("user ", EnsureWrapUTF8(joinRequest.GetName()), " failed to join (unsafe username)"));
    connection->Send(GameProtocol::SENDWRAP_W3GS_GHOST_LOBBY_ERROR("Your username is not allowed."));
    return JoinRequestResult::kFailDelayed;
  }

  // identify their joined realm
  // this is only possible because when we send a game refresh via LAN or battle.net we encode an ID value in the 4 most significant bits of the host counter
  // the client sends the host counter when it joins so we can extract the ID value here
  // note: this is not a replacement for spoof checking since it doesn't verify the user's name and it can be spoofed anyway

  string JoinedRealm;
  uint8_t HostCounterID = integer_cast_lossy<uint8_t>(joinRequest.GetHostCounter() >> HOST_COUNTER_REALM_OFFSET);
  bool IsUnverifiedAdmin = false;

  shared_ptr<CRealm> matchingRealm = nullptr;
  if (HostCounterID >= 0x10) {
    matchingRealm = m_Aura->GetRealmByHostCounter(HostCounterID);
    if (matchingRealm) {
      JoinedRealm = matchingRealm->GetServer();
      IsUnverifiedAdmin = matchingRealm->GetIsModerator(string(joinRequest.GetName())) || matchingRealm->GetIsAdmin(joinRequest.GetName());
    } else {
      // Trying to join from an unknown realm.
      HostCounterID = 0xF;
    }
  }

  if (HostCounterID < 0x10 && joinRequest.GetEntryKey() != m_EntryKey) {
    // check if the user joining via LAN knows the entry key
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "@", JoinedRealm, "] used a wrong LAN key (", to_string(joinRequest.GetEntryKey()), ") - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_WRONGPASSWORD));
    return JoinRequestResult::kFail;
  }

  // Odd host counters are information requests
  if (HostCounterID & 0x1) {
    optional<Version> maybeGameInfoVersion = GetIncomingPlayerVersion(connection, joinRequest, matchingRealm);
    Version gameInfoVersion = maybeGameInfoVersion.value_or(GuessIncomingPlayerVersion(connection, joinRequest, matchingRealm));
    EventBeforeJoin(connection);
    connection->Send(GameProtocol::SEND_W3GS_SLOTINFOJOIN(GetNewUID(), connection->GetSocket()->GetPortLE(), connection->GetIPv4(), m_SlotsConfig, m_RandomSeed, m_Map->GetMapNumControllers(), gameInfoVersion));
    SendVirtualHostPlayerInfo(connection);
    SendFakeUsersInfo(connection);
    SendJoinedPlayersInfo(connection);
    return JoinRequestResult::kFail;
  }

  if (HostCounterID < 0x10 && HostCounterID != 0) {
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "@", JoinedRealm, "] is trying to join over reserved realm ", to_string(HostCounterID), " - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    if (HostCounterID > 0x2) {
      connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_WRONGPASSWORD));
      return JoinRequestResult::kFail;
    }
  }

  if (GetUserFromName<CaseSensitive::kNormalize>(joinRequest.GetName())/* && !m_IsHiddenPlayerNames*/) {
    string joinLowerName = joinRequest.GetLowerName();
    if (m_ReportedJoinFailNames.find(joinLowerName) == end(m_ReportedJoinFailNames)) {
      if (!m_IsHiddenPlayerNames) {
        // FIXME: Someone can probably figure out whether a given player has joined a lobby by trying to impersonate them, and failing to.
        // An alternative would be no longer preventing joins and, potentially, disambiguating their names at CGame::ShowPlayerNamesGameStartLoading.
        SendAllChat(Concat("Entry denied for another user with the same name: [", joinRequest.GetName(), "@", JoinedRealm, "]"));
      }
      m_ReportedJoinFailNames.insert(joinLowerName);
    }
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "] invalid name (taken) - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JoinRequestResult::kFail;
  } else if (joinRequest.GetName() == GetLobbyVirtualHostName()) {
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "] spoofer (matches host name) - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JoinRequestResult::kFail;
  } else if (joinRequest.GetName().length() >= 7 && joinRequest.GetName().substr(0, 5) == "User[") {
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "] spoofer (matches fake users) - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JoinRequestResult::kFail;
  } else if (GetHMCEnabled() && joinRequest.GetName() == m_Map->GetHMCPlayerName()) {
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "] spoofer (matches HMC name) - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JoinRequestResult::kFail;
  } else if (joinRequest.GetName() == m_OwnerName && !m_OwnerRealm.empty() && !JoinedRealm.empty() && m_OwnerRealm != JoinedRealm) {
    // Prevent owner homonyms from other realms from joining. This doesn't affect LAN.
    // But LAN has its own rules, e.g. a LAN owner that leaves the game is immediately demoted.
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "@", JoinedRealm, "] spoofer (matches owner name, but realm mismatch, expected ", m_OwnerRealm, ") - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JoinRequestResult::kFail;
  }

  if (CheckScopeBanned(joinRequest.GetName(), JoinedRealm, connection->GetIPStringStrict()) ||
    CheckUserBanned(connection, joinRequest, matchingRealm, JoinedRealm) ||
    CheckIPBanned(connection, joinRequest, matchingRealm, JoinedRealm)) {
    DLOG_APP_IF(LogLevel::kTrace, Concat("user ", EnsureWrapUTF8(joinRequest.GetName()), " failed to join (banned)"));
    // let banned users "join" the game with an arbitrary UID then immediately close the connection
    // this causes them to be kicked back to the chat channel on battle.net
    optional<Version> maybeGameInfoVersion = GetIncomingPlayerVersion(connection, joinRequest, matchingRealm);
    Version gameInfoVersion = maybeGameInfoVersion.value_or(GuessIncomingPlayerVersion(connection, joinRequest, matchingRealm));
    connection->Send(GameProtocol::SEND_W3GS_SLOTINFOJOIN(1, connection->GetSocket()->GetPortLE(), connection->GetIPv4(), m_SlotsConfig, 0, m_Map->GetMapNumControllers(), gameInfoVersion));
    return JoinRequestResult::kFail;
  }

  if (m_GameLoaded) {
    JoinObserver(connection, joinRequest, matchingRealm);
    return JoinRequestResult::kObserver;
  }

  matchingRealm = nullptr;

  const uint8_t reservedIndex = GetReservedIndex(joinRequest.GetName());
  const bool isReserved = reservedIndex < m_Reserved.size() || (!m_RestoredGame && MatchOwnerName(joinRequest.GetName()) && JoinedRealm == m_OwnerRealm);

  if (m_CheckReservation && !isReserved) {
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "] missing reservation - [", connection->GetSocket()->GetName(), "] (", connection->GetIPString(), ")"));
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JoinRequestResult::kFail;
  }

  if (!GetAllowsIPFlood()) {
    if (!CheckIPFlood(joinRequest.GetName(), &(connection->GetSocket()->m_RemoteHost))) {
      LOG_APP_IF(LogLevel::kWarning, Concat("ipflood rejected from ", AddressToStringStrict(connection->GetSocket()->m_RemoteHost)));
      connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
      return JoinRequestResult::kFail;
    }
  }

  uint8_t SID = 0xFF;
  uint8_t UID = 0xFF;

  if (m_RestoredGame) {
    const vector<CGameSlot>& saveSlots = m_RestoredGame->GetSlots();
    uint8_t matchCounter = 0xFF;
    for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
      if (!saveSlots[i].GetIsPlayerOrFake()) {
        continue;
      }
      if (++matchCounter == reservedIndex) {
        SID = i;
        UID = saveSlots[i].GetUID();
        break;
      }
    }
  } else {
    SID = GetEmptySID(false);

    if (SID == 0xFF && isReserved) {
      // a reserved user is trying to join the game but it's full, try to find a reserved slot

      SID = GetEmptySID(true);
      if (SID != 0xFF) {
        GameUser::CGameUser* kickedPlayer = GetUserFromSID(SID);

        if (kickedPlayer) {
          if (!kickedPlayer->HasLeftReason()) {
            if (m_IsHiddenPlayerNames) {
              kickedPlayer->SetLeftReason("was kicked to make room for a reserved user");
            } else {
              kickedPlayer->SetLeftReason(Concat("was kicked to make room for a reserved user [", joinRequest.GetName(), "]"));
            }
          }
          kickedPlayer->CloseConnection();

          // Ensure the userleave message is sent before the reserved userjoin message.
          SendLeftMessage(kickedPlayer, true);
        }
      }
    }

    if (SID == 0xFF && MatchOwnerName(joinRequest.GetName()) && JoinedRealm == m_OwnerRealm) {
      // the owner is trying to join the game but it's full and we couldn't even find a reserved slot, kick the user in the lowest numbered slot
      // updated this to try to find a user slot so that we don't end up kicking a computer

      SID = 0;

      for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
        if (m_SlotsConfig.Inspect(i).GetIsPlayerOrFake()) {
          SID = i;
          break;
        }
      }

      GameUser::CGameUser* kickedPlayer = GetUserFromSID(SID);

      if (kickedPlayer) {
        if (!kickedPlayer->HasLeftReason()) {
          if (m_IsHiddenPlayerNames) {
            kickedPlayer->SetLeftReason("was kicked to make room for the owner");
          } else {
            kickedPlayer->SetLeftReason(Concat("was kicked to make room for the owner [", joinRequest.GetName(), "]"));
          }
        }
        kickedPlayer->CloseConnection();
        // Ensure the userleave message is sent before the game owner' userjoin message.
        SendLeftMessage(kickedPlayer, true);
      }
    }
  }

  if (SID >= GetNumSlots()) {
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    DLOG_APP_IF(LogLevel::kTrace, Concat("user [", joinRequest.GetName(), "@", JoinedRealm, "] failed to join (full)"));
    return JoinRequestResult::kFail;
  }

  // we have a slot for the new user
  // make room for them by deleting the virtual host user if we have to

  if (m_SlotsConfig.GetIsOpen(SID) && GetNumSlotsOpen() == 1 && GetNumJoinedUsersOrFake() > 1)
    DeleteVirtualHost();

  EventBeforeJoin(connection);
  JoinPlayer(connection, joinRequest, SID, UID, HostCounterID, JoinedRealm, isReserved, IsUnverifiedAdmin);
  return JoinRequestResult::kPlayer;
}

void CGame::EventBeforeJoin(CConnection* connection)
{
  if (connection->GetIsUDPTunnel()) {
    vector<uint8_t> packet = {GPSProtocol::Magic::GPS_HEADER, GPSProtocol::Magic::UDPFIN, 4, 0};
    connection->Send(packet);
  }
}

bool CGame::CheckUserBanned(CConnection* connection, const CIncomingJoinRequest& joinRequest, shared_ptr<CRealm> matchingRealm, string& hostName)
{
  // check if the user name is banned in their own realm
  bool isSelfServerBanned = matchingRealm && matchingRealm->IsBannedPlayer(joinRequest.GetName(), hostName);
  bool isBanned = isSelfServerBanned;
  // check if the user name is banned in the game creator's realm
  if (!isBanned && m_Creator.GetServiceType() == ServiceType::kRealm && !m_Creator.GetIsExpired() && !MatchesCreatedFromRealm(matchingRealm)) {
    isBanned = GetCreatedFrom<const CRealm>()->IsBannedPlayer(joinRequest.GetName(), hostName);
  }
  // check if the user name is banned in whatever alternate service the game creator comes from
  if (!isBanned && m_Creator.GetServiceType() != ServiceType::kRealm) {
    isBanned = m_Aura->m_DB->GetIsUserBanned(joinRequest.GetName(), hostName, string());
  }
  if (isBanned) {
    string scopeFragment;
    if (isSelfServerBanned) {
      scopeFragment = "in its own realm";
    } else {
      scopeFragment = "in creator's realm";
    }

    // don't allow the user to spam the chat by attempting to join the game multiple times in a row
    if (m_ReportedJoinFailNames.find(joinRequest.GetName()) == end(m_ReportedJoinFailNames)) {
      LOG_APP_IF(LogLevel::kInfo, Concat("user [", joinRequest.GetName(), "@", hostName, "|", connection->GetIPString(), "] entry denied - banned ", scopeFragment));
      if (!m_IsHiddenPlayerNames) {
        SendAllChat(Concat("[", joinRequest.GetName(), "@", hostName, "] is trying to join the game, but is banned"));
      }
      m_ReportedJoinFailNames.insert(joinRequest.GetName());
    } else {
      LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "@", hostName, "|", connection->GetIPString(), "] entry denied - banned ", scopeFragment));
    }
  }
  return isBanned;
}

bool CGame::CheckIPBanned(CConnection* connection, const CIncomingJoinRequest& joinRequest, shared_ptr<CRealm> matchingRealm, string& hostName)
{
  if (isLoopbackAddress(connection->GetRemoteAddress())) {
    return false;
  }
  // check if the user IP is banned in their own realm
  bool isSelfServerBanned = matchingRealm && matchingRealm->IsBannedIP(connection->GetIPStringStrict());
  bool isBanned = isSelfServerBanned;
  // check if the user IP is banned in the game creator's realm
  if (!isBanned && m_Creator.GetServiceType() == ServiceType::kRealm && !m_Creator.GetIsExpired() && !MatchesCreatedFromRealm(matchingRealm)) {
    isBanned = GetCreatedFrom<const CRealm>()->IsBannedIP(connection->GetIPStringStrict());
  }
  // check if the user IP is banned in whatever alternate service the game creator comes from
  if (!isBanned && m_Creator.GetServiceType() != ServiceType::kRealm) {
    isBanned = m_Aura->m_DB->GetIsIPBanned(connection->GetIPStringStrict(), string());
  }
  if (isBanned) {
    string scopeFragment;
    if (isSelfServerBanned) {
      scopeFragment = "in its own realm";
    } else {
      scopeFragment = "in creator's realm";
    }

    // don't allow the user to spam the chat by attempting to join the game multiple times in a row
    if (m_ReportedJoinFailNames.find(joinRequest.GetName()) == end(m_ReportedJoinFailNames)) {
      LOG_APP_IF(LogLevel::kInfo, Concat("user [", joinRequest.GetName(), "@", hostName, "|", connection->GetIPString(), "] entry denied - IP-banned ", scopeFragment));
      if (!m_IsHiddenPlayerNames) {
        SendAllChat(Concat("[", joinRequest.GetName(), "@", hostName, "] is trying to join the game, but is IP-banned"));
      }
      m_ReportedJoinFailNames.insert(joinRequest.GetName());
    } else {
      LOG_APP_IF(LogLevel::kDebug, Concat("user [", joinRequest.GetName(), "@", hostName, "|", connection->GetIPString(), "] entry denied - IP-banned ", scopeFragment));
    }
  }
  return isBanned;
}

void CGame::EventUserLeft(GameUser::CGameUser* user, const uint32_t clientReason)
{
  if (user->GetDisconnected()) return;
  if (m_GameLoading || m_GameLoaded || clientReason == PLAYERLEAVE_GPROXY) {
    LOG_APP_IF(LogLevel::kInfo, Concat("user [", user->GetName(), "] left the game (", GameProtocol::LeftCodeToString(clientReason), ")"));
  }

  if (m_GameLoaded && !user->GetIsObserver() && GetGameResultSourceOfTruth() == GameResultSourceSelect::kOnlyLeaveCode) {
    // TODO?: EventUserLeft GetGameResultSourceOfTruth() 
    // GameResultSourceSelect::kPreferLeaveCode, GameResultSourceSelect::kPreferMMD ?
  }

  // this function is only called when a client leave packet is received, not when there's a socket error or kick
  // however, clients not only send the leave packet by a user clicking on Quit Game
  // clients also will send a leave packet if the server sends unexpected data

  if (clientReason == PLAYERLEAVE_GPROXY && (user->GetCanReconnect() || GetIsLobbyStrict() /* in case GProxy handshake could not be completed*/)) {
    user->SetLeftReason("Game client disconnected automatically");
    user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
  } else {
    if (!user->HasLeftReason()) {
      user->SetLeftReason("Leaving the game voluntarily");
      user->SetLeftCode(PLAYERLEAVE_LOST);
    } else {
      user->SetLeftReason(Concat("left (", user->GetLeftReason(), ")"));
    }
    user->SetIsLeaver(true);
  }
  if (user->GetIsLagging()) {
    StopLagger(user, user->GetLeftReason());
  } else {
    user->DisableReconnect();
    user->CloseConnection();
  }
  TryActionsOnDisconnect(user, true);
  return;
}

void CGame::EventUserLoaded(GameUser::CGameUser* user)
{
  string role = user->GetIsObserver() ? "observer" : "player";
  LOG_APP_IF(LogLevel::kDebug, Concat(role, " [", user->GetName(), "] finished loading in ", ToFormattedString(static_cast<double>(user->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f), " seconds"));

  // Update stats
  const CGameSlot* slot = InspectSlot(GetSIDFromUID(user->GetUID()));
  CGameController* controllerData = GetGameControllerFromColor(slot->GetColor());
  if (controllerData) {
    uint64_t loadingTime = signed_cast<uint64_t>((user->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000);
    // FIXME: Max loading time should be ensured elsewhere.
    assert((loadingTime <= integer_cast<uint64_t>(numeric_limits<uint32_t>::max())) && "Loading time limited to 1193 hours");
    controllerData->SetLoadingTime(integer_cast_lossy<uint32_t>(loadingTime));
  }

  if (!m_Config.m_LoadInGame) {
    vector<uint8_t> packet = GameProtocol::SEND_W3GS_GAMELOADED_OTHERS(user->GetUID());
    if (m_BufferingEnabled & BUFFERING_ENABLED_LOADING) {
      AppendContainer(m_GameHistory->m_LoadingRealBuffer, packet);
    }
    SendAll(packet);
  } else { // load-in-game
    Send(user, m_GameHistory->m_LoadingRealBuffer);
    if (!m_GameHistory->m_LoadingVirtualBuffer.empty()) {
      // CGame::EventUserLoaded - Fake users loaded
      Send(user, m_GameHistory->m_LoadingVirtualBuffer);
    }
    // GProxy sends m_GProxyEmptyActions additional empty actions for every action received.
    // So we need to match it, to avoid desyncs.
    // Note that Warcraft III doesn't respond to empty actions (i.e no keep alive frame).
    if (user->GetCanReconnect()) {
      Send(user, GameProtocol::SEND_W3GS_EMPTY_ACTIONS(m_BeforePlayingEmptyActions));
    } else {
      Send(user, GameProtocol::SEND_W3GS_EMPTY_ACTIONS(m_BeforePlayingEmptyActions * (1 + m_GProxyEmptyActions)));
    }

    user->SetLagging(false);
    user->ClearStartedLaggingTicks();
    RemoveFromLagScreens(user);
    user->SetStatus(USERSTATUS_PLAYING);
    UserList laggingPlayers = GetLaggingUsers();
    if (laggingPlayers.empty()) {
      m_IsLagging = false;
    }
    user->SendOnLoadChatMessages();
    if (m_IsLagging) {
      DLOG_APP_IF(LogLevel::kTrace, Concat("@[", user->GetName(), "] lagger update (+", ToNameListSentence(laggingPlayers), ")"));
      Send(user, GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, m_Aura->GetClockTicks()));
      LogApp(Concat("[LoadInGame] Waiting for ", to_string(laggingPlayers.size()), " other players to load the game..."), LOG_C);

      if (laggingPlayers.size() >= 3) {
        SendChat(user, Concat("[", user->GetName(), "], please wait for ", to_string(laggingPlayers.size()), " players to load the game..."));
      } else {
        SendChat(user, Concat("[", user->GetName(), "], please wait for ", ToNameListSentence(laggingPlayers), " to load the game..."));
      }
    }
  }
}

bool CGame::EventUserIncomingAction(GameUser::CGameUser* user, CIncomingAction& action)
{
  if (!m_GameLoading && !m_GameLoaded) {
    return false;
  }

  if (action.GetLength() > W3GS_ACTION_MAX_PACKET_SIZE) {
    return false;
  }

  CQueuedActionsFrame& actionFrame = user->GetPingEqualizerFrame();

  user->CheckReleaseOnHoldActions();
  user->AddActionCounters();

  vector<const uint8_t*> delimiters = action.SplitAtomic();
  for (size_t i = 0, j = 1, l = delimiters.size(); j < l; i++, j++) {
    const uint8_t actionType = delimiters[i][0];
    const auto actionSize = delimiters[j] - delimiters[i];
    switch (actionType) {
      case ACTION_ALLIANCE_SETTINGS: {
        if (actionSize < 6) break;
        if (delimiters[i][1] == JN_ALLIANCE_SETTINGS_SYNC_DATA) {
          LOG_APP_IF(LogLevel::kDebug, Concat("Player [", user->GetName(), "] synchronizing JNLoader data"));
        } else if (delimiters[i][1] == MH_DOTA_SETTINGS_SYNC_DATA) {
          LOG_APP_IF(LogLevel::kDebug, Concat("Player [", user->GetName(), "] synchronizing DotA data"));
        } else if (delimiters[i][1] < MAX_SLOTS_MODERN) {
          const bool wantsShare = (ByteArrayToUInt32LE(delimiters[i] + 2) & ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY) == ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY;
          const uint8_t targetSID = delimiters[i][1];

          if (user->GetIsSharingUnitsWithSlot(targetSID) != wantsShare) {
            if (wantsShare) {
              LOG_APP_IF(LogLevel::kDebug, Concat("Player [", user->GetName(), "] intends to grant shared unit control to [", GetUserNameFromSID(targetSID), "]"));
            } else {
              LOG_APP_IF(LogLevel::kDebug, Concat("Player [", user->GetName(), "] intends to take away shared unit control from [", GetUserNameFromSID(targetSID), "]"));
            }
            GameUser::CGameUser* targetUser = GetUserFromSID(targetSID);
            if (targetUser && wantsShare) {
              switch (m_Config.m_ShareUnitsHandler) {
                case OnShareUnitsHandler::kNative:
                  break;

                case OnShareUnitsHandler::kRestrictSharee:
                  if (
                    (m_Map->GetMapFlags() & GAMEFLAG_FIXEDTEAMS) &&
                    (InspectSlot(targetSID)->GetTeam() == InspectSlot(user->GetSID())->GetTeam())
                  ) {
                    // This is a well-behaved map (at least if it's melee).
                    // Handle restriction on CGame::SendAllActionsCallback
                    break;
                  }

                  // either the map is not well-behaved, or the client is rogue/griefer - instakick
                  // falls through

                case OnShareUnitsHandler::kKickSharer:
                default:
                  user->SetLeftCode(PLAYERLEAVE_LOST);
                  user->SetLeftReason("autokicked - antishare");
                  SendChat(user, "[ANTISHARE] You have been automatically kicked out of the game.");
                  // Treat as unrecoverable protocol error
                  return false;
              }
            }
          }
        }
        break;
      }

      case ACTION_SAVE: {
        if (!user->GetCanSave()) {
          // Game engine lets referees save without limit nor throttle whatsoever.
          // This path prevents save-spamming leading to unplayable games.
          EventUserDisconnectGameAbuse(user);
          return false;
        }
        break;
      }

      case ACTION_PAUSE: {
        if (!user->GetCanPause() && 0xFF == SimulateActionUID(ACTION_RESUME, user, false, ACTION_SOURCE_ANY & NOT_ACTION_SOURCE_OBSERVER)) {
          // Game engine lets referees pause without limit nor throttle whatsoever.
          // This path prevents pause-spamming leading to unplayable games.
          EventUserDisconnectGameAbuse(user);
          return false;
        }
      }
    }
  }

  if (action.GetError()) {
    LogApp(Concat("Action parser error for [", user->GetName(), "] (", user->GetGameVersionString(), ") <", ByteArrayToHexString(action.GetImmutableAction()), ">"), LOG_C | LOG_P);
  }

  if (user->GetShouldHoldAction(action.GetCount())) {
    if (!user->GetOnHoldActionsAny()) {
      SendChat(user, "Your actions are being restricted.");
    }
    user->AddOnHoldActionsCount(action.GetCount());
    user->GetOnHoldActions().push(std::move(action));
    size_t holdActionsCount = user->GetOnHoldActionsCount();
    if (holdActionsCount > GAME_ACTION_HOLD_QUEUE_MAX_SIZE) {
      return false;
    }
    if (holdActionsCount >= GAME_ACTION_HOLD_QUEUE_MIN_WARNING_SIZE && holdActionsCount % GAME_ACTION_HOLD_QUEUE_MOD_WARNING_SIZE == 0) {
      SendChat(user, "You WILL be kicked if you input further orders and/or actions.");
    }
  } else {
    actionFrame.AddAction(std::move(action));
    if (user->GetHasAPMQuota()) {
      if (!user->GetAPMQuota().TryConsume(action.GetCount())) {
        Print("[APMLimiter] Malfunction detected");
      }
    }
  }

  /*
   * Action has been accepted
   * Now update our state, and add further actions from virtual players if pertinent.
   */

  for (size_t i = 0, j = 1, l = delimiters.size(); j < l; i++, j++) {
    const uint8_t actionType = delimiters[i][0];
    switch (actionType) {
      case ACTION_SAVE:
        SaveEnded(0xFF, actionFrame);
        if (user->GetCanSave()) {
           // Done in CGame::EventUserIncomingAction matching GetCanSave() check
          user->DropRemainingSaves();
        }
        break;
      case ACTION_SAVE_ENDED:
        LOG_APP_IF(LogLevel::kInfo, Concat("[", user->GetName(), "] finished saving the game"));
        break;
      case ACTION_PAUSE:
        if (actionFrame.callback != ON_SEND_ACTIONS_PAUSE) {
          actionFrame.callback = ON_SEND_ACTIONS_PAUSE;
          actionFrame.pauseUID = user->GetUID();
        }
        if (user->GetCanPause()) {
          // Done in CGame::EventUserIncomingAction matching GetCanPause() check
          user->DropRemainingPauses();
        } else {
          // If a referee tries to pause the game after their limit is exceeded,
          // we just have a virtual user overturn the pause.
          Resume(user, actionFrame, false);
        }
        break;
      case ACTION_RESUME:
        actionFrame.callback = ON_SEND_ACTIONS_RESUME;
        //actionFrame.pauseUID = 0xFF;
        break;
      case ACTION_CHAT_TRIGGER: {
        // Handled in CGame::SendAllActionsCallback
        break;
      }
      case ACTION_GAME_CACHE_INT: {
        // This is the W3MMD action type.
        // Handled in CGame::SendAllActionsCallback
        break;
      }
      default:
        break;
    }
  }

  return true;
}

void CGame::EventUserKeepAlive(GameUser::CGameUser* user)
{
  if (!m_GameLoading && !m_GameLoaded) {
    return;
  }

  bool canConsumeFrame = true;
  UserList& otherPlayers = m_SyncPlayers[user];

  if (!otherPlayers.empty() && m_SyncCounter < SYNCHRONIZATION_CHECK_MIN_FRAMES) {
    // Add a grace period in order for any desync warnings to be displayed in chat (rather than just in chat logs!)
    return;
  }

  for (auto& otherPlayer: otherPlayers) {
    if (otherPlayer == user) {
      canConsumeFrame = false;;
      break;
    }

    if (!otherPlayer->HasCheckSums()) {
      canConsumeFrame = false;
      break;
    }
  }

  if (!canConsumeFrame) {
    return;
  }

  const uint32_t MyCheckSum = user->GetCheckSums()->front();
  user->GetCheckSums()->pop();
  ++m_SyncCounterChecked;

  bool DesyncDetected = false;
  UserList DesyncedPlayers;
  UserList::iterator it = otherPlayers.begin();
  while (it != otherPlayers.end()) {
    if ((*it)->GetCheckSums()->front() == MyCheckSum) {
      (*it)->GetCheckSums()->pop();
      ++it;
    } else {
      DesyncDetected = true;
      UserList& BackList = m_SyncPlayers[*it];
      auto BackIterator = std::find(BackList.begin(), BackList.end(), user);
      if (BackIterator == BackList.end()) {
      } else {
        *BackIterator = std::move(BackList.back());
        BackList.pop_back();
      }

      DesyncedPlayers.push_back(*it);
      std::iter_swap(it, otherPlayers.end() - 1);
      otherPlayers.pop_back();
    }
  }
  if (DesyncDetected) {
    m_GameHistory->SetDesynchronized();
    string syncListText = ToNameListSentence(m_SyncPlayers[user]);
    string desyncListText = ToNameListSentence(DesyncedPlayers);
    if (m_Aura->MatchLogLevel(LogLevel::kDebug)) {
      LogApp("===== !! Desync detected !! ======================================", LOG_ALL);
      if (m_Config.m_LoadInGame) {
        LogApp(Concat("Frame ", to_string(m_SyncCounterChecked), " | Load in game: ENABLED"), LOG_C | LOG_P);
      } else {
        LogApp(Concat("Frame ", to_string(m_SyncCounterChecked), " | Load in game: DISABLED"), LOG_C | LOG_P);
      }
      LogApp(Concat("User [", user->GetName(), "] (", user->GetDelayText(true), ") Reconnection: ", user->GetReconnectionText()), LOG_C | LOG_P);
      LogApp(Concat("User [", user->GetName(), "] is synchronized with ", to_string(m_SyncPlayers[user].size()), " user(s): ", syncListText), LOG_C | LOG_P);
      LogApp(Concat("User [", user->GetName(), "] is no longer synchronized with ", desyncListText), LOG_ALL);
      if (GetAnyUsingGProxy()) {
        LogApp(Concat("GProxy: ", GetActiveReconnectProtocolsDetails()), LOG_C);
      }
      LogApp("==================================================================", LOG_C);
    }

    if (GetHasDesyncHandler()) {
      SendAllChat(Concat("Warning! Desync detected (", user->GetDisplayName(), " (", user->GetDelayText(true), ") may not be in the same game as ", desyncListText));
      if (!GetAllowsDesync()) {
        StopDesynchronized("was automatically dropped after desync");
      }
    }
  } else if ((m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) && !m_GameHistory->GetDesynchronized()) {
    m_GameHistory->AddCheckSum(MyCheckSum);
  }
}

void CGame::EventChatTrigger(GameUser::CGameUser* user, string_view chatMessage, const uint32_t first, const uint32_t second)
{
  bool canLogChatTriggers = m_Aura->m_Config.m_LogGameChat != LOG_GAME_CHAT_NEVER && (((m_Config.m_LogChatTypes & LOG_CHAT_TYPE_COMMANDS) > 0) || m_Aura->MatchLogLevel(LogLevel::kDebug));
  if (canLogChatTriggers && (m_Config.m_LogChatTypes & LOG_CHAT_TYPE_COMMANDS) > 0) {
    m_Aura->LogPersistent(Concat(GetLogPrefix(), EnsureWrapUTF8(m_Map->GetServerFileName()), " [CMD] ["+ user->GetExtendedName(), "] ", EnsureWrapUTF8(chatMessage)));
  }

  // Enable --log-level debug to figure out HMC map-specific constants
  // According to TriggerHappy's original HMC code,
  // only the first (lower) two bytes are relevant,
  // and the upper two bytes are zero or can be zeroed in SendHMC().
  //
  // TH also expects both uint32_t values at CGame::SendChatTrigger() to be equal.
  // Unfortunately, I have seen many cases in which these values are different.
  // But maybe the second one doesn't matter, just like the upper bytes above.
  //
  // So if those assumptions, hold,
  // let N be the first integer output here.
  //
  // Then, W3HMC trigger constants are:
  // <map.w3hmc.trigger.main = N & 0xFFFF>
  // <map.w3hmc.trigger.main = (map_w3hmctid1) | (map_w3hmctid2 << 8)> (in terms of TH's implementation)
  //
  // Or, in simpler maths terms:
  // <map.w3hmc.trigger.main = N mod 65536>
  // <map.w3hmc.trigger.main = (map_w3hmctid1) + (map_w3hmctid2 * 256)> (in terms of TH's implementation)
  //
  // IF those assumptions don't hold and it turns out that e.g. both uint32_t values are different, just set
  // (hex works, with 0x prefix)
  // <map.w3hmc.trigger.main = first>
  // <map.w3hmc.trigger.complement = second>
  //

  if (canLogChatTriggers) {
    LOG_APP_IF(LogLevel::kDebug, Concat(EnsureWrapUTF8(m_Map->GetServerFileName()), " Message by [", user->GetName(), "]: ", EnsureWrapUTF8(chatMessage), " triggered : [0x", ToHexString(first), " | 0x", ToHexString(second), "]"));
  }

  if (m_Map->GetMapType() == "microtraining") {
    if (chatMessage == "g" || chatMessage == "go") {
      if (!user->GetInGameReady()) {
        user->SetInGameReady();
        if (GetNumInGameReadyUsers() >= 2) {
          RestartAPMTrainer();
        }
      }
    } else if (chatMessage == "gg") {
      PauseAPMTrainer();
      ResetInGameReadyUsers();
    }
  }
}

void CGame::EventUserChat(GameUser::CGameUser* user, const CIncomingMessageOrSettingsView& incomingChatMessage)
{
  const bool isLobbyChat = incomingChatMessage.GetType() == GameProtocol::ChatToHostType::CTH_MESSAGE_LOBBY;
  if (isLobbyChat == (m_GameLoading || m_GameLoaded)) {
    // Racing condition
    return;
  }

  // relay the chat message to other users
  const uint8_t targetType = incomingChatMessage.GetInGameChannel();
  string_view textContent = incomingChatMessage.GetText();
  assert((!textContent.empty()) && "Chat message cannot be empty");
  const bool muteAll = !isLobbyChat && m_MuteAll;
  bool shouldRelay = m_ChatEnabled && !(muteAll && targetType == CHAT_RECV_ALL) && !user->CheckMuted();
  bool didRelay = false;

  string chatTypeFragment;
  if (isLobbyChat) {
    if (m_Aura->m_Config.m_LogGameChat != LOG_GAME_CHAT_NEVER) {
      Log(Concat("[", user->GetDisplayName(), "] ", textContent));
      if ((m_Config.m_LogChatTypes & LOG_CHAT_TYPE_NON_ASCII) && !IsASCII(textContent)) {
        m_Aura->LogPersistent(Concat(GetLogPrefix(), "[Lobby] ["+ user->GetExtendedName(), "] ", textContent));
      }
    }
  } else {
    switch (targetType) {
      case CHAT_RECV_ALL:
        chatTypeFragment = "[All] ";
        break;
      case CHAT_RECV_ALLY:
        chatTypeFragment = "[Allies] ";
        break;
      case CHAT_RECV_OBS:
        // [Observer] or [Referees]
        chatTypeFragment = "[Observer] ";
        break;
      default:
        if (!muteAll) {
          // also don't relay in-game private messages if we're currently muting all
          uint8_t privateTarget = targetType - CHAT_RECV_PRIVATE_OFFSET;
          chatTypeFragment = Concat("[Private ", ToDecString(ToBaseOne(privateTarget)), "] ");
        }
    }

    if (m_Aura->m_Config.m_LogGameChat == LOG_GAME_CHAT_ALWAYS) {
      Log(Concat(chatTypeFragment, "[", user->GetDisplayName(), "] ", textContent));
    }
  }

  // handle bot commands
  {
    CommandHistory* cmdHistory = user->GetCommandHistory();
    shared_ptr<CRealm> realm = user->GetRealm(false);
    CCommandConfig* commandCFG = realm ? realm->GetCommandConfig() : m_Aura->m_Config.m_LANCommandCFG;
    const bool commandsEnabled = commandCFG->m_Enabled && (
      !realm || !(commandCFG->m_RequireVerified && !user->GetIsRealmVerified())
    );
    bool isCommand = false;
    const uint8_t activeSmartCommand = cmdHistory->GetSmartCommand();
    cmdHistory->ClearSmartCommand();
    if (commandsEnabled) {
      CommandTokensView commandTokens;
      ExtractMessageTokensAny(textContent, m_Config.m_PrivateCmdToken, m_Config.m_BroadcastCmdToken, commandTokens);
      isCommand = commandTokens.matchType != CommandTokensMatchType::kNone;
      if (isCommand) {
        string cmdToken(commandTokens.token);
        string command = ToLowerCase(commandTokens.cmd);
        string target(commandTokens.target);
        cmdHistory->SetUsedAnyCommands(true);
        shouldRelay = shouldRelay && !GetIsHiddenPlayerNames();
        // If we want users identities hidden, we must keep bot responses private.
        if (shouldRelay && !didRelay) {
          SendChatMessage(user, incomingChatMessage);
          didRelay = true;
        }
        shared_ptr<CCommandContext> ctx = nullptr;
        try {
          ctx = make_shared<CCommandContext>(
            ServiceType::kLAN /* or realm, actually*/, m_Aura, commandCFG,
            shared_from_this(), user, !muteAll && !GetIsHiddenPlayerNames() && (commandTokens.matchType == CommandTokensMatchType::kBroadcast),
            &std::cout
          );
        } catch (...) {}
        if (ctx) ctx->Run(cmdToken, command, target);
      } else if (textContent == "?trigger") {
        isCommand = true;
        shouldRelay = shouldRelay && !GetIsHiddenPlayerNames();
        if (shouldRelay && !didRelay) {
          SendChatMessage(user, incomingChatMessage);
          didRelay = true;
        }
        SendCommandsHelp(m_Config.m_BroadcastCmdToken.empty() ? m_Config.m_PrivateCmdToken : m_Config.m_BroadcastCmdToken, user, false);
      } else if (textContent == "/p" || textContent == "/ping" || textContent == "/game" || textContent == "/apm") {
        isCommand = true;
        shouldRelay = shouldRelay && !GetIsHiddenPlayerNames();
        // Note that when the WC3 client is connected to a realm, all slash commands are sent to the bnet server.
        // Therefore, these commands are only effective over LAN.
        if (shouldRelay && !didRelay) {
          SendChatMessage(user, incomingChatMessage);
          didRelay = true;
        }
        shared_ptr<CCommandContext> ctx = nullptr;
        try {
          ctx = make_shared<CCommandContext>(ServiceType::kLAN /* or realm, actually*/, m_Aura, commandCFG, shared_from_this(), user, false, &std::cout);
        } catch (...) {}
        if (ctx) {
          string cmdToken(m_Config.m_PrivateCmdToken);
          string command(textContent.substr(1));
          string target;
          ctx->Run(cmdToken, command, target);
        }
      } else if (isLobbyChat && !cmdHistory->GetUsedAnyCommands()) {
        if (shouldRelay && !didRelay) {
          SendChatMessage(user, incomingChatMessage);
          didRelay = true;
        }
        if (!CheckSmartCommands(user, textContent, activeSmartCommand, commandCFG) && !cmdHistory->GetSentAutoCommandsHelp()) {
          bool anySentCommands = false;
          for (const auto& otherPlayer : m_Users) {
            if (otherPlayer->GetCommandHistory()->GetUsedAnyCommands()) anySentCommands = true;
          }
          if (!anySentCommands) {
            SendCommandsHelp(m_Config.m_BroadcastCmdToken.empty() ? m_Config.m_PrivateCmdToken : m_Config.m_BroadcastCmdToken, user, true);
          }
        }
      }
    }
    if (!isCommand) {
      cmdHistory->ClearLastCommand();
    }
    if (shouldRelay) {
      if (!didRelay) {
        SendChatMessage(user, incomingChatMessage);
        didRelay = true;
      }
    } else if (!isCommand) {
      if (m_ChatEnabled && muteAll && targetType == CHAT_RECV_ALL) {
        SendChat(user, "Error - You may only use [Allied] chat. Press (Shift+Enter.)");
      } else {
        SendChat(user, "Error - Chat is disabled.");
      }
    }
    if (m_Aura->m_Config.m_LogGameChat != LOG_GAME_CHAT_NEVER) {
      bool logMessage = false;
      for (const auto& word : m_Config.m_LoggedWords) {
        if (textContent.find(word) != string::npos) {
          logMessage = true;
          break;
        }
      }
      if (logMessage) {
        m_Aura->LogPersistent(Concat(GetLogPrefix(), chatTypeFragment, "["+ user->GetExtendedName(), "] ", textContent));
      }
    }
  }
}

void CGame::EventUserChatOrPlayerSettings(GameUser::CGameUser* user, const CIncomingMessageOrSettingsView& incomingChatMessage)
{
  if (incomingChatMessage.GetFromUID() != user->GetUID()) {
    return;
  }

  switch (incomingChatMessage.GetType()) {
    case GameProtocol::ChatToHostType::CTH_MESSAGE_LOBBY:
    case GameProtocol::ChatToHostType::CTH_MESSAGE_INGAME:
      EventUserChat(user, incomingChatMessage);
      break;
    case GameProtocol::ChatToHostType::CTH_TEAMCHANGE:
      EventUserRequestTeam(user, incomingChatMessage.GetByte());
      break;
    case GameProtocol::ChatToHostType::CTH_COLOURCHANGE:
      EventUserRequestColor(user, incomingChatMessage.GetByte());
      break;
    case GameProtocol::ChatToHostType::CTH_RACECHANGE:
      EventUserRequestRace(user, incomingChatMessage.GetByte());
      break;
    case GameProtocol::ChatToHostType::CTH_HANDICAPCHANGE:
      EventUserRequestHandicap(user, incomingChatMessage.GetByte());
      break;
  }
}

void CGame::EventUserRequestTeam(GameUser::CGameUser* user, uint8_t team)
{
  // user is requesting a team change

  if (m_CountDownStarted || m_RestoredGame) {
    return;
  }

  if (m_Locked || user->GetIsActionLocked()) {
    SendChat(user, "You are not allowed to change your alignment.");
    return;
  }

  if (team > GetObserverTeam()) {
    return;
  }

  if (team == GetObserverTeam()) {
    if (m_Map->GetGameObservers() != GameObserversMode::kStartOrOnDefeat && m_Map->GetGameObservers() != GameObserversMode::kReferees) {
      return;
    }
  } else if (team >= m_Map->GetMapNumTeams()) {
    return;
  }

  uint8_t SID = GetSIDFromUID(user->GetUID());
  const CGameSlot* slot = InspectSlot(SID);
  if (!slot) {
    return;
  }

  if (team == slot->GetTeam()) {
    if (!SwapEmptyAllySlot(SID)) return;
  } else if (m_CustomLayout & CUSTOM_LAYOUT_LOCKTEAMS) {
    if (m_IsDraftMode) {
      SendChat(user, "This lobby has draft mode enabled. Only team captains may assign users.");
    } else {
      switch (m_CustomLayout) {
        case CUSTOM_LAYOUT_ONE_VS_ALL:
        SendChat(user, "This is a One-VS-All lobby. You may not switch to another team.");
          break;
        case CUSTOM_LAYOUT_HUMANS_VS_AI:
          SendChat(user, "This is a humans VS AI lobby. You may not switch to another team.");
          break;
        case CUSTOM_LAYOUT_FFA:
          SendChat(user, "This is a free-for-all lobby. You may not switch to another team.");
          break;
        default:
          SendChat(user, "This lobby has a custom teams layout. You may not switch to another team.");
          break;
      }
    } 
  } else {
    SetSlotTeam(GetSIDFromUID(user->GetUID()), team, false);
  }
}

void CGame::EventUserRequestColor(GameUser::CGameUser* user, uint8_t color)
{
  // user is requesting a color change

  if (m_CountDownStarted || m_RestoredGame) {
    return;
  }

  if (m_Locked || user->GetIsActionLocked()) {
    SendChat(user, "You are not allowed to change your player color.");
    return;
  }

  if (!m_Map->GetModernColorsEnabled() && color >= MAX_SLOTS_LEGACY) {
    SendChat(user, "This lobby does not support modern player colors. Please choose among the first 12 options.");
    return;
  }

  if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS) {
    // user should directly choose a different slot instead.
    return;
  }

  if (color >= GetObserverColor()) {
    return;
  }

  uint8_t SID = GetSIDFromUID(user->GetUID());

  if (SID < m_SlotsConfig.GetCount()) {
    // make sure the user isn't an observer

    if (m_SlotsConfig.GetIsObserver(SID)) {
      return;
    }

    if (!SetSlotColor(SID, color, false)) {
      LOG_APP_IF(LogLevel::kDebug, Concat(user->GetName(), " failed to switch to color ", to_string(static_cast<uint16_t>(color))));
    }
  }
}

void CGame::EventUserRequestRace(GameUser::CGameUser* user, uint8_t race)
{
  // user is requesting a race change

  if (m_CountDownStarted || m_RestoredGame) {
    return;
  }

  if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS)
    return;

  if (m_Map->GetMapFlags() & GAMEFLAG_RANDOMRACES) {
    SendChat(user, "This game lobby has forced random races.");
    return;
  }

  if (m_Locked || user->GetIsActionLocked()) {
    SendChat(user, "You are not allowed to change your race.");
    return;
  }

  if (race != SLOTRACE_HUMAN && race != SLOTRACE_ORC && race != SLOTRACE_NIGHTELF && race != SLOTRACE_UNDEAD && race != SLOTRACE_RANDOM)
    return;

  CGameSlot* slot = GetSlot(GetSIDFromUID(user->GetUID()));
  if (slot) {
    slot->SetRace(race | SLOTRACE_SELECTABLE);
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  }
}

void CGame::EventUserRequestHandicap(GameUser::CGameUser* user, uint8_t handicap)
{
  // user is requesting a handicap change

  if (m_CountDownStarted || m_RestoredGame) {
    return;
  }

  if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS)
    return;

  if (handicap != 50 && handicap != 60 && handicap != 70 && handicap != 80 && handicap != 90 && handicap != 100)
    return;

  if (m_Locked || user->GetIsActionLocked()) {
    SendChat(user, "You are not allowed to change your handicap.");
    return;
  }

  CGameSlot* slot = GetSlot(GetSIDFromUID(user->GetUID()));
  if (slot) {
    slot->SetHandicap(handicap);
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  }
}

void CGame::EventUserDropRequest(GameUser::CGameUser* user)
{
  if (!m_GameLoaded) {
    return;
  }

  if (m_IsLagging) {
    LOG_APP_IF(LogLevel::kDebug, Concat("user [", user->GetName(), "] voted to drop laggers"));
    SendAllChat(Concat("Player [", user->GetDisplayName(), "] voted to drop laggers"));

    // check if at least half the users voted to drop
    uint8_t votesCount = 0;
    for (auto& eachPlayer : m_Users) {
      if (eachPlayer->GetDropVote()) {
        ++votesCount;
      }
    }

    if (static_cast<uint8_t>(m_Users.size()) < 2 * votesCount) {
      StopLaggers("lagged out (dropped by vote)");
    }
  }
}

void CGame::EventUserMapSize(GameUser::CGameUser* user, const CIncomingMapFileSize& clientMap)
{
  bool isFirstCheck = !user->GetMapChecked();

  user->SetMapChecked(true);
  const uint32_t expectedMapSize = m_Map->GetMapSizeClamped(user->GetGameVersion());
  UpdateUserMapProgression(user, clientMap.GetFileSize(), expectedMapSize);

  if (clientMap.GetFlag() != 1 || clientMap.GetFileSize() != expectedMapSize) {
    // user doesn't have the map
    const MapTransferCheckResult checkResult = CheckCanTransferMap(user, user->GetRealm(false), user->GetGameVersion(), user->GetDownloadAllowed());
    if (checkResult == MapTransferCheckResult::kAllowed) {
      MapTransfer& mapTransfer = user->GetMapTransfer();
      if (!mapTransfer.GetStarted() && clientMap.GetFlag() == 1) {
        // inform the client that we are willing to send the map

        LOG_APP_IF(LogLevel::kDebug, Concat("map download started for user [", user->GetName(), "]"));
        Send(user, GameProtocol::SEND_W3GS_STARTDOWNLOAD(GetHostUID()));
        mapTransfer.Start();
      } else {
        mapTransfer.SetLastAck(clientMap.GetFileSize());
      }
    } else if (!user->GetMapKicked()) {
      const bool willKick = !user->GetIsReserved();
      if (isFirstCheck) {
        string fromURL, kickFragment;
        if (!GetMapSiteURL().empty()) {
          fromURL = Concat(" from <", EnsureUTF8(GetMapSiteURL()), ">");
        }
        if (willKick) {
           kickFragment = Concat(" (Kick in ", to_string(m_Config.m_LacksMapKickDelay / 1000), " seconds...)");
        }
        SendChat(user, Concat(user->GetName(), ", please download the map", fromURL, " before joining.", kickFragment));
      }

      if (willKick) {
        user->AddKickReason(GameUser::KickReason::kMapMissing);
        user->KickAtLatest(m_Aura->GetClockTicks() + m_Config.m_LacksMapKickDelay);

        if (!user->HasLeftReason()) {
          string reason;
          switch (checkResult) {
            case MapTransferCheckResult::kInvalid:
              reason = "invalid";
              break;
            case MapTransferCheckResult::kMissing:
              reason = "missing";
              break;
            case MapTransferCheckResult::kDisabled:
              reason = "disabled";
              break;
            case MapTransferCheckResult::kTooLargeVersion:
              LOG_APP_IF(LogLevel::kDebug, Concat("user [", user->GetName(), "] running v", ToVersionString(user->GetGameVersion()), " cannot download ", ToFormattedString(m_Map->GetMapSizeMB()), " MB map in-game"));
              // falls through
            case MapTransferCheckResult::kTooLargeConfig:
              reason = "too large";
              break;
            case MapTransferCheckResult::kBufferBloat:
              reason = "bufferbloat";
              break;
            // kAllowed checked above
            IGNORE_CASE(MapTransferCheckResult::kAllowed)
          }
          user->SetLeftReason(Concat("autokicked - they don't have the map, and it cannot be transferred (", reason, ")"));
        }
      }
    }
  } else if (user->GetMapTransfer().GetStarted()) {
    // calculate download rate
    const double seconds = static_cast<double>(m_Aura->GetClockTicks() - user->GetMapTransfer().GetStartedTicks()) / 1000.f;
    //const double Rate    = static_cast<double>(expectedMapSize) / 1024.f / seconds;
    LOG_APP_IF(LogLevel::kDebug, Concat("map download finished for user [", user->GetName(), "] in ", ToFormattedString(seconds), " seconds"));
    SendAllChat(Concat("Player [", user->GetDisplayName(), "] downloaded the map in ", ToFormattedString(seconds), " seconds"/* (", ToFormattedString(Rate), " KB/sec)"*/));
    user->GetMapTransfer().Finish();
    EventUserMapReady(user);
  } else {
    EventUserMapReady(user);
  }
}

void CGame::EventUserPongToHost(GameUser::CGameUser* user)
{
  if (m_CountDownStarted || user->GetDisconnected()) {
    return;
  }

  if (!user->GetLatencySent() && user->GetIsRTTMeasuredConsistent()) {
    if (user->GetIsNameCensored()) {
      SendChat(user, Concat(user->GetName(), "(*), your latency is ", user->GetDelayText(false)), LogLevelExtra::kDebug);
    } else {
      SendChat(user, Concat(user->GetName(), ", your latency is ", user->GetDelayText(false)), LogLevelExtra::kDebug);
    }
    user->SetLatencySent(true);
  }

  if ((!user->GetIsReady() && user->GetMapReady() && !user->GetIsObserver()) &&
    (!m_CountDownStarted && !m_ChatOnly && m_Aura->m_StartedGames.size() < m_Aura->m_Config.m_MaxStartedGames) &&
    (user->GetReadyReminderIsDue() && user->GetIsRTTMeasuredConsistent())) {
    if (!m_AutoStartRequirements.empty()) {
      switch (GetPlayersReadyMode()) {
        case PlayersReadyMode::kExpectRace: {
          SendChat(user, Concat("Choose your race for the match to automatically start (or type ", GetCmdToken(), "ready)"));
          break;
        }
        case PlayersReadyMode::kExplicit: {
          SendChat(user, Concat("Type ", GetCmdToken(), "ready for the match to automatically start."));
          break;
        }
        case PlayersReadyMode::kFast: {
          // This is an "always-ready" mode. Even !afk cannot be used.
          // GameUser::CGameUser::UpdateReady() takes care of updating user readiness as soon as they are map-ready.
          UNREACHABLE();
          break;
        }
        IGNORE_ENUM_LAST(PlayersReadyMode)
      }
      user->SetReadyReminded();
    }
  }

  // autokick users with excessive pings but only if they're not reserved and we've received at least 3 pings from them
  // see the Update function for where we send pings

  optional<uint32_t> latencyMs = user->GetOperationalRTT();
  if (!latencyMs.has_value()) return; // if using system RTT

  if (*latencyMs >= m_Config.m_AutoKickPing && !user->GetIsReserved() && !user->GetIsOwner(nullopt)) {
    if (m_Users.size() > 1 && user->GetIsRTTMeasuredBadConsistent()) {
      if (!user->HasLeftReason()) {
        user->SetLeftReason(Concat("autokicked - excessive ping of ", to_string(*latencyMs), "ms"));
      }
      user->AddKickReason(GameUser::KickReason::kHighPing);
      user->KickAtLatest(m_Aura->GetClockTicks() + HIGH_PING_KICK_DELAY);
      if (!user->GetHasHighPing()) {
        SendAllChat(Concat("Player [", user->GetDisplayName(), "] has an excessive ping of ", to_string(*latencyMs), "ms. Autokicking..."));
        user->SetHasHighPing(true);
      }
    }
  } else {
    user->RemoveKickReason(GameUser::KickReason::kHighPing);
    user->CheckStillKicked();
    if (user->GetHasHighPing()) {
      bool hasHighPing = *latencyMs >= m_Config.m_SafeHighPing;
      if (!hasHighPing) {
        user->SetHasHighPing(hasHighPing);
        SendAllChat(Concat("Player [", user->GetDisplayName(), "] ping went down to ", to_string(*latencyMs), "ms"));
      } else if (*latencyMs >= m_Config.m_WarnHighPing && user->GetPongCounter() % 4 == 0) {
        // Still high ping. We need to keep sending these intermittently (roughly every 20-25 seconds), so that
        // users don't assume that lack of news is good news.
        SendChat(user, Concat(user->GetName(), ", you have a high ping of ", to_string(*latencyMs), "ms"));
      }
    } else {
      bool hasHighPing = *latencyMs >= m_Config.m_WarnHighPing;
      if (hasHighPing) {
        user->SetHasHighPing(hasHighPing);
        SendAllChat(Concat("Player [", user->GetDisplayName(), "] has a high ping of ", to_string(*latencyMs), "ms"));
      }
    }
  }
}

void CGame::EventUserMapReady(GameUser::CGameUser* user)
{
  if (user->GetMapReady()) {
    return;
  }
  user->SetMapReady(true);
  UpdateReadyCounters();
}

// keyword: EventGameLoading
void CGame::EventGameStartedLoading()
{
  m_StartedLoadingTicks = m_Aura->GetClockTicks();
  m_LastLagScreenResetTime = m_Aura->GetClockTime();

  // Remove the virtual host user to ensure consistent game state and networking.
  DeleteVirtualHost();

  if (m_IsHiddenPlayerNames && m_Config.m_HideInGameNames != HideIGNMode::kAlways) {
    ShowPlayerNamesGameStartLoading();
  }

  ResolveVirtualUsers();
  RunHCLEncoding();

  //if (GetNumJoinedUsersOrFake() < 2) {
    // This is a single-user game. Neither chat events nor bot commands will work.
    // Keeping the virtual host does no good - The game client then refuses to remain in the game.
  //}

  // send a final slot info update for HCL, or in case there are pending updates
  if (m_SlotInfoChanged != 0) {
    SendAllSlotInfo();
    UpdateReadyCounters();
  }

  for (const auto& user : m_Users) {
    const uint8_t SID = GetSIDFromUID(user->GetUID());
    user->SetSID(SID);
    user->SetChatChannel(CHAT_RECV_PRIVATE_OFFSET + m_SlotsConfig.Inspect(SID).GetColor());
  }

  m_ReconnectProtocols = CalcActiveReconnectProtocols();
  if (m_GProxyEmptyActions > 0 && m_ReconnectProtocols == RECONNECT_ENABLED_GPROXY_EXTENDED) {
    m_GProxyEmptyActions = 0;
    for (const auto& user : m_Users) {
      if (user->GetCanReconnect()) {
        user->GetGProxy()->UpdateEmptyActions(0);
      }
    }
  }
  for (const auto& user : m_Users) {
    user->GetGProxy()->EventGameStart();
  }

  ResolveBuffering();

  if (m_Map->GetGameObservers() != GameObserversMode::kReferees) {
    for (auto& user : m_Users) {
      if (user->GetIsObserver()) {
        // Full observers cannot pause nor save a WC3 game.
        user->SetCannotPause();
        user->SetCannotSave();
      }
    }
  }

  if (!m_Config.m_SaveGameAllowed) {
    for (auto& user : m_Users) {
      user->SetCannotSave();
    }
    for (auto& fakeUser : m_FakeUsers) {
      fakeUser.SetCannotSave();
    }
  }

  if (!m_Config.m_PauseGameAllowed) {
    for (auto& user : m_Users) {
      user->SetCannotPause();
    }
    for (auto& fakeUser : m_FakeUsers) {
      fakeUser.SetCannotPause();
    }
  }

  for (auto& user : m_Users) {
    user->SetStatus(USERSTATUS_LOADING_SCREEN);
    user->SetWhoisShouldBeSent(false);
    user->UnMute();
    if (!user->GetHasAPMQuota() && m_Config.m_MaxAPM.has_value()) {
      user->RestrictAPM(m_Config.m_MaxAPM.value(), m_Config.m_MaxBurstAPM.value_or(APM_RATE_LIMITER_BURST_ACTIONS));
    }
    if (user->GetHasAPMQuota()) {
      user->GetAPMQuota().PauseRefillUntil(user->GetHandicapTicks());
    }
  }

  for (auto& user : m_Users) {
    UserList otherPlayers;
    for (auto& otherPlayer : m_Users) {
      if (otherPlayer != user) {
        otherPlayers.push_back(otherPlayer);
      }
    }
    m_SyncPlayers[user] = otherPlayers;
  }

  m_ChatEnabled = m_Config.m_EnableInGameChat;
  m_APMTrainerPaused = m_Map->GetMapType() == "microtraining";
  m_GameLoading = true;

  // since we use a fake countdown to deal with leavers during countdown the COUNTDOWN_START and COUNTDOWN_END packets are sent in quick succession
  // send a start countdown packet

  {
    vector<uint8_t> packet = GameProtocol::SEND_W3GS_COUNTDOWN_START();
    SendAll(packet);
  }

  // send an end countdown packet

  {
    vector<uint8_t> packet = GameProtocol::SEND_W3GS_COUNTDOWN_END();
    SendAll(packet);
  }

  // record the starting users
  // fake observers are counted, this is a feature to prevent premature game ending
  m_StartPlayers = GetNumJoinedPlayersOrFakeUsers() - m_JoinedVirtualHosts;
  LOG_APP_IF(LogLevel::kInfo, Concat("started loading: ", ToDecString(GetNumJoinedPlayers()), " p | ", ToDecString(GetNumComputers()), " comp | ", ToDecString(GetNumJoinedObservers()), " obs | ", to_string(m_FakeUsers.size() - m_JoinedVirtualHosts), " fake | ", ToDecString(m_JoinedVirtualHosts), " vhost | ", ToDecString(m_ControllersWithMap), " controllers"));

  if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
    AppendContainer(m_GameHistory->m_PlayersBuffer, GetFakeUsersLoadedInfo());
    AppendContainer(m_GameHistory->m_PlayersBuffer, GetJoinedPlayersInfo());
  }

  // When load-in-game is disabled, m_LoadingVirtualBuffer also includes
  // load messages for disconnected real players, but we let automatic resizing handle that.

  m_GameHistory->m_LoadingVirtualBuffer.reserve(5 * m_FakeUsers.size());
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    // send a game loaded packet for each fake user
    AppendContainer(m_GameHistory->m_LoadingVirtualBuffer, fakeUser.GetGameLoadedBytes());
  }

  if (GetAnyUsingGProxy()) {
    // Always send an empty action.
    // This ensures GProxy clients are correctly initialized, and 
    // keeps complexity in check. It's just 6 bytes, too...
    //
    // NOTE: It's specially important when load-in-game is enabled.

    ++m_BeforePlayingEmptyActions;
  }

  m_Actions.emplaceBack();
  m_CurrentActionsFrame = m_Actions.head;
  ResetUserPingEqualizerDelays();

  // enable stats

  if (m_Rated) {
    m_Rated = GetIsAPrioriCompatibleWithGameResultsConstraints(m_UnratedReason);
  }

  StoreGameControllers();
  InitStats();
  InitHMC();

  m_ReconnectProtocols = CalcActiveReconnectProtocols();

  // release map data from memory
  ClearLoadedMapChunk();
  //m_Map->ClearMapFileContents();

  if (m_BufferingEnabled & BUFFERING_ENABLED_LOADING) {
    // Preallocate memory for all SEND_W3GS_GAMELOADED_OTHERS packets
    m_GameHistory->m_LoadingRealBuffer.reserve(5 * m_Users.size());
  }

  if (m_Config.m_LoadInGame) {
    for (const auto& user : m_Users) {
      vector<uint8_t> packet = GameProtocol::SEND_W3GS_GAMELOADED_OTHERS(user->GetUID());
      AppendContainer(m_GameHistory->m_LoadingRealBuffer, packet);
    }

    // Only when load-in-game is enabled, initialize everyone's m_IsLagging flag to true
    // this ensures CGame::UpdateLoaded() will send W3GS_STOP_LAG messages only when appropriate.
    SetEveryoneLagging();
  }

  m_VersionErrors.clear();

  if (m_GameDiscoveryActive) {
    SendGameDiscoveryDecreate();
    m_GameDiscoveryActive = false;
  }

  // and finally reenter battle.net chat
  AnnounceDecreateToRealms();

  ClearBannableUsers();
  UpdateBannableUsers();
}

void CGame::AddProvisionalBannableUser(const GameUser::CGameUser* user)
{
  const bool isOversized = m_Bannables.size() > GAME_BANNABLE_MAX_HISTORY_SIZE;
  bool matchedSameName = false, matchedShrink = false;
  size_t matchIndex = 0, shrinkIndex = 0;
  while (matchIndex < m_Bannables.size()) {
    if (user->GetName() == m_Bannables[matchIndex]->GetName()) {
      matchedSameName = true;
      break;
    }
    if (isOversized && !matchedShrink && GetUserFromName(m_Bannables[matchIndex]->GetName(), true) == nullptr) {
      shrinkIndex = matchIndex;
      matchedShrink = true;
    }
    matchIndex++;
  }

  if (matchedSameName) {
    delete m_Bannables[matchIndex];
  } else if (matchedShrink) {
    delete m_Bannables[shrinkIndex];
    m_Bannables.erase(m_Bannables.begin() + signed_cast<ptrdiff_t>(shrinkIndex));
  }

  CDBBan* bannable = new CDBBan(
    user->GetName(),
    user->GetRealmDataBaseID(false),
    string(), // auth server
    user->GetIPStringStrict(),
    string(), // date
    string(), // expiry
    false, // temporary ban (permanent == false)
    string(), // moderator
    string() // reason
  );

  if (matchedSameName) {
    m_Bannables[matchIndex] = bannable;
  } else {
    m_Bannables.push_back(bannable);
  }

  m_LastLeaverBannable = bannable;
}

void CGame::ClearBannableUsers()
{
  for (auto& bannable : m_Bannables) {
    delete bannable;
  }
  m_Bannables.clear();
  m_LastLeaverBannable = nullptr;
}

void CGame::UpdateBannableUsers()
{
  // record everything we need to ban each user in case we decide to do so later
  // this is because when a user leaves the game an admin might want to ban that user
  // but since the user has already left the game we don't have access to their information anymore
  // so we create a "potential ban" for each user and only store it in the database if requested to by an admin

  for (auto& user : m_Users) {
    m_Bannables.push_back(new CDBBan(
      user->GetName(),
      user->GetRealmDataBaseID(false),
      string(), // auth server
      user->GetIPStringStrict(),
      string(), // date
      string(), // expiry
      false, // temporary ban (permanent == false)
      string(), // moderator
      string() // reason
    ));
  }
}

void CGame::UpdateUserMapProgression(GameUser::CGameUser* user, const double current, const double expected)
{
  uint8_t newDownloadStatus = 100;
  if (current <= expected) {
    newDownloadStatus = static_cast<uint8_t>(static_cast<uint32_t>(PERCENT_FACTOR * current / expected));
  }

  if (user->GetMapTransfer().GetStatus() == newDownloadStatus) {
    return;
  }

  user->GetMapTransfer().SetStatus(newDownloadStatus);

  CGameSlot* slot = GetSlot(GetSIDFromUID(user->GetUID()));
  if (slot) {
    // only send the slot info if the download status changed
    slot->SetDownloadStatus(newDownloadStatus);

    // we don't actually send the new slot info here
    // this is an optimization because it's possible for a user to download a map very quickly
    // if we send a new slot update for every percentage change in their download status it adds up to a lot of data
    // instead, we mark the slot info as "out of date" and update it only once in awhile
    // (once per second when this comment was made)

    m_SlotInfoChanged |= (SLOTS_DOWNLOAD_PROGRESS_CHANGED);
  }
}

void CGame::UpdateUserMapProgression(CAsyncObserver* user, const double current, const double expected)
{
  uint8_t newDownloadStatus = 100;
  if (current <= expected) {
    newDownloadStatus = static_cast<uint8_t>(static_cast<uint32_t>(PERCENT_FACTOR * current / expected));
  }

  // Note: Updates for every 1% progression translate to a ~10 KB overhead.
  if (newDownloadStatus == user->GetMapTransfer().GetStatus()) {
    return;
  }

  user->GetMapTransfer().SetStatus(newDownloadStatus);
  user->UpdateDownloadProgression(newDownloadStatus);
}

bool CGame::ResolvePlayerObfuscation() const
{
  if (m_Config.m_HideInGameNames == HideIGNMode::kAlways || m_Config.m_HideInGameNames == HideIGNMode::kHost) {
    return true;
  }
  if (m_Config.m_HideInGameNames == HideIGNMode::kNever) {
    return false;
  }

  if (m_ControllersWithMap < 3) {
    return false;
  }

  unordered_set<uint8_t> activeTeams = {};
  for (const auto& slot : m_SlotsConfig.slots) {
    if (slot.GetTeam() == GetObserverTeam()) {
      continue;
    }
    if (activeTeams.find(slot.GetTeam()) != activeTeams.end()) {
      return false;
    }
    activeTeams.insert(slot.GetTeam());
  }

  return true;
}

void CGame::RunPlayerObfuscation()
{
  m_IsHiddenPlayerNames = ResolvePlayerObfuscation();

  if (m_IsHiddenPlayerNames) {
    vector<uint8_t> pseudonymUIDs = vector<uint8_t>(GetPlayers().size());
    uint8_t i = static_cast<uint8_t>(pseudonymUIDs.size());
    while (i--) {
      pseudonymUIDs[i] = i;
    }
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(begin(pseudonymUIDs), end(pseudonymUIDs), gen);

    i = 0;
    for (auto& player : m_Users) {
      if (player->GetIsObserver() || player->GetLeftMessageSent()) {
        continue;
      }
      player->SetPseudonymUID(pseudonymUIDs[i++]);
    }
  }
}

bool CGame::CheckSmartCommands(GameUser::CGameUser* user, string_view message, const uint8_t activeCmd, CCommandConfig* commandCFG)
{
  if (message.length() >= 2) {
    string_view prefixSensitive = message.substr(0, 2);
    string prefix = ToLowerCase(string(prefixSensitive));
    if (prefix[0] == 'g' && prefix[1] == 'o' && prefixSensitive.find_first_not_of("goGO") == string_view::npos && !HasOwnerInGame()) {
      if (activeCmd == SMART_COMMAND_GO) {
        shared_ptr<CCommandContext> ctx = nullptr;
        try {
          ctx = make_shared<CCommandContext>(ServiceType::kLAN /* or realm, actually*/, m_Aura, commandCFG, shared_from_this(), user, false, &std::cout);
        } catch (...) {
          return true;
        }
        string cmdToken = m_Config.m_PrivateCmdToken;
        string command = "start";
        string target;
        ctx->Run(cmdToken, command, target);
      } else {
        user->GetCommandHistory()->SetSmartCommand(SMART_COMMAND_GO);
        SendChat(user, Concat("You may type [", message, "] again to start the game."));
      }
      return true;
    }
  }
  return false;
}

void CGame::EventGameBeforeLoaded()
{
  if (!m_Config.m_LoadInGame && !m_GameHistory->m_LoadingVirtualBuffer.empty()) {
    // CGame::UpdateLoading: Fake users loaded
    if (m_GameHistory->m_LoadingVirtualBuffer.size() == 5 * m_FakeUsers.size()) {
      SendAll(m_GameHistory->m_LoadingVirtualBuffer);
    } else {
      // Cannot just send the whole m_LoadingVirtualBuffer, because, when load-in-game is disabled,
      // it will also contain load packets for real users who didn't actually load the game,
      // but these packets were already sent to real users
      vector<uint8_t> onlyFakeUsersLoaded = vector<uint8_t>(
        m_GameHistory->m_LoadingVirtualBuffer.begin(),
        m_GameHistory->m_LoadingVirtualBuffer.begin() + signed_cast<ptrdiff_t>(5u * m_FakeUsers.size())
      );
      SendAll(onlyFakeUsersLoaded);
    }
  }
}

void CGame::EventGameLoaded()
{
  m_LastActionExpectedTicks = GetTicks(); // high-res ticks for actions scheduler
  m_FinishedLoadingTicks = m_Aura->GetClockTicks();
  m_MapGameStartTime = CGameInteractiveHost::GetMapTime();
  m_GameLoading = false;
  m_GameLoaded = true;

  RunPlayerObfuscation();

  LOG_APP_IF(LogLevel::kInfo, Concat("finished loading: ", ToDecString(GetNumJoinedPlayers()), " p | ", ToDecString(GetNumComputers()), " comp | ", ToDecString(GetNumJoinedObservers()), " obs | ", to_string(m_FakeUsers.size() - m_JoinedVirtualHosts), " fake | ", ToDecString(m_JoinedVirtualHosts), " vhost"));

  m_IsSinglePlayer = GetIsSinglePlayerMode();

  if (!m_Users.empty()) {
    if (GetArePlayersSameVersion()) {
      m_LoadedVersion = m_Users[0]->GetGameVersion();
    }
    if (GetArePlayersSameSlotsProtocol()) {
      m_LoadedSlotsProtocol = GetSlotsProtocolVersion(m_Users[0]->GetGameVersion());
    }
  }

  // send shortest, longest, and personal load times to each user

  const GameUser::CGameUser* Shortest = nullptr;
  const GameUser::CGameUser* Longest  = nullptr;

  size_t majorityThreshold = m_Users.size() / 2;
  ImmutableUserList DesyncedPlayers;
  if (m_Users.size() >= 2) {
    for (const auto& user : m_Users) {
      if (user->GetFinishedLoading()) {
        if (!Shortest || user->GetFinishedLoadingTicks() < Shortest->GetFinishedLoadingTicks()) {
          Shortest = user;
        } else if (Shortest && (!Longest || user->GetFinishedLoadingTicks() > Longest->GetFinishedLoadingTicks())) {
          Longest = user;
        }
      }

      if (m_SyncPlayers[user].size() < majorityThreshold) {
        DesyncedPlayers.push_back(user);
      }
    }
  }

  for (const auto& user : m_Users) {
    user->SetStatus(USERSTATUS_PLAYING);
    if (user->GetIsNativeReferee()) {
      // Natively, referees get unlimited saves. But we limit them to 3 in multiplayer games.
      user->SetRemainingSaves(m_Users.size() >= 2 ? GAME_SAVES_PER_REFEREE_ANTIABUSE : GAME_SAVES_PER_REFEREE_DEFAULT);
      // Similarly, limit pauses to 7 in multiplayer games.
      user->SetRemainingPauses(m_Users.size() >= 2 ? GAME_PAUSES_PER_REFEREE_ANTIABUSE : GAME_PAUSES_PER_REFEREE_DEFAULT);
    }
  }

  ImmutableUserList players = GetPlayers();
  if (players.size() <= 2) {
    m_PlayedBy = ToNameListSentence(players, true);
  } else {
    m_PlayedBy = Concat(players[0]->GetName(), ", and others");
  }

  if (Shortest && Longest) {
    SendAllChat(Concat("Shortest load by user [", Shortest->GetDisplayName(), "] was ", ToFormattedString(static_cast<double>(Shortest->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f), " seconds"));
    SendAllChat(Concat("Longest load by user [", Longest->GetDisplayName(), "] was ", ToFormattedString(static_cast<double>(Longest->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f), " seconds"));
  }
  const uint32_t numDisconnectedPlayers = integer_cast<uint32_t>(m_StartPlayers) + integer_cast<uint32_t>(m_JoinedVirtualHosts) - integer_cast<uint32_t>(GetNumJoinedPlayersOrFakeUsers());
  if (0 < numDisconnectedPlayers) {
    SendAllChat(Concat(to_string(numDisconnectedPlayers), " user(s) disconnected during game load."));
    LogRemote(Concat("Fully loaded. ", to_string(players.size()), " players - ", to_string(numDisconnectedPlayers), " user(s) disconnected"));
  } else {
    LogRemote(Concat("Fully loaded. ", to_string(players.size()), " players"));
  }
  if (!DesyncedPlayers.empty()) {
    if (GetHasDesyncHandler()) {
      SendAllChat(Concat("Some users desynchronized during game load: ", ToNameListSentence(DesyncedPlayers)));
      LogRemote(Concat("Some users desynchronized during game load: ", ToNameListSentence(DesyncedPlayers)));
      if (!GetAllowsDesync()) {
        StopDesynchronized("was automatically dropped after desync");
      }
    }
  }

  for (auto& user : m_Users) {
    if (user->GetFinishedLoading()) {
      SendChat(user, Concat("Your load time was ", ToFormattedString(static_cast<double>(user->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f), " seconds"));
    }
  }

  if (!m_Rated) {
    if (m_UnratedReason.empty()) {
      //SendAllChat("This game is unrated");
    } else {
      //SendAllChat("This game is unrated because ", m_UnratedReason);
      m_UnratedReason.clear();
    }
  }

  // GProxy hangs trying to reconnect
  if (m_IsSinglePlayer && !GetAnyUsingGProxy()) {
    SendAllChat("HINT: Single-user game detected. In-game commands will be DISABLED.");
    // FIXME? This creates a large lag spike client-side.
    // Tested at 793b88d5 (2024-09-07): caused the WC3 client to straight up quit the game.
    // Tested at e6fd6133 (2024-09-25): correctly untracks wormwar.ini (yet lags), correctly untracks lastrefugeamai.ini --observers=no
    SendEveryoneElseLeftAndDisconnect("single-player game untracked");
  } else if (TrySendFakeUsersShareControl()) {
    SendAllChat("Virtual players will share unit control with their allies");
  }

  if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
    m_GameHistory->SetDefaultLatency(m_LatencyTicks);
    m_GameHistory->SetActiveLatency(m_LatencyTicks);
    m_GameHistory->SetSpectatorActiveLatency(m_LatencyTicks);
    m_GameHistory->SetGProxyEmptyActions(m_GProxyEmptyActions);
    m_GameHistory->SetStartedTicks(m_FinishedLoadingTicks);
  } else {
    // These buffers serve no purpose anymore.
    m_GameHistory->m_LoadingRealBuffer = vector<uint8_t>();
    m_GameHistory->m_LoadingVirtualBuffer = vector<uint8_t>();
  }

  // move the game to the games in progress vector
  if (m_Config.m_EnableJoinObserversInProgress || m_Config.m_EnableJoinPlayersInProgress) {
    m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_MAJOR;
    ChangeGameName(GetGameSpectatorName());
    m_EntryKey = GetRandomUInt32();
    m_HostCounter = m_Aura->NextHostCounter();
    m_Aura->TrackGameJoinInProgress(shared_from_this());

    if (GetUDPEnabled() && !m_GameDiscoveryActive) {
      SendGameDiscoveryCreate();
      m_GameDiscoveryActive = true;
    }

    // TODO: Broadcast watchable game to PvPGN realms
  }

  HandleGameLoadedStats();
}

void CGame::HandleGameLoadedStats()
{
  if (!m_Config.m_SaveStats) {
    return;
  }
  vector<string> exportPlayerNames;
  vector<uint8_t> exportPlayerIDs;
  vector<uint8_t> exportSlotIDs;
  vector<uint8_t> exportColorIDs;

  for (uint8_t SID = 0; SID < GetNumSlots(); ++SID) {
    const CGameSlot* slot = InspectSlot(SID);
    if (!slot->GetIsPlayerOrFake()) {
      continue;
    }
    const GameUser::CGameUser* user = GetUserFromSID(SID);
    exportSlotIDs.push_back(SID);
    exportColorIDs.push_back(slot->GetColor());
    if (user == nullptr) {
      const CGameVirtualUser* virtualUserMatch = InspectVirtualUserFromSID(SID);
      if (virtualUserMatch) {
        exportPlayerNames.push_back(string());
        exportPlayerIDs.push_back(virtualUserMatch->GetUID());
      }
    } else {
      exportPlayerNames.push_back(user->GetName());
      exportPlayerIDs.push_back(user->GetUID());
    }
  }

  const int64_t hiResTicks = GetTicks();
  if (!m_Aura->m_DB->Begin()) {
    LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to begin transaction for game loaded data");
    return;
  }
  m_Aura->m_DB->UpdateLatestHistoryGameId(m_PersistentId);

  string mapClientPath(EnsureUTF8(m_Map->GetClientPath()));
  string mapServerPath = SanitizeUTF8Path(m_Map->GetServerPath());

  m_Aura->m_DB->GameAdd(
    m_PersistentId,
    m_CreatorText,
    mapClientPath,
    mapServerPath,
    m_Map->GetMapCRC32(),
    exportPlayerNames,
    exportPlayerIDs,
    exportSlotIDs,
    exportColorIDs
  );

  for (auto& controllerData : m_GameControllers) {
    m_Aura->m_DB->UpdateGamePlayerOnStart(m_PersistentId, controllerData);
  }
  if (!m_Aura->m_DB->Commit()) {
    LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to commit transaction for game loaded data");
  } else {
    LOG_APP_IF(LogLevel::kDebug, Concat("[STATS] commited game loaded data in ", to_string(GetTicks() - hiResTicks), " ms"));
  }
}


RemakeCheckResult CGame::CheckRemakeable() const
{
  if (!m_Map) return RemakeCheckResult::kMapUnknown;
  if (m_RestoredGame) return RemakeCheckResult::kLoadedGame;
  if (m_FromAutoReHost) return RemakeCheckResult::kAutoReHosted;
  if (m_JoinInProgressVirtualUser.has_value()) return RemakeCheckResult::kSpectators;
  return RemakeCheckResult::kOk;
}

bool CGame::GetIsRemakeable() const
{
  RemakeCheckResult checkResult = CheckRemakeable();
  return checkResult == RemakeCheckResult::kOk;
}

void CGame::RemakeStart()
{
  m_Config.m_SaveStats = false;
  m_Remaking = true;
  m_Remade = false;
  m_LobbyLoading = true;
}

void CGame::Remake()
{
  m_Aura->EventGameReset(shared_from_this());
  Reset();

  const int64_t loopTime = m_Aura->GetClockTime();
  const int64_t loopTicks = m_Aura->GetClockTicks();

  m_FromAutoReHost = false;
  m_EffectiveTicks = 0;
  m_CreationTime = loopTime;
  m_LastPingTicks = loopTicks;
  m_LastDiscoveryTicks = loopTicks;
  m_LastRefreshTime = loopTime;
  m_LastDownloadCounterResetTicks = loopTicks;
  m_LastCountDownTicks = APP_MIN_TICKS;
  m_StartedLoadingTicks = 0;
  m_FinishedLoadingTicks = 0;
  m_MapGameStartTime = 0;
  m_LastActionSentTicks = 0;
  m_LastActionExpectedTicks = 0;
  m_LastPausedTicks = 0;
  m_PausedTicksDeltaSum = 0;
  m_StartedLaggingTime = 0;
  m_LastLagScreenTime = 0;
  m_PingReportedSinceLagTimes = 0;
  m_LastUserSeenTicks = loopTicks;
  m_LastOwnerSeenTicks = loopTicks;
  m_StartedKickVoteTime = 0;
  m_LastStatsUpdateTime = 0;
  m_GameOver = GAME_ONGOING;
  m_GameOverTime = nullopt;
  m_LastPlayerLeaveTicks = nullopt;
  m_LastLagScreenResetTime = 0;
  m_SyncCounter = 0;
  m_SyncCounterChecked = 0;
  m_PingEqualizerMaxFrames = 0;
  m_PingEqualizerActiveDelayFrames = 0;
  m_LastPingEqualizerGameTicks = 0;

  m_CountDownCounter = 0;
  m_StartPlayers = 0;
  m_ControllersBalanced = false;
  m_ControllersReadyCount = 0;
  m_ControllersNotReadyCount = 0;
  m_ControllersWithMap = 0;
  m_AutoStartRequirements.clear();
  m_CustomLayout = 0;

  m_IsAutoVirtualPlayers = false;
  m_HMCVirtualUser.reset();
  m_AHCLVirtualUser.reset();
  m_InertVirtualUser.reset();
  m_JoinInProgressVirtualUser.reset();
  m_VirtualHostUID = 0xFF;
  m_GProxyEmptyActions = (
    m_Aura->m_Net.m_Config.m_ReconnectWaitTicksLegacy > 0 ?
    signed_cast_lossy<uint8_t>(m_Aura->m_Net.m_Config.m_ReconnectWaitTicksLegacy / 60000 - 1) :
    0
  );
  m_ExitingSoon = false;
  m_SlotInfoChanged = SLOTS_UNCHANGED;
  m_JoinedVirtualHosts = 0;
  m_ReconnectProtocols = 0;
  //m_Replaceable = false;
  //m_Replacing = false;
  //m_PublicStart = false;
  m_Locked = false;
  m_CountDownStarted = false;
  m_CountDownFast = false;
  m_CountDownUserInitiated = false;
  m_GameLoading = false;
  m_GameLoaded = false;
  m_IsLagging = false;
  m_IsDraftMode = false;
  m_IsHiddenPlayerNames = false;
  m_HadLeaver = false;
  m_UsesCustomReferees = false;
  m_SentPriorityWhois = false;
  m_Remaking = false;
  m_Remade = true;
  m_LoadedVersion.reset();
  m_LoadedSlotsProtocol.reset();
  m_IsSinglePlayer = false;
  m_Rated = false;
  m_HMCEnabled = false;
  m_BufferingEnabled = BUFFERING_ENABLED_NONE;
  m_BeforePlayingEmptyActions = 0;
  m_APMTrainerPaused = false;
  m_APMTrainerTicks = 0;
  m_GameHistory = make_shared<GameHistory>();
  m_GameResultsSource = GameResultSource::kNone;
  m_GameDiscoveryInfoChanged = GAME_DISCOVERY_CHANGED_NEW;

  NextCreationCounter();
  m_HostCounter = m_Aura->NextHostCounter();
  m_RandomSeed = GetRandomUInt32();
  m_EntryKey = GetRandomUInt32();
  m_ChatEnabled = m_Config.m_EnableLobbyChat;
  InitSlots();

  m_KickVotePlayer.clear();

  m_LobbyLoading = false;
  LOG_APP_IF(LogLevel::kInfo, "finished loading after remake");
  CreateVirtualHost();
}

uint8_t CGame::GetSIDFromUID(uint8_t UID) const
{
  if (m_SlotsConfig.GetCount() > 0xFF)
    return 0xFF;

  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (m_SlotsConfig.Inspect(i).GetUID() == UID)
      return i;
  }

  return 0xFF;
}

GameUser::CGameUser* CGame::GetUserFromUID(uint8_t UID) const
{
  for (auto& user : m_Users) {
    if (!user->GetLeftMessageSent() && user->GetUID() == UID)
      return user;
  }

  return nullptr;
}

GameUser::CGameUser* CGame::GetUserFromSID(uint8_t SID) const
{
  if (SID >= GetNumSlots())
    return nullptr;

  const uint8_t UID = m_SlotsConfig.Inspect(SID).GetUID();

  for (auto& user : m_Users)
  {
    if (!user->GetLeftMessageSent() && user->GetUID() == UID)
      return user;
  }

  return nullptr;
}

string CGame::GetUserNameFromUID(uint8_t UID) const
{
  for (auto& user : m_Users) {
    if (!user->GetLeftMessageSent() && user->GetUID() == UID)
      return user->GetName();
  }

  return string();
}

string CGame::GetUserNameFromSID(uint8_t SID) const
{
  GameUser::CGameUser* user = GetUserFromSID(SID);
  if (user) {
    return user->GetName();
  }
  return Concat("Slot ", ToDecString(ToBaseOne(SID)));
}

GameUser::CGameUser* CGame::GetOwner() const
{
  if (!HasOwnerSet()) return nullptr;
  GameUser::CGameUser* maybeOwner = GetUserFromName(m_OwnerName, false);
  if (!maybeOwner || !maybeOwner->GetIsOwner(nullopt)) return nullptr;
  return maybeOwner;
}

bool CGame::HasOwnerSet() const
{
  return !m_OwnerName.empty();
}

bool CGame::HasOwnerInGame() const
{
  if (!HasOwnerSet()) return false;
  GameUser::CGameUser* maybeOwner = GetUserFromName(m_OwnerName, false);
  if (!maybeOwner) return false;
  return maybeOwner->GetIsOwner(nullopt);
}

GameUser::CGameUser* CGame::GetUserFromName(string name, bool sensitive) const
{
  if (!sensitive) {
    transform(begin(name), end(name), begin(name), [](char c) { return static_cast<char>(std::tolower(c)); });
  }

  for (auto& user : m_Users) {
    if (!user->GetDeleteMe()) {
      string testName = sensitive ? user->GetName() : user->GetLowerName();
      if (testName == name) {
        return user;
      }
    }
  }

  return nullptr;
}

template <CaseSensitive sensitive>
GameUser::CGameUser* CGame::GetUserFromName(string_view name) const
{
  string tmp;
  if constexpr (sensitive == CaseSensitive::kNormalize) {
    tmp = name;
    tmp = ToLowerCase(tmp);
    name = tmp;
  }

  for (auto& user : m_Users) {
    if (!user->GetDeleteMe()) {
      if constexpr (sensitive == CaseSensitive::kNormalize) {
        if (user->GetLowerName() == name) return user;
      } else {
        if (user->GetName() == name) return user;
      }
    }
  }

  return nullptr;
}

template GameUser::CGameUser* CGame::GetUserFromName<CaseSensitive::kStrict>(string_view name) const;
template GameUser::CGameUser* CGame::GetUserFromName<CaseSensitive::kNormalize>(string_view name) const;

GameUserSearchResult CGame::GetUserFromNamePartial(const string& name) const
{
  GameUserSearchResult result;
  if (name.empty()) {
    return result;
  }

  string inputLower = ToLowerCase(name);

  // try to match each user with the passed string (e.g. "Varlock" would be matched with "lock")

  for (auto& user : m_Users) {
    if (!user->GetDeleteMe()) {
      string testName = user->GetLowerName();
      if (testName.find(inputLower) != string::npos) {
        ++result.matchCount;
        result.user = user;

        // if the name matches exactly stop any further matching

        if (testName == inputLower) {
          result.matchCount = 1;
          break;
        }
      }
    }
  }

  if (result.matchCount != 1) {
    result.user = nullptr;
  }
  return result;
}

GameUserSearchResult CGame::GetUserFromDisplayNamePartial(const string& name) const
{
  GameUserSearchResult result;
  if (name.empty()) {
    return result;
  }

  string inputLower = ToLowerCase(name);

  // try to match each user with the passed string (e.g. "Varlock" would be matched with "lock")

  for (auto& user : m_Users) {
    if (!user->GetDeleteMe()) {
      string testName = ToLowerCase(user->GetDisplayName());
      if (testName.find(inputLower) != string::npos) {
        ++result.matchCount;
        result.user = user;

        // if the name matches exactly stop any further matching

        if (testName == inputLower) {
          result.matchCount = 1;
          break;
        }
      }
    }
  }

  if (result.matchCount != 1) {
    result.user = nullptr;
  }
  return result;
}

BannableUserSearchResult CGame::GetBannableFromNamePartial(const string& name) const
{
  BannableUserSearchResult result;

  if (name.empty()) {
    return result;
  }

  string inputLower = ToLowerCase(name);

  // try to match each user with the passed string (e.g. "Varlock" would be matched with "lock")

  for (auto& bannable : m_Bannables) {
    string testName = ToLowerCase(bannable->GetName());

    if (testName.find(inputLower) != string::npos) {
      ++result.matchCount;
      result.bannable = bannable;

      // if the name matches exactly stop any further matching

      if (testName == inputLower) {
        result.matchCount = 1;
        break;
      }
    }
  }

  if (result.matchCount != 1) {
    result.bannable = nullptr;
  }
  return result;
}

GameUser::CGameUser* CGame::GetUserFromColor(uint8_t color) const
{
  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i)
  {
    if (m_SlotsConfig.Inspect(i).GetColor() == color)
      return GetUserFromSID(i);
  }

  return nullptr;
}

uint8_t CGame::GetColorFromUID(uint8_t UID) const
{
  const CGameSlot* slot = InspectSlot(GetSIDFromUID(UID));
  if (!slot) return 0xFF;
  return slot->GetColor();
}

uint8_t CGame::GetNewUID() const
{
  // find an unused UID for a new user to use

  for (uint8_t TestUID = 1; TestUID < 0xFF; ++TestUID)
  {
    if (TestUID == m_VirtualHostUID)
      continue;

    bool inUse = false;
    for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
      if (fakeUser.GetUID() == TestUID) {
        inUse = true;
        break;
      }
    }
    if (inUse) {
      continue;
    }
    for (auto& user : m_Users) {
      if (!user->GetLeftMessageSent() && (user->GetUID() == TestUID || user->GetOldUID() == TestUID)) {
        inUse = true;
        break;
      }
    }
    if (!inUse) {
      return TestUID;
    }
  }

  // this should never happen

  return 0xFF;
}

uint8_t CGame::GetNewPseudonymUID() const
{
  // find an unused Pseudonym UID

  bool inUse = false;
  for (uint8_t TestUID = 1; TestUID < 0xFF; ++TestUID) {
    for (auto& user : m_Users) {
      if (!user->GetLeftMessageSent() && user->GetPseudonymUID() == TestUID) {
        inUse = true;
        break;
      }
    }
    if (!inUse) {
      return TestUID;
    }
  }

  // this should never happen

  return 0xFF;
}

uint8_t CGame::GetNewTeam() const
{
  bitset<MAX_SLOTS_MODERN> usedTeams;
  for (auto& slot : m_SlotsConfig.slots) {
    if (slot.GetColor() == GetObserverColor()) continue;
    if (slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED) continue;
    usedTeams.set(slot.GetTeam());
  }
  const uint8_t endTeam = m_Map->GetMapNumTeams();
  for (uint8_t team = 0; team < endTeam; ++team) {
    if (!usedTeams.test(team)) {
      return team;
    }
  }
  return GetObserverTeam();
}

uint8_t CGame::GetNewColor() const
{
  uint8_t minControllerInvalidColor = GetMinControllerInvalidColor();
  bitset<MAX_SLOTS_MODERN> usedColors;
  for (auto& slot : m_SlotsConfig.slots) {
    if (slot.GetColor() == GetObserverColor()) continue;
    usedColors.set(slot.GetColor());
  }
  for (uint8_t color = 0; color < minControllerInvalidColor; ++color) {
    if (!usedColors.test(color)) {
      return color;
    }
  }
  return GetObserverColor(); // should never happen
}

bool CGame::CheckActorRequirements(const GameUser::CGameUser* user, const uint8_t actorMask) const
{
  if (!user->GetIsObserver()) return (actorMask & ACTION_SOURCE_PLAYER) > 0;
  if (m_Map->GetGameObservers() == GameObserversMode::kReferees) return (actorMask & ACTION_SOURCE_REFEREE) > 0;
  return (actorMask & ACTION_SOURCE_OBSERVER) > 0;
}

uint8_t CGame::SimulateActionUID(const uint8_t actionType, GameUser::CGameUser* user, const bool isDisconnect, uint8_t actorMask)
{
  // Note that the game client desyncs if the UID of an actual user is used.
  switch (actionType) {
    case ACTION_PAUSE: {
      if (isDisconnect && CheckActorRequirements(user, actorMask) && user->GetCanPause()) {
        return user->GetUID();
      }
      
      for (CGameVirtualUser& fakeUser : m_FakeUsers) {
        if (fakeUser.GetCanPause()) {
          // Referees could get unlimited pauses, but that's abusable, so we limit them just like regular players.
          fakeUser.DropRemainingPauses();
          return fakeUser.GetUID();
        }
      }
      return 0xFF;
    }
    case ACTION_RESUME: {
      if (isDisconnect && CheckActorRequirements(user, actorMask)) {
        return user->GetUID();
      }

      for (CGameVirtualUser& fakeUser : m_FakeUsers) {
        if (fakeUser.GetCanResume()) {
          return fakeUser.GetUID();
        }
      }

      return 0xFF;
    }

    case ACTION_SAVE: {
      if (isDisconnect && CheckActorRequirements(user, actorMask) && user->GetCanSave()) {
        return user->GetUID();
      }
      for (CGameVirtualUser& fakeUser : m_FakeUsers) {
        if (fakeUser.GetCanSave()) {
          // Referees could get unlimited saves, but that's abusable, so we limit them just like regular players.
          fakeUser.DropRemainingSaves();
          return fakeUser.GetUID();
        }
      }
      return 0xFF;
    }

    case ACTION_MINIMAPSIGNAL: {
      if (isDisconnect && CheckActorRequirements(user, actorMask)) {
        return user->GetUID();
      }
      for (CGameVirtualUser& fakeUser : m_FakeUsers) {
        if (!fakeUser.GetCanMiniMapSignal(user)) continue;
        if (!fakeUser.GetIsObserver()) {
          if ((actorMask & ACTION_SOURCE_PLAYER) > 0) {
            return fakeUser.GetUID();
          }
        } else {
          if ((actorMask & (m_Map->GetGameObservers() == GameObserversMode::kReferees ? ACTION_SOURCE_REFEREE : ACTION_SOURCE_OBSERVER)) > 0) {
            return fakeUser.GetUID();
          }
        }
      }
      return 0xFF;
    }

    default: {
      return 0xFF;
    }
  }
}

void CGame::ResolveVirtualUsers()
{
  if (m_RestoredGame) {
    const uint8_t activePlayers = static_cast<uint8_t>(GetNumJoinedUsersOrFake()); // though it shouldn't be possible to manually add fake users
    const uint8_t expectedPlayers = m_RestoredGame->GetNumHumanSlots();
    if (activePlayers < expectedPlayers) {
      if (m_IsAutoVirtualPlayers) {
        // Restored games do not allow custom fake users, so we should only reach this point with actual users joined.
        // This code path is triggered by !fp enable.
        const uint8_t addedCounter = FakeAllSlots();
        LOG_APP_IF(LogLevel::kInfo, Concat("resuming ", to_string(expectedPlayers), "-user game. ", to_string(addedCounter), " virtual users added."));
      } else {
        LOG_APP_IF(LogLevel::kInfo, Concat("resuming ", to_string(expectedPlayers), "-user game. ", ToDecString(expectedPlayers - activePlayers), " missing."));
      }
    }
    return;
  }

  // Host to bot map communication (W3HMC)
  {
    const uint8_t SID = GetHMCSID();
    CGameSlot* slot = GetSlot(SID);
    if (slot) {
      if (slot->GetSlotStatus() == SLOTSTATUS_OPEN || slot->GetSlotStatus() == SLOTSTATUS_CLOSED) {
        if (GetNumControllers() < m_Map->GetMapNumControllers()) {
          const CGameVirtualUser* virtualUser = CreateFakeUserInner(SID, GetNewUID(), m_Map->GetHMCPlayerName(), false);
          m_HMCVirtualUser = CGameVirtualUserReference(*virtualUser);
          LOG_APP_IF(LogLevel::kInfo, Concat("W3HMC virtual user added at slot ", ToDecString(ToBaseOne(SID))));
        }
      } else {
        CGameVirtualUser* virtualUser = GetVirtualUserFromSID(SID);
        // this is the first virtual user resolved, so no need to check GetIsSlotAssignedToSystemVirtualUser
        if (virtualUser && virtualUser->GetIsObserver() && !GetIsCustomForces()/* && !GetIsSlotAssignedToSystemVirtualUser(SID)*/) {
          SetSlotTeamAndColorAuto(SID);
          virtualUser->SetIsObserver(slot->GetTeam() == GetObserverTeam());
          m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        }
        if (virtualUser && !virtualUser->GetIsObserver()) {
          m_HMCVirtualUser = CGameVirtualUserReference(*virtualUser);
          LOG_APP_IF(LogLevel::kInfo, Concat("W3HMC virtual user assigned to slot ", ToDecString(ToBaseOne(SID))));
        }
      }
    }
  }

  // AHCL
  {
    const uint8_t SID = GetAHCLSID();
    CGameSlot* slot = GetSlot(SID);
    if (slot) {
      if (slot->GetSlotStatus() == SLOTSTATUS_OPEN || slot->GetSlotStatus() == SLOTSTATUS_CLOSED) {
        if (GetNumControllers() < m_Map->GetMapNumControllers()) {
          const CGameVirtualUser* virtualUser = CreateFakeUserInner(SID, GetNewUID(), m_Map->GetAHCLPlayerName(), false);
          m_AHCLVirtualUser = CGameVirtualUserReference(*virtualUser);
          LOG_APP_IF(LogLevel::kInfo, Concat("AHCL virtual user added at slot ", ToDecString(ToBaseOne(SID))));
        }
      } else {
        CGameVirtualUser* virtualUser = GetVirtualUserFromSID(SID);
        // HMC and AHCL are fully compatible, so no need to check GetIsSlotAssignedToSystemVirtualUser
        if (virtualUser && virtualUser->GetIsObserver() && !GetIsCustomForces()/* && !GetIsSlotAssignedToSystemVirtualUser(SID)*/) {
          SetSlotTeamAndColorAuto(SID);
          virtualUser->SetIsObserver(slot->GetTeam() == GetObserverTeam());
          m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        }
        if (virtualUser && !virtualUser->GetIsObserver()) {
          m_AHCLVirtualUser = CGameVirtualUserReference(*virtualUser);
          LOG_APP_IF(LogLevel::kInfo, Concat("AHCL virtual user assigned to slot ", ToDecString(ToBaseOne(SID))));
        }
      }
    }
  }

  // Hack to workaround WC3Stats.com bad handling of MMD info about computers
  // https://github.com/wc3stats/w3lib/blob/4e96ea411e01a41c5492b85fd159a0cb318ea2b8/src/w3g/Model/W3MMD.php#L140-L157
  if (m_Map->GetMMDSupported() && m_Map->GetMMDAboutComputers()) {
    uint8_t remainingComputers = GetNumComputers();
    while (remainingComputers > 0) {
      if (m_Map->GetGameObservers() == GameObserversMode::kReferees || m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat) {
        optional<string> virtualPlayerName;
        if (m_Map->GetMapType() == "evergreen") {
          virtualPlayerName = "AMAI Insane";
        } else {
          virtualPlayerName = "Computer";
        }
        if (CreateFakeObserver(virtualPlayerName)) {
          --remainingComputers;
          CGameVirtualUser& virtualUser = m_FakeUsers.back();
          virtualUser.DisableAllActions();
          m_InertVirtualUser = CGameVirtualUserReference(virtualUser);
          ++m_JoinedVirtualHosts;
          LOG_APP_IF(LogLevel::kDebug, Concat("Added virtual player for WC3Stats workaround [", virtualPlayerName.value(), "]"));
        }// else {
        break;
        //}
      }
    }
  }

  // Join-in-progress

  bool joinInProgressIsNativeObserver = false;
  bool addedVirtualHost = false;
  if (m_Config.m_EnableJoinObserversInProgress) {
    // W3MMD v1 lets observers and virtual players send actions, so it would desync any CAsyncObserver
    const bool mmdIncompatibility = m_Map->GetMMDSupported() && !m_Map->GetMMDSupportsVirtualPlayers() && !m_Map->GetMMDPrioritizePlayers();
    if (!mmdIncompatibility) {
      uint8_t spectatorTeam = GetIsCustomForces() ? m_Map->GetMapCustomizableObserverTeam() : GetObserverTeam();
      uint8_t SID = GetIsCustomForces() ? GetEmptyTeamSID(spectatorTeam) : GetEmptySID(false);
      bool isEmptyAvailable = SID != 0xFF;
      if (!isEmptyAvailable) {
        // Exclude HMC / AHCL virtual users to avoid desyncs.
        // Also exclude so-called "inert" virtual user to avoid misrepresentation in WC3Stats
        SID = GetPassiveVirtualUserTeamSID(spectatorTeam);
      }
      CGameSlot* slot = GetSlot(SID);
      if (slot) {
        CGameVirtualUser* virtualUser = nullptr;
        if (isEmptyAvailable) {
          virtualUser = CreateFakeUserInner(SID, GetNewUID(), GetLobbyVirtualHostName(), spectatorTeam == GetObserverTeam());
          addedVirtualHost = true;
        } else {
          virtualUser = GetVirtualUserFromSID(SID);
        }
        // SID was either empty or passive, so we don't need to check GetIsSlotAssignedToSystemVirtualUser() again
        if (virtualUser && slot->GetTeam() != spectatorTeam && !GetIsCustomForces()/* && !GetIsSlotAssignedToSystemVirtualUser(SID)*/) {
          slot->SetTeam(spectatorTeam);
          virtualUser->SetIsObserver(slot->GetTeam() == GetObserverTeam());
          m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        }
        if (virtualUser && slot->GetTeam() == spectatorTeam) {
          virtualUser->DisableAllActions();
          virtualUser->SetAllowedConnections(VIRTUAL_USER_ALLOW_CONNECTIONS_OBSERVER);
          m_JoinInProgressVirtualUser = CGameVirtualUserReference(*virtualUser);
          joinInProgressIsNativeObserver = slot->GetTeam() == GetObserverTeam();
          if (isEmptyAvailable) {
            LOG_APP_IF(LogLevel::kInfo, Concat("Join-in-progress observer virtual user added at slot ", ToDecString(ToBaseOne(SID)), " [", virtualUser->GetName(), "]"));
          } else {
            LOG_APP_IF(LogLevel::kInfo, Concat("Join-in-progress observer virtual user assigned to slot ", ToDecString(ToBaseOne(SID)), " [", virtualUser->GetName(), "]"));
          }
        }
      }
    } else {
      LOG_APP_IF(LogLevel::kWarning, "Join-in-progress feature disabled due to incompatibility with W3MMD <map.w3mmd.features.virtual_players = no>, <map.w3mmd.features.prioritize_players = no>");
    }
  }

  if (!m_JoinInProgressVirtualUser.has_value()) {
    m_Config.m_EnableJoinObserversInProgress = false;
  }

  const uint8_t beforeFakeObserverCount = GetNumFakeObservers();
  const bool addVirtualHost = beforeFakeObserverCount <= 2 && !addedVirtualHost;
  if (addVirtualHost) {
    optional<string> virtualPlayerName;
    virtualPlayerName = GetLobbyVirtualHostName();
    // Assign an available slot to our virtual host.
    // That makes it a fake user.
    if (m_Map->GetGameObservers() == GameObserversMode::kReferees) {
      if (CreateFakeObserver(nullopt)) {
        ++m_JoinedVirtualHosts;
        // As a referee, the virtual host can send messages and receive messages from any player.
        LOG_APP_IF(LogLevel::kDebug, "Added virtual host as referee");
      }
    } else if (m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat) {
      if ((joinInProgressIsNativeObserver && beforeFakeObserverCount <= 1) || (GetNumJoinedObservers() > 0 && beforeFakeObserverCount == 0)) {
        if (CreateFakeObserver(nullopt)) {
          ++m_JoinedVirtualHosts;
          // As a full observer, the virtual host can send messages and receive messages from other full observers.
          LOG_APP_IF(LogLevel::kDebug, "Added virtual host as full observer");
        }
      }
    }
  }

  if (m_IsAutoVirtualPlayers && GetNumJoinedPlayersOrFake() < 2) {
    if (CreateFakePlayer(nullopt)) {
      ++m_JoinedVirtualHosts;
      LOG_APP_IF(LogLevel::kDebug, "Added filler virtual player");
    }
  }

  for (auto& fakeUser : m_FakeUsers) {
    if (fakeUser.GetIsObserver()) {
      fakeUser.SetCannotShareUnits();
    }
  }
}

void CGame::ResolveBuffering()
{
  if (m_Config.m_LoadInGame) {
    m_BufferingEnabled |= BUFFERING_ENABLED_LOADING;
  }
  if (m_Config.m_EnableJoinObserversInProgress || m_Config.m_EnableJoinPlayersInProgress) {
    m_BufferingEnabled |= BUFFERING_ENABLED_ALL;
  }
}

bool CGame::GetHasAnyActiveTeam() const
{
  return m_SlotsConfig.GetHasAnyActiveTeam();
}

bool CGame::GetHasAnyUser() const
{
  if (m_Users.empty()) {
    return false;
  }

  for (const auto& user : m_Users) {
    if (!user->GetDeleteMe()) {
      return true;
    }
  }
  return false;
}

bool CGame::GetIsRealPlayerSlot(const uint8_t SID) const
{
  const CGameSlot* slot = InspectSlot(SID);
  if (!slot || !slot->GetIsPlayerOrFake()) return false;
  const GameUser::CGameUser* user = GetUserFromSID(SID);
  if (user == nullptr) return false;
  return !user->GetDeleteMe();
}

bool CGame::GetIsVirtualPlayerSlot(const uint8_t SID) const
{
  const CGameSlot* slot = InspectSlot(SID);
  if (!slot || !slot->GetIsPlayerOrFake()) return false;
  const GameUser::CGameUser* user = GetUserFromSID(SID);
  return user == nullptr;
}

bool CGame::GetHasAnotherPlayer(const uint8_t ExceptSID) const
{
  uint8_t SID = ExceptSID;
  do {
    SID = static_cast<uint8_t>((uint8_t)(SID + TINY_ONE) % m_SlotsConfig.GetCount());
  } while (!GetIsRealPlayerSlot(SID) && SID != ExceptSID);
  return SID != ExceptSID;
}

vector<uint8_t> CGame::GetAllUIDs() const
{
  vector<uint8_t> result;
  result.reserve(m_Users.size() + 1);

  for (auto& user : m_Users) {
    if (user->GetLeftMessageSent()) {
      continue;
    }
    result.push_back(user->GetUID());
  }

  if (m_JoinInProgressVirtualUser.has_value()) {
    result.push_back(m_JoinInProgressVirtualUser->GetUID());
  }

  return result;
}

std::vector<uint8_t> CGame::GetAllChatUIDs() const
{
  std::vector<uint8_t> result;

  for (auto& user : m_Users) {
    if (user->GetLeftMessageSent() || user->GetIsInLoadingScreen()) {
      continue;
    }
    result.push_back(user->GetUID());
  }

  if (m_JoinInProgressVirtualUser.has_value()) {
    result.push_back(m_JoinInProgressVirtualUser->GetUID());
  }

  return result;
}

std::vector<uint8_t> CGame::GetObserverChatUIDs() const
{
  std::vector<uint8_t> result;
  for (auto& user : m_Users) {
    if (!user->GetLeftMessageSent() && user->GetIsObserver())
      result.push_back(user->GetUID());
  }

  // if a tree falls in the forest and nobody is there to hear it does it make a sound? for simplicity and some pointless privacy, let's say no
  if (!result.empty() && m_JoinInProgressVirtualUser.has_value()) {
    result.push_back(m_JoinInProgressVirtualUser->GetUID());
  }

  return result;
}

std::vector<uint8_t> CGame::GetFilteredChatUIDs(uint8_t fromUID, const vector<uint8_t>& /*toUIDs*/) const
{
  std::vector<uint8_t> result;

  for (auto& user : m_Users) {
    if (user->GetLeftMessageSent() || user->GetIsInLoadingScreen()) continue;
    if (user->GetUID() != fromUID)
      result.push_back(user->GetUID());
  }

  if (m_JoinInProgressVirtualUser.has_value() && m_JoinInProgressVirtualUser->GetUID() != fromUID) {
    result.push_back(m_JoinInProgressVirtualUser->GetUID());
  }

  return result;
}

std::vector<uint8_t> CGame::GetFilteredChatObserverUIDs(uint8_t fromUID, const vector<uint8_t>& /*toUIDs*/) const
{
  std::vector<uint8_t> result;
  for (auto& user : m_Users) {
    if (user->GetLeftMessageSent() || user->GetIsInLoadingScreen()) continue;
    if (user->GetIsObserver() && user->GetUID() != fromUID) {
      result.push_back(user->GetUID());
    }
  }

  if (m_JoinInProgressVirtualUser.has_value() && m_JoinInProgressVirtualUser->GetUID() != fromUID) {
    result.push_back(m_JoinInProgressVirtualUser->GetUID());
  }

  return result;
}

uint8_t CGame::GetPublicHostUID() const
{
  // First try to use a fake user.
  // But fake users are not available while the game is loading.
  if (!m_GameLoading && !m_FakeUsers.empty()) {
    // After loaded, we need to carefully consider who to speak as.
    if (!m_GameLoading && !m_GameLoaded) {
      return m_FakeUsers.back().GetUID();
    }
    for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
      if (fakeUser.GetIsObserver() && m_Map->GetGameObservers() != GameObserversMode::kReferees) {
        continue;
      }
      return fakeUser.GetUID();
    }
  }

  // try to find the owner next

  for (auto& user : m_Users) {
    if (user->GetLeftMessageSent()) {
      continue;
    }
    if (user->GetIsObserver() && m_Map->GetGameObservers() != GameObserversMode::kReferees) {
      continue;
    }
    if (MatchOwnerName(user->GetName())) {
      if (user->GetIsRealmVerified() && user->GetRealmHostName() == m_OwnerRealm) {
        return user->GetUID();
      }
      if (user->GetRealmHostName().empty() && m_OwnerRealm.empty()) {
        return user->GetUID();
      }
      break;
    }
  }

  // okay then, just use the first available user
  uint8_t fallbackUID = 0xFF;

  for (auto& user : m_Users) {
    if (user->GetLeftMessageSent()) {
      continue;
    }
    if (user->GetCanUsePublicChat()) {
      return user->GetUID();
    } else if (fallbackUID == 0xFF) {
      fallbackUID = user->GetUID();
    }
  }

  return fallbackUID;
}

uint8_t CGame::GetHiddenHostUID() const
{
  // First try to use a fake user.
  // But fake users are not available while the game is loading.

  vector<uint8_t> availableUIDs;
  //vector<uint8_t> availableRefereeUIDs;

  if (!m_GameLoading && !m_FakeUsers.empty()) {
    for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
      if (fakeUser.GetIsObserver() && m_Map->GetGameObservers() != GameObserversMode::kReferees) {
        continue;
      }
      if (fakeUser.GetIsObserver()) {
        //availableRefereeUIDs.push_back(static_cast<uint8_t>(fakePlayer));
        return fakeUser.GetUID();
      } else {
        availableUIDs.push_back(fakeUser.GetUID());
      }
    }
  }

  uint8_t fallbackUID = 0xFF;
  for (auto& user : m_Users) {
    if (user->GetLeftMessageSent() || user->GetIsInLoadingScreen()) {
      continue;
    }
    if (user->GetCanUsePublicChat()) {
      if (user->GetIsObserver()) {
        //availableRefereeUIDs.push_back(user->GetUID());
        return user->GetUID();
      } else {
        availableUIDs.push_back(user->GetUID());
      }
    } else if (fallbackUID == 0xFF) {
      fallbackUID = user->GetUID();
    }
  }

  if (!availableUIDs.empty()) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distribution(1, static_cast<int>(availableUIDs.size()));
    return availableUIDs[signed_cast<size_t>(distribution(gen) - 1)];
  }

  return fallbackUID;
}

uint8_t CGame::GetHostUID() const
{
  // return the user to be considered the host (it can be any user)
  // mainly used for sending text messages from the bot

  if (GetHasVirtualHost()) {
    return m_VirtualHostUID;
  }

  if (GetIsHiddenPlayerNames()) {
    return GetHiddenHostUID();
  } else {
    return GetPublicHostUID();
  }
}

MapTransferCheckResult CGame::CheckCanTransferMap(const CConnection* /*connection*/, shared_ptr<const CRealm> realm, const Version& version, const bool gotPermission)
{
  if (!m_Map->GetMapFileIsValid()) {
    return m_Map->HasMismatch() ? MapTransferCheckResult::kInvalid : MapTransferCheckResult::kMissing;
  }
  if (m_Aura->m_Net.m_Config.m_AllowTransfers == MAP_TRANSFERS_NEVER) {
    return MapTransferCheckResult::kDisabled;
  }
  if (!m_Map->GetMapSizeIsNativeSupported(version)) {
    return MapTransferCheckResult::kTooLargeVersion;
  }

  const uint32_t expectedMapSize = m_Map->GetMapSize();
  uint32_t maxTransferSize = m_Aura->m_Net.m_Config.m_MaxUploadSize;
  if (realm) {
    maxTransferSize = realm->GetMaxUploadSize();
  }
  bool isTooLarge = expectedMapSize > maxTransferSize * 1024;

  if (!(gotPermission || (m_Aura->m_Net.m_Config.m_AllowTransfers == MAP_TRANSFERS_AUTOMATIC && !isTooLarge))) {
    return m_Aura->m_Net.m_Config.m_AllowTransfers == MAP_TRANSFERS_AUTOMATIC ? MapTransferCheckResult::kTooLargeConfig : MapTransferCheckResult::kDisabled;
  }
  if (m_Aura->m_Config.m_MaxStartedGames <= m_Aura->m_StartedGames.size()) {
    return MapTransferCheckResult::kBufferBloat;
  }
  if (!m_Aura->m_StartedGames.empty() && m_Aura->m_Net.m_Config.m_HasBufferBloat) {
    return MapTransferCheckResult::kBufferBloat;
  }
  return MapTransferCheckResult::kAllowed;
}

FileChunkTransient CGame::GetMapChunk(size_t start)
{
  FileChunkTransient chunk = m_Map->GetMapFileChunk(start);
  // Ensure the SharedByteArray isn't deallocated
  SetLoadedMapChunk(chunk.bytes);
  return chunk;
}

CGameSlot* CGame::GetSlot(const uint8_t SID)
{
  return m_SlotsConfig.GetSafe(SID);
}

const CGameSlot* CGame::InspectSlot(const uint8_t SID) const
{
  return m_SlotsConfig.InspectSafe(SID);
}

uint8_t CGame::GetEmptySID(bool reserved) const
{
  if (m_SlotsConfig.GetCount() > 0xFF)
    return 0xFF;

  // look for an empty slot for a new user to occupy
  // if reserved is true then we're willing to use closed or occupied slots as long as it wouldn't displace a user with a reserved slot

  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (!m_SlotsConfig.GetIsOpen(i)) {
      continue;
    }
    return i;
  }

  if (reserved)
  {
    // no empty slots, but since user is reserved give them a closed slot

    for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
      if (m_SlotsConfig.GetIsClosed(i) && !GetIsSlotReservedForSystemVirtualUser(i)) {
        return i;
      }
    }

    // no closed slots either, give them an occupied slot but not one occupied by another reserved user
    // first look for a user who is downloading the map and has the least amount downloaded so far

    uint8_t LeastSID = 0xFF;
    uint8_t LeastDownloaded = 100;

    for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
      if (!m_SlotsConfig.Inspect(i).GetIsPlayerOrFake()) continue;
      GameUser::CGameUser* Player = GetUserFromSID(i);
      if (Player && !Player->GetIsReserved() && m_SlotsConfig.Inspect(i).GetDownloadStatus() < LeastDownloaded) {
        LeastSID = i;
        LeastDownloaded = m_SlotsConfig.Inspect(i).GetDownloadStatus();
      }
    }

    if (LeastSID != 0xFF) {
      return LeastSID;
    }

    // nobody who isn't reserved is downloading the map, just choose the first user who isn't reserved

    for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
      if (!m_SlotsConfig.Inspect(i).GetIsPlayerOrFake()) continue;
      GameUser::CGameUser* Player = GetUserFromSID(i);
      if (Player && !Player->GetIsReserved()) {
        return i;
      }
    }
  }

  return 0xFF;
}

uint8_t CGame::GetEmptyTeamSID(const uint8_t team) const
{
  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (m_SlotsConfig.GetIsOpen(i) && m_SlotsConfig.Inspect(i).GetTeam() == team)
      return i;
  }
  return 0xFF;
}

uint8_t CGame::GetEmptyPlayerSID() const
{
  if (m_SlotsConfig.GetCount() > 0xFF)
    return 0xFF;

  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (!m_SlotsConfig.GetIsOpen(i)) continue;
    if (!GetIsCustomForces()) {
      return i;
    }
    if (m_SlotsConfig.Inspect(i).GetTeam() != GetObserverTeam()) {
      return i;
    }
  }

  return 0xFF;
}

uint8_t CGame::GetEmptyObserverSID() const
{
  if (m_SlotsConfig.GetCount() > 0xFF)
    return 0xFF;

  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (!m_SlotsConfig.GetIsOpen(i)) continue;
    if (m_SlotsConfig.GetIsObserver(i)) {
      return i;
    }
  }

  return 0xFF;
}

uint8_t CGame::GetVirtualUserTeamSID(const uint8_t team) const
{
  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (m_SlotsConfig.Inspect(i).GetTeam() == team && GetIsVirtualPlayerSlot(i)) {
      return i;
    }
  }
  return 0xFF;
}

uint8_t CGame::GetPassiveVirtualUserTeamSID(const uint8_t team) const
{
  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    if (m_SlotsConfig.Inspect(i).GetTeam() != team) {
      continue;
    }
    if (GetIsSlotAssignedToSystemVirtualUser(i)) {
      continue;
    }
    if (GetIsVirtualPlayerSlot(i)) {
      return i;
    }
  }
  return 0xFF;
}

bool CGame::SwapEmptyAllySlot(const uint8_t SID)
{
  if (!GetIsCustomForces()) {
    return false;
  }
  const uint8_t team = m_SlotsConfig.Inspect(SID).GetTeam();

  // Look for the next ally, starting from the current SID, and wrapping over.
  uint8_t allySID = SID;
  do {
    allySID = static_cast<uint8_t>((uint8_t)(allySID + TINY_ONE) % m_SlotsConfig.GetCount());
  } while (allySID != SID && !(m_SlotsConfig.slots[allySID].GetTeam() == team && m_SlotsConfig.slots[allySID].GetSlotStatus() == SLOTSTATUS_OPEN));

  if (allySID == SID) {
    return false;
  }
  return SwapSlots(SID, allySID);
}

bool CGame::SwapSlots(const uint8_t SID1, const uint8_t SID2)
{
  if (SID1 >= GetNumSlots() || SID2 >= GetNumSlots() || SID1 == SID2) {
    return false;
  }
  if (GetIsSlotReservedForSystemVirtualUser(SID1) || GetIsSlotReservedForSystemVirtualUser(SID2)) {
    return false;
  }

  {
    // Slot1, Slot2 are implementation details
    // Depending on the branch, they may not necessarily match the actual slots after the swap.
    CGameSlot Slot1 = m_SlotsConfig.slots[SID1];
    CGameSlot Slot2 = m_SlotsConfig.slots[SID2];

    if (!Slot1.GetIsSelectable() || !Slot2.GetIsSelectable()) {
      return false;
    }

    if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS) {
      // don't swap the type, team, color, race, or handicap
      m_SlotsConfig.slots[SID1] = CGameSlot(Slot1.GetType(), Slot2.GetUID(), Slot2.GetDownloadStatus(), Slot2.GetSlotStatus(), Slot2.GetComputer(), Slot1.GetTeam(), Slot1.GetColor(), Slot1.GetRace(), Slot2.GetComputerType(), Slot1.GetHandicap());
      m_SlotsConfig.slots[SID2] = CGameSlot(Slot2.GetType(), Slot1.GetUID(), Slot1.GetDownloadStatus(), Slot1.GetSlotStatus(), Slot1.GetComputer(), Slot2.GetTeam(), Slot2.GetColor(), Slot2.GetRace(), Slot1.GetComputerType(), Slot2.GetHandicap());
    } else {
      if (GetIsCustomForces()) {
        // except if custom forces is set, then we must preserve teams...
        const uint8_t teamOne = Slot1.GetTeam();
        const uint8_t teamTwo = Slot2.GetTeam();

        Slot1.SetTeam(teamTwo);
        Slot2.SetTeam(teamOne);

        // additionally, if custom forces is set, and exactly 1 of the slots is observer, then we must also preserve colors
        const uint8_t colorOne = Slot1.GetColor();
        const uint8_t colorTwo = Slot2.GetColor();
        if (teamOne != teamTwo && (teamOne == GetObserverTeam() || teamTwo == GetObserverTeam())) {
          Slot1.SetColor(colorTwo);
          Slot2.SetColor(colorOne);
        }
      }

      // swap everything (what we swapped already is reverted)
      m_SlotsConfig.slots[SID1] = Slot2;
      m_SlotsConfig.slots[SID2] = Slot1;
    }
  }

  uint8_t i = static_cast<uint8_t>(m_FakeUsers.size());
  while (i--) {
    uint8_t fakeSID = m_FakeUsers[i].GetSID();
    if (fakeSID == SID1) {
      m_FakeUsers[i].SetSID(SID2);
      m_FakeUsers[i].SetIsObserver(m_SlotsConfig.slots[SID2].GetTeam() == GetObserverTeam());
    } else if (fakeSID == SID2) {
      m_FakeUsers[i].SetSID(SID1);
      m_FakeUsers[i].SetIsObserver(m_SlotsConfig.slots[SID1].GetTeam() == GetObserverTeam());
    }
  }

  // Players that are at given slots afterwards.
  GameUser::CGameUser* PlayerOne = GetUserFromSID(SID1);
  GameUser::CGameUser* PlayerTwo = GetUserFromSID(SID2);
  if (PlayerOne) {
    PlayerOne->SetIsObserver(m_SlotsConfig.slots[SID1].GetTeam() == GetObserverTeam());
    if (PlayerOne->GetIsObserver()) {
      PlayerOne->SetPowerObserver(PlayerOne->GetIsObserver() && m_Map->GetGameObservers() == GameObserversMode::kReferees);
      PlayerOne->ClearUserReady();
    }
  }
  if (PlayerTwo) {
    PlayerTwo->SetIsObserver(m_SlotsConfig.slots[SID2].GetTeam() == GetObserverTeam());
    if (PlayerTwo->GetIsObserver()) {
      PlayerTwo->SetPowerObserver(PlayerTwo->GetIsObserver() && m_Map->GetGameObservers() == GameObserversMode::kReferees);
      PlayerTwo->ClearUserReady();
    }
  }

  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  return true;
}

bool CGame::OpenSlot(const uint8_t SID, const bool kick)
{
  const CGameSlot* slot = InspectSlot(SID);
  if (!slot || !slot->GetIsSelectable()) {
    return false;
  }
  if (GetIsSlotReservedForSystemVirtualUser(SID)) {
    return false;
  }

  GameUser::CGameUser* user = GetUserFromSID(SID);
  if (user && !user->GetDeleteMe()) {
    if (!kick) return false;
    if (!user->HasLeftReason()) {
      user->SetLeftReason("was kicked when opening a slot");
    }
    // fromOpen = true, so that EventUserAfterDisconnect doesn't call OpenSlot() itself
    user->CloseConnection(true);
  } else if (slot->GetSlotStatus() == SLOTSTATUS_CLOSED) {
    ResetLayout(false);
  }
  if (user && m_CustomLayout == CUSTOM_LAYOUT_ONE_VS_ALL && slot->GetTeam() == m_CustomLayoutData.first) {
    ResetLayout(false);
  }
  if (!user && slot->GetIsPlayerOrFake()) {
    DeleteFakeUser(SID);
  }
  if (GetIsCustomForces()) {
    m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_OPEN, SLOTCOMP_NO, slot->GetTeam(), slot->GetColor(), m_Map->GetLobbyRace(slot));
  } else {
    m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_OPEN, SLOTCOMP_NO, GetObserverTeam(), GetObserverColor(), SLOTRACE_RANDOM);
  }
  if (user && !GetHasAnotherPlayer(SID)) {
    EventLobbyLastPlayerLeaves();
  }
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  return true;
}

bool CGame::OpenSlot()
{
  uint8_t SID = 0;
  while (SID < m_SlotsConfig.GetCount()) {
    if (!GetIsSlotReservedForSystemVirtualUser(SID) && m_SlotsConfig.GetIsClosed(SID)) {
      return OpenSlot(SID, false);
    }
    ++SID;
  }
  return false;
}

bool CGame::CanLockSlotForJoins(const uint8_t SID)
{
  CGameSlot* slot = GetSlot(SID);
  if (!slot || !slot->GetIsSelectable()) {
    return false;
  }
  if (slot->GetSlotStatus() == SLOTSTATUS_CLOSED) {
    // Changing a closed slot for anything doesn't decrease the
    // amount of slots available for humans.
    return true;
  }
  const uint8_t openSlots = static_cast<uint8_t>(GetNumSlotsOpen());
  if (openSlots >= 2) {
    return true;
  }
  if (slot->GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
    if (openSlots >= 1) return true;
    return GetHasAnotherPlayer(SID);
  }

  return GetHasAnyUser();
}

bool CGame::CloseSlot(const uint8_t SID, const bool kick)
{
  if (!CanLockSlotForJoins(SID)) {
    return false;
  }
  const CGameSlot* slot = InspectSlot(SID);
  const uint8_t openSlots = static_cast<uint8_t>(GetNumSlotsOpen());
  GameUser::CGameUser* user = GetUserFromSID(SID);
  if (user && !user->GetDeleteMe()) {
    if (!kick) return false;
    if (!user->HasLeftReason()) {
      user->SetLeftReason("was kicked when closing a slot");
    }
    user->CloseConnection();
  }
  if (slot->GetSlotStatus() == SLOTSTATUS_OPEN && openSlots == 1 && GetNumJoinedUsersOrFake() > 1) {
    DeleteVirtualHost();
  }
  if (!user && slot->GetIsPlayerOrFake()) {
    DeleteFakeUser(SID);
  }

  if (GetIsCustomForces()) {
    m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_CLOSED, SLOTCOMP_NO, slot->GetTeam(), slot->GetColor(), m_Map->GetLobbyRace(slot));
  } else {
    m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_CLOSED, SLOTCOMP_NO, GetObserverTeam(), GetObserverColor(), SLOTRACE_RANDOM);
  }
  
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  return true;
}

bool CGame::CloseSlot()
{
  uint8_t SID = 0;
  while (SID < m_SlotsConfig.GetCount()) {
    if (m_SlotsConfig.GetIsOpen(SID)) {
      return CloseSlot(SID, false);
    }
    ++SID;
  }
  return false;
}

bool CGame::ComputerSlot(uint8_t SID, uint8_t skill, bool kick)
{
  if (SID >= GetNumSlots() || skill > SLOTCOMP_HARD) {
    return false;
  }
  if (GetIsSlotReservedForSystemVirtualUser(SID)) {
    return false;
  }

  CGameSlot Slot = m_SlotsConfig.Inspect(SID);
  if (!Slot.GetIsSelectable()) {
    return false;
  }
  if (Slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED && GetNumControllers() == m_Map->GetMapNumControllers()) {
    return false;
  }
  if (Slot.GetTeam() == GetObserverTeam()) {
    if (GetIsCustomForces()) {
      return false;
    }
  }
  if (!CanLockSlotForJoins(SID)) {
    return false;
  }
  GameUser::CGameUser* Player = GetUserFromSID(SID);
  if (Player && !Player->GetDeleteMe()) {
    if (!kick) return false;
    if (!Player->HasLeftReason()) {
      Player->SetLeftReason("was kicked when creating a computer in a slot");
    }
    Player->CloseConnection();
  }

  // ignore layout, override computers
  if (ComputerSlotInner(SID, skill, true, true)) {
    if (GetNumSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
    m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  }
  return true;
}

bool CGame::SetSlotTeam(const uint8_t SID, const uint8_t team, const bool force)
{
  CGameSlot* slot = GetSlot(SID);
  if (!slot || slot->GetTeam() == team || !slot->GetIsSelectable()) {
    return false;
  }
  if (GetIsCustomForces()) {
    const uint8_t newSID = GetSelectableTeamSlotFront(team, GetNumSlots(), GetNumSlots(), force);
    if (newSID == 0xFF) return false;
    return SwapSlots(SID, newSID);
  } else {
    const bool fromObservers = slot->GetTeam() == GetObserverTeam();
    const bool toObservers = team == GetObserverTeam();
    if (toObservers && !slot->GetIsPlayerOrFake()) return false;
    if (fromObservers && !toObservers && GetNumControllers() >= m_Map->GetMapNumControllers()) {
      // Observer cannot become controller if the map's controller limit has been reached.
      return false;
    }

    slot->SetTeam(team);
    if (toObservers || fromObservers) {
      if (toObservers) {
        slot->SetColor(GetObserverColor());
        slot->SetRace(SLOTRACE_RANDOM);
      } else {
        slot->SetColor(GetNewColor());
        if (m_Map->GetMapFlags() & GAMEFLAG_RANDOMRACES) {
          slot->SetRace(SLOTRACE_RANDOM);
        } else {
          slot->SetRace(SLOTRACE_RANDOM | SLOTRACE_SELECTABLE);
        }
      }

      GameUser::CGameUser* user = GetUserFromUID(slot->GetUID());
      if (user) {
        user->SetIsObserver(toObservers);
        if (toObservers) {
          user->SetPowerObserver(!m_UsesCustomReferees && m_Map->GetGameObservers() == GameObserversMode::kReferees);
          user->ClearUserReady();
        } else {
          user->SetPowerObserver(false);
        }
      } else {
        CGameVirtualUser* virtualUserMatch = GetVirtualUserFromSID(SID);
        if (virtualUserMatch) {
          virtualUserMatch->SetIsObserver(toObservers);
        }
      }
    }

    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
    return true;
  }
}

bool CGame::SetSlotColor(const uint8_t SID, const uint8_t color, const bool force)
{
  CGameSlot* slot = GetSlot(SID);
  if (!slot || slot->GetColor() == color || !slot->GetIsSelectable()) {
    return false;
  }

  if (slot->GetSlotStatus() != SLOTSTATUS_OCCUPIED || slot->GetTeam() == GetObserverTeam()) {
    // Only allow active users to choose their colors.
    //
    // Open/closed slots do actually have a color when Fixed Player Settings is enabled,
    // but I'm still not providing API for it.
    return false;
  }

  CGameSlot* takenSlot = nullptr;
  uint8_t takenSID = 0xFF;

  // if the requested color is taken, try to exchange colors
  for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
    CGameSlot* matchSlot = &(m_SlotsConfig.Get(i));
    if (matchSlot->GetColor() != color) continue;
    if (!matchSlot->GetIsSelectable()) {
      return false;
    }
    if (!force) {
      // user request - may only use the color of an unoccupied slot
      // closed slots are okay, too (note that they only have a valid color when using Custom Forces)
      if (matchSlot->GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
        return false;
      }
    }
    takenSlot = matchSlot;
    takenSID = i;
    break;
  }

  if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS) {
    // With fixed user settings we try to swap slots.
    // This is not exposed to EventUserRequestColor,
    // but it's useful for !color.
    //
    // Old: !swap 3 7
    // Now: !color Arthas, teal
    if (!takenSlot) {
      // But we found no slot to swap with.
      return false;
    } else {
      SwapSlots(SID, takenSID); // Guaranteed to succeed at this point;
      m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
      return true;
    }
  } else {
    if (takenSlot) takenSlot->SetColor(m_SlotsConfig.Inspect(SID).GetColor());
    m_SlotsConfig.Get(SID).SetColor(color);
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
    return true;
  }
}

void CGame::SetSlotTeamAndColorAuto(const uint8_t SID)
{
  // Custom Forces must use m_SlotsConfig.Inspect(SID).GetColor() / m_SlotsConfig.Inspect(SID).GetTeam()
  if (m_SlotsConfig.GetLayout() != MAPLAYOUT_ANY) return;
  CGameSlot* slot = GetSlot(SID);
  if (!slot) return;
  if (GetNumControllers() >= m_Map->GetMapNumControllers()) {
    return;
  }
  switch (GetCustomLayout()) {
    case CUSTOM_LAYOUT_ONE_VS_ALL:
      slot->SetTeam(m_CustomLayoutData.second);
      break;
    case CUSTOM_LAYOUT_HUMANS_VS_AI:
      if (slot->GetIsPlayerOrFake()) {
        slot->SetTeam(m_CustomLayoutData.first);
      } else {
        slot->SetTeam(m_CustomLayoutData.second);
      }      
      break;
    case CUSTOM_LAYOUT_FFA:
      slot->SetTeam(GetNewTeam());
      break;
    case CUSTOM_LAYOUT_DRAFT:
      // Player remains as observer until someone picks them.
      break;
    default: {
      bool otherTeamError = false;
      uint8_t otherTeam = GetObserverTeam();
      uint8_t numSkipped = 0;
      for (uint8_t i = 0; i < m_SlotsConfig.GetCount(); ++i) {
        const CGameSlot* otherSlot = InspectSlot(i);
        if (otherSlot->GetSlotStatus() != SLOTSTATUS_OCCUPIED) {
          if (i < SID) ++numSkipped;
          continue;
        }
        if (otherSlot->GetTeam() == GetObserverTeam()) {
          if (i < SID) ++numSkipped;
        } else if (otherTeam != GetObserverTeam()) {
          otherTeamError = true;
        } else {
          otherTeam = otherSlot->GetTeam();
        }
      }
      if (m_Map->GetMapNumControllers() == 2 && !otherTeamError && otherTeam < 2) {
        // Streamline team selection for 1v1 maps
        slot->SetTeam(1 - otherTeam);
      } else {
        slot->SetTeam(MOD_TINY(MINUS_TINY(SID, numSkipped), m_Map->GetMapNumTeams()));
      }
      break;
    }
  }
  slot->SetColor(GetNewColor());
}

void CGame::OpenAllSlots()
{
  bool anyChanged = false;
  uint8_t i = GetNumSlots();
  while (i--) {
    if (!GetIsSlotReservedForSystemVirtualUser(i) && m_SlotsConfig.GetIsClosed(i)) {
      m_SlotsConfig.Get(i).SetSlotStatus(SLOTSTATUS_OPEN);
      anyChanged = true;
    }
  }

  if (anyChanged) {
    m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  }
}

uint8_t CGame::GetFirstCloseableSlot()
{
  bool hasPlayer = false;
  uint8_t firstSID = 0xFF;
  for (uint8_t SID = 0; SID < m_SlotsConfig.GetCount(); ++SID) {
    if (m_SlotsConfig.GetIsOpen(SID)) {
      if (firstSID == 0xFF) firstSID = static_cast<uint8_t>(SID + TINY_ONE);
      if (hasPlayer) break;
    } else if (GetIsRealPlayerSlot(SID)) {
      hasPlayer = true;
      if (firstSID != 0xFF) break;
    }
  }

  if (hasPlayer) return 0;
  return firstSID;
}

bool CGame::CloseAllTeamSlots(const uint8_t team)
{
  const uint8_t firstSID = GetFirstCloseableSlot();
  if (firstSID == 0xFF) return false;

  bool anyChanged = false;
  uint8_t SID = GetNumSlots();
  while (firstSID < SID--) {
    if (m_SlotsConfig.GetIsOpen(SID) && m_SlotsConfig.Inspect(SID).GetTeam() == team) {
      m_SlotsConfig.Get(SID).SetSlotStatus(SLOTSTATUS_CLOSED);
      anyChanged = true;
    }
  }

  if (anyChanged) {
    if (GetNumJoinedUsersOrFake() > 1)
      DeleteVirtualHost();
    m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  }

  return anyChanged;
}

bool CGame::CloseAllTeamSlots(const bitset<MAX_SLOTS_MODERN> occupiedTeams)
{
  if (!GetIsCustomForces()) return false;
  const uint8_t firstSID = GetFirstCloseableSlot();
  if (firstSID == 0xFF) return false;

  bool anyChanged = false;
  uint8_t SID = GetNumSlots();
  while (firstSID < SID--) {
    if (m_SlotsConfig.GetIsOpen(SID) && occupiedTeams.test(m_SlotsConfig.Inspect(SID).GetTeam())) {
      m_SlotsConfig.Get(SID).SetSlotStatus(SLOTSTATUS_CLOSED);
      anyChanged = true;
    }
  }

  if (anyChanged) {
    if (GetNumJoinedUsersOrFake() > 1)
      DeleteVirtualHost();
    m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  }

  return anyChanged;
}

bool CGame::CloseAllSlots()
{
  const uint8_t firstSID = GetFirstCloseableSlot();
  if (firstSID == 0xFF) return false;

  bool anyChanged = false;
  uint8_t SID = GetNumSlots();
  while (firstSID < SID--) {
    if (m_SlotsConfig.GetIsOpen(SID)) {
      m_SlotsConfig.Get(SID).SetSlotStatus(SLOTSTATUS_CLOSED);
      anyChanged = true;
    }
  }

  if (anyChanged) {
    if (GetNumJoinedUsersOrFake() > 1)
      DeleteVirtualHost();
    m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  }

  return anyChanged;
}

bool CGame::ComputerSlotInner(const uint8_t SID, const uint8_t skill, const bool ignoreLayout, const bool overrideComputers)
{
  const CGameSlot* slot = InspectSlot(SID);
  if ((!ignoreLayout || GetIsRealPlayerSlot(SID)) && slot->GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
    return false;
  }
  if (!overrideComputers && slot->GetIsComputer()) {
    return false;
  }
  if (GetIsSlotReservedForSystemVirtualUser(SID)) {
    return false;
  }

  // !comp NUMBER bypasses current layout, so it may
  // add computers in closed slots (in regular layouts), or
  // in open slots (in HUMANS_VS_AI)
  // if it does, reset the layout
  bool resetLayout = false;
  if (m_CustomLayout == CUSTOM_LAYOUT_HUMANS_VS_AI) {
    if (slot->GetSlotStatus() == SLOTSTATUS_OPEN || (GetIsCustomForces() && slot->GetTeam() != m_CustomLayoutData.second)) {
      if (ignoreLayout) {
        resetLayout = true;
      } else {
        return false;
      }
    }
  } else {
    if (slot->GetSlotStatus() == SLOTSTATUS_CLOSED) {
      if (!ignoreLayout) {
        return false;
      }
    }
  }
  if (GetIsCustomForces()) {
    if (slot->GetTeam() == GetObserverTeam()) {
      return false;
    }
    if (slot->GetIsPlayerOrFake()) DeleteFakeUser(SID);
    m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RDY, SLOTSTATUS_OCCUPIED, SLOTCOMP_YES, slot->GetTeam(), slot->GetColor(), m_Map->GetLobbyRace(slot), skill);
    if (resetLayout) ResetLayout(false);
  } else {
    if (slot->GetIsPlayerOrFake()) DeleteFakeUser(SID);
    m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RDY, SLOTSTATUS_OCCUPIED, SLOTCOMP_YES, GetObserverTeam(), GetObserverColor(), m_Map->GetLobbyRace(slot), skill);
    SetSlotTeamAndColorAuto(SID);
  }
  return true;
}

bool CGame::ComputerNSlots(const uint8_t skill, const uint8_t expectedCount, const bool ignoreLayout, const bool overrideComputers)
{
  uint8_t currentCount = GetNumComputers();
  if (expectedCount == currentCount) {
    // noop
    return true;
  }

  if (expectedCount < currentCount) {
    uint8_t SID = GetNumSlots();
    while (SID--) {
      if (m_SlotsConfig.GetIsOccupied(SID) && m_SlotsConfig.GetIsComputer(SID)) {
        if (OpenSlot(SID, false) && --currentCount == expectedCount) {
          if (m_CustomLayout == CUSTOM_LAYOUT_HUMANS_VS_AI && currentCount == 0) ResetLayout(false);
          return true;
        }
      }
    }
    return false;
  }

  if (m_Map->GetMapNumControllers() <= GetNumControllers()) {
    return false;
  }

  const bool hasUsers = GetHasAnyUser(); // Ensure this is called outside the loop.
  uint8_t remainingControllers = m_Map->GetMapNumControllers() - GetNumControllers();
  if (!hasUsers) --remainingControllers; // Refuse to lock the last slot
  if (expectedCount - currentCount > remainingControllers) {
    return false;
  }
  uint8_t remainingComputers = overrideComputers ? expectedCount : (expectedCount - currentCount);
  uint8_t SID = 0;
  while (0 < remainingComputers && SID < m_SlotsConfig.GetCount()) {
    // overrideComputers false means only newly added computers are counted in remainingComputers
    if (ComputerSlotInner(SID, skill, ignoreLayout, overrideComputers)) {
      --remainingComputers;
    }
    ++SID;
  }

  if (GetNumSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;

  return remainingComputers == 0;
}

bool CGame::ComputerAllSlots(const uint8_t skill)
{
  if (m_Map->GetMapNumControllers() <= GetNumControllers()) {
    return false;
  }

  const bool hasUsers = GetHasAnyUser(); // Ensure this is called outside the loop.
  uint32_t remainingSlots = m_Map->GetMapNumControllers() - GetNumControllers();

  // Refuse to lock the last slot
  if (!hasUsers && m_SlotsConfig.GetCount() == m_Map->GetMapNumControllers()) {
    --remainingSlots;
  }

  if (remainingSlots == 0) {
    return false;
  }

  uint8_t SID = 0;
  while (0 < remainingSlots && SID < m_SlotsConfig.GetCount()) {
    // don't ignore layout, don't override computers
    if (ComputerSlotInner(SID, skill, false, false)) {
      --remainingSlots;
    }
    ++SID;
  }

  if (GetNumSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  return true;
}

void CGame::ShuffleSlots()
{
  // we only want to shuffle the user slots (exclude observers)
  // that means we need to prevent this function from shuffling the open/closed/computer slots too
  // so we start by copying the user slots to a temporary vector

  vector<CGameSlot> playerSlots;

  for (auto& slot : m_SlotsConfig.slots) {
    if (slot.GetIsPlayerOrFake() && slot.GetTeam() != GetObserverTeam()) {
      playerSlots.push_back(slot);
    }
  }

  // now we shuffle playerSlots

  if (GetIsCustomForces()) {
    // rather than rolling our own probably broken shuffle algorithm we use random_shuffle because it's guaranteed to do it properly
    // so in order to let random_shuffle do all the work we need a vector to operate on
    // unfortunately we can't just use playerSlots because the team/color/race shouldn't be modified
    // so make a vector we can use

    vector<uint8_t> SIDs;

    for (uint8_t i = 0; i < playerSlots.size(); ++i)
      SIDs.push_back(i);

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(begin(SIDs), end(SIDs), g);

    // now put the playerSlots vector in the same order as the SIDs vector

    vector<CGameSlot> slots;

    // as usual don't modify the type/team/color/race

    for (uint8_t i = 0; i < SIDs.size(); ++i) {
      slots.emplace_back(playerSlots[SIDs[i]].GetType(), playerSlots[SIDs[i]].GetUID(), playerSlots[SIDs[i]].GetDownloadStatus(), playerSlots[SIDs[i]].GetSlotStatus(), playerSlots[SIDs[i]].GetComputer(), playerSlots[i].GetTeam(), playerSlots[i].GetColor(), playerSlots[i].GetRace());
    }

    playerSlots = slots;
  } else {
    // regular game
    // it's easy when we're allowed to swap the team/color/race!

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(begin(playerSlots), end(playerSlots), g);
  }

  // now we put m_SlotsConfig back together again

  auto currentPlayer = begin(playerSlots);
  vector<CGameSlot> slots;

  for (auto& slot : m_SlotsConfig.slots) {
    if (slot.GetIsPlayerOrFake() && slot.GetTeam() != GetObserverTeam()) {
      slots.push_back(*currentPlayer);
      ++currentPlayer;
    } else {
      slots.push_back(slot);
    }
  }

  m_SlotsConfig.slots = slots;
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
}

void CGame::ReportSpoofed(const string& server, GameUser::CGameUser* user)
{
  if (!m_IsHiddenPlayerNames) {
    SendAllChat(Concat("Name spoof detected. The real [", user->GetName(), "@", server, "] is not in this game."));
  }
  if (GetIsLobbyStrict() && MatchOwnerName(user->GetName())) {
    if (!user->HasLeftReason()) {
      user->SetLeftReason("autokicked - spoofing the game owner");
    }
    user->CloseConnection();
  }
}

void CGame::AddToRealmVerified(const string& server, GameUser::CGameUser* Player, bool sendMessage)
{
  // Must only be called on a lobby, for many reasons (e.g. GetName() used instead of GetDisplayName())
  Player->SetRealmVerified(true);
  if (sendMessage) {
    if (!m_IsHiddenPlayerNames && MatchOwnerName(Player->GetName()) && m_OwnerRealm == Player->GetRealmHostName()) {
      SendAllChat(Concat("Identity accepted for game owner [", Player->GetName(), "@", server, "]"));
    } else {
      SendChat(Player, Concat("Identity accepted for [", Player->GetName(), "@", server, "]"));
    }
  }
}

void CGame::AddToReserved(const string& name)
{
  if (m_RestoredGame && m_Reserved.size() >= m_Map->GetVersionMaxSlots()) {
    return;
  }

  string inputLower = ToLowerCase(name);

  // check that the user is not already reserved
  for (const auto& element : m_Reserved) {
    string matchLower = ToLowerCase(element);
    if (matchLower == inputLower) {
      return;
    }
  }

  m_Reserved.push_back(name);

  // upgrade the user if they're already in the game

  for (auto& user : m_Users) {
    string matchLower = ToLowerCase(user->GetName());

    if (matchLower == inputLower) {
      user->SetReserved(true);
      break;
    }

    // Reserved users are never kicked for latency reasons nor map missing.
    user->RemoveKickReason(GameUser::KickReason::kHighPing);
    user->RemoveKickReason(GameUser::KickReason::kMapMissing);
    user->CheckStillKicked();
  }
}

void CGame::RemoveFromReserved(const string& name)
{
  if (m_Reserved.empty()) return;

  uint8_t index = GetReservedIndex(name);
  if (index == 0xFF) {
    return;
  }
  m_Reserved.erase(m_Reserved.begin() + index);

  // demote the user if they're already in the game
  GameUser::CGameUser* matchPlayer = GetUserFromName(name, false);
  if (matchPlayer) {
    matchPlayer->SetReserved(false);
  }
}

bool CGame::ReserveAll()
{
  bool anyAdded = false;
  for (auto& user : m_Users) {
    if (user->GetIsReserved()) continue;
    user->SetReserved(true);
    m_Reserved.push_back(user->GetLowerName());

    // Reserved users are never kicked for latency reasons nor map missing.
    user->RemoveKickReason(GameUser::KickReason::kHighPing);
    user->RemoveKickReason(GameUser::KickReason::kMapMissing);
    user->CheckStillKicked();

    anyAdded = true;
  }
  return anyAdded;
}

bool CGame::RemoveAllReserved()
{
  bool anyRemoved = !m_Reserved.empty();
  m_Reserved.clear();
  for (auto& user : m_Users) {
    user->SetReserved(false);
  }
  return anyRemoved;
}

bool CGame::MatchOwnerName(const string& name) const
{
  string matchLower = ToLowerCase(name);
  string ownerLower = ToLowerCase(m_OwnerName);
  return matchLower == ownerLower;
}

uint8_t CGame::GetReservedIndex(const string& name) const
{
  string inputLower = ToLowerCase(name);

  uint8_t index = 0;
  while (index < m_Reserved.size()) {
    string matchLower = ToLowerCase(m_Reserved[index]);
    if (matchLower == inputLower) {
      break;
    }
    ++index;
  }

  if (index == m_Reserved.size()) {
    return 0xFF;
  }

  return index;
}

string CGame::GetBannableIP(const string& name, const string& hostName) const
{
  for (const CDBBan* bannable : m_Bannables) {
    if (bannable->GetName() == name && bannable->GetServer() == hostName) {
      return bannable->GetIP();
    }
  }
  return string();
}

bool CGame::GetIsScopeBanned(const string& rawName, const string& hostName, const string& addressLiteral) const
{
  string name = ToLowerCase(rawName);

  bool checkIP = false;
  if (!addressLiteral.empty()) {
    optional<sockaddr_storage> maybeAddress = CNet::ParseAddress(addressLiteral);
    checkIP = maybeAddress.has_value() && !isLoopbackAddress(&(maybeAddress.value()));
  }
  for (const CDBBan* ban : m_ScopeBans) {
    if (ban->GetName() == name && ban->GetServer() == hostName) {
      return true;
    }
    if (checkIP && ban->GetIP() == addressLiteral) {
      return true;
    }
  }
  return false;
}

bool CGame::CheckScopeBanned(const string& rawName, const string& hostName, const string& addressLiteral)
{
  if (GetIsScopeBanned(rawName, hostName, addressLiteral)) {
    if (m_ReportedJoinFailNames.find(rawName) == end(m_ReportedJoinFailNames)) {
      LOG_APP_IF(LogLevel::kInfo, Concat("user [", rawName, "@", hostName, "|", addressLiteral, "] entry denied: game-scope banned"));
      SendAllChat(Concat("[", rawName, "@", hostName, "] is trying to join the game, but is banned"));
      m_ReportedJoinFailNames.insert(rawName);
    } else {
      LOG_APP_IF(LogLevel::kDebug, Concat("user [", rawName, "@", hostName, "|", addressLiteral, "] entry denied: game-scope banned"));
    }
    return true;
  }
  return false;
}

bool CGame::AddScopeBan(const string& rawName, const string& hostName, const string& addressLiteral)
{
  if (m_ScopeBans.size() >= MAX_SCOPE_BANS) {
    return false;
  }

  string name = ToLowerCase(rawName);

  m_ScopeBans.push_back(new CDBBan(
    name,
    hostName,
    string(), // auth server
    addressLiteral,
    string(), // date
    string(), // expiry
    false, // temporary ban (permanent == false)
    string(), // moderator
    string() // reason
  ));
  return true;
}

bool CGame::RemoveScopeBan(const string& rawName, const string& hostName)
{
  string name = ToLowerCase(rawName);

  for (auto it = begin(m_ScopeBans); it != end(m_ScopeBans); ++it) {
    if ((*it)->GetName() == name && (*it)->GetServer() == hostName) {
      it = m_ScopeBans.erase(it);
      return true;
    }
  }
  return false;
}

vector<size_t> CGame::GetPlayersFramesBehind() const
{
  uint8_t i = static_cast<uint8_t>(m_Users.size());
  vector<size_t> framesBehind(i, 0);
  while (i--) {
    if (m_Users[i]->GetIsObserver()) {
      continue;
    }
    if (m_SyncCounter <= m_Users[i]->GetNormalSyncCounter()) {
      continue;
    }
    framesBehind[i] = static_cast<size_t>(m_SyncCounter - m_Users[i]->GetNormalSyncCounter());
  }
  return framesBehind;
}

vector<size_t> CGame::GetUsersFramesBehind() const
{
  uint8_t i = static_cast<uint8_t>(m_Users.size());
  vector<size_t> framesBehind(i, 0);
  while (i--) {
    if (m_SyncCounter <= m_Users[i]->GetNormalSyncCounter()) {
      continue;
    }
    framesBehind[i] = static_cast<size_t>(m_SyncCounter - m_Users[i]->GetNormalSyncCounter());
  }
  return framesBehind;
}

UserList CGame::GetLaggingUsers() const
{
  UserList laggingPlayers;
  if (!m_IsLagging) return laggingPlayers;
  for (const auto& user : m_Users) {
    if (!user->GetIsLagging()) {
      continue;
    }
    laggingPlayers.push_back(user);
  }
  return laggingPlayers;
}

uint8_t CGame::CountLaggingPlayers() const
{
  uint8_t count = 0;
  if (!m_IsLagging) return count;
  for (const auto& user : m_Users) {
    if (!user->GetIsLagging()) {
      continue;
    }
    ++count;
  }
  return count;
}

UserList CGame::CalculateNewLaggingPlayers() const
{
  UserList laggingPlayers;
  if (!m_IsLagging) return laggingPlayers;
  for (const auto& user : m_Users) {
    if (user->GetIsObserver()) {
      continue;
    }
    if (user->GetIsLagging() || user->GetDisconnectNoticeSent() || user->GetDisconnectedUnrecoverably()) {
      continue;
    }
    if (!user->GetFinishedLoading()) {
      laggingPlayers.push_back(user);
    } else if (m_Config.m_EnableLagScreen && !user->GetIsSyncCounterStopLag()) {
      laggingPlayers.push_back(user);
    }
  }
  return laggingPlayers;
}

void CGame::RemoveFromLagScreens(GameUser::CGameUser* user) const
{
  for (const auto& otherUser : m_Users) {
    if (user == otherUser || otherUser->GetIsInLoadingScreen()) {
      continue;
    }
    DLOG_APP_IF(LogLevel::kTrace, Concat("@[", otherUser->GetName(), "] lagger update (-", user->GetName(), ")"));
    Send(otherUser, GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetClockTicks()));
  }
}

void CGame::ResetLagScreen()
{
  const UserList laggingPlayers = GetLaggingUsers();
  if (laggingPlayers.empty()) {
    return;
  }
  const vector<uint8_t> startLagPacket = GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, m_Aura->GetClockTicks());
  const bool anyUsingGProxy = GetAnyUsingGProxy();

  if (m_GameLoading) {
    ++m_BeforePlayingEmptyActions;
  }

  for (auto& user : m_Users) {
    if (user->GetFinishedLoading()) {
      for (auto& otherUser : m_Users) {
        if (!otherUser->GetIsLagging()) continue;
        DLOG_APP_IF(LogLevel::kTrace, Concat("@[", user->GetName(), "] lagger update (-", otherUser->GetName(), ")"));
        Send(user, GameProtocol::SEND_W3GS_STOP_LAG(otherUser, m_Aura->GetClockTicks()));
      }

      Send(user, GameProtocol::GetEmptyAction());
      /*
      user->AddSyncCounterOffset(1);
      */

      // GProxy sends these empty actions itself for every action received.
      // So we need to match it, to avoid desyncs.
      // Note that Warcraft III doesn't respond to empty actions (i.e no keep alive frame).
      if (anyUsingGProxy && !user->GetCanReconnect()) {
        Send(user, GameProtocol::SEND_W3GS_EMPTY_ACTIONS(m_GProxyEmptyActions));
      }

      DLOG_APP_IF(LogLevel::kTrace, Concat("@[", user->GetName(), "] lagger update (+", ToNameListSentence(laggingPlayers), ")"));
      Send(user, startLagPacket);

      if (m_GameLoading) {
        SendChat(user, Concat("Please wait for ", to_string(laggingPlayers.size()), " player(s) to load the game."));
      }
    }
  }

  m_LastLagScreenResetTime = m_Aura->GetClockTime();
}

pair<double, double> CGame::GetLagDetectionRangeMilliSeconds(bool isObserver)
{
  if (isObserver) {
    return make_pair<double, double>((double)m_LagStopMaxObserversFrames * (double)m_NextLatencyTicks, (double)m_LagStartMinObserversFrames * (double)m_NextLatencyTicks);
  } else {
    return make_pair<double, double>((double)m_LagStopMaxPlayersFrames * (double)m_NextLatencyTicks, (double)m_LagStartMinPlayersFrames * (double)m_NextLatencyTicks);
  }
}

bool CGame::TrySetupLatency(uint16_t latency, optional<RangeSizeType> playerSyncRange, optional<RangeSizeType> observerSyncRange)
{
  if (latency <= 0) return false;
  if (playerSyncRange.has_value()) {
    // ensure stopMax < startMin
    size_t stopMax = playerSyncRange->first / (size_t)latency;
    size_t startMin = playerSyncRange->second / (size_t)latency;
    if (startMin <= stopMax) return false;
  }
  if (observerSyncRange.has_value()) {
    // ensure stopMax < startMin
    size_t stopMax = observerSyncRange->first / (size_t)latency;
    size_t startMin = observerSyncRange->second / (size_t)latency;
    if (startMin <= stopMax) return false;
  }
  SetupLatency(latency, playerSyncRange, observerSyncRange);
  return true;
}

void CGame::SetupLatency(uint16_t latency, optional<RangeSizeType> playerSyncRange, optional<RangeSizeType> observerSyncRange)
{
  m_NextLatencyTicks = static_cast<int64_t>(latency);
  if (playerSyncRange.has_value()) {
    m_LagStopMaxPlayersFrames = playerSyncRange->first / (size_t)latency;
    m_LagStartMinPlayersFrames = playerSyncRange->second / (size_t)latency;
  }
  if (observerSyncRange.has_value()) {
    m_LagStopMaxObserversFrames = observerSyncRange->first / (size_t)latency;
    m_LagStartMinObserversFrames = observerSyncRange->second / (size_t)latency;
  }
  int64_t maxFrames = ((int64_t)m_Config.m_LatencyEqualizerMaxDelay / m_NextLatencyTicks) + 1;
  m_PingEqualizerMaxFrames = maxFrames > 0xFF ? 0xFF : static_cast<uint8_t>(maxFrames);
}

void CGame::ResetLatency()
{
  SetupLatency(
    m_Config.m_Latency,
    RangeSizeType{(size_t)m_Config.m_LagStopDefaultControllerSyncMilliSeconds, (size_t)m_Config.m_LagStartDefaultControllerSyncMilliSeconds},
    RangeSizeType{(size_t)m_Config.m_LagStopDefaultObserverSyncMilliSeconds, (size_t)m_Config.m_LagStartDefaultObserverSyncMilliSeconds}
  );

  for (auto& user : m_Users)  {
    user->ResetSyncCounterOffset();
  }
}

void CGame::NormalizeSyncCounters() const
{
  for (auto& user : m_Users) {
    if (user->GetIsObserver()) continue;
    size_t normalSyncCounter = user->GetNormalSyncCounter();
    if (m_SyncCounter <= normalSyncCounter) {
      continue;
    }
    user->AddSyncCounterOffset(m_SyncCounter - normalSyncCounter);
  }
}

bool CGame::GetIsReserved(const string& name) const
{
  return GetReservedIndex(name) < m_Reserved.size();
}

bool CGame::GetIsProxyReconnectable() const
{
  if (m_IsMirror) return 0 != m_Config.m_ReconnectionMode;
  return 0 != (m_Aura->m_Net.m_Config.m_ProxyReconnect & m_Config.m_ReconnectionMode);
}

bool CGame::GetIsProxyReconnectableLong() const
{
  if (m_IsMirror) return 0 != (m_Config.m_ReconnectionMode & RECONNECT_ENABLED_GPROXY_EXTENDED);
  return 0 != ((m_Aura->m_Net.m_Config.m_ProxyReconnect & m_Config.m_ReconnectionMode) & RECONNECT_ENABLED_GPROXY_EXTENDED);
}

bool CGame::IsDownloading() const
{
  // returns true if at least one user is downloading the map

  for (auto& user : m_Users)
  {
    if (user->GetMapTransfer().GetStarted() && !user->GetMapTransfer().GetFinished())
      return true;
  }

  return false;
}

void CGame::UncacheOwner()
{
  for (auto& user : m_Users) {
    user->SetOwner(false);
  }
}

void CGame::SetOwner(string_view name, string_view realm)
{
  m_OwnerName = string(name);
  m_OwnerRealm = string(realm);
  m_LastOwnerAssignedTicks = m_Aura->GetClockTicks();

  UncacheOwner();

  GameUser::CGameUser* user = GetUserFromName<CaseSensitive::kNormalize>(name);
  if (user && user->GetRealmHostName() == realm) {
    user->SetOwner(true);

    // Owner is never kicked for latency reasons.
    user->RemoveKickReason(GameUser::KickReason::kHighPing);
    user->CheckStillKicked();
  }
}

void CGame::ReleaseOwner()
{
  if (m_Exiting) {
    return;
  }
  LOG_APP_IF(LogLevel::kInfo, Concat("Owner \"", m_OwnerName, "@", ToFormattedRealm(m_OwnerRealm), "\" removed."));
  m_LastOwner = m_OwnerName;
  m_OwnerName.clear();
  m_OwnerRealm.clear();
  UncacheOwner();
  ResetLayout(false);
  m_Locked = false;
  SendAllChat(Concat("This game is now ownerless. Type ", GetCmdToken(), "owner to take ownership of this game."));
}

void CGame::ResetDraft()
{
  m_IsDraftMode = true;
  for (auto& user : m_Users) {
    user->SetDraftCaptain(0);
  }
}

void CGame::ResetTeams(const bool alsoCaptains)
{
  if (!(m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat || m_Map->GetGameObservers() == GameObserversMode::kReferees)) {
    return;
  }
  uint8_t SID = GetNumSlots();
  while (SID--) {
    CGameSlot* slot = GetSlot(SID);
    if (slot->GetTeam() == GetObserverTeam()) continue;
    if (!slot->GetIsPlayerOrFake()) continue;
    if (!alsoCaptains) {
      GameUser::CGameUser* user = GetUserFromSID(SID);
      if (user && user->GetIsDraftCaptain()) continue;
    }
    if (!SetSlotTeam(SID, GetObserverTeam(), false)) {
      break;
    }
  }
}

void CGame::ResetSync()
{
  m_SyncCounter = 0;
  for (auto& TargetPlayer: m_Users) {
    TargetPlayer->SetSyncCounter(0);
  }
}

void CGame::CountKickVotes()
{
  uint32_t Votes = 0, VotesNeeded = static_cast<uint32_t>(ceil((GetNumJoinedPlayers() - 1) * static_cast<float>(m_Config.m_VoteKickPercentage) / 100));
  for (auto& eachPlayer : m_Users) {
    if (eachPlayer->GetKickVote().value_or(false))
      ++Votes;
  }

  if (Votes >= VotesNeeded) {
    GameUser::CGameUser* victim = GetUserFromName(m_KickVotePlayer, true);

    if (victim) {
      if (!victim->HasLeftReason()) {
        victim->SetLeftReason("was kicked by vote");
        victim->SetLeftCode(PLAYERLEAVE_LOST);
      }
      victim->CloseConnection();

      Log(Concat("votekick against user [", m_KickVotePlayer, "] passed with ", to_string(Votes), "/", to_string(GetNumJoinedPlayers()), " votes"));
      SendAllChat(Concat("A votekick against user [", m_KickVotePlayer, "] has passed"));
    } else {
      LOG_APP_IF(LogLevel::kError, Concat("votekick against user [", m_KickVotePlayer, "] errored"));
    }

    m_KickVotePlayer.clear();
    m_StartedKickVoteTime = 0;
  }
}

bool CGame::GetCanStartGracefulCountDown() const
{
  if (m_CountDownStarted || m_ChatOnly) {
    return false;
  }

  if (m_Aura->m_StartedGames.size() >= m_Aura->m_Config.m_MaxStartedGames) {
    return false;
  }

  if (m_HCLCommandString.size() > GetNumSlotsOccupied()) {
    return false;
  }

  bool enoughTeams = false;
  uint8_t sameTeam = GetObserverTeam();
  for (const auto& slot : m_SlotsConfig.slots) {
    if (slot.GetIsPlayerOrFake() && slot.GetDownloadStatus() != 100) {
      GameUser::CGameUser* Player = GetUserFromUID(slot.GetUID());
      if (Player) {
        return false;
      }
    }
    if (slot.GetTeam() != GetObserverTeam()) {
      if (sameTeam == GetObserverTeam()) {
        sameTeam = slot.GetTeam();
      } else if (sameTeam != slot.GetTeam()) {
        enoughTeams = true;
      }
    }
  }

  if (0 == m_ControllersWithMap) {
    return false;
  } else if (m_ControllersWithMap < 2 && !m_RestoredGame) {
    return false;
  } else if (!enoughTeams) {
    return false;
  }

  if (GetNumJoinedPlayers() >= 2) {
    for (const auto& user : m_Users) {
      if (user->GetIsReserved() || user->GetIsOwner(nullopt) || user->GetIsObserver()) {
        continue;
      }
      if (!user->GetIsRTTMeasuredConsistent()) {
        return false;
      } else if (user->GetPingKicked()) {
        return false;
      }
    }
  }

  for (const auto& user : m_Users) {
    // Skip non-referee observers
    if (!user->GetIsOwner(nullopt) && user->GetIsObserver()) {
      if (m_Map->GetGameObservers() != GameObserversMode::kReferees) continue;
      if (m_UsesCustomReferees && !user->GetIsPowerObserver()) continue;
    }

    shared_ptr<CRealm> realm = user->GetRealm(false);
    if (realm && realm->GetUnverifiedCannotStartGame() && !user->GetIsRealmVerified()) {
      return false;
    }
  }

  return m_Aura->GetTicksIsFirstOrAfterDelay(m_LastPlayerLeaveTicks, 2000);
}

void CGame::StartCountDown(bool fromUser, bool force)
{
  if (m_CountDownStarted)
    return;

  if (m_ChatOnly) {
    SendAllChat("This lobby is in chat-only mode. Please join another hosted game.");
    shared_ptr<const CGame> recentLobby = m_Aura->GetMostRecentLobby();
    if (recentLobby && recentLobby != shared_from_this()) {
      SendAllChat(Concat("Currently hosting: ", recentLobby->GetStatusDescription()));
    }
    return;
  }

  if (m_Aura->m_StartedGames.size() >= m_Aura->m_Config.m_MaxStartedGames) {
    SendAllChat(Concat("This game cannot be started while there are ", to_string(m_Aura->m_Config.m_MaxStartedGames), " additional games in progress."));
    return;
  }

  if (m_Map->GetHMCEnabled()) {
    const uint8_t SID = m_Map->GetHMCSlot();
    const CGameSlot* slot = InspectSlot(SID);
    if (!slot || !slot->GetIsPlayerOrFake() || GetUserFromSID(SID)) {
      SendAllChat(Concat("This game requires a fake player on slot ", ToDecString(ToBaseOne(SID))));
      return;
    }
    const CGameVirtualUser* virtualUserMatch = InspectVirtualUserFromSID(SID);
    if (virtualUserMatch && virtualUserMatch->GetIsObserver()) {
      SendAllChat(Concat("This game requires a fake player (not observer) on slot ", ToDecString(ToBaseOne(SID))));
      return;
    }
    if (!virtualUserMatch && m_Map->GetHMCRequired()) {
      SendAllChat(Concat("This game requires a fake player on slot ", ToDecString(ToBaseOne(SID))));
      return;
    }
  }

  // if the user sent "!start force" skip the checks and start the countdown
  // otherwise check that the game is ready to start

  uint8_t sameTeam = GetObserverTeam();

  if (force) {
    for (const auto& user : m_Users) {
      bool shouldKick = !user->GetMapReady();
      if (!shouldKick) {
        shared_ptr<CRealm> realm = user->GetRealm(false);
        if (realm && realm->GetUnverifiedCannotStartGame() && !user->GetIsRealmVerified()) {
          shouldKick = true;
        }
      }
      if (shouldKick) {
        if (!user->HasLeftReason()) {
          user->SetLeftReason("kicked when starting the game");
        }
        user->CloseConnection();
        CloseSlot(GetSIDFromUID(user->GetUID()), true);
      }
    }
  } else {
    bool ChecksPassed = true;
    bool enoughTeams = false;

    // check if the HCL command string is short enough
    if (m_HCLCommandString.size() > GetNumSlotsOccupied()) {
      SendAllChat(Concat("The HCL command string is too long. Use [", GetCmdToken(), "go force] to start anyway"));
      ChecksPassed = false;
    }

    UserList downloadingUsers;

    // check if everyone has the map
    for (const auto& slot : m_SlotsConfig.slots) {
      if (slot.GetIsPlayerOrFake() && slot.GetDownloadStatus() != 100) {
        GameUser::CGameUser* player = GetUserFromUID(slot.GetUID());
        if (player) downloadingUsers.push_back(player);
      }
      if (slot.GetTeam() != GetObserverTeam()) {
        if (sameTeam == GetObserverTeam()) {
          sameTeam = slot.GetTeam();
        } else if (sameTeam != slot.GetTeam()) {
          enoughTeams = true;
        }
      }
    }
    if (!downloadingUsers.empty()) {
      SendAllChat(Concat("Players still downloading the map: ", ToNameListSentence(downloadingUsers)));
      ChecksPassed = false;
    } else if (0 == m_ControllersWithMap) {
      SendAllChat("Nobody has downloaded the map yet.");
      ChecksPassed = false;
    } else if (m_ControllersWithMap < 2 && !m_RestoredGame) {
      SendAllChat(Concat("Only ", to_string(m_ControllersWithMap), " user has the map."));
      ChecksPassed = false;
    } else if (!enoughTeams) {
      SendAllChat("Players are not arranged in teams.");
      ChecksPassed = false;
    }

    UserList highPingUsers;
    UserList pingNotMeasuredUsers;
    UserList unverifiedUsers;

    // check if everyone's ping is measured and acceptable
    if (GetNumJoinedPlayers() >= 2) {
      for (const auto& user : m_Users) {
        if (user->GetIsReserved() || user->GetIsOwner(nullopt) || user->GetIsObserver()) {
          continue;
        }
        if (!user->GetIsRTTMeasuredConsistent()) {
          pingNotMeasuredUsers.push_back(user);
        } else if (user->GetPingKicked()) {
          highPingUsers.push_back(user);
        }
      }
    }

    for (const auto& user : m_Users) {
      // Skip non-referee observers
      if (!user->GetIsOwner(nullopt) && user->GetIsObserver()) {
        if (m_Map->GetGameObservers() != GameObserversMode::kReferees) continue;
        if (m_UsesCustomReferees && !user->GetIsPowerObserver()) continue;
      }
      shared_ptr<CRealm> realm = user->GetRealm(false);
      if (realm && realm->GetUnverifiedCannotStartGame() && !user->GetIsRealmVerified()) {
        unverifiedUsers.push_back(user);
      }
    }

    if (!highPingUsers.empty()) {
      SendAllChat(Concat("Players with high ping: ", ToNameListSentence(highPingUsers)));
      ChecksPassed = false;
    }
    if (!pingNotMeasuredUsers.empty()) {
      SendAllChat(Concat("Players NOT yet pinged thrice: ", ToNameListSentence(pingNotMeasuredUsers)));
      ChecksPassed = false;
    }
    if (!unverifiedUsers.empty()) {
      SendAllChat(Concat("Players NOT verified (whisper sc): ", ToNameListSentence(unverifiedUsers)));
      ChecksPassed = false;
    }
    if (!m_Aura->GetTicksIsFirstOrAfterDelay(m_LastPlayerLeaveTicks, 2000)) {
      SendAllChat("Someone left the game less than two seconds ago!");
      ChecksPassed = false;
    }

    if (!ChecksPassed)
      return;
  }

  m_Replaceable = false;
  m_CountDownStarted = true;
  m_CountDownUserInitiated = fromUser;
  m_CountDownCounter = m_Config.m_LobbyCountDownStartValue;

  if (!m_KickVotePlayer.empty()) {
    m_KickVotePlayer.clear();
    m_StartedKickVoteTime = 0;
  }

  for (auto& user : m_Users) {
    if (!user->GetDisconnected()) {
      user->ResetKickReason();
      user->ResetLeftReason();
    }
    if (user->GetKickQueued()) {
      user->ClearKickByTicks();
    }
  }

  if (GetNumJoinedUsersOrFake() == 1 && (0 == GetNumSlotsOpen() || m_Map->GetGameObservers() != GameObserversMode::kReferees)) {
    SendAllChat("HINT: Single-user game detected. In-game commands will be DISABLED.");
    if (GetNumSlotsOccupied() != m_Map->GetVersionMaxSlots()) {
      SendAllChat(Concat("HINT: To avoid this, you may enable map referees, or add a fake user [", GetCmdToken(), "fp]"));
    }
  }

  if (m_FakeUsers.size() == 1) {
    SendAllChat(Concat("HINT: ", to_string(m_FakeUsers.size()), " slot is occupied by a fake user."));
  } else if (!m_FakeUsers.empty()) {
    SendAllChat(Concat("HINT: ", to_string(m_FakeUsers.size()), " slots are occupied by fake users."));
  }
}

void CGame::StartCountDownFast(bool fromUser)
{
  StartCountDown(fromUser, true);
  if (m_CountDownStarted) {
    // 500 ms countdown
    m_CountDownCounter = 1;
    m_CountDownFast = true;
  }
}

void CGame::StopCountDown()
{
  m_CountDownStarted = false;
  m_CountDownFast = false;
  m_CountDownUserInitiated = false;
  m_CountDownCounter = 0;
}

bool CGame::StopPlayers(const string& reason)
{
  // disconnect every user and set their left reason to the passed string
  // we use this function when we want the code in the Update function to run before the destructor (e.g. saving users to the database)
  // therefore calling this function when m_GameLoading || m_GameLoaded is roughly equivalent to setting m_Exiting = true
  // the only difference is whether the code in the Update function is executed or not

  bool anyStopped = false;
  for (auto& user : m_Users) {
    if (user->GetDeleteMe()) continue;
    user->SetLeftReason(reason);
    user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
    user->TrySetEnding();
    user->DisableReconnect();
    user->CloseConnection();
    user->SetDeleteMe(true);
    anyStopped = true;
  }
  m_PauseUser = nullptr;
  return anyStopped;
}

void CGame::StopLagger(GameUser::CGameUser* user, const string& reason)
{
  RemoveFromLagScreens(user);
  user->SetLeftReason(reason);
  user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
  user->DisableReconnect();
  user->CloseConnection();
  user->SetLagging(false);

  if (!user->GetIsEndingOrEnded()) {
    Resume(user, user->GetPingEqualizerFrame(), true);
    QueueLeftMessage(user);
  }
}

/*
 * CGame::StopLaggers(const string& reason)
 * When load-in-game is enabled, this will also drop users that haven't finished loading.
 */
void CGame::StopLaggers(const string& reason)
{
  UserList laggingUsers = GetLaggingUsers();
  for (const auto& user : laggingUsers) {
    StopLagger(user, reason);
  }
  bool saved = false;
  for (const auto& user : laggingUsers) {
    TryShareUnitsOnDisconnect(user, false);
    if (!saved) saved = TrySaveOnDisconnect(user, false);
  }
  ResetDropVotes();
}

void CGame::ResetDropVotes() const
{
  for (auto& user : m_Users) {
    user->SetDropVote(false);
  }
}

void CGame::StopDesynchronized(const string& reason)
{
  size_t majorityThreshold = m_Users.size() / 2;
  for (GameUser::CGameUser* user : m_Users) {
    auto it = m_SyncPlayers.find(static_cast<const GameUser::CGameUser*>(user));
    if (it == m_SyncPlayers.end()) {
      continue;
    }
    if ((it->second).size() < majorityThreshold) {
      user->SetLeftReason(reason);
      user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
      user->DisableReconnect();
      user->CloseConnection();

      if (!user->GetIsEndingOrEnded()) {
        Resume(user, user->GetPingEqualizerFrame(), true);
        QueueLeftMessage(user);
      }
    }
  }
}

void CGame::StopLoadPending(const string& reason)
{
  if (m_Config.m_LoadInGame) {
    StopLaggers(reason);
  } else {
    for (GameUser::CGameUser* user : m_Users) {
      if (user->GetFinishedLoading()) {
        continue;
      }
      user->SetLeftReason(reason);
      user->SetLeftCode(PLAYERLEAVE_DISCONNECT);
      user->DisableReconnect();
      user->CloseConnection();
    }
  }
}

string CGame::GetSaveFileName(const uint8_t UID) const
{
  auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
  struct tm timeinfo;
#ifdef _WIN32
  localtime_s(&timeinfo, &now);
#else
  localtime_r(&now, &timeinfo);
#endif

  ostringstream oss;
  oss << put_time(&timeinfo, "%m-%d_%H-%M");
  return Concat("auto_p", ToDecString(GetSIDFromUID(UID) + 1), "_", oss.str(), ".w3z");
}

bool CGame::Save(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect)
{
  const uint8_t UID = SimulateActionUID(ACTION_SAVE, user, isDisconnect, ACTION_SOURCE_ANY & NOT_ACTION_SOURCE_OBSERVER);
  if (UID == 0xFF) return false;

  string fileName = GetSaveFileName(UID);
  LOG_APP_IF(LogLevel::kInfo, Concat("saving as ", fileName));

  {
    const uint32_t success = 1;
    vector<uint8_t> ActionStart, ActionEnd;
    ActionStart.push_back(ACTION_SAVE);
    AppendByteArrayString(ActionStart, fileName, true);
    ActionEnd.push_back(ACTION_SAVE_ENDED);
    AppendNumberLE(ActionEnd, success);
    actionFrame.AddAction(std::move(CIncomingAction(UID, ActionStart)));
    actionFrame.AddAction(std::move(CIncomingAction(UID, ActionEnd)));
  }

  // Add actions for everyone else saves finishing
  SaveEnded(UID);
  return true;
}

void CGame::SaveEnded(const uint8_t exceptUID, CQueuedActionsFrame& actionFrame)
{
  const uint32_t success = 1;
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    if (fakeUser.GetUID() == exceptUID || !fakeUser.GetCanSaveEnded()) {
      continue;
    }
    vector<uint8_t> Action;
    Action.push_back(ACTION_SAVE_ENDED);
    AppendNumberLE(Action, success);
    actionFrame.AddAction(std::move(CIncomingAction(fakeUser.GetUID(), Action)));
  }
}

bool CGame::Pause(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect)
{
  const uint8_t UID = SimulateActionUID(ACTION_PAUSE, user, isDisconnect, ACTION_SOURCE_ANY & NOT_ACTION_SOURCE_OBSERVER);
  if (UID == 0xFF) return false;

  actionFrame.AddAction(std::move(CIncomingAction(UID, ACTION_PAUSE)));
  if (actionFrame.callback != ON_SEND_ACTIONS_PAUSE) {
    actionFrame.callback = ON_SEND_ACTIONS_PAUSE;
    actionFrame.pauseUID = user->GetUID();
  }
  return true;
}

bool CGame::Resume(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect)
{
  const uint8_t UID = SimulateActionUID(ACTION_RESUME, user, isDisconnect, ACTION_SOURCE_ANY & NOT_ACTION_SOURCE_OBSERVER);
  if (UID == 0xFF) return false;

  actionFrame.AddAction(std::move(CIncomingAction(UID, ACTION_RESUME)));
  actionFrame.callback = ON_SEND_ACTIONS_RESUME;
  //actionFrame.pauseUID = 0xFF;
  return true;
}

bool CGame::SendMiniMapSignal(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect, const float x, const float y, const uint32_t duration)
{
  const uint8_t UID = SimulateActionUID(ACTION_MINIMAPSIGNAL, user, isDisconnect, ACTION_SOURCE_ANY);
  if (UID == 0xFF) return false;

  {
    vector<uint8_t> Action;
    Action.push_back(ACTION_MINIMAPSIGNAL);
    AppendNumberLE(Action, x);
    AppendNumberLE(Action, y);
    AppendNumberLE(Action, duration);
    actionFrame.AddAction(std::move(CIncomingAction(UID, Action)));
  }

  return true;
}

bool CGame::Save(GameUser::CGameUser* user, const bool isDisconnect)
{
  return Save(user, GetLastActionFrame(), isDisconnect);
}

void CGame::SaveEnded(const uint8_t exceptUID)
{
  SaveEnded(exceptUID, GetLastActionFrame());
}

bool CGame::Pause(GameUser::CGameUser* user, const bool isDisconnect)
{
  return Pause(user, GetLastActionFrame(), isDisconnect);
}

bool CGame::Resume(GameUser::CGameUser* user, const bool isDisconnect)
{
  return Resume(user, GetLastActionFrame(), isDisconnect);
}

bool CGame::SendMiniMapSignal(GameUser::CGameUser* user, const bool isDisconnect, const float x, const float y, const uint32_t duration)
{
  return SendMiniMapSignal(user, GetLastActionFrame(), isDisconnect, x, y, duration);
}

bool CGame::Trade(const uint8_t fromUID, const uint8_t SID, CQueuedActionsFrame& actionFrame, const uint32_t gold, const uint32_t lumber)
{
  vector<uint8_t> Action;
  Action.push_back(ACTION_TRANSFER_RESOURCES);
  Action.push_back(SID);
  AppendNumberLE(Action, gold);
  AppendNumberLE(Action, lumber);
  actionFrame.AddAction(std::move(CIncomingAction(fromUID, Action)));
  return true;
}

bool CGame::Trade(GameUser::CGameUser* fromUser, const uint8_t SID, CQueuedActionsFrame& actionFrame, const bool isDisconnect, const uint32_t gold, const uint32_t lumber)
{
  if (!isDisconnect) return false;
  return Trade(fromUser->GetUID(), SID, actionFrame, gold, lumber);
}

bool CGame::Trade(GameUser::CGameUser* fromUser, const uint8_t SID, const bool isDisconnect, const uint32_t gold, const uint32_t lumber)
{
  return Trade(fromUser, SID, GetLastActionFrame(), isDisconnect, gold, lumber);
}

bool CGame::ShareUnits(const uint8_t fromUID, const uint8_t SID, CQueuedActionsFrame& actionFrame)
{
  vector<uint8_t> Action;
  Action.push_back(ACTION_ALLIANCE_SETTINGS);
  Action.push_back(SID);
  AppendNumberLE(Action, ALLIANCE_SETTINGS_ALLY | ALLIANCE_SETTINGS_SHARED_VISION | ALLIANCE_SETTINGS_SHARED_CONTROL | ALLIANCE_SETTINGS_SHARED_VICTORY);
  actionFrame.AddAction(std::move(CIncomingAction(fromUID, Action)));
  return true;
}

bool CGame::ShareUnits(GameUser::CGameUser* fromUser, const uint8_t SID, CQueuedActionsFrame& actionFrame, const bool /*isDisconnect*/)
{
  return ShareUnits(fromUser->GetUID(), SID, actionFrame);
}

bool CGame::ShareUnits(GameUser::CGameUser* fromUser, const uint8_t SID, const bool isDisconnect)
{
  if (!isDisconnect) return false;
  return ShareUnits(fromUser, SID, GetLastActionFrame(), isDisconnect);
}

bool CGame::SendChatTrigger(const uint8_t UID, const string& message, const uint32_t firstValue, const uint32_t secondValue)
{
  vector<uint8_t> action = {ACTION_CHAT_TRIGGER};
  AppendNumberLE(action, firstValue);
  AppendNumberLE(action, secondValue);
  AppendByteArrayString(action, message, true);
  GetLastActionFrame().AddAction(std::move(CIncomingAction(UID, action)));
  return true;
}

bool CGame::SendChatTriggerBytes(const uint8_t UID, const string& message, const array<uint8_t, 8>& triggerBytes)
{
  vector<uint8_t> action = {ACTION_CHAT_TRIGGER};
  AppendContainer(action, triggerBytes);
  AppendByteArrayString(action, message, true);
  GetLastActionFrame().AddAction(std::move(CIncomingAction(UID, action)));
  return true;
}

bool CGame::SendChatTriggerSymmetric(const uint8_t UID, const string& message, const uint16_t identifier)
{
  return SendChatTrigger(UID, message, (uint32_t)identifier, (uint32_t)identifier);
}

void CGame::TryActionsOnDisconnect(GameUser::CGameUser* user, const bool isVoluntary)
{
  TryShareUnitsOnDisconnect(user, isVoluntary);
  TrySaveOnDisconnect(user, isVoluntary);
}

bool CGame::TryShareUnitsOnDisconnect(GameUser::CGameUser* user, const bool /*isVoluntary*/)
{
  if (m_Config.m_LeaverHandler != OnPlayerLeaveHandler::kShareUnits) return false;
  if (!m_GameLoaded) {
    return false;
  }
  uint8_t fromSID = GetSIDFromUID(user->GetUID());
  if (fromSID == 0xFF) return false;

  uint8_t SID = GetNumSlots();
  while (SID--) {
    if (SID == fromSID || !m_SlotsConfig.Inspect(SID).GetIsPlayerOrFake()) continue;
    if (m_SlotsConfig.Inspect(SID).GetTeam() != m_SlotsConfig.Inspect(SID).GetTeam()) continue;
    ShareUnits(user, SID, true);
  }
  return true;
}

bool CGame::TrySaveOnDisconnect(GameUser::CGameUser* user, const bool isVoluntary)
{
  if (m_SaveOnLeave == SAVE_ON_LEAVE_NEVER) {
    return false;
  }

  if (!m_GameLoaded || m_Users.size() <= 1) {
    // Nobody can actually save this game.
    return false;
  }

  if (GetNumControllers() <= 2) {
    // 1v1 never auto-saves, not even if there are observers,
    // not even if !save enable is active.
    return false;
  }

  if (!GetLaggingUsers().empty()) {
    return false;
  }

  if (m_SaveOnLeave != SAVE_ON_LEAVE_ALWAYS) {
    if (isVoluntary) {
      // Is saving on voluntary leaves pointless?
      //
      // Not necessarily.
      //
      // Even if rage quits are unlikely to be reverted,
      // leavers may be replaced by fake users
      // This is impactful in maps such as X Hero Siege,
      // where leavers' heroes are automatically removed.
      //
      // However, since voluntary leaves are the rule, even
      // when games end normally, saving EVERYTIME will turn out to be annoying.
      //
      // Instead, we can do it only if the game has been running for a preconfigured time.
      // But how much time will be relative according to the map.
      // And it would force usage of mapcfg files...
      //
      // Considering that it's also not exactly trivial to load a game,
      // then automating this would bring about too many cons.
      //
      // Which is why allow autosaving on voluntary leaves,
      // but only if users want so by using !save enable.
      // Sadly, that's not the case in this branch, so no save for you.
      return false;
    } else if (!m_Aura->GetTicksIsAfterDelay(m_FinishedLoadingTicks, 420000)) {
      // By default, leaves before the 7th minute do not autosave.
      return false;
    }
  }

  if (Save(user, true)) {
    Pause(user, true);
    // In FFA games, it's okay to show the real name (instead of GetDisplayName()) when disconnected.
    SendAllChat(Concat("Game saved on ", user->GetName(), "'s disconnection."));
    SendAllChat("They may rejoin on reload if an ally sends them their save. Foes' save files will NOT work.");
    SendAllChat("Game is on pause. (F10 to Resume).");
    return true;
  } else {
    LOG_APP_IF(LogLevel::kWarning, "Failed to automatically save game on leave");
  }

  return false;
}

bool CGame::GetIsCheckJoinable() const
{
  return m_Config.m_CheckJoinable;
}

void CGame::SetIsCheckJoinable(const bool nCheckIsJoinable) 
{
  m_Config.m_CheckJoinable = nCheckIsJoinable;
}

bool CGame::GetHasReferees() const
{
  return m_Map->GetGameObservers() == GameObserversMode::kReferees;
}

bool CGame::GetIsSupportedGameVersion(const Version& version) const
{
  if (!GetIsValidVersion(version)) return false;
  if (m_GameLoaded) {
    switch (m_Config.m_CrossPlayMode) {
      case CrossPlayMode::kForce:
        break;
      case CrossPlayMode::kOptimistic:
        if (!m_LoadedSlotsProtocol.has_value() || m_LoadedSlotsProtocol.value() != GetSlotsProtocolVersion(version)) {
          // Different slots will cause insta-desync otherwise
          return false;
        }
        break;
      default:
        if (!m_LoadedVersion.has_value() || m_LoadedVersion.value() != version) {
          return false;
        }
        break;
    }
  }
  return m_SupportedGameVersions.test(ToVersionOrdinal(version));
}

void CGame::SetSupportedGameVersion(const Version& version) {
  if (!GetIsValidVersion(version)) return;
  m_SupportedGameVersions.set(ToVersionOrdinal(version));
  if (version < m_SupportedGameVersionsMin) m_SupportedGameVersionsMin = version;
  if (version > m_SupportedGameVersionsMax) m_SupportedGameVersionsMax = version;
}

void CGame::OpenObserverSlots()
{
  const uint8_t enabledCount = m_Map->GetVersionMaxSlots() - GetMap()->GetMapNumDisabled();
  if (m_SlotsConfig.GetCount() >= enabledCount) return;
  LOG_APP_IF(LogLevel::kDebug, Concat("adding ", to_string(enabledCount - m_SlotsConfig.GetCount()), " observer slots"));
  while (m_SlotsConfig.GetCount() < enabledCount) {
    m_SlotsConfig.slots.emplace_back(GetIsCustomForces() ? SLOTTYPE_NONE : SLOTTYPE_USER, UID_ZERO, SLOTPROG_RST, SLOTSTATUS_OPEN, SLOTCOMP_NO, GetObserverTeam(), GetObserverColor(), SLOTRACE_RANDOM);
  }
}

void CGame::CloseObserverSlots()
{
  uint8_t count = 0;
  uint8_t i = GetNumSlots();
  while (i--) {
    if (m_SlotsConfig.GetIsObserver(i)) {
      m_SlotsConfig.slots.erase(m_SlotsConfig.slots.begin() + i);
      ++count;
    }
  }
  if (count > 0 && m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    LogApp(Concat("deleted ", to_string(count), " observer slots"), LOG_C);
  }
}

bool CGame::GetHasVirtualHost() const
{
  return m_VirtualHostUID != 0xFF;
}

// Virtual host is needed to generate network traffic when only one user is in the game or lobby.
// Fake users may also accomplish the same purpose.
bool CGame::CreateVirtualHost()
{
  if (GetHasVirtualHost())
    return false;

  if (m_GameLoading || m_GameLoaded) {
    // In principle, CGame::CreateVirtualHost() should not be called when the game has started loading,
    // but too many times has that asssumption broke due to faulty logic.
    LOG_APP_IF(LogLevel::kDebug, "Rejected creation of virtual host after game started");
    return false;
  }

  m_VirtualHostUID = GetNewUID();

  // When this message is sent because an slot is made available by a leaving user,
  // we gotta ensure that the virtual host join message is sent after the user's leave message.
  if (!m_Users.empty()) {
    SendAll(GameProtocol::SEND_W3GS_PLAYERINFO_EXCLUDE_IP(GetVersion(), m_VirtualHostUID, GetLobbyVirtualHostName()));
  }
  return true;
}

bool CGame::DeleteVirtualHost()
{
  if (!GetHasVirtualHost()) {
    return false;
  }

  // When this message is sent because the last slot is filled by an incoming user,
  // we gotta ensure that the virtual host leave message is sent before the user's join message.
  if (!m_Users.empty()) {
    SendAll(GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(m_VirtualHostUID, PLAYERLEAVE_LOBBY));
  }
  m_VirtualHostUID = 0xFF;
  return true;
}

bool CGame::GetHasPvPGNPlayers() const
{
  for (const auto& user : m_Users) {
    if (user->GetRealm(false)) {
      return true;
    }
  }
  return false;
}

bool CGame::GetIsSlotReservedForSystemVirtualUser(const uint8_t SID) const
{
  if (m_Map->GetHMCEnabled() && SID == m_Map->GetHMCSlot()) return true;
  if (m_Map->GetAHCLEnabled() && SID == m_Map->GetAHCLSlot()) return true;
  return false;
}

bool CGame::GetIsSlotAssignedToSystemVirtualUser(const uint8_t SID) const
{
  // HMC works by letting virtual user send triggers
  if (m_HMCVirtualUser.has_value() && m_HMCVirtualUser->GetSID() == SID) return true;
  // AHCL works by letting virtual user send actions
  if (m_AHCLVirtualUser.has_value() && m_AHCLVirtualUser->GetSID() == SID) return true;
  // WC3Stats parser hack
  if (m_InertVirtualUser.has_value() && m_InertVirtualUser->GetSID() == SID) return true;
  if (m_JoinInProgressVirtualUser.has_value() && m_JoinInProgressVirtualUser->GetSID() == SID) return true;
  return false;
}

CGameVirtualUser* CGame::GetVirtualUserFromSID(const uint8_t SID)
{
  uint8_t i = static_cast<uint8_t>(m_FakeUsers.size());
  while (i--) {
    if (SID == m_FakeUsers[i].GetSID()) {
      return &(m_FakeUsers[i]);
    }
  }
  return nullptr;
}

const CGameVirtualUser* CGame::InspectVirtualUserFromSID(const uint8_t SID) const
{
  uint8_t i = static_cast<uint8_t>(m_FakeUsers.size());
  while (i--) {
    if (SID == m_FakeUsers[i].GetSID()) {
      return &(m_FakeUsers[i]);
    }
  }
  return nullptr;
}

CGameVirtualUser* CGame::CreateFakeUserInner(const uint8_t SID, const uint8_t UID, const string& name, bool asObserver)
{
  const bool isCustomForces = GetIsCustomForces();
  if (!m_Users.empty()) {
    SendAll(GameProtocol::SEND_W3GS_PLAYERINFO_EXCLUDE_IP(GetVersion(), UID, name));
  }
  m_SlotsConfig.slots[SID] = CGameSlot(
    m_SlotsConfig.Inspect(SID).GetType(),
    UID,
    SLOTPROG_RDY,
    SLOTSTATUS_OCCUPIED,
    SLOTCOMP_NO,
    isCustomForces ? m_SlotsConfig.Inspect(SID).GetTeam() : GetObserverTeam(),
    isCustomForces ? m_SlotsConfig.Inspect(SID).GetColor() : GetObserverColor(),
    m_Map->GetLobbyRace(&m_SlotsConfig.Inspect(SID))
  );
  if (!isCustomForces && !asObserver) SetSlotTeamAndColorAuto(SID);

  m_FakeUsers.emplace_back(shared_from_this(), SID, UID, name).SetIsObserver(m_SlotsConfig.GetIsObserver(SID));
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  return &m_FakeUsers.back();
}

bool CGame::CreateFakeUser(const optional<string> playerName)
{
  // Fake users need not be explicitly restricted in any layout, so let's just use an empty slot.
  uint8_t SID = GetEmptySID(false);
  if (SID >= GetNumSlots()) return false;
  if (!CanLockSlotForJoins(SID)) return false;

  if (GetNumSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), playerName.value_or(Concat("User[", ToDecString(ToBaseOne(SID)), "]")), false);
  return true;
}

bool CGame::CreateFakePlayer(const optional<string> playerName)
{
  const bool isCustomForces = GetIsCustomForces();
  uint8_t SID = isCustomForces ? GetEmptyPlayerSID() : GetEmptySID(false);
  if (SID >= GetNumSlots()) return false;

  if (isCustomForces && (m_SlotsConfig.GetIsObserver(SID))) {
    return false;
  }
  if (!CanLockSlotForJoins(SID)) {
    return false;
  }
  if (GetNumSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), playerName.value_or(Concat("User[", ToDecString(ToBaseOne(SID)), "]")), false);
  return true;
}

bool CGame::CreateFakeObserver(const optional<string> playerName)
{
  if (!(m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat || m_Map->GetGameObservers() == GameObserversMode::kReferees)) {
    return false;
  }

  const bool isCustomForces = GetIsCustomForces();
  uint8_t SID = isCustomForces ? GetEmptyObserverSID() : GetEmptySID(false);
  if (SID >= GetNumSlots()) return false;

  if (isCustomForces && (m_SlotsConfig.Inspect(SID).GetTeam() != GetObserverTeam())) {
    return false;
  }
  if (!CanLockSlotForJoins(SID)) {
    return false;
  }
  if (GetNumSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), playerName.value_or(Concat("User[", ToDecString(ToBaseOne(SID)), "]")), true);
  return true;
}

bool CGame::DeleteFakeUser(uint8_t SID)
{
  CGameSlot* slot = GetSlot(SID);
  if (!slot) return false;
  const bool isSystemReservedSlot = GetIsSlotReservedForSystemVirtualUser(SID);
  for (auto it = begin(m_FakeUsers); it != end(m_FakeUsers); ++it) {
    if (slot->GetUID() == it->GetUID()) {
      if (GetIsCustomForces()) {
        m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, slot->GetTeam(), slot->GetColor(), /* only important if MAPOPT_FIXEDPLAYERSETTINGS */ m_Map->GetLobbyRace(slot));
      } else {
        m_SlotsConfig.slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, GetObserverTeam(), GetObserverColor(), SLOTRACE_RANDOM);
      }
      // Ensure this is sent before virtual host rejoins
      SendAll(it->GetGameQuitBytes(PLAYERLEAVE_LOBBY));
      UnrefFakeUser(it->GetSID());
      it = m_FakeUsers.erase(it);
      CreateVirtualHost();
      m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
      m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
      return true;
    }
  }
  return false;
}

void CGame::UnrefFakeUser(const uint8_t SID)
{
  if (m_HMCVirtualUser.has_value() && SID == m_HMCVirtualUser->GetSID()) {
    m_HMCVirtualUser.reset();
  }
  if (m_AHCLVirtualUser.has_value() && SID == m_AHCLVirtualUser->GetSID()) {
    m_AHCLVirtualUser.reset();
  }
  if (m_InertVirtualUser.has_value() && SID == m_InertVirtualUser->GetSID()) {
    m_InertVirtualUser.reset();
  }
  if (m_JoinInProgressVirtualUser.has_value() && SID == m_JoinInProgressVirtualUser->GetSID()) {
    m_JoinInProgressVirtualUser.reset();
  }
}


const CGameVirtualUser* CGame::InspectVirtualUserFromRef(const CGameVirtualUserReference* ref) const
{
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    if (fakeUser.GetSID() == ref->GetSID()) {
      return &fakeUser;
    }
  }
  return nullptr;
}

uint8_t CGame::FakeAllSlots()
{
  // Ensure this is called outside any loops.
  const bool hasUsers = GetHasAnyUser();

  uint8_t addedCounter = 0;
  if (m_RestoredGame) {
    if (m_Reserved.empty()) return 0;
    uint8_t reservedIndex = 0xFF;
    uint8_t reservedCount = static_cast<uint8_t>(m_Reserved.size());
    for (uint8_t SID = 0; SID < m_SlotsConfig.GetCount(); ++SID) {
      const CGameSlot* savedSlot = m_RestoredGame->InspectSlot(SID);
      if (!savedSlot || !savedSlot->GetIsPlayerOrFake()) {
        continue;
      }
      if (++reservedIndex >= reservedCount) break;
      if (m_SlotsConfig.GetIsOpen(SID)) {
        CreateFakeUserInner(SID, savedSlot->GetUID(), m_Reserved[reservedIndex], false);
        ++addedCounter;
      }
    }
  } else {
    uint8_t remainingControllers = m_Map->GetMapNumControllers() - GetNumControllers();
    if (!hasUsers && m_SlotsConfig.GetCount() == m_Map->GetMapNumControllers()) {
      --remainingControllers;
    }
    for (uint8_t SID = 0; SID < m_SlotsConfig.GetCount(); ++SID) {
      if (!m_SlotsConfig.GetIsOpen(SID)) {
        continue;
      }
      CreateFakeUserInner(SID, GetNewUID(), Concat("User[", ToDecString(ToBaseOne(SID)), "]"), false);
      ++addedCounter;
      if (0 == --remainingControllers) {
        break;
      }
    }
  }
  if (GetNumSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
  return addedCounter;
}

void CGame::DeleteFakeUsersLobby()
{
  if (m_FakeUsers.empty())
    return;

  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    const uint8_t SID = fakeUser.GetSID();
    const bool isSystemReservedSlot = GetIsSlotReservedForSystemVirtualUser(SID);
    if (GetIsCustomForces()) {
      m_SlotsConfig.slots[SID] = CGameSlot(m_SlotsConfig.Inspect(SID).GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, m_SlotsConfig.Inspect(SID).GetTeam(), m_SlotsConfig.Inspect(SID).GetColor(), /* only important if MAPOPT_FIXEDPLAYERSETTINGS */ m_Map->GetLobbyRace(&(m_SlotsConfig.Inspect(SID))));
    } else {
      m_SlotsConfig.slots[SID] = CGameSlot(m_SlotsConfig.Inspect(SID).GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, GetObserverTeam(), GetObserverColor(), SLOTRACE_RANDOM);
    }
    // Ensure this is sent before virtual host rejoins
    SendAll(fakeUser.GetGameQuitBytes(PLAYERLEAVE_LOBBY));
  }

  m_HMCVirtualUser.reset();
  m_AHCLVirtualUser.reset();
  m_InertVirtualUser.reset();
  m_JoinInProgressVirtualUser.reset();
  m_FakeUsers.clear();
  CreateVirtualHost();
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
}

void CGame::DeleteFakeUsersLoaded()
{
  if (m_FakeUsers.empty())
    return;

  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    SendAll(fakeUser.GetGameQuitBytes(PLAYERLEAVE_DISCONNECT));
  }

  m_HMCVirtualUser.reset();
  m_AHCLVirtualUser.reset();
  m_InertVirtualUser.reset();
  m_JoinInProgressVirtualUser.reset();
  m_FakeUsers.clear();
}

void CGame::RemoveCreator()
{
  m_Creator.Reset();
}

bool CGame::GetIsStageAcceptingJoins() const
{
  // This method does not care whether this is actually a mirror game. This is intended.
  if (m_LobbyLoading || m_Exiting || GetIsGameOver()) return false;
  // we only want to broadcast if the countdown hasn't started (or if the game has loaded and join-in-progress is enabled)
  if (!m_CountDownStarted) return true;
  if (!m_GameLoaded) return false;
  if (!m_LoadedSlotsProtocol.has_value()) return false;
  //if (m_GameHistory->GetSoftDesynchronizedSameVersion()) return false;
  return m_Config.m_EnableJoinObserversInProgress || m_Config.m_EnableJoinPlayersInProgress;
}

bool CGame::GetUDPEnabled() const
{
  return m_Config.m_UDPEnabled;
}

void CGame::SetUDPEnabled(bool toEnabled)
{
  if (m_Config.m_UDPEnabled == toEnabled) {
    return;
  }
  m_Config.m_UDPEnabled = toEnabled;

  if (toEnabled != m_GameDiscoveryActive && (!toEnabled || GetIsStageAcceptingJoins())) {
    if (toEnabled) {
      SendGameDiscoveryCreate();
    } else {
      SendGameDiscoveryDecreate();
    }
    m_GameDiscoveryActive = toEnabled;
  }
}

bool CGame::GetHasDesyncHandler() const
{
  return m_Config.m_DesyncHandler == OnDesyncHandler::kDrop || m_Config.m_DesyncHandler == OnDesyncHandler::kNotify;
}

bool CGame::GetAllowsDesync() const
{
  return m_Config.m_DesyncHandler != OnDesyncHandler::kDrop;
}

OnIPFloodHandler CGame::GetIPFloodHandler() const
{
  return m_Config.m_IPFloodHandler;
}

bool CGame::GetAllowsIPFlood() const
{
  return m_Config.m_IPFloodHandler != OnIPFloodHandler::kDeny;
}

string CGame::GetCustomCreationCounterText(shared_ptr<const CRealm> realm, char counter) const
{
  string counterTemplate;
  if (realm) {
    counterTemplate = realm->GetReHostCounterTemplate();
  } else {
    counterTemplate = m_Aura->m_Config.m_LANReHostCounterTemplate;
  }

  vector<pair<uint64_t, string>> texts;
  texts.reserve(1);
  texts.emplace_back(HashCode("COUNT"), string(static_cast<string::size_type>(1u), counter));
  const FlatMap<uint64_t, string> textCache(move(texts));
  const FlatMap<uint64_t, function<string()>> textFuncMap;

  return ReplaceTemplate(counterTemplate, nullptr, &textCache, nullptr, &textFuncMap);
}

string CGame::GetCreationCounterText(shared_ptr<const CRealm> realm) const
{
  if (m_CreationCounter == 0) return string();

  // creation counter may be large, but we reduce it to a base-36 character
  // Base-36 suffix 0123456789abcdefghijklmnopqrstuvwxyz
  uint16_t creationCounter = MOD_SHORT(m_CreationCounter, 36);

  unsigned char counter;
  if (m_CreationCounter < 10) {
    counter = integer_cast_lossy<uint8_t>(PLUS_SHORT(48u, creationCounter));
  } else {
    counter = integer_cast_lossy<uint8_t>(PLUS_SHORT(87u, creationCounter));
  }

  return GetCustomCreationCounterText(realm, static_cast<char>(counter));
}

string CGame::GetNextCreationCounterText(shared_ptr<const CRealm> realm) const
{
  uint16_t creationCounter = MOD_SHORT(PLUS_SHORT(m_CreationCounter, 1), 36);
  ++creationCounter;

  // Base-36 suffix 0123456789abcdefghijklmnopqrstuvwxyz
  unsigned char counter;
  if (creationCounter < 10) {
    counter = integer_cast_lossy<uint8_t>(48u + creationCounter);
  } else {
    counter = integer_cast_lossy<uint8_t>(87u + creationCounter);
  }

  return GetCustomCreationCounterText(realm, static_cast<char>(counter));
}

string CGame::GetIndexHostName() const
{
  return m_Config.m_IndexHostName;
}

string CGame::GetLobbyVirtualHostName() const
{
  return m_Config.m_LobbyVirtualHostName;
}

uint8_t CGame::CalcMaxEqualizerDelayFrames() const
{
  if (!m_Config.m_LatencyEqualizerEnabled) return 0;
  uint8_t max = 0;
  for (const auto& user : m_Users) {
    uint8_t thisOffset = user->GetPingEqualizerOffset();
    if (max < thisOffset) max = thisOffset;
  }
  // static_assert(max < m_Actions.size());
  return max;
}

int64_t CGame::GetActiveLatency() const
{
  return m_LatencyTicks;
}

int64_t CGame::GetNextLatency(int64_t frameDrift) const
{
  if (frameDrift <= m_Config.m_LatencyDriftMax) return static_cast<int64_t>(m_NextLatencyTicks);
  int64_t latency = m_NextLatencyTicks + 2 * frameDrift;
  int64_t maxLatency = static_cast<int64_t>(m_Config.m_LatencyMax);
  return min(latency, maxLatency);
}

int64_t CGame::GetLastActionLateBy() const
{
  return m_LastActionSentTicks - m_LastActionExpectedTicks;
}

size_t CGame::GetSyncLimit(bool isObserver) const
{
  return isObserver ? m_LagStartMinObserversFrames : m_LagStartMinPlayersFrames;
}

size_t CGame::GetSyncLimitSafe(bool isObserver) const
{
  return isObserver ? m_LagStopMaxObserversFrames : m_LagStopMaxPlayersFrames;
}

GamePlayerResult CGame::ResolveUndecidedComputerOrVirtualAuto(CGameController* controllerData, const GameResultConstraints& constraints, const GameResultTeamAnalysis& teamAnalysis)
{
  if (teamAnalysis.undecidedUserTeams.test(controllerData->GetTeam())) {
    if (constraints.GetUndecidedUserHandler() == GameResultUserUndecidedHandler::kLoserSelfAndAllies) {
      return GamePlayerResult::kLoser;
    }
  } else if (teamAnalysis.winnerTeams.none() && GetNumTeams() == 2) {
    return GamePlayerResult::kWinner;
  }
  return GamePlayerResult::kLoser;
}

GamePlayerResult CGame::ResolveUndecidedController(CGameController* controllerData, const GameResultConstraints& constraints, const GameResultTeamAnalysis& teamAnalysis)
{
  switch (controllerData->GetType()) {
    case GameControllerType::kVirtual: {
      switch (constraints.GetUndecidedVirtualHandler()) {
        case GameResultVirtualUndecidedHandler::kNone:
          return GamePlayerResult::kUndecided;
        case GameResultVirtualUndecidedHandler::kLoserSelf:
          return GamePlayerResult::kLoser;
        case GameResultVirtualUndecidedHandler::kAuto:
          return ResolveUndecidedComputerOrVirtualAuto(controllerData, constraints, teamAnalysis);
        IGNORE_ENUM_LAST(GameResultVirtualUndecidedHandler)
      }
      break;
    }

    case GameControllerType::kUser: {
      switch (constraints.GetUndecidedUserHandler()) {
        case GameResultUserUndecidedHandler::kNone:
          return GamePlayerResult::kUndecided;
        case GameResultUserUndecidedHandler::kLoserSelf:
        case GameResultUserUndecidedHandler::kLoserSelfAndAllies:
          return GamePlayerResult::kLoser;
        IGNORE_ENUM_LAST(GameResultUserUndecidedHandler)
      }
      break;
    }

    case GameControllerType::kComputer: {
      switch (constraints.GetUndecidedComputerHandler()) {
        case GameResultComputerUndecidedHandler::kNone:
          return GamePlayerResult::kUndecided;
        case GameResultComputerUndecidedHandler::kLoserSelf:
          return GamePlayerResult::kLoser;
        case GameResultComputerUndecidedHandler::kAuto:
          return ResolveUndecidedComputerOrVirtualAuto(controllerData, constraints, teamAnalysis);
        IGNORE_ENUM_LAST(GameResultComputerUndecidedHandler)
      }
      break;
    }

    IGNORE_ENUM_LAST(GameControllerType)
  }

  //UNREACHABLE();
  return GamePlayerResult::kUndecided;
}

GameResultTeamAnalysis CGame::GetGameResultTeamAnalysis() const
{
  GameResultTeamAnalysis analysis;

  for (const auto& controllerData : m_GameControllers) {
    if (!controllerData || controllerData->GetIsObserver()) continue;
    GamePlayerResult result = GamePlayerResult::kUndecided;
    if (controllerData->GetHasClientLeftCode()) {
      GamePlayerResult maybeResult = GameProtocol::LeftCodeToResult(controllerData->GetClientLeftCode());
      if (maybeResult != GamePlayerResult::kUndecided) {
        result = maybeResult;
      }
    }

    bitset<MAX_SLOTS_MODERN>* targetBitSet = nullptr;
    switch (result) {
      case GamePlayerResult::kWinner:
        targetBitSet = &analysis.winnerTeams;
        break;
      case GamePlayerResult::kLoser:
        targetBitSet = &analysis.loserTeams;
        break;
      case GamePlayerResult::kDrawer:
        targetBitSet = &analysis.drawerTeams;
        break;
      case GamePlayerResult::kUndecided: {
        switch (controllerData->GetType()) {
          case GameControllerType::kVirtual:
            targetBitSet = &analysis.undecidedVirtualTeams;
            break;
          case GameControllerType::kUser:
            targetBitSet = &analysis.undecidedUserTeams;
            break;
          case GameControllerType::kComputer:
            targetBitSet = &analysis.undecidedComputerTeams;
            break;
          IGNORE_ENUM_LAST(GameControllerType)
        }
        break;
      }
      IGNORE_ENUM_LAST(GamePlayerResult)
    }

    targetBitSet->set(controllerData->GetTeam());

    if (controllerData->GetHasLeftGame()) {
      int64_t gameEndTime = m_EffectiveTicks / 1000;
      // TODO: Grace period for considering a player as leaver: hardcoded as 3 minutes
      if (gameEndTime > 180 && controllerData->GetLeftGameTime() < static_cast<uint64_t>(gameEndTime - 180)) {
        analysis.leaverTeams.set(controllerData->GetTeam());
      }
    }
  }

  return analysis;
}

optional<GameResults> CGame::GetGameResultsMMD()
{
  if (m_CustomStats) {
    return m_CustomStats->GetGameResults(GetMap()->GetGameResultConstraints());
  }
  if (m_DotaStats) {
    return m_DotaStats->GetGameResults(GetMap()->GetGameResultConstraints());
  }
  return nullopt;
}

optional<GameResults> CGame::GetGameResultsLeaveCode()
{
  optional<GameResults> gameResults;
  gameResults.emplace();

  GameResultTeamAnalysis teamAnalysis = GetGameResultTeamAnalysis();

  for (const auto& controllerData : m_GameControllers) {
    if (!controllerData || controllerData->GetIsObserver()) continue;
    GamePlayerResult result = GamePlayerResult::kUndecided;
    if (controllerData->GetHasClientLeftCode()) {
      GamePlayerResult maybeResult = GameProtocol::LeftCodeToResult(controllerData->GetClientLeftCode());
      if (maybeResult != GamePlayerResult::kUndecided) {
        result = maybeResult;
      }
    }
    if (result == GamePlayerResult::kUndecided) {
      result = ResolveUndecidedController(controllerData, m_Map->GetGameResultConstraints(), teamAnalysis);
    }
    vector<CGameController*>* resultGroup = nullptr;
    switch (result) {
      case GamePlayerResult::kWinner:
        resultGroup = &gameResults->winners;
        break;
      case GamePlayerResult::kLoser:
        resultGroup = &gameResults->losers;
        break;
      case GamePlayerResult::kDrawer:
        resultGroup = &gameResults->drawers;
        break;
      case GamePlayerResult::kUndecided:
        resultGroup = &gameResults->undecided;
        break;
      IGNORE_ENUM_LAST(GamePlayerResult)
    }
    resultGroup->push_back(controllerData);
  }

  return gameResults;
}

GameResultSource CGame::TryConfirmResults(optional<GameResults> gameResults, GameResultSource resultsSource) {
  if (!gameResults.has_value() || !CheckGameResults(gameResults.value())) {
    if (resultsSource == GameResultSource::kMMD) {
      LOG_APP_IF(LogLevel::kDebug, "MMD failed to provide valid game results");
    } else {
      LOG_APP_IF(LogLevel::kDebug, "Players failed to provide valid game results");
    }
    return GameResultSource::kNone;
  }
  m_GameResultsSource = resultsSource;
  m_GameResults.swap(gameResults);
  m_GameResults->Confirm();
  if (resultsSource == GameResultSource::kMMD) {
    LOG_APP_IF(LogLevel::kDebug, Concat("Resolved winners (MMD): ", JoinStrings(m_GameResults->GetWinnersNames())));
  } else {
    LOG_APP_IF(LogLevel::kDebug, Concat("Resolved winners: ", JoinStrings(m_GameResults->GetWinnersNames())));
  }
  return m_GameResultsSource;
}

GameResultSource CGame::RunGameResults()
{
  if (m_GameResultsSource != GameResultSource::kNone) return m_GameResultsSource;
  
  FlushStatsQueue();

  const GameResultSourceSelect sourceOfTruth = GetGameResultSourceOfTruth();

  if (sourceOfTruth == GameResultSourceSelect::kOnlyMMD) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method only-mmd");
    optional<GameResults> results = GetGameResultsMMD();
    return TryConfirmResults(results, GameResultSource::kMMD);
  }

  if (sourceOfTruth == GameResultSourceSelect::kOnlyLeaveCode) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method only-exit");
    optional<GameResults> results = GetGameResultsLeaveCode();
    return TryConfirmResults(results, GameResultSource::kLeaveCode);
  }

  if (sourceOfTruth == GameResultSourceSelect::kPreferMMD) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method prefer-mmd");
    optional<GameResults> results = GetGameResultsMMD();
    if (TryConfirmResults(results, GameResultSource::kMMD) != GameResultSource::kNone) {
      return m_GameResultsSource;
    }
    results = GetGameResultsLeaveCode();
    return TryConfirmResults(results, GameResultSource::kLeaveCode);
  }

  if (sourceOfTruth == GameResultSourceSelect::kPreferLeaveCode) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method prefer-exit");
    optional<GameResults> results = GetGameResultsLeaveCode();
    if (TryConfirmResults(results, GameResultSource::kLeaveCode) != GameResultSource::kNone) {
      return m_GameResultsSource;
    }
    results = GetGameResultsMMD();
    return TryConfirmResults(results, GameResultSource::kMMD);
  }

  return GameResultSource::kNone;
}

bool CGame::GetIsAPrioriCompatibleWithGameResultsConstraints(string& reason) const
{
  // ?_?
  if (m_RestoredGame) {
    reason = "game was loaded";
    return false;
  }

  // ?_?
  if (m_Map->GetMMDEnabled() && m_Map->GetMMDType() == MMD_TYPE_DOTA) {
    if (m_StartPlayers < 6) {
      reason = "too few users";
      return false;
    } else if (!m_ControllersBalanced || !m_FakeUsers.empty()) {
      reason = "imbalanced";
      return false;
    }
  }

  return true;
}

bool CGame::CheckGameResults(const GameResults& /*gameResults*/) const
{
  // TODO: CheckGameResults contraints (m_Map.m_GameResults)
  //bool canDraw;
  //bool canAllWin;
  //bool canAllLose;
  //bool canWinMultiplePlayers;
  //bool canWinMultipleTeams;
  //bool undecidedIsLoser;

  //gameResults;
  return true;
}

void CGame::RunHCLEncoding()
{
  // encode the HCL command string in the slot handicaps
  // here's how it works:
  //  the user inputs a command string to be sent to the map
  //  it is almost impossible to send a message from the bot to the map so we encode the command string in the slot handicaps
  //  this works because there are only 6 valid handicaps but Warcraft III allows the bot to set up to 256 handicaps
  //  we encode the original (unmodified) handicaps in the new handicaps and use the remaining space to store a short message
  //  only occupied slots deliver their handicaps to the map and we can send one character (from a list) per handicap
  //  when the map finishes loading, assuming it's designed to use the HCL system, it checks if anyone has an invalid handicap
  //  if so, it decodes the message from the handicaps and restores the original handicaps using the encoded values
  //  the meaning of the message is specific to each map and the bot doesn't need to understand it
  //  e.g. you could send game modes, # of rounds, level to start on, anything you want as long as it fits in the limited space available
  //  note: if you attempt to use the HCL system on a map that does not support HCL the bot will drastically modify the handicaps
  //  since the map won't automatically restore the original handicaps in this case your game will be ruined

  if (m_HCLCommandString.empty()) {
    return;
  }

  if (m_HCLCommandString.size() > GetNumSlotsOccupied()) {
    LOG_APP_IF(LogLevel::kInfo, Concat("failed to encode game mode as HCL string [", m_HCLCommandString, "] because there aren't enough occupied slots"));
    return;
  }

  const bool encodeVirtualPlayers = m_Map->GetHCLAboutVirtualPlayers();
  string HCLChars = encodeVirtualPlayers ? HCL_CHARSET_SMALL : HCL_CHARSET_STANDARD;

  if (m_HCLCommandString.find_first_not_of(HCLChars) != string::npos) {
    LOG_APP_IF(LogLevel::kError, Concat("failed to encode game mode as HCL string [", m_HCLCommandString, "] because it contains invalid characters"));
    return;
  }

  uint8_t encodingMap[256];
  uint8_t j = 0;

  for (auto& encode : encodingMap) {
    // the following 7 handicap values are forbidden for compatibility
    //
    // when the HCL parser in the map/WC3 client encounters these values,
    // it means that the host is not using HCL, so they are passed through

    if (j == 0 || j == 50 || j == 60 || j == 70 || j == 80 || j == 90 || j == 100)
      ++j;

    encode = j++;
  }

  uint8_t currentSlot = 0;

  for (const auto& character : m_HCLCommandString) {
    while (m_SlotsConfig.slots[currentSlot].GetSlotStatus() != SLOTSTATUS_OCCUPIED)
      ++currentSlot;

    bool isVirtualPlayer = m_SlotsConfig.slots[currentSlot].GetIsPlayerOrFake() && !GetIsRealPlayerSlot(currentSlot);
    uint32_t handicapIndex = (integer_cast<uint32_t>(m_SlotsConfig.slots[currentSlot].GetHandicap()) - 50u) / 10u;
    uint32_t charIndex = integer_cast_lossy<uint32_t>(HCLChars.find(character));
    uint32_t slotInfo = handicapIndex;
    if (encodeVirtualPlayers && isVirtualPlayer) {
      slotInfo += 6u;
    }
    slotInfo += charIndex * (encodeVirtualPlayers ? 12u : 6u);
    // max() = 7+5+40*6 = 252 | 7+11+19*12 = 246
    if (encodeVirtualPlayers) {
      assert((slotInfo <= 246) && "slotInfo should not be more than 246");
    } else {
      assert((slotInfo <= 252) && "slotInfo should not be more than 252");
    }
    m_SlotsConfig.slots[currentSlot++].SetHandicap(encodingMap[slotInfo]);
  }

  // See documentation for the decoding algorithm
  // https://gist.github.com/Slayer95/a15fc75f38d0b3fdf356613ede96cf7f
  //
  // Variant encodeVirtualPlayers: K = 12
  // value = encodedHandicap % K
  // virtual = encodedHandicap - (value * K) > 6
  // handicap = encodedHandicap - (value * K) - (virtual ? 6 : 0)

  m_SlotInfoChanged |= SLOTS_HCL_INJECTED;
  LOG_APP_IF(LogLevel::kDebug, Concat("using game mode [", m_HCLCommandString, "]"));
  DLOG_APP_IF(LogLevel::kTrace, Concat("mode [", m_HCLCommandString, "] encoded as handicaps <", ByteArrayToDecString(GetHandicaps()), ">"));
}

bool CGame::SendHMC(const string& message)
{
  if (!m_HMCVirtualUser.has_value()) return false;
  const array<uint8_t, 8> triggerBytes = m_Map->GetHMCTrigger();
  const uint8_t UID = m_HMCVirtualUser->GetUID();
  return SendChatTriggerBytes(UID, message, triggerBytes);
}

bool CGame::CreateHMCPlayer()
{
  const uint8_t SID = GetHMCSID();
  if (SID == 0xFF) return false;
  if (!CanLockSlotForJoins(SID)) return false;

  if (GetNumSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), m_Map->GetHMCPlayerName(), false);
  return true;
}

uint8_t CGame::GetHMCSID() const
{
  if (!m_Map->GetHMCEnabled()) return 0xFF;
  const uint8_t slot = m_Map->GetHMCSlot();
  if (slot >= GetNumSlots()) return 0xFF;
  return slot;
}

uint8_t CGame::GetAHCLSID() const
{
  if (!m_Map->GetAHCLEnabled()) return 0xFF;
  const uint8_t slot = m_Map->GetAHCLSlot();
  if (slot >= GetNumSlots()) return 0xFF;
  return slot;
}
