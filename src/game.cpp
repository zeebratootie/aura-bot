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
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_IF for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        LogApp(U, LOG_C); \
    }

#define LOG_APP_IF_CUSTOM(T, U, V) \
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_IF_CUSTOM for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        LogApp(U, V); \
    }


#ifdef DEBUG
#define DLOG_APP_IF(T, U) \
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_IF for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        LogApp(U, LOG_C); \
    }

#define DLOG_APP_IF_CUSTOM(T, U, V) \
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_IF_CUSTOM for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        LogApp(U, V); \
    }
#else
#define DLOG_APP_IF(T, U)
#define DLOG_APP_IF_CUSTOM(T, U, V)
#endif

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
    m_OwnerLess(nGameSetup->m_OwnerLess),
    m_OwnerName(nGameSetup->m_Owner.first),
    m_OwnerRealm(nGameSetup->m_Owner.second),
    m_Creator(nGameSetup->m_Creator),
    m_CreatorText(nGameSetup->m_Attribution),
    m_RealmsExcluded(nGameSetup->m_RealmsExcluded),
    m_MapPath(nGameSetup->m_Map->GetClientPath()),
    m_MapSiteURL(nGameSetup->m_Map->GetMapSiteURL()),
    m_CreationTime(nAura->GetLoopTime()),
    m_LastPingTicks(APP_MIN_TICKS),
    m_LastCheckActionsTicks(APP_MIN_TICKS),
    m_LastRefreshTime(nAura->GetLoopTime()),
    m_LastDownloadCounterResetTicks(nAura->GetLoopTicks()),
    m_LastCountDownTicks(APP_MIN_TICKS),
    m_StartedLoadingTicks(0),
    m_FinishedLoadingTicks(0),
    m_MapGameStartTime(0),
    m_EffectiveTicks(0),
    m_LatencyTicks(0),
    m_NextLatencyTicks(0),
    m_LastActionSentTicks(0),
    m_LastActionLateBy(0),
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
    m_LastUserSeenTicks(nAura->GetLoopTicks()),
    m_LastOwnerSeenTicks(nAura->GetLoopTicks()),
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
    m_DisplayMode(nGameSetup->m_RealmsDisplayMode),
    m_IsAutoVirtualPlayers(false),
    m_VirtualHostUID(0xFF),
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
    m_GameDiscoveryInfoChanged(GAME_DISCOVERY_CHANGED_NEW),
    m_GameDiscoveryInfoVersionOffset(0),
    m_GameDiscoveryInfoDynamicOffset(0)
{
  if (!m_Config.m_Valid) {
    m_Exiting = true;
    return;
  }

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

  m_ChatEnabled = m_Config.m_EnableLobbyChat;
  m_IsHiddenPlayerNames = m_Config.m_HideLobbyNames;

  if (nGameSetup->m_HCL.has_value()) {
    m_HCLCommandString = nGameSetup->m_HCL.value();
  } else if (nGameSetup->m_Map->GetHCLEnabled()) {
    m_HCLCommandString = nGameSetup->m_Map->GetHCLDefaultValue();
  }

  m_GameFlags = CalcGameFlags();
  m_LatencyTicks = m_NextLatencyTicks;

  if (!nGameSetup->GetIsMirror()) {
    for (const auto& userName : nGameSetup->m_Reservations) {
      AddToReserved(userName);
    }

    m_RandomSeed = GetRandomUInt32();

    ResetLatency();

    // wait time of 1 minute  = 0 empty actions required
    // wait time of 2 minutes = 1 empty action required...

    if (m_GProxyEmptyActions > 0) {
      m_GProxyEmptyActions = static_cast<uint8_t>(m_Aura->m_Net.m_Config.m_ReconnectWaitTicksLegacy / 60000 - 1);
      if (m_GProxyEmptyActions > 9) {
        m_GProxyEmptyActions = 9;
      }
    }

    // start listening for connections
    if (!InitNet()) {
      m_Exiting = true;
    }

    // Only maps in <bot.maps_path>
    if (m_Map->GetMapFileIsFromManagedFolder()) {
      auto it = m_Aura->m_MapFilesTimedBusyLocks.find(m_Map->GetServerPath());
      if (it == m_Aura->m_MapFilesTimedBusyLocks.end()) {
        m_Aura->m_MapFilesTimedBusyLocks[m_Map->GetServerPath()] = make_pair<int64_t, uint16_t>(m_Aura->GetLoopTicks(), (uint16_t)0u);
      } else {
        it->second.first = m_Aura->GetLoopTicks();
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
    }
    if (!address || (m_IsMirrorProxy && !InitNet())) {
      m_Exiting = true;
    }
  }

  InitSlots();
  UpdateReadyCounters();

  if (!m_IsMirror) {
    if (nGameSetup->m_AutoStartSeconds.has_value() || nGameSetup->m_AutoStartPlayers.has_value()) {
      uint8_t autoStartPlayers = nGameSetup->m_AutoStartPlayers.value_or(0);
      int64_t autoStartSeconds = (int64_t)nGameSetup->m_AutoStartSeconds.value_or(0);
      if (!nGameSetup->m_AutoStartPlayers.has_value() || autoStartPlayers > m_ControllersReadyCount) {
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
}

void CGame::InitSlots()
{
  if (m_RestoredGame) {
    uint8_t i = 0xFF;
    m_Slots = m_RestoredGame->GetSlots();
    // reset user slots
    for (auto& slot : m_Slots) {
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

  m_Slots = m_Map->GetSlots();

  const bool useObservers = m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat || m_Map->GetGameObservers() == GameObserversMode::kReferees;

  // Match actual observer slots to set map flags.
  if (!useObservers) {
    CloseObserverSlots();
  }

  const bool customForces = m_Map->GetMapOptions() & MAPOPT_CUSTOMFORCES;
  const bool fixedPlayers = m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS;
  bitset<MAX_SLOTS_MODERN> usedColors;
  for (auto& slot : m_Slots) {
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
      slot.SetTeam(m_Map->GetVersionMaxSlots());
    }

    // Ensure colors are unique for each playable slot.
    // Observers must have color 12 or 24, according to game version.
    if (slot.GetTeam() == m_Map->GetVersionMaxSlots()) {
      slot.SetColor(m_Map->GetVersionMaxSlots());
    } else {
      const uint8_t originalColor = slot.GetColor();
      if (usedColors.test(originalColor)) {
        uint8_t testColor = originalColor;
        do {
          testColor = (testColor + 1) % m_Map->GetVersionMaxSlots();
        } while (usedColors.test(testColor) && testColor != originalColor);
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
  m_Socket = m_Aura->m_Net.GetOrCreateTCPServer(hostPort, "Game <<" + GetShortNameLAN() + ">>");

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
  if (!m_GameHistory->GetIsFinished()) {
    m_GameHistory->SetFinishedTicks(m_Aura->GetLoopTicks());
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

CGameController* CGame::GetGameControllerFromColor(uint8_t colour) const
{
  if (colour == m_Map->GetVersionMaxSlots()) {
    // observer color is ambiguous
    return nullptr;
  }
  for (const auto& controllerData : m_GameControllers) {
    if (controllerData && controllerData->GetColor() == colour) {
      return controllerData;
    }
  }
  return nullptr;
}

void CGame::StoreGameControllers()
{
  m_GameControllers.reserve(m_Slots.size());
  for (uint8_t SID = 0, slotCount = static_cast<uint8_t>(m_Slots.size()); SID < slotCount; ++SID) {
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

bool CGame::EventGameCacheInteger(const uint8_t UID, const uint8_t* actionStart, const uint8_t* actionEnd)
{
  if (!m_CustomStats && !m_DotaStats && !m_GameInteractiveHost) return false;

  const uint8_t* stringStart;
  const uint8_t* stringEnd;
  string cacheFileName, missionKey, key;
  uint32_t value;

  stringStart = actionStart + 1u;
  stringEnd = FindNullDelimiterOrStart(stringStart, actionEnd);
  if (stringEnd == stringStart) return false;
  cacheFileName = string(reinterpret_cast<const char*>(stringStart), reinterpret_cast<const char*>(stringEnd));

  stringStart = stringEnd + 1u;
  stringEnd = FindNullDelimiterOrStart(stringStart, actionEnd);
  if (stringEnd == stringStart) return false;
  missionKey = string(reinterpret_cast<const char*>(stringStart), reinterpret_cast<const char*>(stringEnd));

  stringStart = stringEnd + 1u;
  stringEnd = FindNullDelimiterOrStart(stringStart, actionEnd);
  if (stringEnd == stringStart) return false;
  key = string(reinterpret_cast<const char*>(stringStart), reinterpret_cast<const char*>(stringEnd));

  if (actionEnd != stringEnd + 5u) return false;
  value = ByteArrayToUInt32(stringEnd + 1, false);

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
    LOG_APP_IF(LogLevel::kDebug, "[STATS] saving game end player data to database")
    if (m_Aura->m_DB->Begin()) {
      for (auto& controllerData : m_GameControllers) {
        m_Aura->m_DB->UpdateGamePlayerOnEnd(m_PersistentId, controllerData, m_EffectiveTicks / 1000);
      }
      if (!m_Aura->m_DB->Commit()) {
        LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to commit game end player data")
      } else {
        LOG_APP_IF(LogLevel::kDebug, "[STATS] commited game end player data in " + to_string(GetTicks() - hiResTicks) + " ms")
      }
    } else {
      LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to begin transaction game end player data")
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

  it->second.first = m_Aura->GetLoopTicks();
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
  m_GameOverTime = m_Aura->GetLoopTime();
  if (isMMD) {
    m_GameOverTolerance = 300;
  } else {
    m_GameOverTolerance = 60;
  }
  if (!m_GameHistory->GetIsFinished()) {
    m_GameHistory->SetFinishedTicks(m_Aura->GetLoopTicks());
    m_GameHistory->UpdateSpectatorActions((int64_t)m_Config.m_SpectatorDelay);
  }

  if (GetNumJoinedUsers() > 0) {
    SendAllChat("Gameover timer started (disconnecting in " + to_string(m_GameOverTolerance.value_or(60)) + " seconds...)");
  }

  if (GetIsLobbyOrMirror()) {
    if (m_GameDiscoveryActive) {
      SendGameDiscoveryDecreate();
      m_GameDiscoveryActive = false;
    }
    if (m_DisplayMode != GAME_DISPLAY_NONE) {
      AnnounceDecreateToRealms(); // STOPADV @ ResetGameBroadcastData(), SEND_ENTERCHAT
    }
    m_ChatOnly = true;
    StopCountDown();
  }

  m_Aura->UntrackGameJoinInProgress(shared_from_this());
}

CGame::~CGame()
{
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

uint8_t CGame::GetLayout() const
{
  if (m_RestoredGame) return MAPLAYOUT_FIXED_PLAYERS;
  return GetMap()->GetMapLayoutStyle();
}

bool CGame::GetIsCustomForces() const
{
  if (m_RestoredGame) return true;
  return GetMap()->GetMapLayoutStyle() != MAPLAYOUT_ANY;
}

void CGame::UpdateSelectBlockTime(int64_t& usecBlockTime) const
{
  // return the number of ticks (ms) until the next "timed action", which for our purposes is the next game update
  // the main Aura loop will make sure the next loop update happens at or before this value
  // note: there's no reason this function couldn't take into account the game's other timers too but they're far less critical
  // warning: this function must take into account when actions are not being sent (e.g. during loading or lagging)

  if (!m_GameLoaded || m_IsLagging || usecBlockTime == 0)
    return;

  // hi-res ticks for actions scheduler
  const int64_t ticksSinceLastUpdate = GetTicks() - m_LastActionSentTicks;

  if (ticksSinceLastUpdate > m_LatencyTicks - m_LastActionLateBy) {
    usecBlockTime = 0;
    return;
  }

  int64_t maybeBlockTime = (m_LatencyTicks - m_LastActionLateBy - ticksSinceLastUpdate) * 1000;
  if (maybeBlockTime < usecBlockTime) {
    usecBlockTime = maybeBlockTime;
  }
}

uint32_t CGame::GetSlotsOccupied() const
{
  uint32_t NumSlotsOccupied = 0;

  for (const auto& slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED)
      ++NumSlotsOccupied;
  }

  return NumSlotsOccupied;
}

uint32_t CGame::GetSlotsOpen() const
{
  uint32_t NumSlotsOpen = 0;

  for (const auto& slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_OPEN)
      ++NumSlotsOpen;
  }

  return NumSlotsOpen;
}

bool CGame::HasSlotsOpen() const
{
  for (const auto& slot : m_Slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OPEN)
      return true;
  }
  return false;
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
  return GetNumJoinedPlayers() + GetNumFakePlayers();
}

uint8_t CGame::GetNumJoinedObserversOrFake() const
{
  return GetNumJoinedObservers() + GetNumFakeObservers();
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

uint8_t CGame::GetNumOccupiedSlots() const
{
  uint8_t count = 0;
  for (const auto& slot : m_Slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
      ++count;
    }
  }
  return count;
}

uint8_t CGame::GetNumPotentialControllers() const
{
  uint8_t count = 0;
  for (const auto& slot : m_Slots) {
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
  uint8_t count = 0;
  for (const auto& slot : m_Slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetTeam() != m_Map->GetVersionMaxSlots()) {
      ++count;
    }
  }
  return count;
}

uint8_t CGame::GetNumComputers() const
{
  uint8_t count = 0;
  for (const auto& slot : m_Slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetIsComputer()) {
      ++count;
    }
  }
  return count;
}

uint8_t CGame::GetNumTeams() const
{
  bitset<MAX_SLOTS_MODERN> teams;
  for (const auto& slot : m_Slots) {
    if (slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED) continue;
    if (slot.GetTeam() == m_Map->GetVersionMaxSlots()) continue;
    teams.set(slot.GetTeam());
  }
  return static_cast<uint8_t>(teams.count());
}

uint8_t CGame::GetNumTeamControllersOrOpen(const uint8_t team) const
{
  uint8_t count = 0;
  for (const auto& slot : m_Slots) {
    if (slot.GetSlotStatus() == SLOTSTATUS_CLOSED) continue;
    if (slot.GetTeam() == team) {
      ++count;
    }
  }
  return count;
}

string CGame::GetClientFileName() const
{
  size_t LastSlash = m_MapPath.rfind('\\');
  if (LastSlash == string::npos) {
    return m_MapPath;
  }
  return m_MapPath.substr(LastSlash + 1);
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
    candidateName = startingControllers[0]->GetName() + " vs " + startingControllers[1]->GetName();
  }
  if (!candidateName.empty() && candidateName.size() <= m_Aura->m_MaxGameNameSize) {
    return candidateName;
  }
  if (numControllers == 2) {
    vector<string> controllers;
    controllers.push_back(startingControllers[0]->GetShortName());
    controllers.push_back(startingControllers[1]->GetShortName());
    candidateName = JoinStrings(controllers, " vs ", false);
    if (candidateName.size() > m_Aura->m_MaxGameNameSize) {
      candidateName = "Melee VS";
    }
  } else if (m_CustomLayout == CUSTOM_LAYOUT_FFA) {
    candidateName = "FFA " + to_string(numControllers) + "P";
  }
  if (!candidateName.empty() && candidateName.size() <= m_Aura->m_MaxGameNameSize) {
    return candidateName;
  }
  return m_GameName;
}

string CGame::GetStatusDescription() const
{
  if (m_IsMirror)
     return "[" + GetMap()->GetMapTitle() + "] (Mirror) \"" + GetShortNameLAN() + "\"";

  string Description = (
    "[" + GetMap()->GetMapTitle() + "] \"" + GetShortNameLAN() + "\" - " + m_OwnerName + " - " +
    ToDecString(GetNumJoinedPlayersOrFake()) + "/" + ToDecString(m_GameLoading || m_GameLoaded ? m_ControllersWithMap : static_cast<uint8_t>(m_Slots.size()))
  );

  if (m_GameLoading || m_GameLoaded)
    Description += " : " + to_string((m_EffectiveTicks / 1000) / 60) + "min";
  else
    Description += " : " + to_string((m_Aura->GetLoopTime() - m_CreationTime) / 60) + "min";

  return Description;
}

string CGame::GetEndDescription(shared_ptr<const CRealm> realm) const
{
  if (m_IsMirror)
     return "[" + GetMap()->GetMapTitle() + "] (Mirror) \"" + GetCustomGameName(realm, true) + "\"";

  string winnersFragment;

  if (m_GameResults.has_value()) {
    vector<string> winnerNames = m_GameResults->GetWinnersNames();
    if (winnerNames.size() > 2) {
      winnersFragment = "Winners: [" + winnerNames[0] + "], and others";
    } else if (winnerNames.size() == 2) {
      winnersFragment = "Winners: [" + winnerNames[0] + "] and [" + winnerNames[1] + "]";
    } else if (winnerNames.size() == 1) {
      winnersFragment = "Winner: [" + winnerNames[0] + "]";
    }
  }

  string Description = (
    "[" + GetMap()->GetMapTitle() + "] \"" + GetCustomGameName(realm, true) + "\". " +  winnersFragment
  );

  if (m_GameLoading || m_GameLoaded)
    Description += " : " + to_string((m_EffectiveTicks / 1000) / 60) + "min";
  else
    Description += " : " + to_string((m_Aura->GetLoopTime() - m_CreationTime) / 60) + "min";

  return Description;
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
    return "[" + GetCategory() + ": " + GetShortNameLAN() + " | Frame " + to_string(m_SyncCounter) + "] ";
  } else {
    return "[" + GetCategory() + ": " + GetShortNameLAN() + "] ";
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
  const int64_t loopTime = m_Aura->GetLoopTime();
  if (loopTime < m_CreationTime) return 0;
  return (uint32_t)(loopTime - m_CreationTime);
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

    m_LastRefreshTime = m_Aura->GetLoopTime();
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
      UpdateReadyCounters();
      m_SlotInfoChanged &= ~SLOTS_DOWNLOAD_PROGRESS_CHANGED;
    }

    m_LastDownloadCounterResetTicks = m_Aura->GetLoopTicks();
  }
}

bool CGame::UpdateLobby()
{
  if (m_SlotInfoChanged & SLOTS_ALIGNMENT_CHANGED) {
    SendAllSlotInfo();
    UpdateReadyCounters();
    m_SlotInfoChanged &= ~SLOTS_ALIGNMENT_CHANGED;
  }

  if (GetIsAutoStartDue()) {
    SendAllChat("Game automatically starting in. . .");
    StartCountDown(false, true);
  }

  if (!m_Users.empty()) {
    m_LastUserSeenTicks = m_Aura->GetLoopTicks();
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

      SendAllChat(to_string(m_CountDownCounter--) + ". . .");
    } else if (GetNumJoinedUsers() >= 1) { // allow observing AI vs AI matches
      shouldStartLoading = true;
    } else {
      // Some operations may remove fake users during countdown.
      // Ensure that the game doesn't start if there are neither real nor fake users.
      // (If a user leaves or joins, the countdown is stopped elsewhere.)
      LOG_APP_IF(LogLevel::kDebug, "countdown stopped - lobby is empty.")
      StopCountDown();
    }

    m_LastCountDownTicks = m_Aura->GetLoopTicks();
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
  if (!m_GameLoading && GetSlotsOpen() > 0) {
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
        StopLoadPending("was automatically dropped after " + to_string(m_Config.m_LoadingTimeout / 1000) + " seconds");
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
      vector<uint32_t> framesBehind = GetUsersFramesBehind();
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
        uint32_t worstLaggerFrames = 0;
        uint32_t bestLaggerFrames = 0xFFFFFFFF;
        UserList laggingPlayers;
        i = static_cast<uint8_t>(m_Users.size());
        while (i--) {
          if (framesBehind[i] > GetSyncLimitSafe(m_Users[i]->GetIsObserver()) && !m_Users[i]->GetDisconnectedUnrecoverably()) {
            m_Users[i]->SetLagging(true);
            m_Users[i]->SetStartedLaggingTicks(m_Aura->GetLoopTicks());
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
          m_Users[bestLaggerIndex]->SetStartedLaggingTicks(0);
          laggingPlayers.erase(laggingPlayers.begin() + (m_Users.size() - 1 - bestLaggerIndex));
        }

        if (!laggingPlayers.empty()) {
          // start the lag screen
          DLOG_APP_IF(LogLevel::kTrace, "global lagger update (+" + ToNameListSentence(laggingPlayers) + ")")
          SendAll(GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, m_Aura->GetLoopTicks()));
          ResetDropVotes();

          m_IsLagging = true;
          m_StartedLaggingTime = m_Aura->GetLoopTime();
          m_LastLagScreenResetTime = m_Aura->GetLoopTime();

          // print debug information
          double worstLaggerSeconds = static_cast<double>(worstLaggerFrames) * static_cast<double>(m_LatencyTicks) / static_cast<double>(1000.);
          if (m_Aura->MatchLogLevel(LogLevel::kInfo)) {
            LogApp("started lagging on " + ToNameListSentence(laggingPlayers, true) + ".", LOG_ALL);
            LogApp("worst lagger is [" + m_Users[worstLaggerIndex]->GetName() + "] (" + ToFormattedString(worstLaggerSeconds) + " seconds behind)", LOG_C);
          }
        }
      }
      m_LastLagStartCheckTime = m_Aura->GetLoopTicks();
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
          StopLagger(user, "failed to reconnect within " + to_string((m_Aura->GetLoopTicks() - user->GetStartedLaggingTicks()) / 1000) + " seconds");
        } else {
          StopLagger(user, "was automatically dropped after " + to_string((m_Aura->GetLoopTicks() - user->GetStartedLaggingTicks()) / 1000) + " seconds");
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
        user->SetStartedLaggingTicks(0);
        DLOG_APP_IF(LogLevel::kTrace, "global lagger update (-" + user->GetName() + ")")
        SendAll(GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetLoopTicks()));
        LOG_APP_IF(LogLevel::kInfo, "lagging user disconnected [" + user->GetName() + "]")
      } else if (!user->GetIsSyncCounterStopLag()) {
        ++playersLaggingCounter;
      } else {
        DLOG_APP_IF(LogLevel::kTrace, "global lagger update (-" + user->GetName() + ")")
        SendAll(GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetLoopTicks()));
        user->SetLagging(false);
        user->SetStartedLaggingTicks(0);
        LOG_APP_IF(LogLevel::kInfo, "user no longer lagging [" + user->GetName() + "] (" + user->GetDelayText(true) + ")")
      }
    }

    if (playersLaggingCounter == 0) {
      m_IsLagging = false;
      m_LastActionSentTicks = hiResTicks - m_LatencyTicks;
      m_LastActionLateBy = 0;
      m_PingReportedSinceLagTimes = 0;
      LOG_APP_IF(LogLevel::kInfo, "stopped lagging after " + ToFormattedString(static_cast<double>(m_Aura->GetLoopTime() - m_StartedLaggingTime)) + " seconds")
    }
  }

  if (m_IsLagging) {
    // reset m_LastActionSentTicks because we want the game to stop running while the lag screen is up
    // exact timing is relevant for scheduler
    m_LastActionSentTicks = hiResTicks;

    // keep track of the last lag screen time so we can avoid timing out users
    m_LastLagScreenTime = m_Aura->GetLoopTime();

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
          Log("game timed out after " + to_string(m_Config.m_PlayingTimeout / 1000) + " seconds");
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

bool CGame::Update(fd_set* fd, fd_set* send_fd)
{
  const int64_t loopTicks = m_Aura->GetLoopTicks();
  const int64_t hiResTicks = GetTicks();

  // ping every 5 seconds
  // changed this to ping during game loading as well to hopefully fix some problems with people disconnecting during loading
  // changed this to ping during the game as well

  if (!m_LobbyLoading && m_Aura->GetTicksIsAfterDelay(m_LastPingTicks, 5000)) {
    // we must send pings to users who are downloading the map because
    // Warcraft III disconnects from the lobby if it doesn't receive a ping every ~90 seconds
    // so if the user takes longer than 90 seconds to download the map they would be disconnected unless we keep sending pings

    vector<uint8_t> pingPacket = GameProtocol::SEND_W3GS_PING_FROM_HOST(loopTicks);
    for (auto& user : m_Users) {
      // Avoid ping-spamming GProxy-reconnected players
      if (!user->GetDisconnected()) {
        user->Send(pingPacket);
      }
    }

    // we also broadcast the game to the local network every 5 seconds so we hijack this timer for our nefarious purposes
    if (GetUDPEnabled() && GetIsStageAcceptingJoins()) {
      if (!(m_Aura->m_Net.m_UDPMainServerEnabled && m_Aura->m_Net.m_Config.m_UDPBroadcastStrictMode)) {
        SendGameDiscoveryInfo();
      } else {
        SendGameDiscoveryRefresh();
      }
      m_GameDiscoveryActive = true;
    }

    if (m_GameDiscoveryInfoChanged & GAME_DISCOVERY_CHANGED_SLOTS) {
      SendGameDiscoveryInfoMDNS();
      m_GameDiscoveryInfoChanged &= ~GAME_DISCOVERY_CHANGED_SLOTS;
    }

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
          SendChat(user, "[APM] Recent: " + to_string(static_cast<size_t>(round(recentAPM))) + " - Average: " + to_string(static_cast<size_t>(round(user->GetAPM()))));
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

  if (m_GameLoaded && !m_IsLagging && hiResTicks - m_LastActionSentTicks >= m_LatencyTicks - m_LastActionLateBy)
    SendAllActions();

  UpdateLogs();

  // end the game if there aren't any users left
  if (m_Users.empty() && (m_GameLoading || m_GameLoaded || m_ExitingSoon)) {
    if (!m_Exiting) {
      //FlushStatsQueue();
      LOG_APP_IF(LogLevel::kInfo, "is over (no users left)")
      m_Exiting = true;
    }
    return m_Exiting;
  }

  if (m_GameLoading) {
    UpdateLoading();
  }

  // expire the votekick
  if (!m_KickVotePlayer.empty() && m_Aura->GetTimeIsAfterDelay(m_StartedKickVoteTime, 60)) {
    LOG_APP_IF(LogLevel::kDebug, "votekick against user [" + m_KickVotePlayer + "] expired")
    SendAllChat("A votekick against user [" + m_KickVotePlayer + "] has expired");
    m_KickVotePlayer.clear();
    m_StartedKickVoteTime = 0;
  }


  // start the gameover timer if there's only a configured number of players left
  // do not count observers, but fake users are counted regardless
  uint8_t RemainingPlayers = GetNumJoinedPlayersOrFakeUsers() - m_JoinedVirtualHosts;
  if (RemainingPlayers != m_StartPlayers && !GetIsGameOverTrusted() && (m_GameLoading || m_GameLoaded)) {
    if (RemainingPlayers == 0) {
      LOG_APP_IF(LogLevel::kInfo, "gameover timer started: 0 p | " + ToDecString(GetNumJoinedObservers()) + " obs | 0 fake")
      StartGameOverTimer();
    } else if (RemainingPlayers <= m_Config.m_NumPlayersToStartGameOver) {
      LOG_APP_IF(LogLevel::kInfo, "gameover timer started: " + ToDecString(GetNumJoinedPlayers()) + " p | " + ToDecString(GetNumComputers()) + " comp | " + ToDecString(GetNumJoinedObservers()) + " obs | " + to_string(m_FakeUsers.size() - m_JoinedVirtualHosts) + " fake | " + ToDecString(m_JoinedVirtualHosts) + " vhost")
      StartGameOverTimer();
    }
  }

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
    m_LastStatsUpdateTime = m_Aura->GetLoopTime();
  }

  if (GetIsStageAcceptingJoins()) {
    // Also updates mirror games.
    UpdateJoinable();
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
  const int64_t actionLateBy = GetLastActionLateBy(oldLatency);
  const int64_t newLatency = GetNextLatency(actionLateBy);
  if (newLatency != oldLatency) {
    m_LatencyTicks = newLatency;
    if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
      vector<uint8_t> storedLatency = CreateByteArray(static_cast<uint16_t>(newLatency), false);
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
  const int64_t hiResTicks = GetTicks();
  if (m_LastActionSentTicks != 0) {
    if (actionLateBy > m_Config.m_PerfThreshold && !m_IsSinglePlayer) {
      m_Aura->LogPerformanceWarning(TaskType::kGameFrame, this, actionLateBy, oldLatency, newLatency);
    }
    m_LastActionLateBy = actionLateBy;
  }
  m_LastActionSentTicks = hiResTicks;

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
    Print(GetLogPrefix() + logText);
  }
  if (logTargets & LOG_P) {
    m_Aura->LogPersistent(GetLogPrefix() + logText);
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
    string logText = GetLogPrefix() + text;
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
  LogRemoteRaw(GetLogPrefix() + text);
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
    string logText = GetLogPrefix() + record->ToString();
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
    string logText = GetLogPrefix() + record->ToString();
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
  uint8_t i = 0;
  while (i < static_cast<uint8_t>(m_Slots.size())) {
    LogApp("slot_" + ToDecString(i) + " = <" + ByteArrayToHexString(m_Slots[i].GetProtocolArray()) + ">", LOG_C);
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

void CGame::SendMulti(const std::vector<uint8_t>& UIDs, const std::vector<uint8_t>& data) const
{
  for (auto& UID : UIDs) {
    if (m_JoinInProgressVirtualUser.has_value() && UID == m_JoinInProgressVirtualUser->GetUID()) {
      if (m_GameLoaded && (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING)) {
        GameFrame& frame = m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_CHAT);
        frame.m_Bytes = vector<uint8_t>(begin(data), begin(data) + data.size());
      }
    } else {
      Send(UID, data);
    }
  }
}

void CGame::SendAll(const std::vector<uint8_t>& data) const
{
  for (auto& user : m_Users) {
    user->Send(data);
  }
}

void CGame::SendAsChat(CConnection* user, const std::vector<uint8_t>& data) const
{
  if (user->GetType() == IncomingConnectionType::kPlayer && static_cast<const GameUser::CGameUser*>(user)->GetIsInLoadingScreen()) {
    return;
  }
  user->Send(data);
}

bool CGame::SendAllAsChat(const std::vector<uint8_t>& data) const
{
  bool success = false;
  for (auto& user : m_Users) {
    if (user->GetIsInLoadingScreen()) {
      continue;
    }
    user->Send(data);
    success = true;
  }
  if (!success) return success;

  if (m_GameLoaded && (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING)) {
    GameFrame& frame = m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_CHAT);
    frame.m_Bytes = vector<uint8_t>(begin(data), begin(data) + data.size());
  }

  return success;
}

bool CGame::SendObserversAsChat(const std::vector<uint8_t>& data) const
{
  if (!m_GameLoaded) return false;
  bool success = false;
  for (auto& user : m_Users) {
    if (!user->GetIsObserver()) {
      continue;
    }
    user->Send(data);
    success = true;
  }
  if (!success) return success;

  if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
    GameFrame& frame = m_GameHistory->m_PlayingBuffer.emplace_back(GAME_FRAME_TYPE_CHAT);
    frame.m_Bytes = vector<uint8_t>(begin(data), begin(data) + data.size());
  }

  return success;
}

void CGame::SendChat(uint8_t fromUID, GameUser::CGameUser* user, const string& message, const LogLevelExtra logLevel) const
{
  // send a private message to one user - it'll be marked [Private] in Warcraft 3

  if (message.empty() || !user || user->GetIsInLoadingScreen()) {
    return;
  }

#ifdef DEBUG
  if (m_Aura->MatchLogLevel(logLevel)) {
    const GameUser::CGameUser* fromUser = GetUserFromUID(fromUID);
    if (fromUser) {
      LogApp("sent as [" + fromUser->GetName() + "] -> [" + user->GetName() + " (UID:" + ToDecString(user->GetUID()) + ")] <<" + message + ">>", LOG_C);
    } else if (fromUID == m_VirtualHostUID) {
      LogApp("sent as Virtual Host -> [" + user->GetName() + " (UID:" + ToDecString(user->GetUID()) + ")] <<" + message + ">>", LOG_C);
    } else {
      LogApp("sent as [UID:" + ToDecString(fromUID) + "] -> [" + user->GetName() + " (UID:" + ToDecString(user->GetUID()) + ")] <<" + message + ">>", LOG_C);
    }
  }
#else
  if (m_Aura->MatchLogLevel(logLevel)) {
    LogApp("sent to [" + user->GetName() + "] <<" + message + ">>", LOG_C);
  }
#endif

  vector<uint8_t> packet;
  if (!m_GameLoading && !m_GameLoaded) {
    packet = GameProtocol::SEND_W3GS_CHAT_FROM_HOST_LOBBY(fromUID, CreateByteArray(user->GetUID()), GameProtocol::Magic::ChatType::CHAT_LOBBY, message);
  } else {
    // based on my limited testing it seems that the extra flags' first byte contains 3 plus the recipient's colour to denote a private message
    packet = GameProtocol::SEND_W3GS_CHAT_FROM_HOST_IN_GAME(fromUID, CreateByteArray(user->GetUID()), GameProtocol::Magic::ChatType::CHAT_IN_GAME, user->GetChatChannel(), message);
  }
  SendAsChat(user, packet);
}

void CGame::SendChat(uint8_t fromUID, uint8_t toUID, const string& message, const LogLevelExtra logLevel) const
{
  SendChat(fromUID, GetUserFromUID(toUID), message, logLevel);
}

void CGame::SendChat(GameUser::CGameUser* user, const string& message, const LogLevelExtra logLevel) const
{
  SendChat(GetHostUID(), user, message, logLevel);
}

void CGame::SendChat(uint8_t toUID, const string& message, const LogLevelExtra logLevel) const
{
  SendChat(GetHostUID(), toUID, message, logLevel);
}

void CGame::SendChat(CAsyncObserver* spectator, const string& message, const LogLevelExtra /*logLevel*/) const
{
  spectator->SendChat(message);
}

bool CGame::SendAllChat(uint8_t fromUID, const string& message) const
{
  if (m_GameLoading && !m_Config.m_LoadInGame)
    return false;

  if (message.empty())
    return false;

  vector<uint8_t> toUIDs = GetAllChatUIDs();
  if (toUIDs.empty()) {
    return false;
  }

  if (m_Aura->GetIsLoggingTrace()) {
    const GameUser::CGameUser* fromUser = GetUserFromUID(fromUID);
    if (fromUser) {
      LogApp("sent as [" + fromUser->GetName() + "] <<" + message + ">>", LOG_C);
    } else if (fromUID == m_VirtualHostUID) {
      LogApp("sent as Virtual Host <<" + message + ">>", LOG_C);
    } else {
      LogApp("sent as [UID:" + ToDecString(fromUID) + "] <<" + message + ">>", LOG_C);
    }
  } else {
    LOG_APP_IF(LogLevel::kInfo, "sent <<" + message + ">>")
  }

  // send a public message to all users - it'll be marked [All] in Warcraft 3

  vector<uint8_t> packet;
  if (!m_GameLoading && !m_GameLoaded) {
    packet = GameProtocol::SEND_W3GS_CHAT_FROM_HOST_LOBBY(fromUID, toUIDs, GameProtocol::Magic::ChatType::CHAT_LOBBY, message);
  } else {
    packet = GameProtocol::SEND_W3GS_CHAT_FROM_HOST_IN_GAME(fromUID, toUIDs, GameProtocol::Magic::ChatType::CHAT_IN_GAME, CHAT_RECV_ALL, message);
  }
  return SendAllAsChat(packet);
}

bool CGame::SendAllChat(const string& message) const
{
  return SendAllChat(GetHostUID(), message);
}

bool CGame::SendObserverChat(uint8_t fromUID, const string& message) const
{
  if (!m_GameLoaded)
    return false;

  if (message.empty())
    return false;

  vector<uint8_t> toUIDs = GetObserverChatUIDs();
  if (toUIDs.empty()) {
    return false;
  }

  if (m_Aura->GetIsLoggingTrace()) {
    const GameUser::CGameUser* fromUser = GetUserFromUID(fromUID);
    if (fromUser) {
      LogApp("sent as [" + fromUser->GetName() + "] <<" + message + ">>", LOG_C);
    } else if (fromUID == m_VirtualHostUID) {
      LogApp("sent as Virtual Host <<" + message + ">>", LOG_C);
    } else {
      LogApp("sent as [UID:" + ToDecString(fromUID) + "] <<" + message + ">>", LOG_C);
    }
  } else {
    LOG_APP_IF(LogLevel::kInfo, "sent <<" + message + ">>")
  }

  // send a public message to all observers - it'll be marked [Observers] or [Referees] in Warcraft 3

  vector<uint8_t> packet = GameProtocol::SEND_W3GS_CHAT_FROM_HOST_IN_GAME(fromUID, toUIDs, GameProtocol::Magic::ChatType::CHAT_IN_GAME, CHAT_RECV_OBS, message);
  return SendObserversAsChat(packet);
}

bool CGame::SendObserverChat(const string& message) const
{
  return SendObserverChat(GetHostUID(), message);
}

bool CGame::SendSpectatorChat(const CAsyncObserver* excludeSpectator, const string& prefix, const string& message) const
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

bool CGame::SendSpectatorChat(const string& prefix, const string& message) const
{
  return SendSpectatorChat(nullptr, prefix, message);
}

void CGame::UpdateReadyCounters()
{
  const uint8_t numTeams = m_Map->GetMapNumTeams();
  vector<uint8_t> readyControllersByTeam(numTeams, 0);
  m_ControllersWithMap = 0;
  m_ControllersBalanced = true;
  m_ControllersReadyCount = 0;
  m_ControllersNotReadyCount = 0;
  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetSlotStatus() != SLOTSTATUS_OCCUPIED || m_Slots[i].GetTeam() == m_Map->GetVersionMaxSlots()) {
      continue;
    }
    GameUser::CGameUser* Player = GetUserFromSID(i);
    if (!Player) {
      ++m_ControllersWithMap;
      ++m_ControllersReadyCount;
      ++readyControllersByTeam[m_Slots[i].GetTeam()];
    } else if (Player->GetMapReady()) {
      ++m_ControllersWithMap;
      if (Player->UpdateReady()) {
        ++m_ControllersReadyCount;
        ++readyControllersByTeam[m_Slots[i].GetTeam()];
      } else {
        ++m_ControllersNotReadyCount;
      }
    } else {
      ++m_ControllersNotReadyCount;
    }
  }
  uint8_t refCount = 0;
  uint8_t i = static_cast<uint8_t>(numTeams);
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

vector<uint8_t> CGame::GetSlotInfo() const
{
  return GameProtocol::SEND_W3GS_SLOTINFO(m_Slots, m_RandomSeed, GetLayout(), m_Map->GetMapNumControllers());
}

vector<uint8_t> CGame::GetHandicaps() const
{
  vector<uint8_t> handicaps;
  for (const auto& slot : m_Slots) {
    handicaps.push_back(slot.GetHandicap());
  }
  return handicaps;
}

void CGame::SendAllSlotInfo()
{
  if (m_GameLoading || m_GameLoaded)
    return;

  if (!m_Users.empty()) {
    SendAll(GetSlotInfo());
  }

  m_SlotInfoChanged = SLOTS_UNCHANGED;
}

uint8_t CGame::GetNumEnabledTeamSlots(const uint8_t team) const
{
  // Only for Custom Forces
  uint8_t counter = 0;
  for (const auto& slot : m_Slots) {
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
  for (const auto& slot : m_Slots) {
    if (slot.GetTeam() == m_Map->GetVersionMaxSlots()) continue;
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
  for (const auto& slot : m_Slots) {
    if (slot.GetTeam() == m_Map->GetVersionMaxSlots()) continue;
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
  pair<uint8_t, uint8_t> largestTeam = make_pair(m_Map->GetVersionMaxSlots(), (uint8_t)0u);
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
  pair<uint8_t, uint8_t> smallestTeam = make_pair(m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots());
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
  for (const auto& slot : m_Slots) {
    if (slot.GetTeam() == m_Map->GetVersionMaxSlots()) continue;
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
    const CGameSlot& slot = m_Slots[i];
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
    if (largestTeam.second == 0 || smallestTeam.second == m_Map->GetVersionMaxSlots()) {
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
    pair<uint8_t, uint8_t> largestTeam = make_pair(m_Map->GetVersionMaxSlots(), (uint8_t)0u);
    pair<uint8_t, uint8_t> smallestTeam = make_pair(m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots());
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
        for (const auto& slot : m_Slots) {
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
  pair<uint8_t, uint8_t> largestTeam = make_pair(m_Map->GetVersionMaxSlots(), (uint8_t)0u);
  for (uint8_t team = 0; team < numTeams; ++team) {
    if (largestTeam.second < teamSizes[team]) {
      largestTeam = make_pair(team, teamSizes[team]);
    }
  }
  if (largestTeam.second <= 1) {
    return false;
  }

  const uint8_t controllerCount = GetNumControllers();
  if (controllerCount < 2) {
    return false;
  }
  //const uint8_t extraPlayers = controllerCount % largestTeam.second;
  const uint8_t expectedFullTeams = controllerCount / largestTeam.second;
  if (expectedFullTeams < 2) {
    // Compacting is used for NvNvN...
    return false;
  }
  //const uint8_t expectedMaxTeam = expectedFullTeams - (extraPlayers == 0);
  vector<uint8_t> premadeMappings(numTeams, m_Map->GetVersionMaxSlots());
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

  for (auto& slot : m_Slots) {
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
      teamSizes[i] = controllerCount - (largestTeam.second * autoTeamOffset);
    } else {
      teamSizes[i] = 0;
    }
  }

  uint8_t fillingTeamNum = autoTeamOffset;
  for (auto& slot : m_Slots) {
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
    uint8_t SID = static_cast<uint8_t>(m_Slots.size()) - 1;
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
    for (auto& slot : m_Slots) {
      if (slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED) continue;
      const uint8_t targetTeam = slot.GetIsComputer() ? computerTeam : humanTeam;
      const uint8_t wasTeam = slot.GetTeam();
      if (wasTeam != targetTeam && (remainingSlots > 0 || wasTeam != m_Map->GetVersionMaxSlots())) {
        slot.SetTeam(targetTeam);
        m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        if (wasTeam == m_Map->GetVersionMaxSlots()) {
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
  uint8_t SID = static_cast<uint8_t>(m_Slots.size());
  bitset<MAX_SLOTS_MODERN> occupiedTeams;
  while (SID--) {
    CGameSlot* slot = GetSlot(SID);
    if (slot->GetTeam() == m_Map->GetVersionMaxSlots()) continue;
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
      uint8_t swapSID = GetSelectableTeamSlotBack(nextTeam, SID, static_cast<uint8_t>(m_Slots.size()), true);
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
    pair<uint8_t, uint8_t> largestTeam = make_pair(m_Map->GetVersionMaxSlots(), (uint8_t)0u);
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
  pair<uint8_t, uint8_t> smallestTeam = make_pair(m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots());
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
  //uint8_t targetTeam = m_Slots[targetSID].GetTeam();

  const uint8_t teamAll = GetOneVsAllTeamAll();
  if (teamAll == 0xFF) return false;
  const uint8_t teamOne = GetOneVsAllTeamOne(teamAll);

  // Move the alone user to its own team.
  if (isSwap) {
    const uint8_t swapSID = GetSelectableTeamSlotBack(teamOne, static_cast<uint8_t>(m_Slots.size()), static_cast<uint8_t>(m_Slots.size()), true);
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
    uint8_t endObserverSID = static_cast<uint8_t>(m_Slots.size());
    uint8_t endAllSID = endObserverSID;
    uint8_t SID = static_cast<uint8_t>(m_Slots.size()) - 1;
    while (SID != 0xFF) {
      if (SID == targetSID || m_Slots[SID].GetTeam() == teamAll || m_Slots[SID].GetSlotStatus() != SLOTSTATUS_OCCUPIED) {
        --SID;
        continue;
      }

      uint8_t swapSID = GetSelectableTeamSlotBack(teamAll, SID, endAllSID, true);
      bool toObservers = swapSID == 0xFF; // Alliance team is full.
      if (toObservers) {
        if (m_Slots[SID].GetIsComputer()) {
          return false;
        }
        swapSID = GetSelectableTeamSlotBack(m_Map->GetVersionMaxSlots(), SID, endObserverSID, true);
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
      uint8_t SID = static_cast<uint8_t>(m_Slots.size());
      while (SID--) {
        if (SID == targetSID) continue;
        uint8_t wasTeam = m_Slots[SID].GetTeam();
        m_Slots[SID].SetTeam(teamAll);
        m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        if (wasTeam == m_Map->GetVersionMaxSlots()) {
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
    string playerName = TrimString(ToLowerCase(joinRequest.GetName()));
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

Version CGame::GuessIncomingPlayerVersion(const CConnection* /*user*/, const CIncomingJoinRequest& joinRequest, shared_ptr<const CRealm> /*fromRealm*/) const
{
  string lowerName = ToLowerCase(joinRequest.GetName());
  auto versionErrors = m_VersionErrors.find(lowerName);
  if (versionErrors == m_VersionErrors.end() || versionErrors->second.size() == m_SupportedGameVersions.count()) {
    return GetVersion();
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
      return version;
    }
    onlyRangeHeads = false;
  }

  // If all versions failed, just use the default version
  return GetVersion();
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
      fragments.push_back("in " + DurationLeftToString(requirement.second - m_Aura->GetLoopTime()));
    } else if (m_Aura->GetTimeIsAfter(requirement.second)) {
      fragments.push_back("with " + to_string(requirement.first) + " players");
    } else {
      fragments.push_back("with " + to_string(requirement.first) + "+ players after " + DurationLeftToString(requirement.second - m_Aura->GetLoopTime()));
    }
  }

  if (fragments.size() == 1) {
    return "Autostarts " + fragments[0] + ".";
  }

  return "Autostarts " + JoinStrings(fragments, "or", false) + ".";
}

string CGame::GetReadyStatusText() const
{
  string notReadyFragment;
  if (m_ControllersNotReadyCount > 0) {
    if (m_Config.m_BroadcastCmdToken.empty()) {
      notReadyFragment = " Use " + m_Config.m_PrivateCmdToken + "ready when you are.";
    } else {
      notReadyFragment = " Use " + m_Config.m_BroadcastCmdToken + "ready when you are.";
    }
  }
  if (m_ControllersReadyCount == 0) {
    return "No players ready yet." + notReadyFragment;
  }

  if (m_ControllersReadyCount == 1) {
    return "One player is ready." + notReadyFragment;
  }

  return to_string(m_ControllersReadyCount) + " players are ready." + notReadyFragment;
}

string CGame::GetCmdToken() const
{
  return m_Config.m_BroadcastCmdToken.empty() ? m_Config.m_PrivateCmdToken : m_Config.m_BroadcastCmdToken;
}

void CGame::SendAllAutoStart() const
{
  SendAllChat(GetAutoStartText());
}

uint32_t CGame::GetGameType() const
{
  uint32_t mapGameType = 0;
  if (m_DisplayMode == GAME_DISPLAY_PRIVATE) mapGameType |= MAPGAMETYPE_PRIVATEGAME;
  if (m_RestoredGame) {
    mapGameType |= MAPGAMETYPE_SAVEDGAME;
  } else {
    mapGameType |= MAPGAMETYPE_UNKNOWN0;
    mapGameType |= m_Map->GetMapGameType();
  }
  return mapGameType;
}

uint32_t CGame::CalcGameFlags() const
{
  return m_Map->GetGameConvertedFlags();
}

string CGame::GetSourceFilePath() const {
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

array<uint8_t, 2> CGame::GetAnnounceWidth() const
{
  if (GetIsProxyReconnectable()) {
    // use an invalid map width/height to indicate reconnectable games
    return GPSProtocol::SEND_GPSS_DIMENSIONS();
  }
  if (m_RestoredGame) return {0, 0};
  return m_Map->GetMapWidth();
}

array<uint8_t, 2> CGame::GetAnnounceHeight() const
{
  if (GetIsProxyReconnectable()) {
    // use an invalid map width/height to indicate reconnectable games
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
  if (m_VirtualHostUID == 0xFF) {
    return;
  }

  const std::array<uint8_t, 4> IP = {0, 0, 0, 0};
  Send(user, GameProtocol::SEND_W3GS_PLAYERINFO(GetVersion(), m_VirtualHostUID, GetLobbyVirtualHostName(), IP, IP));
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
    AppendByteArrayFast(info, playerInfo);
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
    AppendByteArrayFast(info, playerInfo);
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
    AppendByteArrayFast(info,
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
    DLOG_APP_IF(LogLevel::kTrace, "map requires bypass for v" + ToVersionString(version) + " - size " + ToFormattedString((float)clampedMapSize / (float)(1024. * 1024.)) + " MB")
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

uint8_t CGame::NextSendMap(CConnection* user, const uint8_t UID, MapTransfer& mapTransfer)
{
  if (!mapTransfer.GetIsInProgress()) {
    return MAP_TRANSFER_NONE;
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
      return MAP_TRANSFER_MISSING;
    }

    const vector<uint8_t> packet = GameProtocol::SEND_W3GS_MAPPART(GetHostUID(), UID, lastOffsetEnd, cachedChunk);
    uint32_t chunkSendSize = static_cast<uint32_t>(packet.size() - 18);
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
      if (mapTransfer.GetLastCRC32() == ByteArrayToUInt32(m_Map->GetMapCRC32(), false)) {
        return MAP_TRANSFER_DONE;
      } else {
        return MAP_TRANSFER_INVALID;
      }
    }
  }

  return MAP_TRANSFER_IN_PROGRESS;
}

void CGame::SendWelcomeMessage(GameUser::CGameUser *user) const
{
  for (size_t i = 0; i < m_Aura->m_Config.m_Greeting.size(); i++) {
    string::size_type matchIndex;
    string Line = m_Aura->m_Config.m_Greeting[i];
    if (Line.substr(0, 12) == "{SHORTDESC?}") {
      if (m_Map->GetMapShortDesc().empty()) {
        continue;
      }
      Line = Line.substr(12);
    }
    if (Line.substr(0, 12) == "{SHORTDESC!}") {
      if (!m_Map->GetMapShortDesc().empty()) {
        continue;
      }
      Line = Line.substr(12);
    }
    if (Line.substr(0, 6) == "{URL?}") {
      if (GetMapSiteURL().empty()) {
        continue;
      }
      Line = Line.substr(6);
    }
    if (Line.substr(0, 6) == "{URL!}") {
      if (!GetMapSiteURL().empty()) {
        continue;
      }
      Line = Line.substr(6);
    }
    if (Line.substr(0, 11) == "{FILENAME?}") {
      size_t LastSlash = m_MapPath.rfind('\\');
      if (LastSlash == string::npos || LastSlash > m_MapPath.length() - 6) {
        continue;
      }
      Line = Line.substr(11);
    }
    if (Line.substr(0, 12) == "{AUTOSTART?}") {
      if (m_AutoStartRequirements.empty()) {
        continue;
      }
      Line = Line.substr(12);
    }
    if (Line.substr(0, 11) == "{FILENAME!}") {
      size_t LastSlash = m_MapPath.rfind('\\');
      if (!(LastSlash == string::npos || LastSlash > m_MapPath.length() - 6)) {
        continue;
      }
      Line = Line.substr(11);
    }
    if (Line.substr(0, 10) == "{CREATOR?}") {
      if (m_CreatorText.empty()) {
        continue;
      }
      Line = Line.substr(10);
    }
    if (Line.substr(0, 10) == "{CREATOR!}") {
      if (!m_CreatorText.empty()) {
        continue;
      }
      Line = Line.substr(10);
    }
    if (Line.substr(0, 12) == "{OWNERLESS?}") {
      if (!m_OwnerLess) {
        continue;
      }
      Line = Line.substr(12);
    }
    if (Line.substr(0, 12) == "{OWNERLESS!}") {
      if (m_OwnerLess) {
        continue;
      }
      Line = Line.substr(12);
    }
    if (Line.substr(0, 8) == "{OWNER?}") {
      if (m_OwnerName.empty()) {
        continue;
      }
      Line = Line.substr(8);
    }
    if (Line.substr(0, 8) == "{OWNER!}") {
      if (!m_OwnerName.empty()) {
        continue;
      }
      Line = Line.substr(8);
    }
    if (Line.substr(0, 17) == "{CHECKLASTOWNER?}") {
      if (m_OwnerName == user->GetName() || m_LastOwner != user->GetName()) {
        continue;
      }
      Line = Line.substr(17);
    }
    if (Line.substr(0, 14) == "{REPLACEABLE?}") {
      if (!m_Replaceable) {
        continue;
      }
      Line = Line.substr(14);
    }
    if (Line.substr(0,14) == "{REPLACEABLE!}") {
      if (m_Replaceable) {
        continue;
      }
      Line = Line.substr(14);
    }
    if (Line.substr(0, 6) == "{LAN?}") {
      if (user->GetRealm(false)) {
        continue;
      }
      Line = Line.substr(6);
    }
    if (Line.substr(0, 6) == "{LAN!}") {
      if (!user->GetRealm(false)) {
        continue;
      }
      Line = Line.substr(6);
    }
    // TODO: Name censored warning
    while ((matchIndex = Line.find("{CREATOR}")) != string::npos) {
      Line.replace(matchIndex, 9, m_CreatorText);
    }
    while ((matchIndex = Line.find("{HOSTREALM}")) != string::npos) {
      switch (m_Creator.GetServiceType()) {
        case ServiceType::kRealm: {
          if (m_Creator.GetIsExpired()) {
            Line.replace(matchIndex, 11, "@unknown.battle.net");
          } else {
            Line.replace(matchIndex, 11, "@" + GetCreatedFrom<const CRealm>()->GetCanonicalDisplayName());
          }
          break;
        }
        case ServiceType::kIRC:
          Line.replace(matchIndex, 11, "@" + m_Aura->m_IRC.m_Config.m_HostName);
          break;
        case ServiceType::kDiscord:
          // FIXME: {HOSTREALM} may need to display the Discord guild
          Line.replace(matchIndex, 11, "@users.discord.com");
          break;
        default:
          Line.replace(matchIndex, 11, "@" + ToFormattedRealm());
      }
    }
    while ((matchIndex = Line.find("{OWNER}")) != string::npos) {
      Line.replace(matchIndex, 7, m_OwnerName);
    }
    while ((matchIndex = Line.find("{OWNERREALM}")) != string::npos) {
      Line.replace(matchIndex, 12, "@" + ToFormattedRealm(m_OwnerRealm));
    }
    while ((matchIndex = Line.find("{TRIGGER_PRIVATE}")) != string::npos) {
      Line.replace(matchIndex, 17, m_Config.m_PrivateCmdToken);
    }
    while ((matchIndex = Line.find("{TRIGGER_BROADCAST}")) != string::npos) {
      Line.replace(matchIndex, 19, m_Config.m_BroadcastCmdToken);
    }
    while ((matchIndex = Line.find("{TRIGGER_PREFER_PRIVATE}")) != string::npos) {
      Line.replace(matchIndex, 24, m_Config.m_PrivateCmdToken.empty() ? m_Config.m_BroadcastCmdToken : m_Config.m_PrivateCmdToken);
    }
    while ((matchIndex = Line.find("{TRIGGER_PREFER_BROADCAST}")) != string::npos) {
      Line.replace(matchIndex, 26, m_Config.m_BroadcastCmdToken.empty() ? m_Config.m_PrivateCmdToken : m_Config.m_BroadcastCmdToken);
    }
    while ((matchIndex = Line.find("{URL}")) != string::npos) {
      Line.replace(matchIndex, 5, GetMapSiteURL());
    }
    while ((matchIndex = Line.find("{FILENAME}")) != string::npos) {
      Line.replace(matchIndex, 10, GetClientFileName());
    }
    while ((matchIndex = Line.find("{SHORTDESC}")) != string::npos) {
      Line.replace(matchIndex, 11, m_Map->GetMapShortDesc());
    }
    while ((matchIndex = Line.find("{AUTOSTART}")) != string::npos) {
      Line.replace(matchIndex, 11, GetAutoStartText());
    }
    while ((matchIndex = Line.find("{READYSTATUS}")) != string::npos) {
      Line.replace(matchIndex, 13, GetReadyStatusText());
    }
    SendChat(user, Line, LogLevelExtra::kTrace);
  }
}

void CGame::SendOwnerCommandsHelp(const string& cmdToken, GameUser::CGameUser* user) const
{
  SendChat(user, cmdToken + "open [NUMBER] - opens a slot", LogLevelExtra::kTrace);
  SendChat(user, cmdToken + "close [NUMBER] - closes a slot", LogLevelExtra::kTrace);
  SendChat(user, cmdToken + "fill [DIFFICULTY] - adds computers", LogLevelExtra::kTrace);
  if (m_Map->GetMapNumTeams() > 2) {
    SendChat(user, cmdToken + "ffa - sets free for all game mode", LogLevelExtra::kTrace);
  }
  SendChat(user, cmdToken + "vsall - sets one vs all game mode", LogLevelExtra::kTrace);
  SendChat(user, cmdToken + "terminator - sets humans vs computers", LogLevelExtra::kTrace);
}

void CGame::SendCommandsHelp(const string& cmdToken, GameUser::CGameUser* user, const bool isIntro) const
{
  if (isIntro) {
    SendChat(user, "Welcome, " + user->GetName() + ". Please use " + cmdToken + GetTokenName(cmdToken) + " for commands.", LogLevelExtra::kTrace);
  } else {
    SendChat(user, "Use " + cmdToken + GetTokenName(cmdToken) + " for commands.", LogLevelExtra::kTrace);
  }
  if (!isIntro) return;
  SendChat(user, cmdToken + "ping - view your latency", LogLevelExtra::kTrace);
  SendChat(user, cmdToken + "go - starts the game", LogLevelExtra::kTrace);
  if (!m_OwnerLess && m_OwnerName.empty()) {
    SendChat(user, cmdToken + "owner - acquire permissions over this game", LogLevelExtra::kTrace);
  }
  if (MatchOwnerName(user->GetName())) {
    SendOwnerCommandsHelp(cmdToken, user);
  }
  user->GetCommandHistory()->SetSentAutoCommandsHelp(true);
}

void CGame::EventOutgoingAtomicAction(const uint8_t UID, const uint8_t* actionStart, const uint8_t* actionEnd)
{
  const uint8_t actionType = actionStart[0];

  if (actionType == ACTION_CHAT_TRIGGER) {
    if (actionEnd >= actionStart + 10u) {
      const uint8_t* chatMessageStart = actionStart + 9u;
      const uint8_t* chatMessageEnd = FindNullDelimiterOrStart(chatMessageStart, actionEnd);
      if (chatMessageStart < chatMessageEnd) {
        GameUser::CGameUser* user = GetUserFromUID(UID);
        const string chatMessage = GetStringAddressRange(chatMessageStart, chatMessageEnd);
        if (user) {
          EventChatTrigger(user, chatMessage, ByteArrayToUInt32(actionStart + 1u, false), ByteArrayToUInt32(actionStart + 5u, false));
        }
      }
    }
  }

  if (actionType == ACTION_ALLIANCE_SETTINGS && (actionEnd >= actionStart + 6u) && actionStart[1] < MAX_SLOTS_MODERN) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user) {
      const bool wantsShare = (ByteArrayToUInt32(actionStart + 2u, false) & ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY) == ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY;
      const uint8_t targetSID = actionStart[1];
      if (user->GetIsSharingUnitsWithSlot(targetSID) != wantsShare) {
        if (wantsShare) {
          LOG_APP_IF(LogLevel::kDebug, "Player [" + user->GetName() + "] granted shared unit control to [" + GetUserNameFromSID(targetSID) + "]");
        } else {
          LOG_APP_IF(LogLevel::kDebug, "Player [" + user->GetName() + "] took away shared unit control from [" + GetUserNameFromSID(targetSID) + "]");
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
                user->AddKickReason(GameUser::KickReason::ANTISHARE);
                user->KickAtLatest(m_Aura->GetLoopTicks() + timeout);
                user->AddAbuseCounter();
              }
              user->SetLeftCode(PLAYERLEAVE_LOST);
              user->SetLeftReason("autokicked - antishare");
              SendChat(user, "[ANTISHARE] You will be kicked out of the game unless you remove Shared Unit Control within " + ToDurationString(timeout / 1000) + ".");
              SendChat(targetUser, "[ANTISHARE] You may not perform further actions until [" + user->GetDisplayName() + "] removes Shared Unit Control.");
            }
            if (!wantsShare) {
              targetUser->CheckReleaseOnHoldActions();
            }
          }
        }
        if (!wantsShare && user->GetAntiShareKicked() && !user->GetIsSharingUnitsWithAnyAllies()) {
          user->ResetLeftReason();
          user->RemoveKickReason(GameUser::KickReason::ANTISHARE);
          user->CheckStillKicked();
        }
      }
    }
  }

  if (actionType == ACTION_MINIMAPSIGNAL) {
    GameUser::CGameUser* user = GetUserFromUID(UID);
    if (user && user->GetIsObserver()) {
      SendObserverChat("[" + user->GetName() + "] sent a minimap signal.");
    }
  }

  if (actionType == ACTION_GAME_CACHE_INT && actionEnd >= actionStart + 6u) {
    EventGameCacheInteger(UID, actionStart, actionEnd);
  }
}

void CGame::SendAllActionsCallback()
{
  CQueuedActionsFrame& frame = GetFirstActionFrame();
  switch (frame.callback) {
    case ON_SEND_ACTIONS_PAUSE:
      m_IsPaused = true;
      m_PauseUser = GetUserFromUID(frame.pauseUID);
      m_LastPausedTicks = m_Aura->GetLoopTicks();
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
        EventOutgoingAtomicAction(action.GetUID(), delimiters[i], delimiters[j]);
      }
    }
  }

  for (GameUser::CGameUser* user : frame.leavers) {
    DLOG_APP_IF(LogLevel::kTrace, "[" + user->GetName() + "] running scheduled deletion")
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

  const FlatMap<int64_t, string> textCache;

  vector<pair<int64_t, function<string()>>> textFuncs;
  textFuncs.reserve(3);
  textFuncs.emplace_back(HashCode("MODE"),    [this] () { return this->GetHCLCommandString(); });
  textFuncs.emplace_back(HashCode("NAME"),    [this] () { return this->GetGameName(); });
  textFuncs.emplace_back(HashCode("COUNTER"), [this, realm]() { return this->GetCreationCounterText(realm); });
  static_assert(HashCode("MODE") < HashCode("NAME"), "Hash for MODE is not before NAME");
  static_assert(HashCode("NAME") < HashCode("COUNTER"), "Hash for NAME is not before COUNTER");
  const FlatMap<int64_t, function<string()>> textFuncMap(move(textFuncs));

  string replaced = ReplaceTemplate(nameTemplate, nullptr, &textCache, nullptr, &textFuncMap);
  return TrimString(RemoveDuplicateWhiteSpace(replaced));
}

std::string CGame::GetNextCustomGameName(shared_ptr<const CRealm> realm, bool forceLobby) const
{
  string nameTemplate = GetCustomGameNameTemplate(realm, forceLobby);

  const FlatMap<int64_t, string> textCache;

  vector<pair<int64_t, function<string()>>> textFuncs;
  textFuncs.reserve(3);
  textFuncs.emplace_back(HashCode("MODE"),    [this] () { return this->GetHCLCommandString(); });
  textFuncs.emplace_back(HashCode("NAME"),    [this] () { return this->GetGameName(); });
  textFuncs.emplace_back(HashCode("COUNTER"), [this, realm]() { return this->GetNextCreationCounterText(realm); });
  static_assert(HashCode("MODE") < HashCode("NAME"), "Hash for MODE is not before NAME");
  static_assert(HashCode("NAME") < HashCode("COUNTER"), "Hash for NAME is not before COUNTER");
  const FlatMap<int64_t, function<string()>> textFuncMap(move(textFuncs));

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
    versionPrefix = "[" + ToVersionString(version) + ".UnlockMapSize] ";
  } else {
    versionPrefix = "[" + ToVersionString(version) + "] ";

}
  string startedPhrase;
  if (m_IsMirror || m_RestoredGame || m_OwnerName.empty()) {
    startedPhrase = ". (\"" + GetCustomGameName(realm) + "\")";
  } else {
    startedPhrase = ". (Started by " + m_OwnerName + ": \"" + GetCustomGameName(realm) + "\")";
  }

  string typeWord;
  if (m_RestoredGame) {
    typeWord = "Loaded game";
  } else if (m_DisplayMode == GAME_DISPLAY_PRIVATE) {
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

  return versionPrefix + typeWord + capabilityWord + m_Map->GetServerFileName() + startedPhrase;
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
      protocols.push_back("[" + user->GetName() + ": OFF]");
    } else if (user->GetGProxy()->GetIsExtended()) {
      protocols.push_back("[" + user->GetName() + ": EXT]");
    } else {
      protocols.push_back("[" + user->GetName() + ": ON]");
    }
  }
  return JoinStrings(protocols, false);
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
  frameNodes.reserve(endOffset - startOffset + 1);
  QueuedActionsFrameNode* frameNode = GetFirstActionFrameNode();
  uint8_t offset = startOffset;
  while (offset--) {
    frameNode = frameNode->next;
  }
  offset = endOffset - startOffset + 1;
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
       sortableUserPings.emplace_back(user, user->GetRTT());
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
  uint32_t slotsOff = static_cast<uint32_t>(m_Slots.size() == GetSlotsOpen() ? m_Slots.size() : GetSlotsOpen() + 1);
  uint32_t uptime = GetUptime();
  if (m_Config.m_CrossPlayMode != CrossPlayMode::kForce || (GAMEVER(1u, 24u) <= m_SupportedGameVersionsMin && m_SupportedGameVersionsMax <= GAMEVER(1u, 28u))) {
    vector<uint8_t> info = *(GetGameDiscoveryInfoTemplate());
    WriteUint32(info, gameVersion.second, m_GameDiscoveryInfoVersionOffset);
    WriteUint32(info, slotsOff, m_GameDiscoveryInfoDynamicOffset);
    WriteUint32(info, uptime, m_GameDiscoveryInfoDynamicOffset + 4);
    WriteUint16(info, hostPort, m_GameDiscoveryInfoDynamicOffset + 8);
    return info;    
  } else {
    vector<uint8_t> info = GameProtocol::SEND_W3GS_GAMEINFO(
      GetIsExpansion(),
      gameVersion,
      GetGameType(),
      GetGameFlags(),
      GetAnnounceWidth(),
      GetAnnounceHeight(),
      GetDiscoveryNameLAN(),
      GetIndexHostName(),
      uptime,
      GetSourceFilePath(),
      GetSourceFileHashBlizz(gameVersion),
      static_cast<uint32_t>(m_Slots.size()), // Total Slots
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
  m_GameDiscoveryInfoChanged &= ~GAME_DISCOVERY_CHANGED_MAJOR;
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
    GetAnnounceWidth(),
    GetAnnounceHeight(),
    GetDiscoveryNameLAN(),
    GetIndexHostName(),
    GetSourceFilePath(),
    GetSourceFileHashBlizz(GetVersion()),
    static_cast<uint32_t>(m_Slots.size()), // Total Slots
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
    static_cast<uint32_t>(m_Slots.size() == GetSlotsOpen() ? 1 : m_Slots.size() - GetSlotsOpen()),
    static_cast<uint32_t>(m_Slots.size())
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

  if (!m_Aura->m_Net.SendBroadcast(GetGameDiscoveryInfo(gameVersion, GetHostPortFromType(GAME_DISCOVERY_INTERFACE_IPV4)))) {
    // Ensure the game is available at loopback.
    LOG_APP_IF(LogLevel::kDebug, "sending IPv4 GAMEINFO packet to IPv4 Loopback (game port " + to_string(m_HostPort) + ")")
    m_Aura->m_Net.SendLoopback(GetGameDiscoveryInfo(gameVersion, m_HostPort));
  }

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
      GetAnnounceWidth(),
      GetAnnounceHeight(),
      GetDiscoveryNameLAN(),
      GetIndexHostName(),
      GetUptime(), // dynamic
      GetSourceFilePath(),
      GetSourceFileHashBlizz(GetVersion()),
      static_cast<uint32_t>(m_Slots.size()), // Total Slots
      static_cast<uint32_t>(m_Slots.size() == GetSlotsOpen() ? m_Slots.size() : GetSlotsOpen() + 1),
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
    LOG_APP_IF(LogLevel::kDebug, "deleting user [" + user->GetName() + "]: " + user->GetLeftReason())
  } else {
    LOG_APP_IF(LogLevel::kInfo, "deleting user [" + user->GetName() + "]: " + user->GetLeftReason())
  }

  if (!user->GetIsObserver()) {
    m_LastPlayerLeaveTicks = m_Aura->GetLoopTicks();
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
      DLOG_APP_IF(LogLevel::kTrace, "global lagger update (-" + user->GetName() + ")")
      SendAll(GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetLoopTicks()));
    }
    SendLeftMessage(user, (m_GameLoaded && !user->GetIsObserver()) || (!user->GetIsLeaver() && user->GetAnyKicked()));
    if (m_GameLoaded) m_IsSinglePlayer = GetIsSinglePlayerMode();
  }

  // abort the countdown if there was one in progress, but only if the user who left is actually a controller, or otherwise relevant.
  if (m_CountDownStarted && !m_CountDownFast && !m_GameLoading && !m_GameLoaded) {
    if (!user->GetIsObserver() || GetSlotsOccupied() < m_HCLCommandString.size()) {
      // Intentionally reveal the name of the lobby leaver (may be trolling.)
      SendAllChat("Countdown stopped because [" + user->GetName() + "] left!");
      m_CountDownStarted = false;
    } else {
      // Observers that leave during countdown are replaced by fake observers.
      // This ensures the integrity of many things related to game slots.
      // e.g. this allows m_ControllersWithMap to remain unchanged.
      const uint8_t replaceSID = GetEmptyObserverSID();
      const uint8_t replaceUID = GetNewUID();
      CreateFakeUserInner(replaceSID, replaceUID, "User[" + ToDecString(replaceSID + 1) + "]", false);
      m_FakeUsers.back().SetIsObserver(true);
      CGameSlot* slot = GetSlot(replaceSID);
      slot->SetTeam(m_Map->GetVersionMaxSlots());
      slot->SetColor(m_Map->GetVersionMaxSlots());
      LOG_APP_IF(LogLevel::kInfo, "replaced leaving observer by fake user (SID=" + ToDecString(replaceSID) + "|UID=" + ToDecString(replaceUID) + ")")
    }
  }

  // abort the votekick

  if (!m_KickVotePlayer.empty()) {
    SendAllChat("A votekick against user [" + m_KickVotePlayer + "] has been cancelled");
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
      controllerData->SetLeftGameTime(m_EffectiveTicks / 1000);
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
      LOG_APP_IF(LogLevel::kInfo, "gameover timer started: no players left")
      StartGameOverTimer();
    } else if (!GetIsGameOverTrusted()) {
      if (numJoinedPlayers == 1 && GetNumComputers() == 0) {
        LOG_APP_IF(LogLevel::kInfo, "gameover timer started: remaining 1 p | 0 comp | " + ToDecString(GetNumJoinedObservers()) + " obs")
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

void CGame::ReportAllPings() const
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
    pingsText.push_back((*i)->GetDisplayName() + ": " + (*i)->GetDelayText(false));
  }
  
  SendAllChat(JoinStrings(pingsText, false));

  if (m_IsLagging) {
    GameUser::CGameUser* worstLagger = SortedPlayers[0];
    if (worstLagger->GetDisconnected() && worstLagger->GetCanReconnect()) {
      ImmutableUserList waitingReconnectPlayers = GetWaitingReconnectPlayers();
      uint8_t laggerCount = CountLaggingPlayers() - static_cast<uint8_t>(waitingReconnectPlayers.size());
      string laggerText;
      if (laggerCount > 0) {
        laggerText = " (+" + ToDecString(laggerCount) + " other laggers)";
      }
      SendAllChat(ToNameListSentence(waitingReconnectPlayers) + " disconnected, but may reconnect" + laggerText);
    } else {
      string syncDelayText = worstLagger->GetSyncText();
      if (!syncDelayText.empty()) {
        uint8_t laggerCount = CountLaggingPlayers();
        if (laggerCount > 1) {
          SendAllChat(ToDecString(laggerCount) + " laggers - [" + worstLagger->GetDisplayName() + "] is " + syncDelayText);
        } else {
          SendAllChat("[" + worstLagger->GetDisplayName() + "] is " + syncDelayText);
        }
      }
    }
    if (GetCanDropOwnerMissing()) {
      SendAllChat(GetCmdToken() + "drop command is now freely available");
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
  m_LastOwnerSeenTicks = m_Aura->GetLoopTicks();
}

void CGame::SetLaggingPlayerAndUpdate(GameUser::CGameUser* user)
{
  const int64_t loopTime = m_Aura->GetLoopTime();
  const int64_t loopTicks = m_Aura->GetLoopTicks();
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
    DLOG_APP_IF(LogLevel::kTrace, "global lagger update (+" + ToNameListSentence(laggingPlayers) + ")")
    SendAll(GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, loopTicks));
  }
}

void CGame::SetEveryoneLagging()
{
  if (GetIsLagging()) {
    return;
  }
  const int64_t loopTime = m_Aura->GetLoopTime();
  const int64_t loopTicks = m_Aura->GetLoopTicks();

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
    timeRemaining = m_Aura->GetLoopTicks() - user->GetStartedLaggingTicks() - ticksRemaining.second;
  } else {
    timeRemaining = m_Aura->GetLoopTicks() - user->GetStartedLaggingTicks() - ticksRemaining.first;
  }

  timeRemaining /= 1000;
  if (timeRemaining <= 0) {
    return;
  }

  SendAllChat(user->GetUID(), "Please wait for me to reconnect (time limit: " + to_string(timeRemaining) + " seconds)");
  user->m_LastDisconnectRepeatNoticeTicks = m_Aura->GetLoopTicks();
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
    AppendByteArrayFast(m_GameHistory->m_LoadingVirtualBuffer, packet);
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
        SendAllChat(user->GetDisplayName() + " has disconnected, but is using GProxyDLL and may reconnect");
      } else {
        SendAllChat(user->GetDisplayName() + " has disconnected, but is using GProxy++ and may reconnect");
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
      SendAllChat(user->GetDisplayName() + " has disconnected (connection error - " + errorString + ") but is using GProxy++ and may reconnect");
    }

    OnRecoverableDisconnect(user);
    return;
  }

  if (!user->HasLeftReason()) {
    user->SetLeftReason("has lost the connection (connection error - " + user->GetSocket()->GetErrorString() + ")");
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
      SendAllChat(user->GetDisplayName() + " has terminated the connection, but is using GProxy++ and may reconnect");
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
      SendAllChat(user->GetDisplayName() + " has disconnected (protocol error) but is using GProxy++ and may reconnect");
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
  user->AddKickReason(GameUser::KickReason::ABUSER);
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
  user->AddKickReason(GameUser::KickReason::SPOOFER);
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

void CGame::SendChatMessage(const GameUser::CGameUser* user, const CIncomingChatMessage& chatMessage) const
{
  if (m_GameLoading && !m_Config.m_LoadInGame) {
    return;
  }

  if (!m_GameLoading && !m_GameLoaded) {
    SendMulti(chatMessage.GetToUIDs(), GameProtocol::SEND_W3GS_CHAT_FROM_HOST_LOBBY(chatMessage.GetFromUID(), chatMessage.GetToUIDs(), chatMessage.GetFlag(), chatMessage.GetMessage()));
    return;
  }

  uint8_t extraFlags = (uint8_t)chatMessage.GetExtraFlags();

  // Never allow observers/referees to send private messages to users.
  // Referee rulings/warnings are expected to be public.
  if (user->GetIsObserver() && m_Map->GetGameObservers() == GameObserversMode::kReferees && extraFlags != CHAT_RECV_OBS) {
    vector<uint8_t> targetUIDs;
    if (m_UsesCustomReferees && !user->GetIsPowerObserver()) {
      // When custom referees are enabled, only designated referees can use [All] chat.
      // Others use [Ref] exclusively.
      targetUIDs = GetFilteredChatObserverUIDs(chatMessage.GetFromUID(), chatMessage.GetToUIDs());
      extraFlags = CHAT_RECV_OBS;
    } else {
      targetUIDs = GetFilteredChatUIDs(chatMessage.GetFromUID(), chatMessage.GetToUIDs());
      extraFlags = CHAT_RECV_ALL;
    }
    if (!targetUIDs.empty()) {
      SendMulti(targetUIDs, GameProtocol::SEND_W3GS_CHAT_FROM_HOST_IN_GAME(chatMessage.GetFromUID(), targetUIDs, chatMessage.GetFlag(), extraFlags, chatMessage.GetMessage()));
    }
    return;
  }

  // When observers on defeat (or full observers) are enabled, Aura cannot reliably figure out whether a player became an observer
  // therefore, we can only rely on game clients to properly manage chat visibility
  SendMulti(chatMessage.GetToUIDs(), GameProtocol::SEND_W3GS_CHAT_FROM_HOST_IN_GAME(chatMessage.GetFromUID(), chatMessage.GetToUIDs(), chatMessage.GetFlag(), extraFlags, chatMessage.GetMessage()));
}

void CGame::QueueLeftMessage(GameUser::CGameUser* user) const
{
  CQueuedActionsFrame& frame = user->GetPingEqualizerFrame();
  frame.leavers.push_back(user);
  user->TrySetEnding();
  DLOG_APP_IF(LogLevel::kTrace, "[" + user->GetName() + "] scheduled for deletion in " + ToDecString(user->GetPingEqualizerOffset()) + " frames")
}

void CGame::SendLeftMessage(GameUser::CGameUser* user, const bool sendChat) const
{
  // This function, together with GetLeftMessage and SetLeftMessageSent,
  // controls which UIDs Aura considers available.
  if (sendChat) {
    if (!user->GetIsLeaver()) {
      SendAllChat(user->GetExtendedName() + " " + user->GetLeftReason() + ".");
    } else if (user->GetRealm(false)) {
      // Note: Not necessarily spoof-checked
      SendAllChat(user->GetUID(), user->GetLeftReason() + " [" + user->GetExtendedName() + "].");
    } else {
      SendAllChat(user->GetUID(), user->GetLeftReason());
    }
  }
  LogRemote("[" + user->GetExtendedName() + "] " + user->GetLeftReason());

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
    uint8_t toSID = static_cast<uint8_t>(m_Slots.size());
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
      GProxyFragment = " is using GProxyDLL, a Warcraft III plugin to protect against disconnections. See: <" + m_Aura->m_Net.m_Config.m_AnnounceGProxySite + ">";
    } else if (user->GetCanReconnect()) {
      if (GetIsProxyReconnectableLong()) {
        GProxyFragment = " is using an outdated GProxy++. Please upgrade to GProxyDLL at: <" + m_Aura->m_Net.m_Config.m_AnnounceGProxySite + ">";
      } else {
        GProxyFragment = " is using GProxy, a Warcraft III plugin to protect against disconnections. See: <" + m_Aura->m_Net.m_Config.m_AnnounceGProxySite + ">";
      }
    }
  }
  
  user->SetStatusMessageSent(true);
  if (OwnerFragment.empty() && GProxyFragment.empty()) {
    if (m_Aura->m_Net.m_Config.m_AnnounceIPv6 && user->GetUsingIPv6() && !hideNames) {
      SendAllChat(user->GetDisplayName() + " joined the game over IPv6.");
    }
    return;
  }

  if (hideNames) {
    if (m_IsHiddenPlayerNames) {
      SendChat(user, "[" + user->GetName() + "]" + OwnerFragment + " joined the game as [" + user->GetDisplayName() + "]");
    } else {
      SendChat(user, "[" + user->GetName() + "]" + OwnerFragment + " joined the game.");
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
      SendAllChat(user->GetDisplayName() + OwnerFragment + " joined the game over IPv6.");
    } else {
      SendAllChat(user->GetDisplayName() + OwnerFragment + " joined the game.");
    }
  } else {
    SendAllChat(user->GetDisplayName() + GProxyFragment + IPv6Fragment);
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
    IsReserved
  );

  // Now, socket belongs to GameUser::CGameUser. Don't look for it in CConnection.

  //m_Users.push_back(Player);
  connection->SetSocket(nullptr);
  connection->SetDeleteMe(true);

  if (matchingRealm) {
    Player->SetWhoisShouldBeSent(
      IsUnverifiedAdmin || MatchOwnerName(Player->GetName()) || !HasOwnerSet() ||
      matchingRealm->GetIsFloodImmune() || matchingRealm->GetHasEnhancedAntiSpoof()
    );
  }

  if (GetIsCustomForces()) {
    m_Slots[SID] = CGameSlot(m_Slots[SID].GetType(), Player->GetUID(), SLOTPROG_RST, SLOTSTATUS_OCCUPIED, 0, m_Slots[SID].GetTeam(), m_Slots[SID].GetColor(), m_Map->GetLobbyRace(&m_Slots[SID]));
  } else {
    m_Slots[SID] = CGameSlot(m_Slots[SID].GetType(), Player->GetUID(), SLOTPROG_RST, SLOTSTATUS_OCCUPIED, 0, m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots(), m_Map->GetLobbyRace(&m_Slots[SID]));
    SetSlotTeamAndColorAuto(SID);
  }
  Player->SetIsObserver(m_Slots[SID].GetTeam() == m_Map->GetVersionMaxSlots());

  // send slot info to the new user
  // the SLOTINFOJOIN packet also tells the client their assigned UID and that the join was successful.

  Player->Send(GameProtocol::SEND_W3GS_SLOTINFOJOIN(Player->GetUID(), Player->GetSocket()->GetPortLE(), Player->GetIPv4(), m_Slots, m_RandomSeed, GetLayout(), m_Map->GetMapNumControllers()));

  SendIncomingPlayerInfo(Player);

  // send virtual host info and fake users info (if present) to the new user.

  SendVirtualHostPlayerInfo(Player);
  SendFakeUsersInfo(Player);
  SendJoinedPlayersInfo(Player);

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
  if (m_Config.m_NotifyJoins && m_Config.m_IgnoredNotifyJoinPlayers.find(joinRequest.GetName()) == m_Config.m_IgnoredNotifyJoinPlayers.end()) {
    notifyString = "\x07";
  }

  size_t observerCount = GetObservers().size();
  if (observerCount > 0) {
    LogRemote("[" + Player->GetExtendedName() + "] joined (" + ToDecString(GetNumControllers()) + " / " + to_string(m_Map->GetMapNumControllers()) + ") + " + to_string(observerCount)+ " obs");
  } else {
    LogRemote("[" + Player->GetExtendedName() + "] joined (" + ToDecString(GetNumControllers()) + " / " + to_string(m_Map->GetMapNumControllers()) + ")");
  }

  if (notifyString.empty()) {
    LOG_APP_IF(LogLevel::kInfo, "user joined (P" + to_string(SID + 1) + "): [" + joinRequest.GetName() + "@" + Player->GetRealmHostName() + "#" + to_string(Player->GetUID()) + "] from [" + Player->GetIPString() + "] (" + Player->GetSocket()->GetName() + ")" + notifyString)
  } else {
    LOG_APP_IF(LogLevel::kNotice, "user joined (P" + to_string(SID + 1) + "): [" + joinRequest.GetName() + "@" + Player->GetRealmHostName() + "#" + to_string(Player->GetUID()) + "] from [" + Player->GetIPString() + "] (" + Player->GetSocket()->GetName() + ")" + notifyString)
  }
  if (joinRequest.GetIsCensored()) {
    LOG_APP_IF(LogLevel::kNotice, "user [" + joinRequest.GetName() + "] is censored name - was [" + joinRequest.GetOriginalName() + "]")
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

  Send(observer, GameProtocol::SEND_W3GS_SLOTINFOJOIN(observer->GetUID(), observer->GetSocket()->GetPortLE(), observer->GetIPv4(), m_Slots, m_RandomSeed, GetLayout(), m_Map->GetMapNumControllers()));
  observer->SendOtherPlayersInfo();
  SendMapAndVersionCheck(observer, observer->GetGameVersion());
  Send(observer, GameProtocol::SEND_W3GS_SLOTINFO(m_Slots, m_RandomSeed, GetLayout(), m_Map->GetMapNumControllers()));

  observer->SendChat("This game is in progress. You can join as an spectator.");

  string realmHostName;
  if (fromRealm) realmHostName = fromRealm->GetServer();
  LOG_APP_IF(LogLevel::kInfo, "spectator joined [" + joinRequest.GetName() + "@" + realmHostName + "#" + to_string(observer->GetUID()) + "] from [" + observer->GetIPString() + "]")
}

void CGame::EventObserverMapSize(CAsyncObserver* user, const CIncomingMapFileSize& clientMap)
{
  const bool isFirstCheck = !user->GetMapChecked();

  user->SetMapChecked(true);
  const uint32_t expectedMapSize = m_Map->GetMapSizeClamped(user->GetGameVersion());
  UpdateUserMapProgression(user, clientMap.GetFileSize(), expectedMapSize);

  if (clientMap.GetFlag() != 1 || clientMap.GetFileSize() != expectedMapSize) {
    // observer doesn't have the map
    const uint8_t checkResult = CheckCanTransferMap(user, user->GetRealm(), user->GetGameVersion(), false /* cannot start manual download for observers */);
    if (checkResult == MAP_TRANSFER_CHECK_ALLOWED) {
      MapTransfer& mapTransfer = user->GetMapTransfer();
      if (!mapTransfer.GetStarted() && clientMap.GetFlag() == 1) {
        // inform the client that we are willing to send the map

        LOG_APP_IF(LogLevel::kDebug, "map download started for observer [" + user->GetName() + "]")
        Send(user, GameProtocol::SEND_W3GS_STARTDOWNLOAD(GetHostUID()));
        mapTransfer.Start();
      } else {
        mapTransfer.SetLastAck(clientMap.GetFileSize());
      }
    } else if (isFirstCheck) {
      user->SetTimeoutAtLatest(m_Aura->GetLoopTicks() + m_Config.m_LacksMapKickDelay);

      if (GetMapSiteURL().empty()) {
        user->SendChat("Spectator [" + user->GetName() + "], please download the map before joining. (Kick in " + to_string(m_Config.m_LacksMapKickDelay / 1000) + " seconds...)");
      } else {
        user->SendChat("Spectator [" + user->GetName() + "], please download the map from <" + GetMapSiteURL() + "> before joining. (Kick in " + to_string(m_Config.m_LacksMapKickDelay / 1000) + " seconds...)");
      }

      if (!user->HasLeftReason()) {
        string reason;
        switch (checkResult) {
          case MAP_TRANSFER_CHECK_INVALID:
            reason = "invalid";
            break;
          case MAP_TRANSFER_CHECK_MISSING:
            reason = "missing";
            break;
          case MAP_TRANSFER_CHECK_DISABLED:
            reason = "disabled";
            break;
          case MAP_TRANSFER_CHECK_TOO_LARGE_VERSION:
            LOG_APP_IF(LogLevel::kDebug, "user [" + user->GetName() + "] running v" + ToVersionString(user->GetGameVersion()) + " cannot download " + ToFormattedString(m_Map->GetMapSizeMB()) + " MB map in-game")
            // falls through
          case MAP_TRANSFER_CHECK_TOO_LARGE_CONFIG:
            reason = "too large";
            break;
          case MAP_TRANSFER_CHECK_BUFFERBLOAT:
            reason = "bufferbloat";
            break;
        }
        user->SetLeftReason("autokicked - they don't have the map, and it cannot be transferred (" + reason + ")");
      }
    }
  } else if (user->GetMapTransfer().GetStarted()) {
    // calculate download rate
    const double seconds = static_cast<double>(m_Aura->GetLoopTicks() - user->GetMapTransfer().GetStartedTicks()) / 1000.f;
    LOG_APP_IF(LogLevel::kDebug, "map download finished for observer [" + user->GetName() + "] in " + ToFormattedString(seconds) + " seconds")
    user->SendChat("You downloaded the map in " + ToFormattedString(seconds) + " seconds"/* (" + ToFormattedString(Rate) + " KB/sec)"*/);
    user->GetMapTransfer().Finish();
    user->EventMapReady();
  } else {
    user->EventMapReady();
  }
}

bool CGame::CheckIPFlood(const string joinName, const sockaddr_storage* sourceAddress) const
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
      SendAllChat("Player [" + joinName + "] has the same IP address as: " + ToNameListSentence(usersSameIP));
    }
    return false;
  }
  return true;
}

uint8_t CGame::EventRequestJoin(CConnection* connection, const CIncomingJoinRequest& joinRequest)
{
  if (!GetIsStageAcceptingJoins()) {
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_STARTED));
    return JOIN_RESULT_FAIL;
  }
  if (joinRequest.GetName().empty() || joinRequest.GetName().size() > 15) {
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetOriginalName() + "] invalid name - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  }
  if (joinRequest.GetIsCensored() && m_Config.m_UnsafeNameHandler == OnUnsafeNameHandler::kDeny) {
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetOriginalName() + "] unsafe name - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  }

  // identify their joined realm
  // this is only possible because when we send a game refresh via LAN or battle.net we encode an ID value in the 4 most significant bits of the host counter
  // the client sends the host counter when it joins so we can extract the ID value here
  // note: this is not a replacement for spoof checking since it doesn't verify the user's name and it can be spoofed anyway

  string JoinedRealm;
  uint8_t HostCounterID = joinRequest.GetHostCounter() >> 24;
  bool IsUnverifiedAdmin = false;

  shared_ptr<CRealm> matchingRealm = nullptr;
  if (HostCounterID >= 0x10) {
    matchingRealm = m_Aura->GetRealmByHostCounter(HostCounterID);
    if (matchingRealm) {
      JoinedRealm = matchingRealm->GetServer();
      IsUnverifiedAdmin = matchingRealm->GetIsModerator(joinRequest.GetName()) || matchingRealm->GetIsAdmin(joinRequest.GetName());
    } else {
      // Trying to join from an unknown realm.
      HostCounterID = 0xF;
    }
  }

  if (HostCounterID < 0x10 && joinRequest.GetEntryKey() != m_EntryKey) {
    // check if the user joining via LAN knows the entry key
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "@" + JoinedRealm + "] used a wrong LAN key (" + to_string(joinRequest.GetEntryKey()) + ") - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_WRONGPASSWORD));
    return JOIN_RESULT_FAIL;
  }

  // Odd host counters are information requests
  if (HostCounterID & 0x1) {
    EventBeforeJoin(connection);
    connection->Send(GameProtocol::SEND_W3GS_SLOTINFOJOIN(GetNewUID(), connection->GetSocket()->GetPortLE(), connection->GetIPv4(), m_Slots, m_RandomSeed, GetLayout(), m_Map->GetMapNumControllers()));
    SendVirtualHostPlayerInfo(connection);
    SendFakeUsersInfo(connection);
    SendJoinedPlayersInfo(connection);
    return JOIN_RESULT_FAIL;
  }

  if (HostCounterID < 0x10 && HostCounterID != 0) {
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "@" + JoinedRealm + "] is trying to join over reserved realm " + to_string(HostCounterID) + " - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    if (HostCounterID > 0x2) {
      connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_WRONGPASSWORD));
      return JOIN_RESULT_FAIL;
    }
  }

  if (GetUserFromName(joinRequest.GetName(), false)/* && !m_IsHiddenPlayerNames*/) {
    if (m_ReportedJoinFailNames.find(joinRequest.GetName()) == end(m_ReportedJoinFailNames)) {
      if (!m_IsHiddenPlayerNames) {
        // FIXME: Someone can probably figure out whether a given player has joined a lobby by trying to impersonate them, and failing to.
        // An alternative would be no longer preventing joins and, potentially, disambiguating their names at CGame::ShowPlayerNamesGameStartLoading.
        SendAllChat("Entry denied for another user with the same name: [" + joinRequest.GetName() + "@" + JoinedRealm + "]");
      }
      m_ReportedJoinFailNames.insert(joinRequest.GetName());
    }
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "] invalid name (taken) - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  } else if (joinRequest.GetName() == GetLobbyVirtualHostName()) {
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "] spoofer (matches host name) - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  } else if (joinRequest.GetName().length() >= 7 && joinRequest.GetName().substr(0, 5) == "User[") {
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "] spoofer (matches fake users) - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  } else if (GetHMCEnabled() && joinRequest.GetName() == m_Map->GetHMCPlayerName()) {
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "] spoofer (matches HMC name) - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  } else if (joinRequest.GetName() == m_OwnerName && !m_OwnerRealm.empty() && !JoinedRealm.empty() && m_OwnerRealm != JoinedRealm) {
    // Prevent owner homonyms from other realms from joining. This doesn't affect LAN.
    // But LAN has its own rules, e.g. a LAN owner that leaves the game is immediately demoted.
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "@" + JoinedRealm + "] spoofer (matches owner name, but realm mismatch, expected " + m_OwnerRealm + ") - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  }

  if (CheckScopeBanned(joinRequest.GetName(), JoinedRealm, connection->GetIPStringStrict()) ||
    CheckUserBanned(connection, joinRequest, matchingRealm, JoinedRealm) ||
    CheckIPBanned(connection, joinRequest, matchingRealm, JoinedRealm)) {
    // let banned users "join" the game with an arbitrary UID then immediately close the connection
    // this causes them to be kicked back to the chat channel on battle.net
    const vector<CGameSlot>& Slots = m_Map->InspectSlots();
    connection->Send(GameProtocol::SEND_W3GS_SLOTINFOJOIN(1, connection->GetSocket()->GetPortLE(), connection->GetIPv4(), Slots, 0, GetLayout(), m_Map->GetMapNumControllers()));
    return JOIN_RESULT_FAIL;
  }

  if (m_GameLoaded) {
    JoinObserver(connection, joinRequest, matchingRealm);
    return JOIN_RESULT_OBSERVER;
  }

  matchingRealm = nullptr;

  const uint8_t reservedIndex = GetReservedIndex(joinRequest.GetName());
  const bool isReserved = reservedIndex < m_Reserved.size() || (!m_RestoredGame && MatchOwnerName(joinRequest.GetName()) && JoinedRealm == m_OwnerRealm);

  if (m_CheckReservation && !isReserved) {
    LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "] missing reservation - [" + connection->GetSocket()->GetName() + "] (" + connection->GetIPString() + ")")
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  }

  if (!GetAllowsIPFlood()) {
    if (!CheckIPFlood(joinRequest.GetName(), &(connection->GetSocket()->m_RemoteHost))) {
      LOG_APP_IF(LogLevel::kWarning, "ipflood rejected from " + AddressToStringStrict(connection->GetSocket()->m_RemoteHost))
      connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
      return JOIN_RESULT_FAIL;
    }
  }

  uint8_t SID = 0xFF;
  uint8_t UID = 0xFF;

  if (m_RestoredGame) {
    const vector<CGameSlot>& saveSlots = m_RestoredGame->GetSlots();
    uint8_t matchCounter = 0xFF;
    for (uint8_t i = 0; i < m_Slots.size(); ++i) {
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
              kickedPlayer->SetLeftReason("was kicked to make room for a reserved user [" + joinRequest.GetName() + "]");
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

      for (uint8_t i = 0; i < m_Slots.size(); ++i) {
        if (m_Slots[i].GetIsPlayerOrFake()) {
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
            kickedPlayer->SetLeftReason("was kicked to make room for the owner [" + joinRequest.GetName() + "]");
          }
        }
        kickedPlayer->CloseConnection();
        // Ensure the userleave message is sent before the game owner' userjoin message.
        SendLeftMessage(kickedPlayer, true);
      }
    }
  }

  if (SID >= static_cast<uint8_t>(m_Slots.size())) {
    connection->Send(GameProtocol::SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
    return JOIN_RESULT_FAIL;
  }

  // we have a slot for the new user
  // make room for them by deleting the virtual host user if we have to

  if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN && GetSlotsOpen() == 1 && GetNumJoinedUsersOrFake() > 1)
    DeleteVirtualHost();

  EventBeforeJoin(connection);
  JoinPlayer(connection, joinRequest, SID, UID, HostCounterID, JoinedRealm, isReserved, IsUnverifiedAdmin);
  return JOIN_RESULT_PLAYER;
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
      LOG_APP_IF(LogLevel::kInfo, "user [" + joinRequest.GetName() + "@" + hostName + "|" + connection->GetIPString() + "] entry denied - banned " + scopeFragment)
      if (!m_IsHiddenPlayerNames) {
        SendAllChat("[" + joinRequest.GetName() + "@" + hostName + "] is trying to join the game, but is banned");
      }
      m_ReportedJoinFailNames.insert(joinRequest.GetName());
    } else {
      LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "@" + hostName + "|" + connection->GetIPString() + "] entry denied - banned " + scopeFragment)
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
      LOG_APP_IF(LogLevel::kInfo, "user [" + joinRequest.GetName() + "@" + hostName + "|" + connection->GetIPString() + "] entry denied - IP-banned " + scopeFragment)
      if (!m_IsHiddenPlayerNames) {
        SendAllChat("[" + joinRequest.GetName() + "@" + hostName + "] is trying to join the game, but is IP-banned");
      }
      m_ReportedJoinFailNames.insert(joinRequest.GetName());
    } else {
      LOG_APP_IF(LogLevel::kDebug, "user [" + joinRequest.GetName() + "@" + hostName + "|" + connection->GetIPString() + "] entry denied - IP-banned " + scopeFragment)
    }
  }
  return isBanned;
}

void CGame::EventUserLeft(GameUser::CGameUser* user, const uint32_t clientReason)
{
  if (user->GetDisconnected()) return;
  if (m_GameLoading || m_GameLoaded || clientReason == PLAYERLEAVE_GPROXY) {
    LOG_APP_IF(LogLevel::kInfo, "user [" + user->GetName() + "] left the game (" + GameProtocol::LeftCodeToString(clientReason) + ")");
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
      user->SetLeftReason("left (" + user->GetLeftReason() + ")");
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
  LOG_APP_IF(LogLevel::kDebug, role + " [" + user->GetName() + "] finished loading in " + ToFormattedString(static_cast<double>(user->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds")

  // Update stats
  const CGameSlot* slot = InspectSlot(GetSIDFromUID(user->GetUID()));
  CGameController* controllerData = GetGameControllerFromColor(slot->GetColor());
  if (controllerData) {
    controllerData->SetLoadingTime((user->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000);
  }

  if (!m_Config.m_LoadInGame) {
    vector<uint8_t> packet = GameProtocol::SEND_W3GS_GAMELOADED_OTHERS(user->GetUID());
    if (m_BufferingEnabled & BUFFERING_ENABLED_LOADING) {
      AppendByteArrayFast(m_GameHistory->m_LoadingRealBuffer, packet);
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
    user->SetStartedLaggingTicks(0);
    RemoveFromLagScreens(user);
    user->SetStatus(USERSTATUS_PLAYING);
    UserList laggingPlayers = GetLaggingUsers();
    if (laggingPlayers.empty()) {
      m_IsLagging = false;
    }
    if (m_IsLagging) {
      DLOG_APP_IF(LogLevel::kTrace, "@[" + user->GetName() + "] lagger update (+" + ToNameListSentence(laggingPlayers) + ")")
      Send(user, GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, m_Aura->GetLoopTicks()));
      LogApp("[LoadInGame] Waiting for " + to_string(laggingPlayers.size()) + " other players to load the game...", LOG_C);

      if (laggingPlayers.size() >= 3) {
        SendChat(user, "[" + user->GetName() + "], please wait for " + to_string(laggingPlayers.size()) + " players to load the game...");
      } else {
        SendChat(user, "[" + user->GetName() + "], please wait for " + ToNameListSentence(laggingPlayers) + " to load the game...");
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
    const size_t actionSize = delimiters[j] - delimiters[i];
    if (actionType == ACTION_ALLIANCE_SETTINGS && actionSize >= 6) {
      if (delimiters[i][1] == JN_ALLIANCE_SETTINGS_SYNC_DATA) {
        LOG_APP_IF(LogLevel::kDebug, "Player [" + user->GetName() + "] synchronizing JNLoader data");
      } else if (delimiters[i][1] == MH_DOTA_SETTINGS_SYNC_DATA) {
        LOG_APP_IF(LogLevel::kDebug, "Player [" + user->GetName() + "] synchronizing DotA data");
      } else if (delimiters[i][1] < MAX_SLOTS_MODERN) {
        const bool wantsShare = (ByteArrayToUInt32(delimiters[i] + 2, false) & ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY) == ALLIANCE_SETTINGS_SHARED_CONTROL_FAMILY;
        const uint8_t targetSID = delimiters[i][1];

        if (user->GetIsSharingUnitsWithSlot(targetSID) != wantsShare) {
          if (wantsShare) {
            LOG_APP_IF(LogLevel::kDebug, "Player [" + user->GetName() + "] intends to grant shared unit control to [" + GetUserNameFromSID(targetSID) + "]");
          } else {
            LOG_APP_IF(LogLevel::kDebug, "Player [" + user->GetName() + "] intends to take away shared unit control from [" + GetUserNameFromSID(targetSID) + "]");
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
    }
  }

  if (action.GetError()) {
    LogApp("Action parser error for [" + user->GetName()+ "] (" + user->GetGameVersionString() + ") <" + ByteArrayToHexString(action.GetImmutableAction()) + ">", LOG_C | LOG_P);
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

  switch (action.GetSniffedType()) {
    case ACTION_SAVE:
      LOG_APP_IF(LogLevel::kInfo, "[" + user->GetName() + "] is saving the game")
      SendAllChat("[" + user->GetDisplayName() + "] is saving the game");
      SaveEnded(0xFF, actionFrame);
      if (user->GetCanSave()) {
        user->DropRemainingSaves();
        if (user->GetIsNativeReferee() && !user->GetCanSave()) {
          SendChat(user, "NOTE: You have reached the maximum allowed saves for this game.");
        }
      } else {
        // Game engine lets referees save without limit nor throttle whatsoever.
        // This path prevents save-spamming leading to unplayable games.
        EventUserDisconnectGameAbuse(user);
      }
      break;
    case ACTION_SAVE_ENDED:
      LOG_APP_IF(LogLevel::kInfo, "[" + user->GetName() + "] finished saving the game")
      break;
    case ACTION_PAUSE:
      LOG_APP_IF(LogLevel::kInfo, "[" + user->GetName() + "] paused the game")
      if (!user->GetIsNativeReferee()) {
        user->DropRemainingPauses();
      }
      if (actionFrame.callback != ON_SEND_ACTIONS_PAUSE) {
        actionFrame.callback = ON_SEND_ACTIONS_PAUSE;
        actionFrame.pauseUID = user->GetUID();
      }
      break;
    case ACTION_RESUME:
      if (m_PauseUser) {
        LOG_APP_IF(LogLevel::kInfo, "[" + user->GetName() + "] resumed the game (was paused by [" + m_PauseUser->GetName() + "])")
      } else {
        LOG_APP_IF(LogLevel::kInfo, "[" + user->GetName() + "] resumed the game")
      }
      actionFrame.callback = ON_SEND_ACTIONS_RESUME;
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
        LogApp("Frame " + to_string(m_SyncCounterChecked) + " | Load in game: ENABLED", LOG_C | LOG_P);
      } else {
        LogApp("Frame " + to_string(m_SyncCounterChecked) + " | Load in game: DISABLED", LOG_C | LOG_P);
      }
      LogApp("User [" + user->GetName() + "] (" + user->GetDelayText(true) + ") Reconnection: " + user->GetReconnectionText(), LOG_C | LOG_P);
      LogApp("User [" + user->GetName() + "] is synchronized with " + to_string(m_SyncPlayers[user].size()) + " user(s): " + syncListText, LOG_C | LOG_P);
      LogApp("User [" + user->GetName() + "] is no longer synchronized with " + desyncListText, LOG_ALL);
      if (GetAnyUsingGProxy()) {
        LogApp("GProxy: " + GetActiveReconnectProtocolsDetails(), LOG_C);
      }
      LogApp("==================================================================", LOG_C);
    }

    if (GetHasDesyncHandler()) {
      SendAllChat("Warning! Desync detected (" + user->GetDisplayName() + " (" + user->GetDelayText(true) + ") may not be in the same game as " + desyncListText);
      if (!GetAllowsDesync()) {
        StopDesynchronized("was automatically dropped after desync");
      }
    }
  } else if ((m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) && !m_GameHistory->GetDesynchronized()) {
    m_GameHistory->AddCheckSum(MyCheckSum);
  }
}

void CGame::EventChatTrigger(GameUser::CGameUser* user, const string& chatMessage, const uint32_t first, const uint32_t second)
{
  bool canLogChatTriggers = m_Aura->m_Config.m_LogGameChat != LOG_GAME_CHAT_NEVER && (((m_Config.m_LogChatTypes & LOG_CHAT_TYPE_COMMANDS) > 0) || m_Aura->MatchLogLevel(LogLevel::kDebug));
  if (canLogChatTriggers && (m_Config.m_LogChatTypes & LOG_CHAT_TYPE_COMMANDS) > 0) {
    m_Aura->LogPersistent(GetLogPrefix() + "[" + m_Map->GetServerFileName() + "] [CMD] ["+ user->GetExtendedName() + "] " + chatMessage);
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
    LOG_APP_IF(LogLevel::kDebug, "[" + m_Map->GetServerFileName() + "] Message by [" + user->GetName() + "]: <<" + chatMessage + ">> triggered : [0x" + ToHexString(first) + " | 0x" + ToHexString(second) + "]")
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

void CGame::EventUserChat(GameUser::CGameUser* user, const CIncomingChatMessage& incomingChatMessage)
{
  const bool isLobbyChat = incomingChatMessage.GetType() == GameProtocol::ChatToHostType::CTH_MESSAGE_LOBBY;
  if (isLobbyChat == (m_GameLoading || m_GameLoaded)) {
    // Racing condition
    return;
  }

  // relay the chat message to other users
  const uint8_t targetType = static_cast<uint8_t>(incomingChatMessage.GetExtraFlags());
  bool muteAll = !isLobbyChat && m_MuteAll;
  bool shouldRelay = m_ChatEnabled && !(muteAll && targetType == CHAT_RECV_ALL) && !user->CheckMuted();
  bool didRelay = false;

  string chatTypeFragment;
  if (isLobbyChat) {
    if (m_Aura->m_Config.m_LogGameChat != LOG_GAME_CHAT_NEVER) {
      Log("[" + user->GetDisplayName() + "] " + incomingChatMessage.GetMessage());
      if ((m_Config.m_LogChatTypes & LOG_CHAT_TYPE_NON_ASCII) && !IsASCII(incomingChatMessage.GetMessage())) {
        m_Aura->LogPersistent(GetLogPrefix() + "[Lobby] ["+ user->GetExtendedName() + "] " + incomingChatMessage.GetMessage());
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
          uint8_t privateTarget = targetType - 2;
          chatTypeFragment = "[Private " + ToDecString(privateTarget) + "] ";
        }
    }

    if (m_Aura->m_Config.m_LogGameChat == LOG_GAME_CHAT_ALWAYS) {
      Log(chatTypeFragment + "[" + user->GetDisplayName() + "] " + incomingChatMessage.GetMessage());
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
      const string textContent = incomingChatMessage.GetMessage();
      string cmdToken, command, target;
      uint8_t tokenMatch = ExtractMessageTokensAny(textContent, m_Config.m_PrivateCmdToken, m_Config.m_BroadcastCmdToken, cmdToken, command, target);
      isCommand = tokenMatch != COMMAND_TOKEN_MATCH_NONE;
      if (isCommand) {
        cmdHistory->SetUsedAnyCommands(true);
        shouldRelay = shouldRelay && !GetIsHiddenPlayerNames();
        // If we want users identities hidden, we must keep bot responses private.
        if (shouldRelay && !didRelay) {
          SendChatMessage(user, incomingChatMessage);
          didRelay = true;
        }
        shared_ptr<CCommandContext> ctx = nullptr;
        try {
          ctx = make_shared<CCommandContext>(ServiceType::kLAN /* or realm, actually*/, m_Aura, commandCFG, shared_from_this(), user, !muteAll && !GetIsHiddenPlayerNames() && (tokenMatch == COMMAND_TOKEN_MATCH_BROADCAST), &std::cout);
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
      } else if (textContent == "/p" || textContent == "/ping" || textContent == "/game") {
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
          cmdToken = m_Config.m_PrivateCmdToken;
          command = textContent.substr(1);
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
      string textContent = incomingChatMessage.GetMessage();
      for (const auto& word : m_Config.m_LoggedWords) {
        if (textContent.find(word) != string::npos) {
          logMessage = true;
          break;
        }
      }
      if (logMessage) {
        m_Aura->LogPersistent(GetLogPrefix() + chatTypeFragment + "["+ user->GetExtendedName() + "] " + textContent);
      }
    }
  }
}

void CGame::EventUserChatOrPlayerSettings(GameUser::CGameUser* user, const CIncomingChatMessage& incomingChatMessage)
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

  if (team > m_Map->GetVersionMaxSlots()) {
    return;
  }

  if (team == m_Map->GetVersionMaxSlots()) {
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

void CGame::EventUserRequestColor(GameUser::CGameUser* user, uint8_t colour)
{
  // user is requesting a colour change

  if (m_CountDownStarted || m_RestoredGame) {
    return;
  }

  if (m_Locked || user->GetIsActionLocked()) {
    SendChat(user, "You are not allowed to change your player color.");
    return;
  }

  if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS) {
    // user should directly choose a different slot instead.
    return;
  }

  if (colour >= m_Map->GetVersionMaxSlots()) {
    return;
  }

  uint8_t SID = GetSIDFromUID(user->GetUID());

  if (SID < m_Slots.size()) {
    // make sure the user isn't an observer

    if (m_Slots[SID].GetTeam() == m_Map->GetVersionMaxSlots()) {
      return;
    }

    if (!SetSlotColor(SID, colour, false)) {
      LOG_APP_IF(LogLevel::kDebug, user->GetName() + " failed to switch to color " + to_string(static_cast<uint16_t>(colour)))
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
    LOG_APP_IF(LogLevel::kDebug, "user [" + user->GetName() + "] voted to drop laggers")
    SendAllChat("Player [" + user->GetDisplayName() + "] voted to drop laggers");

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
    const uint8_t checkResult = CheckCanTransferMap(user, user->GetRealm(false), user->GetGameVersion(), user->GetDownloadAllowed());
    if (checkResult == MAP_TRANSFER_CHECK_ALLOWED) {
      MapTransfer& mapTransfer = user->GetMapTransfer();
      if (!mapTransfer.GetStarted() && clientMap.GetFlag() == 1) {
        // inform the client that we are willing to send the map

        LOG_APP_IF(LogLevel::kDebug, "map download started for user [" + user->GetName() + "]")
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
          fromURL = " from <" + GetMapSiteURL() + ">";
        }
        if (willKick) {
           kickFragment = " (Kick in " + to_string(m_Config.m_LacksMapKickDelay / 1000) + " seconds...)";
        }
        SendChat(user, user->GetName() + ", please download the map" + fromURL + " before joining." + kickFragment);
      }

      if (willKick) {
        user->AddKickReason(GameUser::KickReason::MAP_MISSING);
        user->KickAtLatest(m_Aura->GetLoopTicks() + m_Config.m_LacksMapKickDelay);

        if (!user->HasLeftReason()) {
          string reason;
          switch (checkResult) {
            case MAP_TRANSFER_CHECK_INVALID:
              reason = "invalid";
              break;
            case MAP_TRANSFER_CHECK_MISSING:
              reason = "missing";
              break;
            case MAP_TRANSFER_CHECK_DISABLED:
              reason = "disabled";
              break;
            case MAP_TRANSFER_CHECK_TOO_LARGE_VERSION:
              LOG_APP_IF(LogLevel::kDebug, "user [" + user->GetName() + "] running v" + ToVersionString(user->GetGameVersion()) + " cannot download " + ToFormattedString(m_Map->GetMapSizeMB()) + " MB map in-game")
              // falls through
            case MAP_TRANSFER_CHECK_TOO_LARGE_CONFIG:
              reason = "too large";
              break;
            case MAP_TRANSFER_CHECK_BUFFERBLOAT:
              reason = "bufferbloat";
              break;
          }
          user->SetLeftReason("autokicked - they don't have the map, and it cannot be transferred (" + reason + ")");
        }
      }
    }
  } else if (user->GetMapTransfer().GetStarted()) {
    // calculate download rate
    const double seconds = static_cast<double>(m_Aura->GetLoopTicks() - user->GetMapTransfer().GetStartedTicks()) / 1000.f;
    //const double Rate    = static_cast<double>(expectedMapSize) / 1024.f / seconds;
    LOG_APP_IF(LogLevel::kDebug, "map download finished for user [" + user->GetName() + "] in " + ToFormattedString(seconds) + " seconds")
    SendAllChat("Player [" + user->GetDisplayName() + "] downloaded the map in " + ToFormattedString(seconds) + " seconds"/* (" + ToFormattedString(Rate) + " KB/sec)"*/);
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
    SendChat(user, user->GetName() + ", your latency is " + user->GetDelayText(false), LogLevelExtra::kDebug);
    user->SetLatencySent(true);
  }

  if ((!user->GetIsReady() && user->GetMapReady() && !user->GetIsObserver()) &&
    (!m_CountDownStarted && !m_ChatOnly && m_Aura->m_StartedGames.size() < m_Aura->m_Config.m_MaxStartedGames) &&
    (user->GetReadyReminderIsDue() && user->GetIsRTTMeasuredConsistent())) {
    if (!m_AutoStartRequirements.empty()) {
      switch (GetPlayersReadyMode()) {
        case PlayersReadyMode::kExpectRace: {
          SendChat(user, "Choose your race for the match to automatically start (or type " + GetCmdToken() + "ready)");
          break;
        }
        case PlayersReadyMode::kExplicit: {
          SendChat(user, "Type " + GetCmdToken() + "ready for the match to automatically start.");
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

  uint32_t LatencyMilliseconds = user->GetOperationalRTT();
  if (LatencyMilliseconds >= m_Config.m_AutoKickPing && !user->GetIsReserved() && !user->GetIsOwner(nullopt)) {
    if (m_Users.size() > 1 && user->GetIsRTTMeasuredBadConsistent()) {
      if (!user->HasLeftReason()) {
        user->SetLeftReason("autokicked - excessive ping of " + to_string(LatencyMilliseconds) + "ms");
      }
      user->AddKickReason(GameUser::KickReason::HIGH_PING);
      user->KickAtLatest(m_Aura->GetLoopTicks() + HIGH_PING_KICK_DELAY);
      if (!user->GetHasHighPing()) {
        SendAllChat("Player [" + user->GetDisplayName() + "] has an excessive ping of " + to_string(LatencyMilliseconds) + "ms. Autokicking...");
        user->SetHasHighPing(true);
      }
    }
  } else {
    user->RemoveKickReason(GameUser::KickReason::HIGH_PING);
    user->CheckStillKicked();
    if (user->GetHasHighPing()) {
      bool HasHighPing = LatencyMilliseconds >= m_Config.m_SafeHighPing;
      if (!HasHighPing) {
        user->SetHasHighPing(HasHighPing);
        SendAllChat("Player [" + user->GetDisplayName() + "] ping went down to " + to_string(LatencyMilliseconds) + "ms");
      } else if (LatencyMilliseconds >= m_Config.m_WarnHighPing && user->GetPongCounter() % 4 == 0) {
        // Still high ping. We need to keep sending these intermittently (roughly every 20-25 seconds), so that
        // users don't assume that lack of news is good news.
        SendChat(user, user->GetName() + ", you have a high ping of " + to_string(LatencyMilliseconds) + "ms");
      }
    } else {
      bool HasHighPing = LatencyMilliseconds >= m_Config.m_WarnHighPing;
      if (HasHighPing) {
        user->SetHasHighPing(HasHighPing);
        SendAllChat("Player [" + user->GetDisplayName() + "] has a high ping of " + to_string(LatencyMilliseconds) + "ms");
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
  m_StartedLoadingTicks = m_Aura->GetLoopTicks();
  m_LastLagScreenResetTime = m_Aura->GetLoopTime();

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
    user->SetChatChannel(user->GetIsObserver() ? CHAT_RECV_OBS : (3 + m_Slots[SID].GetColor()));
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
  LOG_APP_IF(LogLevel::kInfo, "started loading: " + ToDecString(GetNumJoinedPlayers()) + " p | " + ToDecString(GetNumComputers()) + " comp | " + ToDecString(GetNumJoinedObservers()) + " obs | " + to_string(m_FakeUsers.size() - m_JoinedVirtualHosts) + " fake | " + ToDecString(m_JoinedVirtualHosts) + " vhost | " + ToDecString(m_ControllersWithMap) + " controllers")

  if (m_BufferingEnabled & BUFFERING_ENABLED_PLAYING) {
    AppendByteArrayFast(m_GameHistory->m_PlayersBuffer, GetFakeUsersLoadedInfo());
    AppendByteArrayFast(m_GameHistory->m_PlayersBuffer, GetJoinedPlayersInfo());
  }

  // When load-in-game is disabled, m_LoadingVirtualBuffer also includes
  // load messages for disconnected real players, but we let automatic resizing handle that.

  m_GameHistory->m_LoadingVirtualBuffer.reserve(5 * m_FakeUsers.size());
  for (const CGameVirtualUser& fakeUser : m_FakeUsers) {
    // send a game loaded packet for each fake user
    AppendByteArrayFast(m_GameHistory->m_LoadingVirtualBuffer, fakeUser.GetGameLoadedBytes());
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
      AppendByteArrayFast(m_GameHistory->m_LoadingRealBuffer, packet);
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
    m_Bannables.erase(m_Bannables.begin() + shrinkIndex);
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
  for (const auto& slot : m_Slots) {
    if (slot.GetTeam() == m_Map->GetVersionMaxSlots()) {
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

bool CGame::CheckSmartCommands(GameUser::CGameUser* user, const std::string& message, const uint8_t activeCmd, CCommandConfig* commandCFG)
{
  if (message.length() >= 2) {
    string prefix = ToLowerCase(message.substr(0, 2));
    if (prefix[0] == 'g' && prefix[1] == 'o' && message.find_first_not_of("goGO") == string::npos && !HasOwnerInGame()) {
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
        SendChat(user, "You may type [" + message + "] again to start the game.");
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
      vector<uint8_t> onlyFakeUsersLoaded = vector<uint8_t>(m_GameHistory->m_LoadingVirtualBuffer.begin(), m_GameHistory->m_LoadingVirtualBuffer.begin() + (5 * m_FakeUsers.size()));
      SendAll(onlyFakeUsersLoaded);
    }
  }
}

void CGame::EventGameLoaded()
{
  m_LastActionSentTicks = GetTicks(); // high-res ticks for actions scheduler
  m_FinishedLoadingTicks = m_Aura->GetLoopTicks();
  m_MapGameStartTime = CGameInteractiveHost::GetMapTime();
  m_GameLoading = false;
  m_GameLoaded = true;

  RunPlayerObfuscation();

  LOG_APP_IF(LogLevel::kInfo, "finished loading: " + ToDecString(GetNumJoinedPlayers()) + " p | " + ToDecString(GetNumComputers()) + " comp | " + ToDecString(GetNumJoinedObservers()) + " obs | " + to_string(m_FakeUsers.size() - m_JoinedVirtualHosts) + " fake | " + ToDecString(m_JoinedVirtualHosts) + " vhost")

  m_IsSinglePlayer = GetIsSinglePlayerMode();

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
    }
  }

  ImmutableUserList players = GetPlayers();
  if (players.size() <= 2) {
    m_PlayedBy = ToNameListSentence(players, true);
  } else {
    m_PlayedBy = players[0]->GetName() + ", and others";
  }

  if (Shortest && Longest) {
    SendAllChat("Shortest load by user [" + Shortest->GetDisplayName() + "] was " + ToFormattedString(static_cast<double>(Shortest->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds");
    SendAllChat("Longest load by user [" + Longest->GetDisplayName() + "] was " + ToFormattedString(static_cast<double>(Longest->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds");
  }
  const uint8_t numDisconnectedPlayers = m_StartPlayers + m_JoinedVirtualHosts - GetNumJoinedPlayersOrFakeUsers();
  if (0 < numDisconnectedPlayers) {
    SendAllChat(ToDecString(numDisconnectedPlayers) + " user(s) disconnected during game load.");
    LogRemote("Fully loaded. " + to_string(players.size()) + " players - " + ToDecString(numDisconnectedPlayers) + " user(s) disconnected");
  } else {
    LogRemote("Fully loaded. " + to_string(players.size()) + " players");
  }
  if (!DesyncedPlayers.empty()) {
    if (GetHasDesyncHandler()) {
      SendAllChat("Some users desynchronized during game load: " + ToNameListSentence(DesyncedPlayers));
      LogRemote("Some users desynchronized during game load: " + ToNameListSentence(DesyncedPlayers));
      if (!GetAllowsDesync()) {
        StopDesynchronized("was automatically dropped after desync");
      }
    }
  }

  for (auto& user : m_Users) {
    if (user->GetFinishedLoading()) {
      SendChat(user, "Your load time was " + ToFormattedString(static_cast<double>(user->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds");
    }
  }

  if (!m_Rated) {
    if (m_UnratedReason.empty()) {
      //SendAllChat("This game is unrated");
    } else {
      //SendAllChat("This game is unrated because " + m_UnratedReason);
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

  for (uint8_t SID = 0; SID < static_cast<uint8_t>(m_Slots.size()); ++SID) {
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
    LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to begin transaction for game loaded data")
    return;
  }
  m_Aura->m_DB->UpdateLatestHistoryGameId(m_PersistentId);

  m_Aura->m_DB->GameAdd(
    m_PersistentId,
    m_CreatorText,
    m_Map->GetClientPath(),
    PathToString(m_Map->GetServerPath()),
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
    LOG_APP_IF(LogLevel::kWarning, "[STATS] failed to commit transaction for game loaded data")
  } else {
    LOG_APP_IF(LogLevel::kDebug, "[STATS] commited game loaded data in " + to_string(GetTicks() - hiResTicks) + " ms")
  }
}

bool CGame::GetIsRemakeable()
{
  if (!m_Map || m_RestoredGame || m_FromAutoReHost || m_JoinInProgressVirtualUser.has_value()) {
    return false;
  }
  return true;
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

  const int64_t loopTime = m_Aura->GetLoopTime();
  const int64_t loopTicks = m_Aura->GetLoopTicks();

  m_FromAutoReHost = false;
  m_EffectiveTicks = 0;
  m_CreationTime = loopTime;
  m_LastPingTicks = loopTicks;
  m_LastRefreshTime = loopTime;
  m_LastDownloadCounterResetTicks = loopTicks;
  m_LastCountDownTicks = APP_MIN_TICKS;
  m_StartedLoadingTicks = 0;
  m_FinishedLoadingTicks = 0;
  m_MapGameStartTime = 0;
  m_LastActionSentTicks = 0;
  m_LastActionLateBy = 0;
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
  m_IsSinglePlayer = false;
  m_Rated = false;
  m_HMCEnabled = false;
  m_BufferingEnabled = BUFFERING_ENABLED_NONE;
  m_BeforePlayingEmptyActions = 0;
  m_APMTrainerPaused = false;
  m_APMTrainerTicks = 0;
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
  LOG_APP_IF(LogLevel::kInfo, "finished loading after remake")
  CreateVirtualHost();
}

uint8_t CGame::GetSIDFromUID(uint8_t UID) const
{
  if (m_Slots.size() > 0xFF)
    return 0xFF;

  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetUID() == UID)
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
  if (SID >= static_cast<uint8_t>(m_Slots.size()))
    return nullptr;

  const uint8_t UID = m_Slots[SID].GetUID();

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
  return "Slot " + ToDecString(SID + 1);
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

GameUser::CGameUser* CGame::GetUserFromColor(uint8_t colour) const
{
  for (uint8_t i = 0; i < m_Slots.size(); ++i)
  {
    if (m_Slots[i].GetColor() == colour)
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
  for (auto& slot : m_Slots) {
    if (slot.GetColor() == m_Map->GetVersionMaxSlots()) continue;
    if (slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED) continue;
    usedTeams.set(slot.GetTeam());
  }
  const uint8_t endTeam = m_Map->GetMapNumTeams();
  for (uint8_t team = 0; team < endTeam; ++team) {
    if (!usedTeams.test(team)) {
      return team;
    }
  }
  return m_Map->GetVersionMaxSlots();
}

uint8_t CGame::GetNewColor() const
{
  bitset<MAX_SLOTS_MODERN> usedColors;
  for (auto& slot : m_Slots) {
    if (slot.GetColor() == m_Map->GetVersionMaxSlots()) continue;
    usedColors.set(slot.GetColor());
  }
  for (uint8_t color = 0; color < m_Map->GetVersionMaxSlots(); ++color) {
    if (!usedColors.test(color)) {
      return color;
    }
  }
  return m_Map->GetVersionMaxSlots(); // should never happen
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
        LOG_APP_IF(LogLevel::kInfo, "resuming " + to_string(expectedPlayers) + "-user game. " + to_string(addedCounter) + " virtual users added.")
      } else {
        LOG_APP_IF(LogLevel::kInfo, "resuming " + to_string(expectedPlayers) + "-user game. " + ToDecString(expectedPlayers - activePlayers) + " missing.")
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
          LOG_APP_IF(LogLevel::kInfo, "W3HMC virtual user added at slot " + ToDecString(SID + 1))
        }
      } else {
        CGameVirtualUser* virtualUser = GetVirtualUserFromSID(SID);
        // this is the first virtual user resolved, so no need to check GetIsSlotAssignedToSystemVirtualUser
        if (virtualUser && virtualUser->GetIsObserver() && !GetIsCustomForces()/* && !GetIsSlotAssignedToSystemVirtualUser(SID)*/) {
          SetSlotTeamAndColorAuto(SID);
          virtualUser->SetIsObserver(slot->GetTeam() == m_Map->GetVersionMaxSlots());
          m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        }
        if (virtualUser && !virtualUser->GetIsObserver()) {
          m_HMCVirtualUser = CGameVirtualUserReference(*virtualUser);
          LOG_APP_IF(LogLevel::kInfo, "W3HMC virtual user assigned to slot " + ToDecString(SID + 1))
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
          LOG_APP_IF(LogLevel::kInfo, "AHCL virtual user added at slot " + ToDecString(SID + 1))
        }
      } else {
        CGameVirtualUser* virtualUser = GetVirtualUserFromSID(SID);
        // HMC and AHCL are fully compatible, so no need to check GetIsSlotAssignedToSystemVirtualUser
        if (virtualUser && virtualUser->GetIsObserver() && !GetIsCustomForces()/* && !GetIsSlotAssignedToSystemVirtualUser(SID)*/) {
          SetSlotTeamAndColorAuto(SID);
          virtualUser->SetIsObserver(slot->GetTeam() == m_Map->GetVersionMaxSlots());
          m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        }
        if (virtualUser && !virtualUser->GetIsObserver()) {
          m_AHCLVirtualUser = CGameVirtualUserReference(*virtualUser);
          LOG_APP_IF(LogLevel::kInfo, "AHCL virtual user assigned to slot " + ToDecString(SID + 1))
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
          LOG_APP_IF(LogLevel::kDebug, "Added virtual player for WC3Stats workaround [" + virtualPlayerName.value() + "]")
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
      uint8_t observerTeam = GetIsCustomForces() ? m_Map->GetMapCustomizableObserverTeam() : m_Map->GetVersionMaxSlots();
      uint8_t SID = GetIsCustomForces() ? GetEmptyTeamSID(observerTeam) : GetEmptySID(false);
      bool isEmptyAvailable = SID != 0xFF;
      if (!isEmptyAvailable) {
        // Exclude HMC / AHCL virtual users to avoid desyncs.
        // Also exclude so-called "inert" virtual user to avoid misrepresentation in WC3Stats
        SID = GetPassiveVirtualUserTeamSID(observerTeam);
      }
      CGameSlot* slot = GetSlot(SID);
      if (slot) {
        CGameVirtualUser* virtualUser = nullptr;
        if (isEmptyAvailable) {
          virtualUser = CreateFakeUserInner(SID, GetNewUID(), GetLobbyVirtualHostName(), observerTeam == m_Map->GetVersionMaxSlots());
          addedVirtualHost = true;
        } else {
          virtualUser = GetVirtualUserFromSID(SID);
        }
        // SID was either empty or passive, so we don't need to check GetIsSlotAssignedToSystemVirtualUser() again
        if (virtualUser && slot->GetTeam() != observerTeam && !GetIsCustomForces()/* && !GetIsSlotAssignedToSystemVirtualUser(SID)*/) {
          slot->SetTeam(observerTeam);
          virtualUser->SetIsObserver(slot->GetTeam() == m_Map->GetVersionMaxSlots());
          m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
        }
        if (virtualUser && slot->GetTeam() == observerTeam) {
          virtualUser->DisableAllActions();
          virtualUser->SetAllowedConnections(VIRTUAL_USER_ALLOW_CONNECTIONS_OBSERVER);
          m_JoinInProgressVirtualUser = CGameVirtualUserReference(*virtualUser);
          joinInProgressIsNativeObserver = slot->GetTeam() == m_Map->GetVersionMaxSlots();
          if (isEmptyAvailable) {
            LOG_APP_IF(LogLevel::kInfo, "Join-in-progress observer virtual user added at slot " + ToDecString(SID + 1) + " [" + virtualUser->GetName() + "]")
          } else {
            LOG_APP_IF(LogLevel::kInfo, "Join-in-progress observer virtual user assigned to slot " + ToDecString(SID + 1) + " [" + virtualUser->GetName() + "]")
          }
        }
      }
    } else {
      LOG_APP_IF(LogLevel::kWarning, "Join-in-progress feature disabled due to incompatibility with W3MMD <map.w3mmd.features.virtual_players = no>, <map.w3mmd.features.prioritize_players = no>")
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
        LOG_APP_IF(LogLevel::kDebug, "Added virtual host as referee")
      }
    } else if (m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat) {
      if ((joinInProgressIsNativeObserver && beforeFakeObserverCount <= 1) || (GetNumJoinedObservers() > 0 && beforeFakeObserverCount == 0)) {
        if (CreateFakeObserver(nullopt)) {
          ++m_JoinedVirtualHosts;
          // As a full observer, the virtual host can send messages and receive messages from other full observers.
          LOG_APP_IF(LogLevel::kDebug, "Added virtual host as full observer")
        }
      }
    }
  }

  if (m_IsAutoVirtualPlayers && GetNumJoinedPlayersOrFake() < 2) {
    if (CreateFakePlayer(nullopt)) {
      ++m_JoinedVirtualHosts;
      LOG_APP_IF(LogLevel::kDebug, "Added filler virtual player")
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
  bitset<MAX_SLOTS_MODERN> usedTeams;
  for (const auto& slot : m_Slots) {
    const uint8_t team = slot.GetTeam();
    if (team == m_Map->GetVersionMaxSlots()) continue;
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED) {
      if (usedTeams.test(team)) {
        return true;
      } else {
        usedTeams.set(team);
      }
    }
  }
  return false;
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
    SID = (SID + 1) % m_Slots.size();
  } while (!GetIsRealPlayerSlot(SID) && SID != ExceptSID);
  return SID != ExceptSID;
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

  // if a tree falls in the forest and nobody is there to hear it does it make a sound? for simplicity and some pointless privacy, let's say no
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
    return availableUIDs[distribution(gen) - 1];
  }

  return fallbackUID;
}

uint8_t CGame::GetHostUID() const
{
  // return the user to be considered the host (it can be any user)
  // mainly used for sending text messages from the bot

  if (m_VirtualHostUID != 0xFF) {
    return m_VirtualHostUID;
  }

  if (GetIsHiddenPlayerNames()) {
    return GetHiddenHostUID();
  } else {
    return GetPublicHostUID();
  }
}

uint8_t CGame::CheckCanTransferMap(const CConnection* /*connection*/, shared_ptr<const CRealm> realm, const Version& version, const bool gotPermission)
{
  if (!m_Map->GetMapFileIsValid()) {
    return m_Map->HasMismatch() ? MAP_TRANSFER_CHECK_INVALID : MAP_TRANSFER_CHECK_MISSING;
  }
  if (m_Aura->m_Net.m_Config.m_AllowTransfers == MAP_TRANSFERS_NEVER) {
    return MAP_TRANSFER_CHECK_DISABLED;
  }
  if (!m_Map->GetMapSizeIsNativeSupported(version)) {
    return MAP_TRANSFER_CHECK_TOO_LARGE_VERSION;
  }

  const uint32_t expectedMapSize = m_Map->GetMapSize();
  uint32_t maxTransferSize = m_Aura->m_Net.m_Config.m_MaxUploadSize;
  if (realm) {
    maxTransferSize = realm->GetMaxUploadSize();
  }
  bool isTooLarge = expectedMapSize > maxTransferSize * 1024;

  if (!(gotPermission || (m_Aura->m_Net.m_Config.m_AllowTransfers == MAP_TRANSFERS_AUTOMATIC && !isTooLarge))) {
    return m_Aura->m_Net.m_Config.m_AllowTransfers == MAP_TRANSFERS_AUTOMATIC ? MAP_TRANSFER_CHECK_TOO_LARGE_CONFIG : MAP_TRANSFER_CHECK_DISABLED;
  }
  if (m_Aura->m_Config.m_MaxStartedGames <= m_Aura->m_StartedGames.size()) {
    return MAP_TRANSFER_CHECK_BUFFERBLOAT;
  }
  if (!m_Aura->m_StartedGames.empty() && m_Aura->m_Net.m_Config.m_HasBufferBloat) {
    return MAP_TRANSFER_CHECK_BUFFERBLOAT;
  }
  return MAP_TRANSFER_CHECK_ALLOWED;
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
  if (SID > m_Slots.size()) return nullptr;
  return &(m_Slots[SID]);
}

const CGameSlot* CGame::InspectSlot(const uint8_t SID) const
{
  if (SID > m_Slots.size()) return nullptr;
  return &(m_Slots[SID]);
}

uint8_t CGame::GetEmptySID(bool reserved) const
{
  if (m_Slots.size() > 0xFF)
    return 0xFF;

  // look for an empty slot for a new user to occupy
  // if reserved is true then we're willing to use closed or occupied slots as long as it wouldn't displace a user with a reserved slot

  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetSlotStatus() != SLOTSTATUS_OPEN) {
      continue;
    }
    return i;
  }

  if (reserved)
  {
    // no empty slots, but since user is reserved give them a closed slot

    for (uint8_t i = 0; i < m_Slots.size(); ++i) {
      if (m_Slots[i].GetSlotStatus() == SLOTSTATUS_CLOSED && !GetIsSlotReservedForSystemVirtualUser(i)) {
        return i;
      }
    }

    // no closed slots either, give them an occupied slot but not one occupied by another reserved user
    // first look for a user who is downloading the map and has the least amount downloaded so far

    uint8_t LeastSID = 0xFF;
    uint8_t LeastDownloaded = 100;

    for (uint8_t i = 0; i < m_Slots.size(); ++i) {
      if (!m_Slots[i].GetIsPlayerOrFake()) continue;
      GameUser::CGameUser* Player = GetUserFromSID(i);
      if (Player && !Player->GetIsReserved() && m_Slots[i].GetDownloadStatus() < LeastDownloaded) {
        LeastSID = i;
        LeastDownloaded = m_Slots[i].GetDownloadStatus();
      }
    }

    if (LeastSID != 0xFF) {
      return LeastSID;
    }

    // nobody who isn't reserved is downloading the map, just choose the first user who isn't reserved

    for (uint8_t i = 0; i < m_Slots.size(); ++i) {
      if (!m_Slots[i].GetIsPlayerOrFake()) continue;
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
  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetSlotStatus() == SLOTSTATUS_OPEN && m_Slots[i].GetTeam() == team)
      return i;
  }
  return 0xFF;
}

uint8_t CGame::GetEmptyPlayerSID() const
{
  if (m_Slots.size() > 0xFF)
    return 0xFF;

  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetSlotStatus() != SLOTSTATUS_OPEN) continue;
    if (!GetIsCustomForces()) {
      return i;
    }
    if (m_Slots[i].GetTeam() != m_Map->GetVersionMaxSlots()) {
      return i;
    }
  }

  return 0xFF;
}

uint8_t CGame::GetEmptyObserverSID() const
{
  if (m_Slots.size() > 0xFF)
    return 0xFF;

  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetSlotStatus() != SLOTSTATUS_OPEN) continue;
    if (m_Slots[i].GetTeam() == m_Map->GetVersionMaxSlots()) {
      return i;
    }
  }

  return 0xFF;
}

uint8_t CGame::GetVirtualUserTeamSID(const uint8_t team) const
{
  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetTeam() == team && GetIsVirtualPlayerSlot(i)) {
      return i;
    }
  }
  return 0xFF;
}

uint8_t CGame::GetPassiveVirtualUserTeamSID(const uint8_t team) const
{
  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    if (m_Slots[i].GetTeam() != team) {
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
  const uint8_t team = m_Slots[SID].GetTeam();

  // Look for the next ally, starting from the current SID, and wrapping over.
  uint8_t allySID = SID;
  do {
    allySID = (allySID + 1) % m_Slots.size();
  } while (allySID != SID && !(m_Slots[allySID].GetTeam() == team && m_Slots[allySID].GetSlotStatus() == SLOTSTATUS_OPEN));

  if (allySID == SID) {
    return false;
  }
  return SwapSlots(SID, allySID);
}

bool CGame::SwapSlots(const uint8_t SID1, const uint8_t SID2)
{
  if (SID1 >= static_cast<uint8_t>(m_Slots.size()) || SID2 >= static_cast<uint8_t>(m_Slots.size()) || SID1 == SID2) {
    return false;
  }
  if (GetIsSlotReservedForSystemVirtualUser(SID1) || GetIsSlotReservedForSystemVirtualUser(SID2)) {
    return false;
  }

  {
    // Slot1, Slot2 are implementation details
    // Depending on the branch, they may not necessarily match the actual slots after the swap.
    CGameSlot Slot1 = m_Slots[SID1];
    CGameSlot Slot2 = m_Slots[SID2];

    if (!Slot1.GetIsSelectable() || !Slot2.GetIsSelectable()) {
      return false;
    }

    if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS) {
      // don't swap the type, team, colour, race, or handicap
      m_Slots[SID1] = CGameSlot(Slot1.GetType(), Slot2.GetUID(), Slot2.GetDownloadStatus(), Slot2.GetSlotStatus(), Slot2.GetComputer(), Slot1.GetTeam(), Slot1.GetColor(), Slot1.GetRace(), Slot2.GetComputerType(), Slot1.GetHandicap());
      m_Slots[SID2] = CGameSlot(Slot2.GetType(), Slot1.GetUID(), Slot1.GetDownloadStatus(), Slot1.GetSlotStatus(), Slot1.GetComputer(), Slot2.GetTeam(), Slot2.GetColor(), Slot2.GetRace(), Slot1.GetComputerType(), Slot2.GetHandicap());
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
        if (teamOne != teamTwo && (teamOne == m_Map->GetVersionMaxSlots() || teamTwo == m_Map->GetVersionMaxSlots())) {
          Slot1.SetColor(colorTwo);
          Slot2.SetColor(colorOne);
        }
      }

      // swap everything (what we swapped already is reverted)
      m_Slots[SID1] = Slot2;
      m_Slots[SID2] = Slot1;
    }
  }

  uint8_t i = static_cast<uint8_t>(m_FakeUsers.size());
  while (i--) {
    uint8_t fakeSID = m_FakeUsers[i].GetSID();
    if (fakeSID == SID1) {
      m_FakeUsers[i].SetSID(SID2);
      m_FakeUsers[i].SetIsObserver(SID2 == m_Map->GetVersionMaxSlots());
    } else if (fakeSID == SID2) {
      m_FakeUsers[i].SetSID(SID1);
      m_FakeUsers[i].SetIsObserver(SID1 == m_Map->GetVersionMaxSlots());
    }
  }

  // Players that are at given slots afterwards.
  GameUser::CGameUser* PlayerOne = GetUserFromSID(SID1);
  GameUser::CGameUser* PlayerTwo = GetUserFromSID(SID2);
  if (PlayerOne) {
    PlayerOne->SetIsObserver(m_Slots[SID1].GetTeam() == m_Map->GetVersionMaxSlots());
    if (PlayerOne->GetIsObserver()) {
      PlayerOne->SetPowerObserver(PlayerOne->GetIsObserver() && m_Map->GetGameObservers() == GameObserversMode::kReferees);
      PlayerOne->ClearUserReady();
    }
  }
  if (PlayerTwo) {
    PlayerTwo->SetIsObserver(m_Slots[SID2].GetTeam() == m_Map->GetVersionMaxSlots());
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
    m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_OPEN, SLOTCOMP_NO, slot->GetTeam(), slot->GetColor(), m_Map->GetLobbyRace(slot));
  } else {
    m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_OPEN, SLOTCOMP_NO, m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots(), SLOTRACE_RANDOM);
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
  while (SID < m_Slots.size()) {
    if (!GetIsSlotReservedForSystemVirtualUser(SID) && m_Slots[SID].GetSlotStatus() == SLOTSTATUS_CLOSED) {
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
  const uint8_t openSlots = static_cast<uint8_t>(GetSlotsOpen());
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
  const uint8_t openSlots = static_cast<uint8_t>(GetSlotsOpen());
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
    m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_CLOSED, SLOTCOMP_NO, slot->GetTeam(), slot->GetColor(), m_Map->GetLobbyRace(slot));
  } else {
    m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, SLOTSTATUS_CLOSED, SLOTCOMP_NO, m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots(), SLOTRACE_RANDOM);
  }
  
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  return true;
}

bool CGame::CloseSlot()
{
  uint8_t SID = 0;
  while (SID < m_Slots.size()) {
    if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN) {
      return CloseSlot(SID, false);
    }
    ++SID;
  }
  return false;
}

bool CGame::ComputerSlot(uint8_t SID, uint8_t skill, bool kick)
{
  if (SID >= static_cast<uint8_t>(m_Slots.size()) || skill > SLOTCOMP_HARD) {
    return false;
  }
  if (GetIsSlotReservedForSystemVirtualUser(SID)) {
    return false;
  }

  CGameSlot Slot = m_Slots[SID];
  if (!Slot.GetIsSelectable()) {
    return false;
  }
  if (Slot.GetSlotStatus() != SLOTSTATUS_OCCUPIED && GetNumControllers() == m_Map->GetMapNumControllers()) {
    return false;
  }
  if (Slot.GetTeam() == m_Map->GetVersionMaxSlots()) {
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
    if (GetSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
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
    const uint8_t newSID = GetSelectableTeamSlotFront(team, static_cast<uint8_t>(m_Slots.size()), static_cast<uint8_t>(m_Slots.size()), force);
    if (newSID == 0xFF) return false;
    return SwapSlots(SID, newSID);
  } else {
    const bool fromObservers = slot->GetTeam() == m_Map->GetVersionMaxSlots();
    const bool toObservers = team == m_Map->GetVersionMaxSlots();
    if (toObservers && !slot->GetIsPlayerOrFake()) return false;
    if (fromObservers && !toObservers && GetNumControllers() >= m_Map->GetMapNumControllers()) {
      // Observer cannot become controller if the map's controller limit has been reached.
      return false;
    }

    slot->SetTeam(team);
    if (toObservers || fromObservers) {
      if (toObservers) {
        slot->SetColor(m_Map->GetVersionMaxSlots());
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

bool CGame::SetSlotColor(const uint8_t SID, const uint8_t colour, const bool force)
{
  CGameSlot* slot = GetSlot(SID);
  if (!slot || slot->GetColor() == colour || !slot->GetIsSelectable()) {
    return false;
  }

  if (slot->GetSlotStatus() != SLOTSTATUS_OCCUPIED || slot->GetTeam() == m_Map->GetVersionMaxSlots()) {
    // Only allow active users to choose their colors.
    //
    // Open/closed slots do actually have a color when Fixed Player Settings is enabled,
    // but I'm still not providing API for it.
    return false;
  }

  CGameSlot* takenSlot = nullptr;
  uint8_t takenSID = 0xFF;

  // if the requested color is taken, try to exchange colors
  for (uint8_t i = 0; i < m_Slots.size(); ++i) {
    CGameSlot* matchSlot = &(m_Slots[i]);
    if (matchSlot->GetColor() != colour) continue;
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
    if (takenSlot) takenSlot->SetColor(m_Slots[SID].GetColor());
    m_Slots[SID].SetColor(colour);
    m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
    return true;
  }
}

void CGame::SetSlotTeamAndColorAuto(const uint8_t SID)
{
  // Custom Forces must use m_Slots[SID].GetColor() / m_Slots[SID].GetTeam()
  if (GetLayout() != MAPLAYOUT_ANY) return;
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
      uint8_t otherTeam = m_Map->GetVersionMaxSlots();
      uint8_t numSkipped = 0;
      for (uint8_t i = 0; i < m_Slots.size(); ++i) {
        const CGameSlot* otherSlot = InspectSlot(i);
        if (otherSlot->GetSlotStatus() != SLOTSTATUS_OCCUPIED) {
          if (i < SID) ++numSkipped;
          continue;
        }
        if (otherSlot->GetTeam() == m_Map->GetVersionMaxSlots()) {
          if (i < SID) ++numSkipped;
        } else if (otherTeam != m_Map->GetVersionMaxSlots()) {
          otherTeamError = true;
        } else {
          otherTeam = otherSlot->GetTeam();
        }
      }
      if (m_Map->GetMapNumControllers() == 2 && !otherTeamError && otherTeam < 2) {
        // Streamline team selection for 1v1 maps
        slot->SetTeam(1 - otherTeam);
      } else {
        slot->SetTeam((SID - numSkipped) % m_Map->GetMapNumTeams());
      }
      break;
    }
  }
  slot->SetColor(GetNewColor());
}

void CGame::OpenAllSlots()
{
  bool anyChanged = false;
  uint8_t i = static_cast<uint8_t>(m_Slots.size());
  while (i--) {
    if (!GetIsSlotReservedForSystemVirtualUser(i) && m_Slots[i].GetSlotStatus() == SLOTSTATUS_CLOSED) {
      m_Slots[i].SetSlotStatus(SLOTSTATUS_OPEN);
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
  for (uint8_t SID = 0; SID < m_Slots.size(); ++SID) {
    if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN) {
      if (firstSID == 0xFF) firstSID = SID + 1;
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
  uint8_t SID = static_cast<uint8_t>(m_Slots.size());
  while (firstSID < SID--) {
    if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN && m_Slots[SID].GetTeam() == team) {
      m_Slots[SID].SetSlotStatus(SLOTSTATUS_CLOSED);
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
  uint8_t SID = static_cast<uint8_t>(m_Slots.size());
  while (firstSID < SID--) {
    if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN && occupiedTeams.test(m_Slots[SID].GetTeam())) {
      m_Slots[SID].SetSlotStatus(SLOTSTATUS_CLOSED);
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
  uint8_t SID = static_cast<uint8_t>(m_Slots.size());
  while (firstSID < SID--) {
    if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN) {
      m_Slots[SID].SetSlotStatus(SLOTSTATUS_CLOSED);
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
    if (slot->GetTeam() == m_Map->GetVersionMaxSlots()) {
      return false;
    }
    if (slot->GetIsPlayerOrFake()) DeleteFakeUser(SID);
    m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RDY, SLOTSTATUS_OCCUPIED, SLOTCOMP_YES, slot->GetTeam(), slot->GetColor(), m_Map->GetLobbyRace(slot), skill);
    if (resetLayout) ResetLayout(false);
  } else {
    if (slot->GetIsPlayerOrFake()) DeleteFakeUser(SID);
    m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RDY, SLOTSTATUS_OCCUPIED, SLOTCOMP_YES, m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots(), m_Map->GetLobbyRace(slot), skill);
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
    uint8_t SID = static_cast<uint8_t>(m_Slots.size());
    while (SID--) {
      if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OCCUPIED && m_Slots[SID].GetIsComputer()) {
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
  while (0 < remainingComputers && SID < m_Slots.size()) {
    // overrideComputers false means only newly added computers are counted in remainingComputers
    if (ComputerSlotInner(SID, skill, ignoreLayout, overrideComputers)) {
      --remainingComputers;
    }
    ++SID;
  }

  if (GetSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
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
  if (!hasUsers && m_Slots.size() == m_Map->GetMapNumControllers()) {
    --remainingSlots;
  }

  if (remainingSlots == 0) {
    return false;
  }

  uint8_t SID = 0;
  while (0 < remainingSlots && SID < m_Slots.size()) {
    // don't ignore layout, don't override computers
    if (ComputerSlotInner(SID, skill, false, false)) {
      --remainingSlots;
    }
    ++SID;
  }

  if (GetSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  return true;
}

void CGame::ShuffleSlots()
{
  // we only want to shuffle the user slots (exclude observers)
  // that means we need to prevent this function from shuffling the open/closed/computer slots too
  // so we start by copying the user slots to a temporary vector

  vector<CGameSlot> PlayerSlots;

  for (auto& slot : m_Slots) {
    if (slot.GetIsPlayerOrFake() && slot.GetTeam() != m_Map->GetVersionMaxSlots()) {
      PlayerSlots.push_back(slot);
    }
  }

  // now we shuffle PlayerSlots

  if (GetIsCustomForces()) {
    // rather than rolling our own probably broken shuffle algorithm we use random_shuffle because it's guaranteed to do it properly
    // so in order to let random_shuffle do all the work we need a vector to operate on
    // unfortunately we can't just use PlayerSlots because the team/colour/race shouldn't be modified
    // so make a vector we can use

    vector<uint8_t> SIDs;

    for (uint8_t i = 0; i < PlayerSlots.size(); ++i)
      SIDs.push_back(i);

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(begin(SIDs), end(SIDs), g);

    // now put the PlayerSlots vector in the same order as the SIDs vector

    vector<CGameSlot> Slots;

    // as usual don't modify the type/team/colour/race

    for (uint8_t i = 0; i < SIDs.size(); ++i)
      Slots.emplace_back(PlayerSlots[SIDs[i]].GetType(), PlayerSlots[SIDs[i]].GetUID(), PlayerSlots[SIDs[i]].GetDownloadStatus(), PlayerSlots[SIDs[i]].GetSlotStatus(), PlayerSlots[SIDs[i]].GetComputer(), PlayerSlots[i].GetTeam(), PlayerSlots[i].GetColor(), PlayerSlots[i].GetRace());

    PlayerSlots = Slots;
  } else {
    // regular game
    // it's easy when we're allowed to swap the team/colour/race!

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(begin(PlayerSlots), end(PlayerSlots), g);
  }

  // now we put m_Slots back together again

  auto CurrentPlayer = begin(PlayerSlots);
  vector<CGameSlot> Slots;

  for (auto& slot : m_Slots) {
    if (slot.GetIsPlayerOrFake() && slot.GetTeam() != m_Map->GetVersionMaxSlots()) {
      Slots.push_back(*CurrentPlayer);
      ++CurrentPlayer;
    } else {
      Slots.push_back(slot);
    }
  }

  m_Slots = Slots;
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
}

void CGame::ReportSpoofed(const string& server, GameUser::CGameUser* user)
{
  if (!m_IsHiddenPlayerNames) {
    SendAllChat("Name spoof detected. The real [" + user->GetName() + "@" + server + "] is not in this game.");
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
      SendAllChat("Identity accepted for game owner [" + Player->GetName() + "@" + server + "]");
    } else {
      SendChat(Player, "Identity accepted for [" + Player->GetName() + "@" + server + "]");
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
    user->RemoveKickReason(GameUser::KickReason::HIGH_PING);
    user->RemoveKickReason(GameUser::KickReason::MAP_MISSING);
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
    user->RemoveKickReason(GameUser::KickReason::HIGH_PING);
    user->RemoveKickReason(GameUser::KickReason::MAP_MISSING);
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
      LOG_APP_IF(LogLevel::kInfo, "user [" + rawName + "@" + hostName + "|" + addressLiteral + "] entry denied: game-scope banned")
      SendAllChat("[" + rawName + "@" + hostName + "] is trying to join the game, but is banned");
      m_ReportedJoinFailNames.insert(rawName);
    } else {
      LOG_APP_IF(LogLevel::kDebug, "user [" + rawName + "@" + hostName + "|" + addressLiteral + "] entry denied: game-scope banned")
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

vector<uint32_t> CGame::GetPlayersFramesBehind() const
{
  uint8_t i = static_cast<uint8_t>(m_Users.size());
  vector<uint32_t> framesBehind(i, 0);
  while (i--) {
    if (m_Users[i]->GetIsObserver()) {
      continue;
    }
    if (m_SyncCounter <= m_Users[i]->GetNormalSyncCounter()) {
      continue;
    }
    framesBehind[i] = static_cast<uint32_t>(m_SyncCounter - m_Users[i]->GetNormalSyncCounter());
  }
  return framesBehind;
}

vector<uint32_t> CGame::GetUsersFramesBehind() const
{
  uint8_t i = static_cast<uint8_t>(m_Users.size());
  vector<uint32_t> framesBehind(i, 0);
  while (i--) {
    if (m_SyncCounter <= m_Users[i]->GetNormalSyncCounter()) {
      continue;
    }
    framesBehind[i] = static_cast<uint32_t>(m_SyncCounter - m_Users[i]->GetNormalSyncCounter());
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
    DLOG_APP_IF(LogLevel::kTrace, "@[" + otherUser->GetName() + "] lagger update (-" + user->GetName() + ")")
    Send(otherUser, GameProtocol::SEND_W3GS_STOP_LAG(user, m_Aura->GetLoopTicks()));
  }
}

void CGame::ResetLagScreen()
{
  const UserList laggingPlayers = GetLaggingUsers();
  if (laggingPlayers.empty()) {
    return;
  }
  const vector<uint8_t> startLagPacket = GameProtocol::SEND_W3GS_START_LAG(laggingPlayers, m_Aura->GetLoopTicks());
  const bool anyUsingGProxy = GetAnyUsingGProxy();

  if (m_GameLoading) {
    ++m_BeforePlayingEmptyActions;
  }

  for (auto& user : m_Users) {
    if (user->GetFinishedLoading()) {
      for (auto& otherUser : m_Users) {
        if (!otherUser->GetIsLagging()) continue;
        DLOG_APP_IF(LogLevel::kTrace, "@[" + user->GetName() + "] lagger update (-" + otherUser->GetName() + ")")
        Send(user, GameProtocol::SEND_W3GS_STOP_LAG(otherUser, m_Aura->GetLoopTicks()));
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

      DLOG_APP_IF(LogLevel::kTrace, "@[" + user->GetName() + "] lagger update (+" + ToNameListSentence(laggingPlayers) + ")")
      Send(user, startLagPacket);

      if (m_GameLoading) {
        SendChat(user, "Please wait for " + to_string(laggingPlayers.size()) + " player(s) to load the game.");
      }
    }
  }

  m_LastLagScreenResetTime = m_Aura->GetLoopTime();
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

void CGame::SetOwner(const string& name, const string& realm)
{
  m_OwnerName = name;
  m_OwnerRealm = realm;
  m_LastOwnerAssignedTicks = m_Aura->GetLoopTicks();

  UncacheOwner();

  GameUser::CGameUser* user = GetUserFromName(name, false);
  if (user && user->GetRealmHostName() == realm) {
    user->SetOwner(true);

    // Owner is never kicked for latency reasons.
    user->RemoveKickReason(GameUser::KickReason::HIGH_PING);
    user->CheckStillKicked();
  }
}

void CGame::ReleaseOwner()
{
  if (m_Exiting) {
    return;
  }
  LOG_APP_IF(LogLevel::kInfo, "Owner \"" + m_OwnerName + "@" + ToFormattedRealm(m_OwnerRealm) + "\" removed.")
  m_LastOwner = m_OwnerName;
  m_OwnerName.clear();
  m_OwnerRealm.clear();
  UncacheOwner();
  ResetLayout(false);
  m_Locked = false;
  SendAllChat("This game is now ownerless. Type " + GetCmdToken() + "owner to take ownership of this game.");
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
  uint8_t SID = static_cast<uint8_t>(m_Slots.size());
  while (SID--) {
    CGameSlot* slot = GetSlot(SID);
    if (slot->GetTeam() == m_Map->GetVersionMaxSlots()) continue;
    if (!slot->GetIsPlayerOrFake()) continue;
    if (!alsoCaptains) {
      GameUser::CGameUser* user = GetUserFromSID(SID);
      if (user && user->GetIsDraftCaptain()) continue;
    }
    if (!SetSlotTeam(SID, m_Map->GetVersionMaxSlots(), false)) {
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

      Log("votekick against user [" + m_KickVotePlayer + "] passed with " + to_string(Votes) + "/" + to_string(GetNumJoinedPlayers()) + " votes");
      SendAllChat("A votekick against user [" + m_KickVotePlayer + "] has passed");
    } else {
      LOG_APP_IF(LogLevel::kError, "votekick against user [" + m_KickVotePlayer + "] errored")
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

  if (m_HCLCommandString.size() > GetSlotsOccupied()) {
    return false;
  }

  bool enoughTeams = false;
  uint8_t sameTeam = m_Map->GetVersionMaxSlots();
  for (const auto& slot : m_Slots) {
    if (slot.GetIsPlayerOrFake() && slot.GetDownloadStatus() != 100) {
      GameUser::CGameUser* Player = GetUserFromUID(slot.GetUID());
      if (Player) {
        return false;
      }
    }
    if (slot.GetTeam() != m_Map->GetVersionMaxSlots()) {
      if (sameTeam == m_Map->GetVersionMaxSlots()) {
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
      SendAllChat("Currently hosting: " + recentLobby->GetStatusDescription());
    }
    return;
  }

  if (m_Aura->m_StartedGames.size() >= m_Aura->m_Config.m_MaxStartedGames) {
    SendAllChat("This game cannot be started while there are " +  to_string(m_Aura->m_Config.m_MaxStartedGames) + " additional games in progress.");
    return;
  }

  if (m_Map->GetHMCEnabled()) {
    const uint8_t SID = m_Map->GetHMCSlot();
    const CGameSlot* slot = InspectSlot(SID);
    if (!slot || !slot->GetIsPlayerOrFake() || GetUserFromSID(SID)) {
      SendAllChat("This game requires a fake player on slot " + ToDecString(SID + 1));
      return;
    }
    const CGameVirtualUser* virtualUserMatch = InspectVirtualUserFromSID(SID);
    if (virtualUserMatch && virtualUserMatch->GetIsObserver()) {
      SendAllChat("This game requires a fake player (not observer) on slot " + ToDecString(SID + 1));
      return;
    }
    if (!virtualUserMatch && m_Map->GetHMCRequired()) {
      SendAllChat("This game requires a fake player on slot " + ToDecString(SID + 1));
      return;
    }
  }

  // if the user sent "!start force" skip the checks and start the countdown
  // otherwise check that the game is ready to start

  uint8_t sameTeam = m_Map->GetVersionMaxSlots();

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
    if (m_HCLCommandString.size() > GetSlotsOccupied()) {
      SendAllChat("The HCL command string is too long. Use [" + GetCmdToken() + "go force] to start anyway");
      ChecksPassed = false;
    }

    UserList downloadingUsers;

    // check if everyone has the map
    for (const auto& slot : m_Slots) {
      if (slot.GetIsPlayerOrFake() && slot.GetDownloadStatus() != 100) {
        GameUser::CGameUser* player = GetUserFromUID(slot.GetUID());
        if (player) downloadingUsers.push_back(player);
      }
      if (slot.GetTeam() != m_Map->GetVersionMaxSlots()) {
        if (sameTeam == m_Map->GetVersionMaxSlots()) {
          sameTeam = slot.GetTeam();
        } else if (sameTeam != slot.GetTeam()) {
          enoughTeams = true;
        }
      }
    }
    if (!downloadingUsers.empty()) {
      SendAllChat("Players still downloading the map: " + ToNameListSentence(downloadingUsers));
      ChecksPassed = false;
    } else if (0 == m_ControllersWithMap) {
      SendAllChat("Nobody has downloaded the map yet.");
      ChecksPassed = false;
    } else if (m_ControllersWithMap < 2 && !m_RestoredGame) {
      SendAllChat("Only " + to_string(m_ControllersWithMap) + " user has the map.");
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
      SendAllChat("Players with high ping: " + ToNameListSentence(highPingUsers));
      ChecksPassed = false;
    }
    if (!pingNotMeasuredUsers.empty()) {
      SendAllChat("Players NOT yet pinged thrice: " + ToNameListSentence(pingNotMeasuredUsers));
      ChecksPassed = false;
    }
    if (!unverifiedUsers.empty()) {
      SendAllChat("Players NOT verified (whisper sc): " + ToNameListSentence(unverifiedUsers));
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

  if (GetNumJoinedUsersOrFake() == 1 && (0 == GetSlotsOpen() || m_Map->GetGameObservers() != GameObserversMode::kReferees)) {
    SendAllChat("HINT: Single-user game detected. In-game commands will be DISABLED.");
    if (GetNumOccupiedSlots() != m_Map->GetVersionMaxSlots()) {
      SendAllChat("HINT: To avoid this, you may enable map referees, or add a fake user [" + GetCmdToken() + "fp]");
    }
  }

  if (m_FakeUsers.size() == 1) {
    SendAllChat("HINT: " + to_string(m_FakeUsers.size()) + " slot is occupied by a fake user.");
  } else if (!m_FakeUsers.empty()) {
    SendAllChat("HINT: " + to_string(m_FakeUsers.size()) + " slots are occupied by fake users.");
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
  return "auto_p" + ToDecString(GetSIDFromUID(UID) + 1) + "_" + oss.str() + ".w3z";
}

bool CGame::Save(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect)
{
  const uint8_t UID = SimulateActionUID(ACTION_SAVE, user, isDisconnect, ACTION_SOURCE_ANY &~ ACTION_SOURCE_OBSERVER);
  if (UID == 0xFF) return false;

  string fileName = GetSaveFileName(UID);
  LOG_APP_IF(LogLevel::kInfo, "saving as " + fileName)

  {
    const uint32_t success = 1;
    vector<uint8_t> ActionStart, ActionEnd;
    ActionStart.push_back(ACTION_SAVE);
    AppendByteArrayString(ActionStart, fileName, true);
    ActionEnd.push_back(ACTION_SAVE_ENDED);
    AppendByteArray(ActionEnd, success, false);
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
    AppendByteArray(Action, success, false);
    actionFrame.AddAction(std::move(CIncomingAction(fakeUser.GetUID(), Action)));
  }
}

bool CGame::Pause(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect)
{
  const uint8_t UID = SimulateActionUID(ACTION_PAUSE, user, isDisconnect, ACTION_SOURCE_ANY &~ ACTION_SOURCE_OBSERVER);
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
  const uint8_t UID = SimulateActionUID(ACTION_RESUME, user, isDisconnect, ACTION_SOURCE_ANY &~ ACTION_SOURCE_OBSERVER);
  if (UID == 0xFF) return false;

  actionFrame.AddAction(std::move(CIncomingAction(UID, ACTION_RESUME)));
  actionFrame.callback = ON_SEND_ACTIONS_RESUME;
  return true;
}

bool CGame::SendMiniMapSignal(GameUser::CGameUser* user, CQueuedActionsFrame& actionFrame, const bool isDisconnect, const float x, const float y, const uint32_t duration)
{
  const uint8_t UID = SimulateActionUID(ACTION_MINIMAPSIGNAL, user, isDisconnect, ACTION_SOURCE_ANY);
  if (UID == 0xFF) return false;

  {
    vector<uint8_t> Action;
    Action.push_back(ACTION_MINIMAPSIGNAL);
    AppendByteArray(Action, x, false);
    AppendByteArray(Action, y, false);
    AppendByteArray(Action, duration, false);
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
  AppendByteArray(Action, gold, false);
  AppendByteArray(Action, lumber, false);
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
  AppendByteArray(Action, ALLIANCE_SETTINGS_ALLY | ALLIANCE_SETTINGS_SHARED_VISION | ALLIANCE_SETTINGS_SHARED_CONTROL | ALLIANCE_SETTINGS_SHARED_VICTORY, false);
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
  AppendByteArray(action, firstValue, false);
  AppendByteArray(action, secondValue, false);
  AppendByteArrayString(action, message, true);
  GetLastActionFrame().AddAction(std::move(CIncomingAction(UID, action)));
  return true;
}

bool CGame::SendChatTriggerBytes(const uint8_t UID, const string& message, const array<uint8_t, 8>& triggerBytes)
{
  vector<uint8_t> action = {ACTION_CHAT_TRIGGER};
  AppendByteArrayFast(action, triggerBytes);
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

  uint8_t SID = static_cast<uint8_t>(m_Slots.size());
  while (SID--) {
    if (SID == fromSID || !m_Slots[SID].GetIsPlayerOrFake()) continue;
    if (m_Slots[SID].GetTeam() != m_Slots[SID].GetTeam()) continue;
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
    SendAllChat("Game saved on " + user->GetName() + "'s disconnection.");
    SendAllChat("They may rejoin on reload if an ally sends them their save. Foes' save files will NOT work.");
    return true;
  } else {
    LOG_APP_IF(LogLevel::kWarning, "Failed to automatically save game on leave")
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
  if (m_Slots.size() >= enabledCount) return;
  LOG_APP_IF(LogLevel::kDebug, "adding " + to_string(enabledCount - m_Slots.size()) + " observer slots")
  while (m_Slots.size() < enabledCount) {
    m_Slots.emplace_back(GetIsCustomForces() ? SLOTTYPE_NONE : SLOTTYPE_USER, UID_ZERO, SLOTPROG_RST, SLOTSTATUS_OPEN, SLOTCOMP_NO, m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots(), SLOTRACE_RANDOM);
  }
}

void CGame::CloseObserverSlots()
{
  uint8_t count = 0;
  uint8_t i = static_cast<uint8_t>(m_Slots.size());
  while (i--) {
    if (m_Slots[i].GetTeam() == m_Map->GetVersionMaxSlots()) {
      m_Slots.erase(m_Slots.begin() + i);
      ++count;
    }
  }
  if (count > 0 && m_Aura->MatchLogLevel(LogLevel::kDebug)) {
    LogApp("deleted " + to_string(count) + " observer slots", LOG_C);
  }
}

// Virtual host is needed to generate network traffic when only one user is in the game or lobby.
// Fake users may also accomplish the same purpose.
bool CGame::CreateVirtualHost()
{
  if (m_VirtualHostUID != 0xFF)
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
    const std::array<uint8_t, 4> IP = {0, 0, 0, 0};
    SendAll(GameProtocol::SEND_W3GS_PLAYERINFO(GetVersion(), m_VirtualHostUID, GetLobbyVirtualHostName(), IP, IP));
  }
  return true;
}

bool CGame::DeleteVirtualHost()
{
  if (m_VirtualHostUID == 0xFF) {
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
    const std::array<uint8_t, 4> IP = {0, 0, 0, 0};
    SendAll(GameProtocol::SEND_W3GS_PLAYERINFO(GetVersion(), UID, name, IP, IP));
  }
  m_Slots[SID] = CGameSlot(
    m_Slots[SID].GetType(),
    UID,
    SLOTPROG_RDY,
    SLOTSTATUS_OCCUPIED,
    SLOTCOMP_NO,
    isCustomForces ? m_Slots[SID].GetTeam() : m_Map->GetVersionMaxSlots(),
    isCustomForces ? m_Slots[SID].GetColor() : m_Map->GetVersionMaxSlots(),
    m_Map->GetLobbyRace(&m_Slots[SID])
  );
  if (!isCustomForces && !asObserver) SetSlotTeamAndColorAuto(SID);

  m_FakeUsers.emplace_back(shared_from_this(), SID, UID, name).SetIsObserver(m_Slots[SID].GetTeam() == m_Map->GetVersionMaxSlots());
  m_SlotInfoChanged |= SLOTS_ALIGNMENT_CHANGED;
  m_GameDiscoveryInfoChanged |= GAME_DISCOVERY_CHANGED_SLOTS;
  return &m_FakeUsers.back();
}

bool CGame::CreateFakeUser(const optional<string> playerName)
{
  // Fake users need not be explicitly restricted in any layout, so let's just use an empty slot.
  uint8_t SID = GetEmptySID(false);
  if (SID >= static_cast<uint8_t>(m_Slots.size())) return false;
  if (!CanLockSlotForJoins(SID)) return false;

  if (GetSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), playerName.value_or("User[" + ToDecString(SID + 1) + "]"), false);
  return true;
}

bool CGame::CreateFakePlayer(const optional<string> playerName)
{
  const bool isCustomForces = GetIsCustomForces();
  uint8_t SID = isCustomForces ? GetEmptyPlayerSID() : GetEmptySID(false);
  if (SID >= static_cast<uint8_t>(m_Slots.size())) return false;

  if (isCustomForces && (m_Slots[SID].GetTeam() == m_Map->GetVersionMaxSlots())) {
    return false;
  }
  if (!CanLockSlotForJoins(SID)) {
    return false;
  }
  if (GetSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), playerName.value_or("User[" + ToDecString(SID + 1) + "]"), false);
  return true;
}

bool CGame::CreateFakeObserver(const optional<string> playerName)
{
  if (!(m_Map->GetGameObservers() == GameObserversMode::kStartOrOnDefeat || m_Map->GetGameObservers() == GameObserversMode::kReferees)) {
    return false;
  }

  const bool isCustomForces = GetIsCustomForces();
  uint8_t SID = isCustomForces ? GetEmptyObserverSID() : GetEmptySID(false);
  if (SID >= static_cast<uint8_t>(m_Slots.size())) return false;

  if (isCustomForces && (m_Slots[SID].GetTeam() != m_Map->GetVersionMaxSlots())) {
    return false;
  }
  if (!CanLockSlotForJoins(SID)) {
    return false;
  }
  if (GetSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), playerName.value_or("User[" + ToDecString(SID + 1) + "]"), true);
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
        m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, slot->GetTeam(), slot->GetColor(), /* only important if MAPOPT_FIXEDPLAYERSETTINGS */ m_Map->GetLobbyRace(slot));
      } else {
        m_Slots[SID] = CGameSlot(slot->GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots(), SLOTRACE_RANDOM);
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
    for (uint8_t SID = 0; SID < m_Slots.size(); ++SID) {
      const CGameSlot* savedSlot = m_RestoredGame->InspectSlot(SID);
      if (!savedSlot || !savedSlot->GetIsPlayerOrFake()) {
        continue;
      }
      if (++reservedIndex >= reservedCount) break;
      if (m_Slots[SID].GetSlotStatus() == SLOTSTATUS_OPEN) {
        CreateFakeUserInner(SID, savedSlot->GetUID(), m_Reserved[reservedIndex], false);
        ++addedCounter;
      }
    }
  } else {
    uint8_t remainingControllers = m_Map->GetMapNumControllers() - GetNumControllers();
    if (!hasUsers && m_Slots.size() == m_Map->GetMapNumControllers()) {
      --remainingControllers;
    }
    for (uint8_t SID = 0; SID < m_Slots.size(); ++SID) {
      if (m_Slots[SID].GetSlotStatus() != SLOTSTATUS_OPEN) {
        continue;
      }
      CreateFakeUserInner(SID, GetNewUID(), "User[" + ToDecString(SID + 1) + "]", false);
      ++addedCounter;
      if (0 == --remainingControllers) {
        break;
      }
    }
  }
  if (GetSlotsOpen() == 0 && GetNumJoinedUsersOrFake() > 1) DeleteVirtualHost();
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
      m_Slots[SID] = CGameSlot(m_Slots[SID].GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, m_Slots[SID].GetTeam(), m_Slots[SID].GetColor(), /* only important if MAPOPT_FIXEDPLAYERSETTINGS */ m_Map->GetLobbyRace(&(m_Slots[SID])));
    } else {
      m_Slots[SID] = CGameSlot(m_Slots[SID].GetType(), 0, SLOTPROG_RST, isSystemReservedSlot ? SLOTSTATUS_CLOSED : SLOTSTATUS_OPEN, SLOTCOMP_NO, m_Map->GetVersionMaxSlots(), m_Map->GetVersionMaxSlots(), SLOTRACE_RANDOM);
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
  if (m_GameHistory->GetSoftDesynchronized()) return false;
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

  vector<pair<int64_t, string>> texts;
  texts.reserve(1);
  texts.emplace_back(HashCode("COUNT"), string(static_cast<string::size_type>(1u), counter));
  const FlatMap<int64_t, string> textCache(move(texts));
  const FlatMap<int64_t, function<string()>> textFuncMap;

  return ReplaceTemplate(counterTemplate, nullptr, &textCache, nullptr, &textFuncMap);
}

string CGame::GetCreationCounterText(shared_ptr<const CRealm> realm) const
{
  if (m_CreationCounter == 0) return string();

  // Base-36 suffix 0123456789abcdefghijklmnopqrstuvwxyz
  char counter;
  if (m_CreationCounter < 10) {
    counter = static_cast<uint8_t>(48u + m_CreationCounter);
  } else {
    counter = static_cast<uint8_t>(87u + m_CreationCounter);
  }

  return GetCustomCreationCounterText(realm, counter);
}

string CGame::GetNextCreationCounterText(shared_ptr<const CRealm> realm) const
{
  uint16_t creationCounter = (m_CreationCounter + 1) % 36;
  ++creationCounter;

  // Base-36 suffix 0123456789abcdefghijklmnopqrstuvwxyz
  char counter;
  if (creationCounter < 10) {
    counter = static_cast<uint8_t>(48u + creationCounter);
  } else {
    counter = static_cast<uint8_t>(87u + creationCounter);
  }

  return GetCustomCreationCounterText(realm, counter);
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

int64_t CGame::GetLastActionLateBy(int64_t oldLatency) const
{
  if (m_LastActionSentTicks == 0) return 0;
  const int64_t actualSendInterval = GetTicks() - m_LastActionSentTicks;
  const int64_t expectedSendInterval = oldLatency - m_LastActionLateBy;
  return actualSendInterval - expectedSendInterval;
}

uint32_t CGame::GetSyncLimit(bool isObserver) const
{
  return isObserver ? m_LagStartMinObserversFrames : m_LagStartMinPlayersFrames;
}

uint32_t CGame::GetSyncLimitSafe(bool isObserver) const
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
      LOG_APP_IF(LogLevel::kDebug, "MMD failed to provide valid game results")
    } else {
      LOG_APP_IF(LogLevel::kDebug, "Players failed to provide valid game results")
    }
    return GameResultSource::kNone;
  }
  m_GameResultsSource = resultsSource;
  m_GameResults.swap(gameResults);
  m_GameResults->Confirm();
  if (resultsSource == GameResultSource::kMMD) {
    LOG_APP_IF(LogLevel::kDebug, "Resolved winners (MMD): " + JoinStrings(m_GameResults->GetWinnersNames(), false))
  } else {
    LOG_APP_IF(LogLevel::kDebug, "Resolved winners: " + JoinStrings(m_GameResults->GetWinnersNames(), false))
  }
  return m_GameResultsSource;
}

GameResultSource CGame::RunGameResults()
{
  if (m_GameResultsSource != GameResultSource::kNone) return m_GameResultsSource;
  
  FlushStatsQueue();

  const GameResultSourceSelect sourceOfTruth = GetGameResultSourceOfTruth();

  if (sourceOfTruth == GameResultSourceSelect::kOnlyMMD) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method only-mmd")
    optional<GameResults> results = GetGameResultsMMD();
    return TryConfirmResults(results, GameResultSource::kMMD);
  }

  if (sourceOfTruth == GameResultSourceSelect::kOnlyLeaveCode) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method only-exit")
    optional<GameResults> results = GetGameResultsLeaveCode();
    return TryConfirmResults(results, GameResultSource::kLeaveCode);
  }

  if (sourceOfTruth == GameResultSourceSelect::kPreferMMD) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method prefer-mmd")
    optional<GameResults> results = GetGameResultsMMD();
    if (TryConfirmResults(results, GameResultSource::kMMD) != GameResultSource::kNone) {
      return m_GameResultsSource;
    }
    results = GetGameResultsLeaveCode();
    return TryConfirmResults(results, GameResultSource::kLeaveCode);
  }

  if (sourceOfTruth == GameResultSourceSelect::kPreferLeaveCode) {
    LOG_APP_IF(LogLevel::kDebug, "Resolving game results with method prefer-exit")
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

  if (m_HCLCommandString.size() > GetSlotsOccupied()) {
    LOG_APP_IF(LogLevel::kInfo, "failed to encode game mode as HCL string [" + m_HCLCommandString + "] because there aren't enough occupied slots")
    return;
  }

  const bool encodeVirtualPlayers = m_Map->GetHCLAboutVirtualPlayers();
  string HCLChars = encodeVirtualPlayers ? HCL_CHARSET_SMALL : HCL_CHARSET_STANDARD;

  if (m_HCLCommandString.find_first_not_of(HCLChars) != string::npos) {
    LOG_APP_IF(LogLevel::kError, "failed to encode game mode as HCL string [" + m_HCLCommandString + "] because it contains invalid characters")
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
    while (m_Slots[currentSlot].GetSlotStatus() != SLOTSTATUS_OCCUPIED)
      ++currentSlot;

    bool isVirtualPlayer = m_Slots[currentSlot].GetIsPlayerOrFake() && !GetIsRealPlayerSlot(currentSlot);
    uint8_t handicapIndex = (m_Slots[currentSlot].GetHandicap() - 50) / 10;
    uint8_t charIndex = static_cast<uint8_t>(HCLChars.find(character));
    uint8_t slotInfo = handicapIndex;
    if (encodeVirtualPlayers && isVirtualPlayer) {
      slotInfo += 6;
    }
    slotInfo += charIndex * (encodeVirtualPlayers ? 12 : 6);
    // max() = 7+5+40*6 = 252 | 7+11+19*12 = 246
    m_Slots[currentSlot++].SetHandicap(encodingMap[slotInfo]);
  }

  // See documentation for the decoding algorithm
  // https://gist.github.com/Slayer95/a15fc75f38d0b3fdf356613ede96cf7f
  //
  // Variant encodeVirtualPlayers: K = 12
  // value = encodedHandicap % K
  // virtual = encodedHandicap - (value * K) > 6
  // handicap = encodedHandicap - (value * K) - (virtual ? 6 : 0)

  m_SlotInfoChanged |= SLOTS_HCL_INJECTED;
  LOG_APP_IF(LogLevel::kDebug, "using game mode [" + m_HCLCommandString + "]")
  DLOG_APP_IF(LogLevel::kTrace, "mode [" + m_HCLCommandString + "] encoded as handicaps <" + ByteArrayToDecString(GetHandicaps()) + ">")
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

  if (GetSlotsOpen() == 1)
    DeleteVirtualHost();

  CreateFakeUserInner(SID, GetNewUID(), m_Map->GetHMCPlayerName(), false);
  return true;
}

uint8_t CGame::GetHMCSID() const
{
  if (!m_Map->GetHMCEnabled()) return 0xFF;
  const uint8_t slot = m_Map->GetHMCSlot();
  if (slot >= static_cast<uint8_t>(m_Slots.size())) return 0xFF;
  return slot;
}

uint8_t CGame::GetAHCLSID() const
{
  if (!m_Map->GetAHCLEnabled()) return 0xFF;
  const uint8_t slot = m_Map->GetAHCLSlot();
  if (slot >= static_cast<uint8_t>(m_Slots.size())) return 0xFF;
  return slot;
}
