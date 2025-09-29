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

#include "game_user.h"

#include <utility>

#include "config/config_bot.h"
#include "aura.h"
#include "command.h"
#include "realm.h"
#include "map.h"
#include "protocol/game_protocol.h"
#include "protocol/gps_protocol.h"
#include "protocol/vlan_protocol.h"
#include "game.h"
#include "socket.h"
#include "net.h"
#include "proxy/gproxy_server.h"

using namespace std;
using namespace GameUser;

#define LOG_APP_IF(T, U) \
  do {\
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_IF for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      m_Game.get().LogApp(U, LOG_C); \
    }\
  } while (0)

#define LOG_APP_CUSTOM(T, U, V) \
  do {\
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_CUSTOM for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      m_Game.get().LogApp(U, V); \
    }\
  } while (0)

#ifdef DEBUG
#define DLOG_APP_IF(T, U) \
  do {\
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_IF for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      m_Game.get().LogApp(U, LOG_C); \
    }\
  } while (0)

#define DLOG_APP_CUSTOM(T, U, V) \
  do {\
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_CUSTOM for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
      m_Game.get().LogApp(U, V); \
    }\
  } while (0)
#else
#define DLOG_APP_IF(T, U) do {} while (0)
#define DLOG_APP_CUSTOM(T, U, V) do {} while (0)
#endif

constexpr int USER_METRICS_ACTION_SAMPLE_RATE = 2;
constexpr int USER_METRICS_CHAT_SAMPLE_RATE = 1;
constexpr int USER_METRICS_KEEPALIVE_SAMPLE_RATE = 10;
constexpr size_t USER_METRICS_ACTION_CAPACITY = 100;
constexpr size_t USER_METRICS_CHAT_CAPACITY = 100;
constexpr size_t USER_METRICS_KEEPALIVE_CAPACITY = 100;

//
// CGameUser
//

CGameUser::CGameUser(shared_ptr<CGame> nGame, CConnection* connection, uint8_t nUID, const bool gameVersionIsExact, const Version& gameVersion, uint32_t nJoinedRealmInternalId, string nJoinedRealm, string_view nName, std::array<uint8_t, 4> nInternalIP, bool nReserved)
  : CConnection(*connection),
    m_Game(ref(*nGame)),
    m_IPv4Internal(std::move(nInternalIP)),
    m_RealmInternalId(nJoinedRealmInternalId),
    m_RealmHostName(std::move(nJoinedRealm)),
    m_Name(nName),
    m_LeftCode(PLAYERLEAVE_LOBBY),
    m_Status(USERSTATUS_LOBBY),
    m_IsLeaver(false),
    m_PingEqualizerOffset(0),
    m_PingEqualizerFrameNode(nullptr),
    m_OnHoldActionsCount(0),
    m_PongCounter(0),
    m_SyncCounterOffset(0),
    m_SyncCounter(0),
    m_JoinTicks(nGame->m_Aura->GetClockTicks()),
    m_FinishedLoadingTicks(0),
    m_HandicapTicks(0),
    m_StartedLaggingTicks(0),
    m_SID(0xFF),
    m_UID(nUID),
    m_OldUID(0xFF),
    m_PseudonymUID(0xFF),
    m_ChatChannel(0),
    m_GameVersionIsExact(gameVersionIsExact),
    m_GameVersion(gameVersion),
    m_Verified(false),
    m_Owner(false),
    m_Reserved(nReserved),
    m_Observer(false),
    m_PowerObserver(false),
    m_WhoisShouldBeSent(false),
    m_WhoisSent(false),
    m_MapChecked(false),
    m_MapReady(false),
    m_InGameReady(false),
    m_Ready(false),
    m_KickReason(KickReason::kNone),
    m_HasHighPing(false),
    m_DownloadAllowed(false),
    m_FinishedLoading(false),
    m_Lagging(false),
    m_DropVote(false),
    m_KickVote(false),
    m_Muted(false),
    m_ActionLocked(false),
    m_LeftMessageSent(false),
    m_StatusMessageSent(false),
    m_LatencySent(false),
    m_CheckStatusByTicks(nGame->m_Aura->GetClockTicks() + CHECK_STATUS_LATENCY),
    m_MuteEndTicks(0),

    m_Disconnected(false),
    m_DisconnectNoticeSent(false),
    m_TotalDisconnectTicks(0),

    m_TeamCaptain(0),
    m_ActionCounter(0),
    m_AntiAbuseCounter(0),
    m_RemainingSaves(GAME_SAVES_PER_PLAYER),
#ifndef PROFILING
    m_RemainingPauses(GAME_PAUSES_PER_PLAYER),
    m_OnLoadChatMessages(50, 50)
#else
    m_RemainingPauses(GAME_PAUSES_PER_PLAYER),
    m_OnLoadChatMessages(50, 50),
    m_PerfMetrics(GameUser::UserMetrics(
      USER_METRICS_ACTION_SAMPLE_RATE, USER_METRICS_ACTION_CAPACITY,
      USER_METRICS_CHAT_SAMPLE_RATE, USER_METRICS_CHAT_CAPACITY,
      USER_METRICS_KEEPALIVE_SAMPLE_RATE, USER_METRICS_KEEPALIVE_CAPACITY
    ))
#endif
{
  m_GProxy = make_shared<CGProxyServer>(this);
  m_GProxy->AddRecvPacket(); // REQJOIN at (connection.cpp, game_seeker.cpp) is not passthrough

  m_RecentActionCounter.fill(0);
  m_RTTValues.reserve(MAXIMUM_PINGS_COUNT);
  m_Socket->SetLogErrors(true);
  m_Type = IncomingConnectionType::kPlayer;
  AcquireGameName();
}

CGameUser::~CGameUser()
{
  if (m_Socket) {
    if (!m_LeftMessageSent) {
      Send(GameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(GetUID(), m_Game.get().GetIsLobbyStrict() ? PLAYERLEAVE_LOBBY : GetLeftCode()));
    }
    m_Socket->Flush();
    UnrefConnection();
  }

  for (const auto& ptr : m_Game.get().m_Aura->m_ActiveContexts) {
    shared_ptr<CCommandContext> ctx = ptr.lock();
    if (ctx && ctx->GetGameSource().GetIsUser() && ctx->GetGameSource().GetUser() == this) {
      ctx->SetPartiallyDestroyed();
      ctx->GetGameSource().Reset();
    }
  }
}

optional<uint32_t> CGameUser::GetOperationalRTT() const
{
  if (m_MeasuredRTT.has_value()) {
    return {m_MeasuredRTT.value().second};
  }

  // weighted average of stored pings (max 6 stored = 25-30 seconds)
  // 4:3:2:1:1:1 (more recent = more weight)
  //
  // note that this vector may have the bias of LC-style pings incorporated
  // this means that the output "operational RTT" may sometimes be half the actual RTT.

  uint32_t weightedSum = 0;
  uint8_t backDelta = 0;
  size_t i = m_RTTValues.size();
  uint32_t totalWeight = 0;
  while (i--) {
    const uint32_t weight = (backDelta >= MAX_PING_WEIGHT ? 1 : MAX_PING_WEIGHT - backDelta);
    weightedSum += m_RTTValues[i] * weight;
    totalWeight += weight;
    backDelta++;
  }

  if (totalWeight == 0) {
    return nullopt;
  }

  return {weightedSum / totalWeight};
}

optional<uint32_t> CGameUser::GetDisplayRTT() const
{
  return GetOperationalRTT();
}

optional<uint32_t> CGameUser::GetRTT() const
{
  optional<uint32_t> maybeRTT = GetOperationalRTT();
  if (!maybeRTT.has_value()) return nullopt;
  if (m_Game.get().m_Aura->m_Net.m_Config.m_LiteralRTT) {
    return maybeRTT;
  }
  maybeRTT = maybeRTT.value() * 2;
  return maybeRTT;
}

bool CGameUser::GetIsDownloading() const
{
  return InspectMapTransfer().GetIsInProgress();
}

bool CGameUser::GetIsRTTMeasuredConsistent() const
{
  if (GetIsDownloading()) return false;
  return m_MeasuredRTT.has_value() || GetStoredRTTCount() >= CONSISTENT_PINGS_COUNT;
}

bool CGameUser::GetIsRTTMeasuredBadConsistent() const
{
  if (GetIsDownloading()) return false;
  return m_MeasuredRTT.has_value() || GetStoredRTTCount() >= 2;
}

bool CGameUser::GetCanReconnect() const
{
  return m_GProxy->GetIsEnabled();
}

string CGameUser::GetConnectionErrorString() const
{
  string errorString;
  if (m_Socket) {
    errorString = m_Socket->GetErrorString();
  }
  if (errorString.empty()) {
    errorString = "EUNKNOWN";
  }
  return errorString;
}

// based on my limited testing it seems that the extra flags' first byte contains 3 plus the recipient's color to denote a private message
uint8_t CGameUser::GetChatChannel(bool forcePrivate) const
{
  if (!m_FinishedLoading) return 0;
  return !m_Observer && !forcePrivate ? CHAT_RECV_OBS : m_ChatChannel;
}

string CGameUser::GetGameVersionString() const
{
  if (m_GameVersionIsExact) {
    return Concat("v", ToVersionString(GetGameVersion()));
  } else {
    return Concat("v", ToVersionString(GetGameVersion()), "?");
  }
}

string CGameUser::GetLowerName() const
{
  return ToLowerCase(m_Name);
}

string CGameUser::GetDisplayName() const
{
  if (m_Game.get().GetIsHiddenPlayerNames() && !(m_Observer && m_Game.get().GetGameLoaded())) {
    if (m_PseudonymUID == 0xFF) {
      return Concat("Player ", ToDecString(m_UID));
    } else {
      // After CGame::RunPlayerObfuscation()
      return Concat("Player ", ToDecString(m_PseudonymUID), "?");
    }
  }
  return m_Name;
}

shared_ptr<CGame> CGameUser::GetGame()
{
  return m_Game.get().GetCheckedShared();
}

shared_ptr<CGProxyServer> CGameUser::GetGProxy() const
{
  return m_GProxy;
}

uint32_t CGameUser::GetPingEqualizerDelay() const
{
  if (!m_Game.get().GetGameLoaded()) return 0u;
  return integer_cast_lossy<uint32_t>(GetPingEqualizerOffset()) * signed_cast_lossy<uint32_t>(m_Game.get().GetActiveLatency());
}

CQueuedActionsFrame& CGameUser::GetPingEqualizerFrame()
{
  return GetPingEqualizerFrameNode()->data;
}

void CGameUser::AdvanceActiveGameFrame()
{
  m_PingEqualizerFrameNode = m_PingEqualizerFrameNode->next;
}

bool CGameUser::AddDelayPingEqualizerFrame()
{
  if (m_PingEqualizerFrameNode->next == m_Game.get().GetFirstActionFrameNode()) {
    return false;
  }
  m_PingEqualizerFrameNode = m_PingEqualizerFrameNode->next;
  ++m_PingEqualizerOffset;
  return true;
}

bool CGameUser::SubDelayPingEqualizerFrame()
{
  if (m_PingEqualizerFrameNode == m_Game.get().GetFirstActionFrameNode()) {
    return false;
  }
  m_PingEqualizerFrameNode = m_PingEqualizerFrameNode->prev;
  --m_PingEqualizerOffset;
  return true;
}

void CGameUser::ReleaseOnHoldActionsCount(size_t count)
{
  size_t doneCount = GetPingEqualizerFrame().AddQueuedActionsCount(GetOnHoldActions(), count);
  if (doneCount > 0 && GetHasAPMQuota()) {
    if (!GetAPMQuota().TryConsume(static_cast<double>(doneCount))) {
      Print(Concat(m_Game.get().GetLogPrefix(), "[APMLimit] Malfunction detected - ", to_string(doneCount), " actions released, ", to_string(GetOnHoldActionsCount()), " remaining)"));
    }
  }
}

void CGameUser::ReleaseOnHoldActions()
{
  ReleaseOnHoldActionsCount(GetOnHoldActionsCount());
}

void CGameUser::UpdateAPMQuota()
{
  if (GetHasAPMQuota()) {
    // Note: Idempotent within the same game tick
    GetAPMQuota().Refill(m_Game.get().GetEffectiveTicks());
  }
}

bool CGameUser::GetShouldHoldActionInner()
{
  if (m_Game.get().GetEffectiveTicks() < GetHandicapTicks()) return true;
  if (m_Game.get().m_Config.m_ShareUnitsHandler == OnShareUnitsHandler::kRestrictSharee && GetHasControlOverAnyAlliedUnits()) return true;
  if (GetHasAPMQuota()) {
    UpdateAPMQuota();
    if (GetAPMQuota().GetCurrentCapacity() < 1.) return true;
  }
  return false;
}

bool CGameUser::GetShouldHoldAction(uint16_t count)
{
  if (GetOnHoldActionsAny()) return true;
  // allow MMD actions through
  if (count == 0) return false;
  return GetShouldHoldActionInner();
}

void CGameUser::CheckReleaseOnHoldActions()
{
  if (!GetOnHoldActionsAny()) return;
  if (GetShouldHoldActionInner()) return;
  size_t releasedCount = GetOnHoldActionsCount();
  if (GetHasAPMQuota()) {
    UpdateAPMQuota();
    releasedCount = min(releasedCount, static_cast<size_t>(floor(GetAPMQuota().GetCurrentCapacity())));
  }
  ReleaseOnHoldActionsCount(releasedCount);
}

void CGameUser::AddActionCounters()
{
  ++m_ActionCounter;
  ++m_RecentActionCounter[2];
}

void CGameUser::ShiftRecentActionCounters()
{
  m_RecentActionCounter[0] = m_RecentActionCounter[1];
  m_RecentActionCounter[1] = m_RecentActionCounter[2];
  m_RecentActionCounter[2] = 0;
}

bool CGameUser::CheckMuted()
{
  if (!GetIsMuted()) {
    return false;
  }
  if (m_Aura->GetTicksIsAfter(GetMuteEndTicks())) {
    UnMute();
    return false;
  }

  return true;
}

shared_ptr<CRealm> CGameUser::GetRealm(bool mustVerify) const
{
  if (m_RealmInternalId < 0x10)
    return nullptr;

  if (mustVerify && !m_Verified) {
    return nullptr;
  }

  return m_Game.get().m_Aura->GetRealmByInputId(m_Game.get().m_Aura->m_RealmsIdentifiers[m_RealmInternalId]);
}

string CGameUser::GetRealmDataBaseID(bool mustVerify) const
{
  shared_ptr<CRealm> Realm = GetRealm(mustVerify);
  if (Realm) return Realm->GetDataBaseID();
  return string();
}

bool CGameUser::GetIsBehindFramesNormal(const size_t frameLimit) const
{
  return m_Game.get().GetSyncCounter() > GetNormalSyncCounter() && m_Game.get().GetSyncCounter() - GetNormalSyncCounter() >= frameLimit;
}

bool CGameUser::CloseConnection(bool fromOpen)
{
  if (m_Disconnected) return false;
  if (!m_Game.get().GetGameLoaded() || !GetCanReconnect()) {
    TrySetEnding();
    DisableReconnect();
  }
  m_LastDisconnectTicks = m_Aura->GetClockTicks();
  m_Disconnected = true;
  m_Socket->Close();
  m_Game.get().EventUserAfterDisconnect(this, fromOpen);
  return true;
}

void CGameUser::UnrefConnection(bool deferred)
{
  m_Game.get().m_Aura->m_Net.OnUserKicked(this, deferred);

  if (!m_Disconnected) {
    m_LastDisconnectTicks = m_Aura->GetClockTicks();
    m_Disconnected = true;
  }
}

void CGameUser::ClearStalePings() {
  if (m_RTTValues.empty()) return;
  m_RTTValues[0] = m_RTTValues[m_RTTValues.size() - 1];
  m_RTTValues.erase(m_RTTValues.begin() + 1, m_RTTValues.end());
}

bool CGameUser::GetIsSyncCounterStartLagging() const
{
  return GetIsBehindFramesNormal(m_Game.get().GetSyncLimit(GetIsObserver()));
}

bool CGameUser::GetIsSyncCounterStopLag() const
{
  return !GetIsBehindFramesNormal(m_Game.get().GetSyncLimitSafe(GetIsObserver()));
}

void CGameUser::RefreshUID()
{
  m_OldUID = m_UID;
  m_UID = m_Game.get().GetNewUID();
}

bool CGameUser::Update(fd_set* fd, int64_t timeout)
{
  if (m_Disconnected) {
    if (m_GProxy->GetIsExtended() && GetTotalDisconnectTicks() > m_Game.get().m_Aura->m_Net.m_Config.m_ReconnectWaitTicks) {
      m_Game.get().EventUserKickGProxyExtendedTimeout(this);
    }
    return m_DeleteMe;
  }

  if (m_Socket->HasError()) {
    m_Game.get().EventUserDisconnectSocketError(this);
    return m_DeleteMe;
  }

  if (m_DeleteMe) {
    m_Socket->ClearRecvBuffer(); // in case there are pending bytes from a previous recv
    m_Socket->Discard(fd);
    return m_DeleteMe;
  }

  bool Abort = false;
  if (m_Socket->DoRecv(fd)) {
    CStreamIOSocket* socket = m_Socket;
    string_view data = socket->GetRecvBufferView();

    // extract as many packets as possible from the socket's receive buffer and process them
    // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

    while (data.size() >= 4)
    {
      // bytes 2 and 3 contain the length of the packet
      const uint16_t packetSize = ByteArrayToUInt16LE(data, 2);
      if (packetSize < 4) {
        m_Game.get().EventUserDisconnectGameProtocolError(this, true);
        Abort = true;
        break;
      }
      if (data.size() < packetSize) {
        // we don't have the complete packet yet
        break;
      }

      string_view packet = data.substr(0, packetSize);
      const uint8_t packetFamily = GetByteAt(packet, 0);
      const uint8_t packetType = GetByteAt(packet, 1);

      if (packetFamily == GameProtocol::Magic::W3GS_HEADER)
      {
        m_GProxy->AddRecvPacket();

        // byte 1 contains the packet ID

        switch (packetType)
        {
          case GameProtocol::Magic::LEAVEGAME: {
            if (ValidateLength(packet) && packet.size() >= 8) {
              const uint32_t reason = ByteArrayToUInt32LE(packet, 4);
              m_Game.get().EventUserLeft(this, reason);
              m_Socket->SetLogErrors(false);
            } else {
              m_Game.get().EventUserDisconnectGameProtocolError(this, false);
            }
            Abort = true;
            break;
          }

          case GameProtocol::Magic::GAMELOADED_SELF:
            if (GameProtocol::RECEIVE_W3GS_GAMELOADED_SELF(packet)) {
              if (m_Game.get().GetGameLoading() && !m_FinishedLoading) {
                m_FinishedLoading      = true;
                m_FinishedLoadingTicks = m_Aura->GetClockTicks();
                m_Game.get().EventUserLoaded(this);
              }
            }

            break;

          case GameProtocol::Magic::OUTGOING_ACTION: {
#ifdef PROFILING
            auto t = m_PerfMetrics.action.TryStart();
#endif
            if (ValidateLength(packet) && packet.size() >= 8) {
              CIncomingAction action = GameProtocol::RECEIVE_W3GS_OUTGOING_ACTION(packet, m_UID);
              if (!m_Game.get().EventUserIncomingAction(this, action)) {
                m_Game.get().EventUserDisconnectGameProtocolError(this, false);
                Abort = true;
              }
            }
#ifdef PROFILING
            int64_t dt = t->TryEndNano();
            if (dt > 1e6) {
              LOG_APP_CUSTOM(LogLevel::kWarning, Concat("Action <", GetStringBytesHex(packet), "> took " + to_string(dt / 1e6) + " ms"), LOG_C | LOG_P);
            }
#endif
            break;
          }

          case GameProtocol::Magic::OUTGOING_KEEPALIVE: {
#ifdef PROFILING
            auto t = m_PerfMetrics.keepAlive.TryStart();
#endif
            if (m_SyncCounter >= m_Game.get().GetSyncCounter()) {
              LOG_APP_CUSTOM(LogLevel::kWarning, Concat("player [", m_Name, "] incorrectly ahead of sync"), LOG_C | LOG_P);
              m_Game.get().EventUserDisconnectGameProtocolError(this, false);
              Abort = true;
            } else {
              m_CheckSums.push(GameProtocol::RECEIVE_W3GS_OUTGOING_KEEPALIVE(packet));
              ++m_SyncCounter;
              m_Game.get().EventUserKeepAlive(this);
            }
#ifdef PROFILING
            t->TryEndNano();
#endif
            break;
          }

          case GameProtocol::Magic::CHAT_TO_HOST: {
#ifdef PROFILING
            auto t = m_PerfMetrics.chatToHost.TryStart();
#endif
            CIncomingMessageOrSettingsView incomingChatMessage = GameProtocol::RECEIVE_W3GS_CHAT_TO_HOST(packet);

            if (incomingChatMessage.GetIsValid()) {
              m_Game.get().EventUserChatOrPlayerSettings(this, incomingChatMessage);
            } else {
              // empty chat, not UTF8 or contains control characters: ignore it
            }
#ifdef PROFILING
            int64_t dt = t->TryEndNano();
            if (dt > 1e6) {
              LOG_APP_CUSTOM(LogLevel::kWarning, Concat("Chat message ", SanitizeWrapUTF8(incomingChatMessage.GetText()), " took " + to_string(dt / 1e6) + " ms"), LOG_C | LOG_P);
            }
#endif
            break;
          }

          case GameProtocol::Magic::DROPREQ:
            if (m_Game.get().GetIsLagging() && !m_DropVote) {
              m_DropVote = true;
              m_Game.get().EventUserDropRequest(this);
            }

            break;

          case GameProtocol::Magic::MAPSIZE: {
            if (m_MapReady || m_Game.get().GetGameLoading() || m_Game.get().GetGameLoaded()) {
              // Protection against rogue clients
              break;
            }

            CIncomingMapFileSize incomingMapSize = GameProtocol::RECEIVE_W3GS_MAPSIZE(packet);
            if (incomingMapSize.GetIsValid()) {
              m_Game.get().EventUserMapSize(this, incomingMapSize);
            }
            break;
          }

          case GameProtocol::Magic::PONG_TO_HOST: {
            uint32_t Pong = GameProtocol::RECEIVE_W3GS_PONG_TO_HOST(packet);

            const bool bufferBloatForbidden = m_Game.get().m_Aura->m_Net.m_Config.m_HasBufferBloat && m_Game.get().IsDownloading();
            bool useSystemRTT = !m_Socket->GetIsLoopback() && m_Game.get().GetGameLoaded() && m_Game.get().m_Aura->m_Net.m_Config.m_UseSystemRTT;
            const bool useLiteralRTT = m_Game.get().m_Aura->m_Net.m_Config.m_LiteralRTT;

            // discard pong values when anyone else is downloading if we're configured to do so
            if (!bufferBloatForbidden) {
              if (useSystemRTT && (!m_MeasuredRTT.has_value() || m_Aura->GetTicksIsAfterDelay(m_MeasuredRTT->first, SYSTEM_RTT_POLLING_PERIOD))) {
                optional<uint32_t> rtt = m_Socket->GetRTT();
                if (rtt.has_value()) {
                  m_MeasuredRTT = make_pair(m_Aura->GetClockTicks(), useLiteralRTT ? rtt.value() : (rtt.value() / 2));
                  m_RTTValues.clear();
                } else {
                  useSystemRTT = false;
                }
              }

              if (!useSystemRTT && Pong != 1) {
                // we discard pong values of 1
                // the client sends one of these when connecting plus we return 1 on error to kill two birds with one stone
                // we also discard pong values when we're downloading because they're almost certainly inaccurate
                // this statement also gives the player a 8 second grace period after downloading the map to allow queued (i.e. delayed) ping packets to be ignored
                if (!m_MapTransfer.GetStarted() || (m_MapTransfer.GetFinished() && m_Aura->GetTicksIsAfterDelay(m_MapTransfer.GetFinishedTicks(), 8000))) {
                  if (m_RTTValues.size() == MAXIMUM_PINGS_COUNT) {
                    m_RTTValues.erase(begin(m_RTTValues));
                  }
                  m_RTTValues.push_back(useLiteralRTT ? (signed_cast_lossy<uint32_t>(m_Aura->GetClockTicks()) - Pong) : ((signed_cast_lossy<uint32_t>(m_Aura->GetClockTicks()) - Pong) / 2));
                }
              }

              if (useSystemRTT || Pong != 1) {
                m_Game.get().EventUserPongToHost(this);
              }

              if (!GetIsRTTMeasuredConsistent() && !GetIsDownloading()) {
                // Measure player's ping as fast as possible, by chaining new pings to pongs received.
                Send(GameProtocol::SEND_W3GS_PING_FROM_HOST(m_Aura->GetClockTicks()));
              }
            }

            ++m_PongCounter;
            break;
          }

          case GameProtocol::Magic::DESYNC: {
            LOG_APP_CUSTOM(LogLevel::kNotice, Concat("player [", m_Name, "] sent GameProtocol::Magic::DESYNC"), LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::GAME_OVER: {
            LOG_APP_CUSTOM(LogLevel::kNotice, Concat("player [", m_Name, "] sent GameProtocol::Magic::GAME_OVER"), LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::LEAVE_ACK: {
            LOG_APP_CUSTOM(LogLevel::kNotice, Concat("player [", m_Name, "] sent GameProtocol::Magic::LEAVE_ACK"), LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::CLIENT_INFO: {
            LOG_APP_CUSTOM(LogLevel::kNotice, Concat("player [", m_Name, "] sent GameProtocol::Magic::CLIENT_INFO"), LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::PEER_SET: {
            LOG_APP_CUSTOM(LogLevel::kNotice, Concat("player [", m_Name, "] sent GameProtocol::Magic::PEER_SET"), LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::MAPPART_ERR: {
            LOG_APP_CUSTOM(LogLevel::kWarning, Concat("map download unexpectedly failed for [", m_Name, "] due to hash mismatch"), LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::PROTO_BUF: {
            // Serialized protocol buffers
            // TODO(REFORGED): Not sure how to handle PROTO_BUF in the most compatible way yet.
            vector<uint8_t> resendPacket = vector<uint8_t>(packet.begin(), packet.end());
            if (m_Game.get().GetIsSupportedGameVersion(GAMEVER(1u, 31u))) {
              m_Game.get().SendAll(resendPacket);
            } else {
              Send(resendPacket);
            }
            break;
          }

          case GameProtocol::Magic::MAPPART_OK: // spams a lot
          default: {
            break;
          }
        }
        if (m_Disconnected) {
          Abort = true;
        }
      }
      else if (packetFamily == GPSProtocol::Magic::GPS_HEADER && m_Game.get().GetIsProxyReconnectable()) {
        if (packetType == GPSProtocol::Magic::ACK && packetSize == 8) {
          EventGProxyAck(ByteArrayToUInt32LE(packet, 4));
        } else if (packetType == GPSProtocol::Magic::INIT) {
          EventGProxyClientInit(/* version */ packetSize >= 8 ? ByteArrayToUInt32LE(packet, 4) : 0);
        } else if (packetType == GPSProtocol::Magic::SUPPORT_EXTENDED && packetSize >= 8) {
          EventGProxyExtendedClientInit(packet);
        } else if (packetType == GPSProtocol::Magic::CHANGEKEY && packetSize >= 8) {
          EventGProxyChangeKey(ByteArrayToUInt32LE(packet, 4));
        }
      }

      data.remove_prefix(packetSize);

      if (Abort) {
        // Process no more packets
        data.remove_prefix(data.size());
        break;
      }
    }

    if (data.size() != socket->GetRecvBufferSize()) {
      socket->UpdateRecvBuffer(data);
    }
  } else if (m_Aura->GetTicksIsAfterDelay(m_Socket->GetLastRecv(), timeout)) {
    // check for socket timeouts
    // if we don't receive anything from a player for 70 seconds (20 seconds if reconnectable) we can assume they've dropped
    // this works because in the lobby we send pings every 5 seconds and expect a response to each one
    // and in the game the Warcraft 3 client sends keepalives frequently (at least once per second it looks like)
    m_Game.get().EventUserDisconnectTimedOut(this);
    if (m_Disconnected) {
      if (m_DeleteMe) {
        m_Socket->Discard(fd);
      }
      return m_DeleteMe;
    }
  }

  // EventUserLeft sets the game in a state where this player is still in m_Users, but it has no associated slot.
  // It's therefore crucial to check the Abort flag that it sets to avoid modifying it further.
  // As soon as the CGameUser::Update() call returns, EventUserDeleted takes care of erasing from the m_Users vector.
  if (!Abort) {
    // try to find out why we're requesting deletion
    // in cases other than the ones covered here m_LeftReason should have been set when m_DeleteMe was set
    if (m_Socket->HasError()) {
      m_Game.get().EventUserDisconnectSocketError(this);
    } else if (m_Socket->HasFin() || !m_Socket->GetConnected()) {
      m_Game.get().EventUserDisconnectConnectionClosed(this);
    } else if (m_KickByTicks.has_value() && m_Aura->GetTicksIsAfter(m_KickByTicks.value())) {
      m_Game.get().EventUserKickHandleQueued(this);
    } else if (!m_Verified && m_RealmInternalId >= 0x10 && m_Aura->GetTicksIsAfterDelay(m_JoinTicks, GAME_USER_UNVERIFIED_KICK_TICKS) && m_Game.get().GetIsLobbyStrict()) {
      shared_ptr<CRealm> Realm = GetRealm(false);
      if (Realm && Realm->GetUnverifiedAutoKickedFromLobby()) {
        m_Game.get().EventUserKickUnverified(this);
      }
    }

    if (!m_StatusMessageSent && m_Aura->GetTicksIsAfter(m_CheckStatusByTicks)) {
      m_Game.get().EventUserCheckStatus(this);
    }
  }

  if (!m_Disconnected) {
    m_GProxy->CheckSendAck();

    // wait 5 seconds after joining before sending the /whois or /w
    // if we send the /whois too early battle.net may not have caught up with where the player is and return erroneous results
    if (m_WhoisShouldBeSent && !m_Verified && !m_WhoisSent && !m_RealmHostName.empty() && m_Aura->GetTicksIsAfterDelay(m_JoinTicks, AUTO_REALM_VERIFY_LATENCY)) {
      shared_ptr<CRealm> Realm = GetRealm(false);
      if (Realm) {
        if (m_Game.get().GetDisplayMode() == GAME_DISPLAY_PUBLIC || Realm->GetIsPvPGN()) {
          if (m_Game.get().GetSentPriorityWhois()) {
            Realm->QueuePriorityWhois(Concat("/whois ", m_Name));
            m_Game.get().SetSentPriorityWhois(true);
          } else {
            Realm->QueueCommand(Concat("/whois ", m_Name));
          }
        } else if (m_Game.get().GetDisplayMode() == GAME_DISPLAY_PRIVATE) {
          Realm->QueueWhisper(R"(Spoof check by replying to this message with "sc" [ /r sc ])", m_Name);
        }
      }

      m_WhoisSent = true;
    }
  }

  if (m_DeleteMe) {
    return m_DeleteMe;
  }
  if (m_Socket) {
    if (m_Socket->HasError()) {
      m_Game.get().EventUserDisconnectSocketError(this);
    } else if (m_Socket->HasFin() || !m_Socket->GetConnected()) {
      m_Game.get().EventUserDisconnectConnectionClosed(this);
    }
    return m_DeleteMe;
  }

  return false;
}

MapTransferStatus CGameUser::NextSendMap()
{
  return m_Game.get().NextSendMap(this, GetUID(), GetMapTransfer());
}

void CGameUser::Send(const std::vector<uint8_t>& data)
{
  m_GProxy->EventSendData(data, m_Game.get().GetGameLoaded());

  if (!m_Disconnected && !m_Socket->HasError()) {
    m_Socket->PutBytes(data);
  }
}

void CGameUser::Send(const GameProtocol::PacketWrapper& data)
{
  m_GProxy->EventSendData(data, m_Game.get().GetGameLoaded());

  if (!m_Disconnected && !m_Socket->HasError()) {
    m_Socket->PutBytes(data.data);
  }
}

void CGameUser::SendChat(string_view message)
{
  m_Game.get().SendChat(this, message);
}

void CGameUser::SendOnLoadChatMessages()
{
  if (m_OnLoadChatMessages.GetIsEmpty()) {
    return;
  }
  size_t deletedSize = m_OnLoadChatMessages.GetDeletedSize();
  SendChat("== [LoadInGame] Chat history ==");
  for (const auto& pendingPacket : m_OnLoadChatMessages.GetOldEntries()) {
    Send(pendingPacket);
  }
  if (deletedSize > 0) {
    SendChat(Concat("== [LoadInGame] ", to_string(deletedSize) , " messages omitted... =="));
    for (const auto& pendingPacket : m_OnLoadChatMessages.GetNewEntries()) {
      Send(pendingPacket);
    }
  } else {
    for (const auto& pendingPacket : m_OnLoadChatMessages.GetFastNewEntries()) {
      Send(pendingPacket);
    }
  }
  SendChat("== [LoadInGame] History ended ==");
  m_OnLoadChatMessages.Clear();
}

void CGameUser::EventGProxyClientInit(const uint32_t version)
{
  shared_ptr<CRealm> realm = GetRealm(false);
  const CGame& game = m_Game.get();


  // the port to which the client directly connects
  // this means that if Aura is behind a reverse proxy,
  // this port should match its publicly visible port
  uint16_t port = 6112;
  if (realm) {
    port = realm->GetUsesCustomPort() ? realm->GetPublicHostPort() : game.GetHostPort();
  } else if (m_RealmInternalId == 0) {
    port = game.m_Aura->m_Net.m_Config.m_UDPEnableCustomPortTCP4 ? game.m_Aura->m_Net.m_Config.m_UDPCustomPortTCP4 : game.GetHostPort();
  }

  m_GProxy->Init(
    m_UID, version, port, game.GetGProxyEmptyActions(),
    // Extended
    game.GetIsProxyReconnectableLong(), game.m_Aura->m_Net.m_Config.m_ReconnectWaitTicks, game.GetGameID()
  );

  Print(Concat(game.GetLogPrefix(), "player [", m_Name, "] will reconnect at port ", to_string(port), " if disconnected"));
}

void CGameUser::EventGProxyExtendedClientInit(const string_view data)
{
  GProxyExtendedClientResult extendedMode = m_GProxy->ConfirmExtended(data);
  switch (extendedMode) {
    case GProxyExtendedClientResult::kInvalid:
      Print(Concat(m_Game.get().GetLogPrefix(), "player [", m_Name, "] sent premature GProxy Extended handshake"));
      break;
    case GProxyExtendedClientResult::kAlready:
      Print(Concat(m_Game.get().GetLogPrefix(), "player [", m_Name, "] sent multiple GProxy Extended handshakes"));
      break;
    case GProxyExtendedClientResult::kNormal:
      Print(Concat(m_Game.get().GetLogPrefix(), "player [", m_Name, "] is using GProxy Extended"));
      break;
    case GProxyExtendedClientResult::kCheckGameID:
      Print(Concat(m_Game.get().GetLogPrefix(), "player [", m_Name, "] is using GProxy Extended+"));
      break;
    }
}

void CGameUser::EventGProxyChangeKey(const uint32_t key)
{
  m_GProxy->SynchronizeReconnectKeyFromClient(key);
  Print(Concat(m_Game.get().GetLogPrefix(), "player [", m_Name, "] updated their reconnect key"));
}

double CGameUser::GetAPM() const
{
  if (m_Game.get().GetEffectiveTicks() == 0) return 0.;
  return static_cast<double>(m_ActionCounter) * 60000. / static_cast<double>(m_Game.get().GetEffectiveTicks());
}

double CGameUser::GetRecentAPM() const
{
  if (m_Game.get().GetEffectiveTicks() == 0) return 0.;
  uint32_t weightedSum = m_RecentActionCounter[0] * 24 + m_RecentActionCounter[1] * 36 + m_RecentActionCounter[1] * 60;
  return static_cast<double>(weightedSum) / 10.;
}

double CGameUser::GetMostRecentAPM() const
{
  if (m_Game.get().GetEffectiveTicks() == 0) return 0.;
  return static_cast<double>(m_RecentActionCounter[2]) * 12.;
}

void CGameUser::RestrictAPM(double apm, double burstActions)
{
  m_APMQuota.emplace(APM_RATE_LIMITER_TICK_INTERVAL, apm * APM_RATE_LIMITER_TICK_SCALING_FACTOR, burstActions, burstActions);
}

void CGameUser::EventGProxyAck(const size_t lastPacket)
{
  if (!m_GProxy->UnqueuePackets(lastPacket)) {
#ifdef DEBUG
    if (!m_FinishedLoading) {
      DPRINT_IF(LogLevel::kTrace, Concat(m_Game.get().GetLogPrefix(), "[GPROXY] player [", m_Name, "] sent GPS_ACK before loading the game"));
    } else if (!m_Game.get().GetGameLoaded()) {
      DPRINT_IF(LogLevel::kTrace, Concat(m_Game.get().GetLogPrefix(), "[GPROXY] player [", m_Name, "] sent GPS_ACK before the game is fully loaded"));
    } else {
      DPRINT_IF(LogLevel::kTrace, Concat(m_Game.get().GetLogPrefix(), "[GPROXY] player [", m_Name, "] sent bad lastPacket ", to_string(lastPacket), " < ", to_string(GetGProxy()->GetUnqueuedPacketsCount())));
    }
#endif
  }
}

void CGameUser::EventGProxyReconnect(CConnection* connection, const uint32_t lastPacket)
{
  // prevent potential session hijackers from stealing sudo access
  GetCommandHistory()->SudoModeEnd(m_Game.get().m_Aura, GetGame(), GetName());

  // Runs from the CConnection iterator, so appending to CNet::m_IncomingConnections needs to wait
  // UnrefConnection(deferred = true) takes care of this
  // a new CConnection for the old CStreamIOSocket is created, and is pushed to CNet::m_DownGradedConnections 
  UnrefConnection(true);

  m_Socket = connection->GetSocket();
  connection->SetSocket(nullptr);

  m_Socket->SetLogErrors(true);
  m_Socket->PutBytes(GPSProtocol::SEND_GPSS_RECONNECT((uint32_t)m_GProxy->GetRecvPacketsCount()));
  DCHECK((m_GProxy->UnqueuePackets(lastPacket)), ("EventGProxyReconnect() triggered with an old lastPacket"));
  m_GProxy->SynchronizeFromBuffer();
  m_Disconnected = false;
  m_StartedLaggingTicks = m_Aura->GetClockTicks();
  m_DisconnectNoticeSent = false;
  m_LastDisconnectRepeatNoticeTicks.reset();
  if (m_LastDisconnectTicks.has_value()) {
    m_TotalDisconnectTicks += m_Aura->GetClockTicks() - m_LastDisconnectTicks.value();
  }
  if (GetGProxy()->GetIsExtended()) {
    m_Game.get().SendAllChat(Concat("Player [", GetDisplayName(), "] reconnected with GProxyDLL!"));
  } else {
    m_Game.get().SendAllChat(Concat("Player [", GetDisplayName(), "] reconnected with GProxy++!"));
  }
  if (m_Game.get().m_Aura->MatchLogLevel(LogLevel::kNotice)) {
    Print(Concat(m_Game.get().GetLogPrefix(), "user reconnected: [", GetName(), "@", string(GetRealmHostName()), "#", ToDecString(GetUID()), "] from [", GetIPString(), "] (", m_Socket->GetName(), ")"));
  }
}

void CGameUser::EventGProxyReconnectInvalid()
{
  if (m_Disconnected) return;
  // TODO: Do we need different logic for rotating GProxy keys?
  m_GProxy->RotateReconnectKey();
}

bool CGameUser::GetDisconnectedUnrecoverably() const
{
  return m_Disconnected && !GetCanReconnect();
}

int64_t CGameUser::GetTotalDisconnectTicks() const
{
  if (!m_Disconnected || !m_LastDisconnectTicks.has_value()) {
    return m_TotalDisconnectTicks;
  } else {
    return m_TotalDisconnectTicks + m_Aura->GetClockTicks() - m_LastDisconnectTicks.value();
  }
}

string CGameUser::GetDelayText(bool displaySync) const
{
  string pingText, syncText;
  // Note: When someone is lagging, we actually clear their ping data.
  const bool anyPings = GetIsRTTMeasured();
  optional<uint32_t> rtt = GetOperationalRTT();
  if (!anyPings || !rtt.has_value()) {
    pingText = "?";
  } else {
    uint32_t equalizerDelay = GetPingEqualizerDelay();
    if (GetIsRTTMeasuredConsistent()) {
      pingText = to_string(rtt.value());
    } else {
      pingText = Concat("*", to_string(rtt.value()));
    }
    if (equalizerDelay > 0) {
      if (!m_Game.get().m_Aura->m_Net.m_Config.m_LiteralRTT) equalizerDelay /= 2;
      pingText += Concat("(", to_string(equalizerDelay), ")");
    }
  }
  if (!displaySync || !m_Game.get().GetGameLoaded() || GetNormalSyncCounter() >= m_Game.get().GetSyncCounter()) {
    if (anyPings) return Concat(pingText, "ms");
    return pingText;
  }
  float syncDelay = static_cast<float>(m_Game.get().GetActiveLatency()) * static_cast<float>(m_Game.get().GetSyncCounter() - GetNormalSyncCounter());

  if (m_SyncCounterOffset == 0) {
    // Expect clients to always be at least one RTT behind.
    // The "sync delay" is defined as the additional delay they got.
    syncDelay -= static_cast<float>(GetRTT().value_or(0) + GetPingEqualizerDelay());
  }

  if (!anyPings) {
    return Concat("+", to_string(static_cast<uint32_t>(syncDelay)), "ms");
  } else if (syncDelay <= 0) {
    return Concat(pingText, "ms");
  } else {
    return Concat(pingText, "+", to_string(static_cast<uint32_t>(syncDelay)), "ms");
  }
}

string CGameUser::GetReconnectionText() const
{
  if (!GetCanReconnect()) {
    return "No";
  }
  if (GetGProxy()->GetIsExtended()) {
    return "Extended";
  }
  return "Yes";
}

string CGameUser::GetSyncText() const
{
  if (!m_Game.get().GetGameLoaded() || GetSyncCounter() >= m_Game.get().GetSyncCounter()) {
    return string();
  }
  bool isNormalized = m_SyncCounterOffset > 0;
  string behindTimeText;
  if (GetNormalSyncCounter() < m_Game.get().GetSyncCounter()) {
    float normalSyncDelay = static_cast<float>(m_Game.get().GetActiveLatency()) * static_cast<float>(m_Game.get().GetSyncCounter() - GetNormalSyncCounter());
    behindTimeText = Concat(ToFormattedString(normalSyncDelay / 1000), "s behind");
  }
  if (isNormalized && GetSyncCounter() < m_Game.get().GetSyncCounter()) {
    float totalSyncDelay = static_cast<float>(m_Game.get().GetActiveLatency()) * static_cast<float>(m_Game.get().GetSyncCounter() - GetSyncCounter());
    if (behindTimeText.empty()) {
      behindTimeText += Concat(ToFormattedString(totalSyncDelay / 1000), "s behind unnormalized");
    } else {
      behindTimeText += Concat(" (", ToFormattedString(totalSyncDelay / 1000), "s unnormalized)");
    }
  }
  return behindTimeText;
}

bool CGameUser::GetIsNativeReferee() const
{
  return m_Observer && m_Game.get().GetMap()->GetGameObservers() == GameObserversMode::kReferees;
}

bool CGameUser::GetCanUsePublicChat() const
{
  if (GetIsInLoadingScreen()) return false;
  if (!m_Observer || m_PowerObserver || (!m_Game.get().GetGameLoading() && !m_Game.get().GetGameLoaded())) return true;
  return !m_Game.get().GetUsesCustomReferees() && m_Game.get().GetMap()->GetGameObservers() == GameObserversMode::kReferees;
}

bool CGameUser::Mute(const int64_t seconds)
{
  int64_t muteEndTicks = m_Aura->GetClockTicks() + (seconds * 1000);
  if (m_Muted && m_MuteEndTicks >= muteEndTicks) return false;
  m_Muted = true;
  m_MuteEndTicks = muteEndTicks;
  return true;
}

bool CGameUser::UnMute()
{
  if (!m_Muted) return false;
  m_Muted = false;
  m_MuteEndTicks = 0;
  return true;
}

bool CGameUser::GetIsOwner(optional<bool> assumeVerified) const
{
  if (m_Owner) return true;
  bool isVerified = false;
  if (assumeVerified.has_value()) {
    isVerified = assumeVerified.value();
  } else {
    isVerified = GetIsRealmVerified();
  }
  return m_Game.get().MatchOwnerName(m_Name) && m_RealmHostName == m_Game.get().GetOwnerRealm() && (
    isVerified || m_RealmHostName.empty()
  );
}

void CGameUser::AcquireGameName()
{
  shared_ptr<CRealm> realm = GetRealm(false);
  if (realm && realm->GetGameBroadcast() == GetGame()) {
    m_GameName = realm->GetGameBroadcastName();
  } else {
    m_GameName = m_Game.get().GetCustomGameName(realm, true);
    if (m_GameName.size() > MAX_GAME_NAME_SIZE) {
      m_GameName = m_GameName.substr(0, MAX_GAME_NAME_SIZE);
    }
  }
}

bool CGameUser::UpdateReady()
{
  if (m_UserReady.has_value()) {
    m_Ready = m_UserReady.value();
    return m_Ready;
  }
  if (!m_MapReady) {
    return m_Ready;
  }
  switch (m_Game.get().GetPlayersReadyMode()) {
    case PlayersReadyMode::kFast:
      m_Ready = true;
      break;
    case PlayersReadyMode::kExpectRace:
      if (m_Game.get().GetMap()->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS) {
        m_Ready = true;
      } else if (m_Game.get().GetMap()->GetMapFlags() & GAMEFLAG_RANDOMRACES) {
        m_Ready = true;
      } else {
        const CGameSlot* slot = m_Game.get().InspectSlot(m_Game.get().GetSIDFromUID(GetUID()));
        if (slot) {
          m_Ready = slot->GetRaceFixed() != SLOTRACE_RANDOM;
        } else {
          m_Ready = false;
        }
      }
      break;
    case PlayersReadyMode::kExplicit:
    default: {
      m_Ready = false;
    }
  }
  return m_Ready;
}

void CGameUser::DisableReconnect()
{
  if (!m_GProxy->GetIsEnabled()) return;
  m_GProxy->Disable();
}

bool CGameUser::GetReadyReminderIsDue() const
{
  return m_Aura->GetTicksIsFirstOrAfterDelay(m_ReadyReminderLastTicks, READY_REMINDER_PERIOD);
}

void CGameUser::SetReadyReminded()
{
  m_ReadyReminderLastTicks = m_Aura->GetClockTicks();
}
