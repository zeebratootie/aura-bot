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

using namespace std;
using namespace GameUser;

#define LOG_APP_IF(T, U) \
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_IF for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        m_Game.get().LogApp(U, LOG_C); \
    }

#define LOG_APP_CUSTOM(T, U, V) \
    static_assert(T < LogLevel::LAST, "Use DLOG_APP_CUSTOM for tracing log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        m_Game.get().LogApp(U, V); \
    }

#ifdef DEBUG
#define DLOG_APP_IF(T, U) \
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_IF for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        m_Game.get().LogApp(U, LOG_C); \
    }

#define DLOG_APP_CUSTOM(T, U, V) \
    static_assert(T < LogLevel::LAST, "Invalid tracing log level");\
    static_assert(T >= LogLevel::kTrace, "Use LOG_APP_CUSTOM for regular log levels");\
    if (m_Aura->MatchLogLevel(T)) {\
        m_Game.get().LogApp(U, V); \
    }
#else
#define DLOG_APP_IF(T, U)
#define DLOG_APP_CUSTOM(T, U, V)
#endif

//
// CGameUser
//

CGameUser::CGameUser(shared_ptr<CGame> nGame, CConnection* connection, uint8_t nUID, const bool gameVersionIsExact, const Version& gameVersion, uint32_t nJoinedRealmInternalId, string nJoinedRealm, string nName, std::array<uint8_t, 4> nInternalIP, bool nReserved)
  : CConnection(*connection),
    m_Game(ref(*nGame)),
    m_IPv4Internal(std::move(nInternalIP)),
    m_GProxyBufferSize(0),
    m_RealmInternalId(nJoinedRealmInternalId),
    m_RealmHostName(std::move(nJoinedRealm)),
    m_Name(std::move(nName)),
    m_TotalPacketsSent(0),
    m_TotalPacketsReceived(0),
    m_LeftCode(PLAYERLEAVE_LOBBY),
    m_Status(USERSTATUS_LOBBY),
    m_IsLeaver(false),
    m_PingEqualizerOffset(0),
    m_PingEqualizerFrameNode(nullptr),
    m_OnHoldActionsCount(0),
    m_PongCounter(0),
    m_SyncCounterOffset(0),
    m_SyncCounter(0),
    m_JoinTicks(GetTicks()),
    m_FinishedLoadingTicks(0),
    m_HandicapTicks(0),
    m_StartedLaggingTicks(0),
    m_LastGProxyWaitNoticeSentTime(0),
    m_GProxyReconnectKey(rand()),
    m_SID(0xFF),
    m_UID(nUID),
    m_OldUID(0xFF),
    m_PseudonymUID(0xFF),
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
    m_KickReason(KickReason::NONE),
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
    m_CheckStatusByTicks(GetTicks() + CHECK_STATUS_LATENCY),
    m_MuteEndTicks(0),

    m_GProxy(false),
    m_GProxyPort(0),
    m_GProxyCheckGameID(false),
    m_GProxyDisconnectNoticeSent(false),
    m_GProxyExtended(false),
    m_GProxyVersion(0),
    m_Disconnected(false),
    m_TotalDisconnectTicks(0),

    m_TeamCaptain(0),
    m_ActionCounter(0),
    m_AntiAbuseCounter(0),
    m_RemainingSaves(GAME_SAVES_PER_PLAYER),
    m_RemainingPauses(GAME_PAUSES_PER_PLAYER)
{
  m_RecentActionCounter.fill(0);
  m_RTTValues.reserve(MAXIMUM_PINGS_COUNT);
  m_Socket->SetLogErrors(true);
  m_Type = INCON_TYPE_PLAYER;
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

uint32_t CGameUser::GetOperationalRTT() const
{
  if (m_MeasuredRTT.has_value()) {
    return m_MeasuredRTT.value().second;
  }

  // weighted average of stored pings (max 6 stored = 25-30 seconds)
  // 4:3:2:1:1:1 (more recent = more weight)
  //
  // note that this vector may have the bias of LC-style pings incorporated
  // this means that the output "operational RTT" may sometimes be half the actual RTT.

  uint32_t weightedSum = 0;
  uint8_t backDelta = 0;
  uint8_t i = static_cast<uint8_t>(m_RTTValues.size());
  uint32_t totalWeight = 0;
  while (i--) {
    const uint32_t weight = (backDelta >= MAX_PING_WEIGHT ? 1 : MAX_PING_WEIGHT - backDelta);
    weightedSum += m_RTTValues[i] * weight;
    totalWeight += weight;
    backDelta++;
  }

  if (totalWeight == 0) {
    return 0;
  }

  return weightedSum / totalWeight;
}

uint32_t CGameUser::GetDisplayRTT() const
{
  return GetOperationalRTT();
}

uint32_t CGameUser::GetRTT() const
{
  if (m_Game.get().m_Aura->m_Net.m_Config.m_LiteralRTT) {
    return GetOperationalRTT();
  }
  return GetOperationalRTT() * 2;
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

string CGameUser::GetGameVersionString() const
{
  if (m_GameVersionIsExact) {
    return "v" + ToVersionString(GetGameVersion());
  } else {
    return "v" + ToVersionString(GetGameVersion()) + "?";
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
      return "Player " + ToDecString(m_UID);
    } else {
      // After CGame::RunPlayerObfuscation()
      return "Player " + ToDecString(m_PseudonymUID) + "?";
    }
  }
  return m_Name;
}

shared_ptr<CGame> CGameUser::GetGame()
{
  return m_Game.get().shared_from_this();
}

uint32_t CGameUser::GetPingEqualizerDelay() const
{
  if (!m_Game.get().GetGameLoaded()) return 0u;
  return static_cast<uint32_t>(GetPingEqualizerOffset()) * static_cast<uint32_t>(m_Game.get().GetActiveLatency());
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
      Print(m_Game.get().GetLogPrefix() + "[APMLimit] Malfunction detected - " + to_string(doneCount) + " actions released, " + to_string(GetOnHoldActionsCount()) + " remaining)");
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
  if (GetMuteEndTicks() < GetTicks()) {
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

bool CGameUser::GetIsBehindFramesNormal(const uint32_t frameLimit) const
{
  return m_Game.get().GetSyncCounter() > GetNormalSyncCounter() && m_Game.get().GetSyncCounter() - GetNormalSyncCounter() >= frameLimit;
}

bool CGameUser::CloseConnection(bool fromOpen)
{
  if (m_Disconnected) return false;
  if (!m_Game.get().GetGameLoaded() || !m_GProxy) {
    TrySetEnding();
    DisableReconnect();
  }
  m_LastDisconnectTicks = GetTicks();
  m_Disconnected = true;
  m_Socket->Close();
  m_Game.get().EventUserAfterDisconnect(this, fromOpen);
  return true;
}

void CGameUser::UnrefConnection(bool deferred)
{
  m_Game.get().m_Aura->m_Net.OnUserKicked(this, deferred);

  if (!m_Disconnected) {
    m_LastDisconnectTicks = GetTicks();
    m_Disconnected = true;
  }
}

void CGameUser::ClearStalePings() {
  if (m_RTTValues.empty()) return;
  m_RTTValues[0] = m_RTTValues[m_RTTValues.size() - 1];
  m_RTTValues.erase(m_RTTValues.begin() + 1, m_RTTValues.end());
}

void CGameUser::RefreshUID()
{
  m_OldUID = m_UID;
  m_UID = m_Game.get().GetNewUID();
}

bool CGameUser::Update(fd_set* fd, int64_t timeout)
{
  if (m_Disconnected) {
    if (m_GProxyExtended && GetTotalDisconnectTicks() > m_Game.get().m_Aura->m_Net.m_Config.m_ReconnectWaitTicks) {
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

  const int64_t Ticks = GetTicks();

  bool Abort = false;
  if (m_Socket->DoRecv(fd)) {
    // extract as many packets as possible from the socket's receive buffer and process them

    string*              RecvBuffer         = m_Socket->GetBytes();
    std::vector<uint8_t> Bytes              = CreateByteArray((uint8_t*)RecvBuffer->c_str(), RecvBuffer->size());
    uint32_t             LengthProcessed    = 0;

    // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

    while (Bytes.size() >= 4)
    {
      // bytes 2 and 3 contain the length of the packet
      const uint16_t Length = ByteArrayToUInt16(Bytes, false, 2);
      if (Length < 4) {
        m_Game.get().EventUserDisconnectGameProtocolError(this, true);
        Abort = true;
        break;
      }
      if (Bytes.size() < Length) break;
      const std::vector<uint8_t> Data = std::vector<uint8_t>(begin(Bytes), begin(Bytes) + Length);

      if (Bytes[0] == GameProtocol::Magic::W3GS_HEADER)
      {
        ++m_TotalPacketsReceived;

        // byte 1 contains the packet ID

        switch (Bytes[1])
        {
          case GameProtocol::Magic::LEAVEGAME: {
            if (ValidateLength(Data) && Data.size() >= 8) {
              const uint32_t reason = ByteArrayToUInt32(Data, false, 4);
              m_Game.get().EventUserLeft(this, reason);
              m_Socket->SetLogErrors(false);
            } else {
              m_Game.get().EventUserDisconnectGameProtocolError(this, false);
            }
            Abort = true;
            break;
          }

          case GameProtocol::Magic::GAMELOADED_SELF:
            if (GameProtocol::RECEIVE_W3GS_GAMELOADED_SELF(Data)) {
              if (m_Game.get().GetGameLoading() && !m_FinishedLoading) {
                m_FinishedLoading      = true;
                m_FinishedLoadingTicks = GetTicks();
                m_Game.get().EventUserLoaded(this);
              }
            }

            break;

          case GameProtocol::Magic::OUTGOING_ACTION: {
            if (ValidateLength(Data) && Data.size() >= 8) {
              CIncomingAction action = GameProtocol::RECEIVE_W3GS_OUTGOING_ACTION(Data, m_UID);
              if (!m_Game.get().EventUserIncomingAction(this, action)) {
                m_Game.get().EventUserDisconnectGameProtocolError(this, false);
                Abort = true;
              } else if (m_Disconnected) {
                Abort = true;
              }
            }
            break;
          }

          case GameProtocol::Magic::OUTGOING_KEEPALIVE: {
            if (m_SyncCounter >= m_Game.get().GetSyncCounter()) {
              LOG_APP_CUSTOM(LogLevel::kWarning, "player [" + m_Name + "] incorrectly ahead of sync", LOG_C | LOG_P);
              m_Game.get().EventUserDisconnectGameProtocolError(this, false);
              Abort = true;
            } else {
              m_CheckSums.push(GameProtocol::RECEIVE_W3GS_OUTGOING_KEEPALIVE(Data));
              ++m_SyncCounter;
              m_Game.get().EventUserKeepAlive(this);

              if (m_Disconnected) {
                Abort = true;
              }
            }
            break;
          }

          case GameProtocol::Magic::CHAT_TO_HOST: {
            CIncomingChatMessage incomingChatMessage = GameProtocol::RECEIVE_W3GS_CHAT_TO_HOST(Data);

            if (incomingChatMessage.GetIsValid()) {
              m_Game.get().EventUserChatOrPlayerSettings(this, incomingChatMessage);

              if (m_Disconnected) {
                Abort = true;
              }
            }
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

            CIncomingMapFileSize incomingMapSize = GameProtocol::RECEIVE_W3GS_MAPSIZE(Data);
            if (incomingMapSize.GetIsValid()) {
              m_Game.get().EventUserMapSize(this, incomingMapSize);
            }
            break;
          }

          case GameProtocol::Magic::PONG_TO_HOST: {
            uint32_t Pong = GameProtocol::RECEIVE_W3GS_PONG_TO_HOST(Data);

            const bool bufferBloatForbidden = m_Game.get().m_Aura->m_Net.m_Config.m_HasBufferBloat && m_Game.get().IsDownloading();
            bool useSystemRTT = !m_Socket->GetIsLoopback() && m_Game.get().GetGameLoaded() && m_Game.get().m_Aura->m_Net.m_Config.m_UseSystemRTT;
            const bool useLiteralRTT = m_Game.get().m_Aura->m_Net.m_Config.m_LiteralRTT;

            // discard pong values when anyone else is downloading if we're configured to do so
            if (!bufferBloatForbidden) {
              if (useSystemRTT && (!m_MeasuredRTT.has_value() || m_MeasuredRTT.value().first + SYSTEM_RTT_POLLING_PERIOD < Ticks)) {
                optional<uint32_t> rtt = m_Socket->GetRTT();
                if (rtt.has_value()) {
                  m_MeasuredRTT = make_pair(Ticks, useLiteralRTT ? rtt.value() : (rtt.value() / 2));
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
                if (!m_MapTransfer.GetStarted() || (m_MapTransfer.GetFinished() && GetTicks() - m_MapTransfer.GetFinishedTicks() >= 8000)) {
                  m_RTTValues.push_back(useLiteralRTT ? (static_cast<uint32_t>(GetTicks()) - Pong) : ((static_cast<uint32_t>(GetTicks()) - Pong) / 2));
                  if (m_RTTValues.size() > MAXIMUM_PINGS_COUNT) {
                    m_RTTValues.erase(begin(m_RTTValues));
                  }
                }
              }

              if (useSystemRTT || Pong != 1) {
                m_Game.get().EventUserPongToHost(this);
              }

              if (!GetIsRTTMeasuredConsistent() && !GetIsDownloading()) {
                // Measure player's ping as fast as possible, by chaining new pings to pongs received.
                Send(GameProtocol::SEND_W3GS_PING_FROM_HOST(m_Aura->GetLoopTicks()));
              }
            }

            ++m_PongCounter;
            break;
          }

          case GameProtocol::Magic::DESYNC: {
            LOG_APP_CUSTOM(LogLevel::kNotice, "player [" + m_Name + "] sent GameProtocol::Magic::DESYNC", LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::GAME_OVER: {
            LOG_APP_CUSTOM(LogLevel::kNotice, "player [" + m_Name + "] sent GameProtocol::Magic::GAME_OVER", LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::LEAVE_ACK: {
            LOG_APP_CUSTOM(LogLevel::kNotice, "player [" + m_Name + "] sent GameProtocol::Magic::LEAVE_ACK", LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::CLIENT_INFO: {
            LOG_APP_CUSTOM(LogLevel::kNotice, "player [" + m_Name + "] sent GameProtocol::Magic::CLIENT_INFO", LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::PEER_SET: {
            LOG_APP_CUSTOM(LogLevel::kNotice, "player [" + m_Name + "] sent GameProtocol::Magic::PEER_SET", LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::MAPPART_ERR: {
            LOG_APP_CUSTOM(LogLevel::kNotice, "player [" + m_Name + "] sent GameProtocol::Magic::MAPPART_ERR", LOG_C | LOG_P);
            break;
          }

          case GameProtocol::Magic::PROTO_BUF: {
            // Serialized protocol buffers
            // TODO: Not sure how to handle PROTO_BUF in the most compatible way yet.
            if (m_Game.get().GetIsSupportedGameVersion(GAMEVER(1u, 31u))) {
              m_Game.get().SendAll(Data);
            } else {
              Send(Data);
            }
            break;
          }

          case GameProtocol::Magic::MAPPART_OK: // spams a lot
          default: {
            break;
          }
        }
      }
      else if (Bytes[0] == GPSProtocol::Magic::GPS_HEADER && m_Game.get().GetIsProxyReconnectable()) {
        if (Bytes[1] == GPSProtocol::Magic::ACK && Length == 8) {
          EventGProxyAck(ByteArrayToUInt32(Data, false, 4));
        } else if (Bytes[1] == GPSProtocol::Magic::INIT) {
          InitGProxy(Length >= 8 ? ByteArrayToUInt32(Bytes, false, 4) : 0);
        } else if (Bytes[1] == GPSProtocol::Magic::SUPPORT_EXTENDED && Length >= 8) {
          if (m_GProxy && m_Game.get().GetIsProxyReconnectableLong()) {
            ConfirmGProxyExtended(Data);
          }
        } else if (Bytes[1] == GPSProtocol::Magic::CHANGEKEY && Length >= 8) {
          m_GProxyReconnectKey = ByteArrayToUInt32(Bytes, false, 4);
          Print(m_Game.get().GetLogPrefix() + "player [" + m_Name + "] updated their reconnect key");
        }
      }

      if (Abort) {
        // Process no more packets
        break;
      }

      LengthProcessed += Length;
      Bytes = std::vector<uint8_t>(begin(Bytes) + Length, end(Bytes));
    }

    if (Abort) {
      RecvBuffer->clear();
    } else if (LengthProcessed > 0) {
      *RecvBuffer = RecvBuffer->substr(LengthProcessed);
    }
  } else if (Ticks - m_Socket->GetLastRecv() >= timeout) {
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
    } else if (m_KickByTicks.has_value() && m_KickByTicks.value() < Ticks) {
      m_Game.get().EventUserKickHandleQueued(this);
    } else if (!m_Verified && m_RealmInternalId >= 0x10 && Ticks - m_JoinTicks >= GAME_USER_UNVERIFIED_KICK_TICKS && m_Game.get().GetIsLobbyStrict()) {
      shared_ptr<CRealm> Realm = GetRealm(false);
      if (Realm && Realm->GetUnverifiedAutoKickedFromLobby()) {
        m_Game.get().EventUserKickUnverified(this);
      }
    }

    if (!m_StatusMessageSent && m_CheckStatusByTicks < Ticks) {
      m_Game.get().EventUserCheckStatus(this);
    }
  }

  if (!m_Disconnected) {
    // GProxy++ acks
    if (m_GProxy && (!m_LastGProxyAckTicks.has_value() || Ticks - m_LastGProxyAckTicks.value() >= GPS_ACK_PERIOD)) {
      m_Socket->PutBytes(GPSProtocol::SEND_GPSS_ACK(m_TotalPacketsReceived));
      m_LastGProxyAckTicks = Ticks;
    }

    // wait 5 seconds after joining before sending the /whois or /w
    // if we send the /whois too early battle.net may not have caught up with where the player is and return erroneous results
    if (m_WhoisShouldBeSent && !m_Verified && !m_WhoisSent && !m_RealmHostName.empty() && Ticks - m_JoinTicks >= AUTO_REALM_VERIFY_LATENCY) {
      shared_ptr<CRealm> Realm = GetRealm(false);
      if (Realm) {
        if (m_Game.get().GetDisplayMode() == GAME_DISPLAY_PUBLIC || Realm->GetIsPvPGN()) {
          if (m_Game.get().GetSentPriorityWhois()) {
            Realm->QueuePriorityWhois("/whois " + m_Name);
            m_Game.get().SetSentPriorityWhois(true);
          } else {
            Realm->QueueCommand("/whois " + m_Name);
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

uint8_t CGameUser::NextSendMap()
{
  return m_Game.get().NextSendMap(this, GetUID(), GetMapTransfer());
}

void CGameUser::Send(const std::vector<uint8_t>& data)
{
  // must start counting packet total from beginning of connection
  // accepting fragmented packets should not make an observable difference,
  // but it's the safest behavior, just in case something weird is going on in the caller side.
  size_t count = GameProtocol::GetPacketCount<GameProtocol::FragmentPolicy::kAccept>(data);
  m_TotalPacketsSent += count;

  if (m_GProxy && m_Game.get().GetGameLoaded()) {
    // we can avoid buffering packets until we know the client is using GProxy++ since that'll be determined before the game starts
    // this prevents us from buffering packets for non-reconnectable clients
    m_GProxyBuffer.push(GameProtocol::PacketWrapper(data, count));
    m_GProxyBufferSize += count;
  }

  if (!m_Disconnected && !m_Socket->HasError()) {
    m_Socket->PutBytes(data);
  }
}

void CGameUser::InitGProxy(const uint32_t version)
{
  shared_ptr<CRealm> realm = GetRealm(false);

  m_GProxy = true;
  m_GProxyVersion = version;

  // the port to which the client directly connects
  // this means that if Aura is behind a reverse proxy,
  // this port should match its publicly visible port
  if (realm) {
    m_GProxyPort = realm->GetUsesCustomPort() ? realm->GetPublicHostPort() : m_Game.get().GetHostPort();
  } else if (m_RealmInternalId == 0) {
    m_GProxyPort = m_Game.get().m_Aura->m_Net.m_Config.m_UDPEnableCustomPortTCP4 ? m_Game.get().m_Aura->m_Net.m_Config.m_UDPCustomPortTCP4 : m_Game.get().GetHostPort();
  } else {
    m_GProxyPort = 6112;
  }

  UpdateGProxyEmptyActions();
  CheckGProxyExtendedStartHandShake();

  Print(m_Game.get().GetLogPrefix() + "player [" + m_Name + "] will reconnect at port " + to_string(m_GProxyPort) + " if disconnected");
}

void CGameUser::ConfirmGProxyExtended(const vector<uint8_t>& data)
{
  m_GProxyExtended = true;
  if (data.size() >= 12) {
    m_GProxyCheckGameID = true;
    Print(m_Game.get().GetLogPrefix() + "player [" + m_Name + "] is using GProxy Extended+");
  } else {
    Print(m_Game.get().GetLogPrefix() + "player [" + m_Name + "] is using GProxy Extended");
  }
}

double CGameUser::GetAPM() const
{
  if (m_Game.get().GetEffectiveTicks() == 0) return 0.;
  return static_cast<double>(m_ActionCounter) * 60000. / m_Game.get().GetEffectiveTicks();
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

void CGameUser::UpdateGProxyEmptyActions() const
{
  m_Socket->PutBytes(GPSProtocol::SEND_GPSS_INIT(m_GProxyPort, m_UID, m_GProxyReconnectKey, m_Game.get().GetGProxyEmptyActions()));
}

void CGameUser::CheckGProxyExtendedStartHandShake() const
{
  if (m_GProxyVersion >= 2 && m_Game.get().GetIsProxyReconnectableLong()) {
    m_Socket->PutBytes(GPSProtocol::SEND_GPSS_SUPPORT_EXTENDED(m_Game.get().m_Aura->m_Net.m_Config.m_ReconnectWaitTicks, static_cast<uint32_t>(m_Game.get().GetGameID())));
  }
}

void CGameUser::EventGProxyAck(const size_t lastPacket)
{
  const size_t alreadyUnqueued = m_TotalPacketsSent - m_GProxyBufferSize;
  if (lastPacket <= alreadyUnqueued) return;

  size_t pendingUnqueue = min(m_GProxyBufferSize, lastPacket - alreadyUnqueued);
  size_t thisUnqueue = 0;
  size_t frontCount = 0;
  while (pendingUnqueue > 0) {
    GameProtocol::PacketWrapper& frontPackets = m_GProxyBuffer.front();
    frontCount = frontPackets.count;
    thisUnqueue = min(frontCount, pendingUnqueue);
    if (thisUnqueue == frontCount) {
      m_GProxyBuffer.pop();
    } else {
      frontPackets.Remove(thisUnqueue);
    }
    pendingUnqueue -= thisUnqueue;
    m_GProxyBufferSize -= thisUnqueue;
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
  m_Socket->PutBytes(GPSProtocol::SEND_GPSS_RECONNECT(m_TotalPacketsReceived));
  EventGProxyAck(lastPacket);

  {
    // send remaining packets from buffer,
    // but preserve buffer in case the client disconnects again
    queue<GameProtocol::PacketWrapper> tempBuffer;
    while (!m_GProxyBuffer.empty()) {
      m_Socket->PutBytes(m_GProxyBuffer.front().data);
      tempBuffer.push(move(m_GProxyBuffer.front()));
      m_GProxyBuffer.pop();
    }
    m_GProxyBuffer.swap(tempBuffer);
  }

  m_Disconnected = false;
  m_StartedLaggingTicks = GetTicks();
  m_GProxyDisconnectNoticeSent = false;
  m_LastGProxyWaitNoticeSentTime = 0;
  if (m_LastDisconnectTicks.has_value()) {
    m_TotalDisconnectTicks += GetTicks() - m_LastDisconnectTicks.value();
  }
  if (GetGProxyExtended()) {
    m_Game.get().SendAllChat("Player [" + GetDisplayName() + "] reconnected with GProxyDLL!");
  } else {
    m_Game.get().SendAllChat("Player [" + GetDisplayName() + "] reconnected with GProxy++!");
  }
  if (m_Game.get().m_Aura->MatchLogLevel(LogLevel::kNotice)) {
    Print(m_Game.get().GetLogPrefix() + "user reconnected: [" + GetName() + "@" + GetRealmHostName() + "#" + ToDecString(GetUID()) + "] from [" + GetIPString() + "] (" + m_Socket->GetName() + ")");
  }
}

void CGameUser::EventGProxyReconnectInvalid()
{
  if (m_Disconnected) return;
  // TODO: Do we need different logic for rotating GProxy keys?
  RotateGProxyReconnectKey();
}

void CGameUser::RotateGProxyReconnectKey() const
{
  m_Socket->PutBytes(GPSProtocol::SEND_GPSS_CHANGE_KEY(rand()));
}

int64_t CGameUser::GetTotalDisconnectTicks() const
{
  if (!m_Disconnected || !m_LastDisconnectTicks.has_value()) {
    return m_TotalDisconnectTicks;
  } else {
    return m_TotalDisconnectTicks + GetTicks() - m_LastDisconnectTicks.value();
  }
}

string CGameUser::GetDelayText(bool displaySync) const
{
  string pingText, syncText;
  // Note: When someone is lagging, we actually clear their ping data.
  const bool anyPings = GetIsRTTMeasured();
  if (!anyPings) {
    pingText = "?";
  } else {
    uint32_t rtt = GetOperationalRTT();
    uint32_t equalizerDelay = GetPingEqualizerDelay();
    if (GetIsRTTMeasuredConsistent()) {
      pingText = to_string(rtt);
    } else {
      pingText = "*" + to_string(rtt);
    }
    if (equalizerDelay > 0) {
      if (!m_Game.get().m_Aura->m_Net.m_Config.m_LiteralRTT) equalizerDelay /= 2;
      pingText += "(" + to_string(equalizerDelay) + ")";
    }
  }
  if (!displaySync || !m_Game.get().GetGameLoaded() || GetNormalSyncCounter() >= m_Game.get().GetSyncCounter()) {
    if (anyPings) return pingText + "ms";
    return pingText;
  }
  float syncDelay = static_cast<float>(m_Game.get().GetActiveLatency()) * static_cast<float>(m_Game.get().GetSyncCounter() - GetNormalSyncCounter());

  if (m_SyncCounterOffset == 0) {
    // Expect clients to always be at least one RTT behind.
    // The "sync delay" is defined as the additional delay they got.
    syncDelay -= static_cast<float>(GetRTT() + GetPingEqualizerDelay());
  }

  if (!anyPings) {
    return "+" + to_string(static_cast<uint32_t>(syncDelay)) + "ms";
  } else if (syncDelay <= 0) {
    return pingText + "ms";
  } else {
    return pingText + "+" + to_string(static_cast<uint32_t>(syncDelay)) + "ms";
  }
}

string CGameUser::GetReconnectionText() const
{
  if (!GetGProxyAny()) {
    return "No";
  }
  if (GetGProxyExtended()) {
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
    behindTimeText = ToFormattedString(normalSyncDelay / 1000) + "s behind";
  }
  if (isNormalized && GetSyncCounter() < m_Game.get().GetSyncCounter()) {
    float totalSyncDelay = static_cast<float>(m_Game.get().GetActiveLatency()) * static_cast<float>(m_Game.get().GetSyncCounter() - GetSyncCounter());
    if (behindTimeText.empty()) {
      behindTimeText += ToFormattedString(totalSyncDelay / 1000) + "s behind unnormalized";
    } else {
      behindTimeText += " (" + ToFormattedString(totalSyncDelay / 1000) + "s unnormalized)";
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
  int64_t muteEndTicks = GetTicks() + (seconds * 1000);
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
  if (!m_GProxy) return;
  m_GProxy = false;
  m_GProxyExtended = false;
  m_GProxyDisconnectNoticeSent = false;
  while (!m_GProxyBuffer.empty()) {
    m_GProxyBuffer.pop();
  }
  /*
  m_LastGProxyWaitNoticeSentTime = 0;
  m_GProxyReconnectKey = 0;
  m_LastGProxyAckTicks = nullopt;
  m_GProxyPort = 0;
  m_GProxyCheckGameID = false;
  m_GProxyVersion = 0;
  m_GProxyBufferSize = 0;
  */
}

bool CGameUser::GetReadyReminderIsDue() const
{
  return !m_ReadyReminderLastTicks.has_value() || m_ReadyReminderLastTicks.value() + READY_REMINDER_PERIOD < GetTicks();
}

void CGameUser::SetReadyReminded()
{
  m_ReadyReminderLastTicks = GetTicks();
}