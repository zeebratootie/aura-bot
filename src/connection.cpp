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

#include <utility>

#include "config/config_bot.h"
#include "connection.h"
#include "game_user.h"
#include "aura.h"
#include "realm.h"
#include "map.h"
#include "protocol/game_protocol.h"
#include "protocol/gps_protocol.h"
#include "protocol/vlan_protocol.h"
#include "proxy/gproxy_server.h"
#include "game.h"
#include "socket.h"
#include "net.h"

using namespace std;

//
// CConnection
//

CConnection::CConnection(CAura* nAura, uint16_t nPort, CStreamIOSocket* nSocket)
  : m_Aura(nAura),
    m_Port(nPort),
    m_Type(IncomingConnectionType::kNone),
    m_Socket(nSocket),
    m_DeleteMe(false)
{
}

CConnection::CConnection(const CConnection& nFromCopy)
  : m_Aura(nFromCopy.m_Aura),
    m_Port(nFromCopy.m_Port),
    m_Type(nFromCopy.m_Type),
    m_Socket(nFromCopy.m_Socket),
    m_DeleteMe(nFromCopy.m_DeleteMe)
{
}

CConnection::~CConnection()
{
  delete m_Socket;
  m_Socket = nullptr;
}

uint32_t CConnection::SetFD(fd_set* fd, fd_set* send_fd, int32_t* nfds) const
{
  if (!m_Socket) return 0;
  m_Socket->SetFD(fd, send_fd, nfds);
  return 1;
}

void CConnection::SetTimeout(const int64_t delta)
{
  m_TimeoutTicks = m_Aura->GetClockTicks() + delta;
}

void CConnection::SetTimeoutAtLatest(const int64_t atLatestTicks)
{
  if (!m_TimeoutTicks.has_value() || atLatestTicks < m_TimeoutTicks.value()) {
    m_TimeoutTicks = atLatestTicks;
  }
}

bool CConnection::CloseConnection()
{
  if (!m_Socket->GetConnected()) return false;
  m_Socket->Close();
  return true;
}

IncomingConnectionStatus CConnection::Update(fd_set* fd, fd_set* send_fd, int64_t timeout)
{
  if (m_DeleteMe || !m_Socket || m_Socket->HasError()) {
    return IncomingConnectionStatus::kDestroy;
  }

  if (m_TimeoutTicks.has_value() && m_Aura->GetTicksIsAfter(m_TimeoutTicks.value())) {
    return IncomingConnectionStatus::kDestroy;
  }

  IncomingConnectionStatus result = IncomingConnectionStatus::kOk;
  bool Abort = false;
  if (m_Type == IncomingConnectionType::kKickedPlayer) {
    m_Socket->Discard(fd);
  } else if (m_Socket->DoRecv(fd)) {
    CStreamIOSocket* socket = m_Socket;
    string_view data = socket->GetRecvBufferView();

    // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

    while (data.size() >= 4) {
      // bytes 2 and 3 contain the length of the packet
      const uint16_t packetSize = ByteArrayToUInt16LE(data, 2);
      if (packetSize < 4) {
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
      switch (packetFamily) {
        case GameProtocol::Magic::W3GS_HEADER:
          if (packetType == GameProtocol::Magic::REQJOIN) {
            CIncomingJoinRequest joinRequest = GameProtocol::RECEIVE_W3GS_REQJOIN(packet);
            if (!joinRequest.GetIsValid()) {
              DPRINT_IF(LogLevel::kTrace2, "[AURA] Got invalid REQJOIN <" + GetStringBytesHex(packet) + ">");
              if (joinRequest.GetError() != JoinRequestError::kCannotParse) {
                Send(GameProtocol::SENDWRAP_W3GS_GHOST_LOBBY_ERROR(GameProtocol::JoinRequestErrorToString(joinRequest.GetError())));
                SetTimeoutAtLatest(m_Aura->GetClockTicks() + 8000);
                result = IncomingConnectionStatus::kDestroyDelayed;
                m_Type = IncomingConnectionType::kKickedPlayer;
              }
              Abort = true;
              break;
            }
            DPRINT_IF(LogLevel::kTrace2, "[AURA] Got join request for #" + ToHexString(joinRequest.GetHostCounter()) + " (name: " + joinRequest.GetName() + ")");
            shared_ptr<CGame> targetLobby = m_Aura->GetLobbyOrObservableByHostCounter(joinRequest.GetHostCounter());
            if (!targetLobby) {
              DPRINT_IF(LogLevel::kTrace, "[AURA] Join request for #" + ToHexString(joinRequest.GetHostCounter()) + " did not match a game");
              break;
            }
            if (targetLobby->GetHostPort() != m_Port) {
              DPRINT_IF(LogLevel::kTrace, "[AURA] Join request for #" + ToHexString(joinRequest.GetHostCounter()) + " ignored (bad port)");
              Abort = true;
              break;
            }
            if (targetLobby->GetIsMirror()) {
              if (targetLobby->GetIsMirrorProxy()) {
                m_Aura->m_Net.RegisterGameProxy(this, targetLobby);
                result = IncomingConnectionStatus::kPromotedPassThrough;
              } else {
                DPRINT_IF(LogLevel::kTrace, "[AURA] Join request for #" + ToHexString(joinRequest.GetHostCounter()) + "ignored (non-proxy mirror)");
              }
              Abort = true;
              break;
            }
            joinRequest.UpdateCensored(targetLobby->m_Config.m_UnsafeNameHandler, targetLobby->m_Config.m_PipeConsideredHarmful);
            if (joinRequest.GetIsCensored()) {
              DPRINT_IF(LogLevel::kTrace, Concat("[AURA] User name censored: [", joinRequest.GetOriginalName(), "] -> [", joinRequest.GetName(), "]"));
            }
            JoinRequestResult joinResult = targetLobby->EventRequestJoin(this, joinRequest);
            switch (joinResult) {
              case JoinRequestResult::kPlayer: {
                result = IncomingConnectionStatus::kPromoted; // must be destroyed
                m_Type = IncomingConnectionType::kPlayer;
                assert((m_Socket == nullptr) && "Connection should no longer have a socket");
                break;
              }
              case JoinRequestResult::kObserver: {
                result = IncomingConnectionStatus::kPromoted; // must be destroyed
                m_Type = IncomingConnectionType::kObserver;
                assert((m_Socket == nullptr) && "Connection should no longer have a socket");
                break;
              }
              case JoinRequestResult::kFailDelayed: {
                result = IncomingConnectionStatus::kDestroyDelayed;
                m_Type = IncomingConnectionType::kKickedPlayer;
                SetTimeoutAtLatest(m_Aura->GetClockTicks() + 8000);
                break;
              }
              case JoinRequestResult::kFail: {
                result = IncomingConnectionStatus::kDestroy; // must be destroyed
                m_Type = IncomingConnectionType::kKickedPlayer;
                break;
              }
            }
            Abort = true;
          } else if (GameProtocol::Magic::SEARCHGAME <= packetType && packetType <= GameProtocol::Magic::DECREATEGAME) {
            if (packetSize > 1024) {
              Abort = true;
              break;
            }
            struct UDPPkt pkt;
            pkt.socket = m_Socket;
            pkt.sender = m_Socket->m_RemoteHost;
            memcpy(pkt.buf, packet.data(), packetSize);
            pkt.length = packetSize;
            m_Aura->m_Net.HandleUDP(pkt);
          } else {
            Abort = true;
            break;
          }
          break;

        case GPSProtocol::Magic::GPS_HEADER: {
          if (packetSize >= 13 && packetType == GPSProtocol::Magic::RECONNECT && m_Type == IncomingConnectionType::kNone && m_Aura->m_Net.m_Config.m_ProxyReconnect > 0) {
            const uint32_t reconnectKey = ByteArrayToUInt32LE(packet, 5);
            const uint32_t lastPacket = ByteArrayToUInt32LE(packet,  9);
            GameUser::CGameUser* targetUser = nullptr;
            if (packetSize >= 17) {
              targetUser = m_Aura->m_Net.GetReconnectTargetUser(ByteArrayToUInt32LE(packet, 13), GetByteAt(packet, 4));
            } else {
              targetUser = m_Aura->m_Net.GetReconnectTargetUserLegacy(GetByteAt(packet, 4), reconnectKey);
            }
            if (!targetUser || !targetUser->GetGProxy()->ValidateReconnect(reconnectKey, lastPacket)) {
              m_Socket->PutBytes(GPSProtocol::SEND_GPSS_REJECT(targetUser == nullptr ? REJECTGPS_NOTFOUND : REJECTGPS_INVALID));
              if (targetUser) targetUser->EventGProxyReconnectInvalid();
              Abort = true;
            } else {
              // reconnect successful!
              targetUser->EventGProxyReconnect(this, lastPacket);
              result = IncomingConnectionStatus::kReconnected;
              Abort = true;
            }          
          } else if (packetSize >= 4 && packetType == GPSProtocol::Magic::UDPSYN && m_Aura->m_Net.m_Config.m_EnableTCPWrapUDP) {
            // in-house extension
            m_Aura->m_Net.RegisterGameSeeker(this, IncomingConnectionType::kUDPTunnel);
            result = IncomingConnectionStatus::kPromoted;
            Abort = true;
          }
          break;
        }

        case VLANProtocol::Magic::VLAN_HEADER: {
          if (m_Type != IncomingConnectionType::kNone || !m_Aura->m_Net.m_Config.m_VLANEnabled) {
            Abort = true;
            break;
          }
          m_Aura->m_Net.RegisterGameSeeker(this, IncomingConnectionType::kVLAN);
          result = IncomingConnectionStatus::kPromotedPassThrough;
          Abort = true;
          break;
        }

        default:
          Abort = true;
      }

      if (result != IncomingConnectionStatus::kPromotedPassThrough) {
        data.remove_prefix(packetSize);
      }

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
    return IncomingConnectionStatus::kDestroy;
  }

  if (Abort && result != IncomingConnectionStatus::kDestroyDelayed) {
    m_DeleteMe = true;
  }

  /*
  if (result == IncomingConnectionStatus::kPROMOTED || result == IncomingConnectionStatus::kPROMOTED_PASSTHROUGH || result == IncomingConnectionStatus::kRECONNECTED) {
    return result;
  }
  */

  // At this point, m_Socket may have been transferred to GameUser::CGameUser
  if (m_DeleteMe || !m_Socket->GetConnected() || m_Socket->HasError() || m_Socket->HasFin()) {
    return IncomingConnectionStatus::kDestroy;
  }

  m_Socket->DoSend(send_fd);

  if (m_Type == IncomingConnectionType::kKickedPlayer && !m_Socket->GetIsSendPending()) {
    return IncomingConnectionStatus::kDestroy;
  }

  return IncomingConnectionStatus::kOk;
}

void CConnection::Send(const std::vector<uint8_t>& data)
{
  if (m_Socket && !m_Socket->HasError()) {
    m_Socket->PutBytes(data);
  }
}

void CConnection::Send(const GameProtocol::PacketWrapper& data)
{
  if (m_Socket && !m_Socket->HasError()) {
    m_Socket->PutBytes(data.data);
  }
}
