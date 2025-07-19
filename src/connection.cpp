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
  m_TimeoutTicks = m_Aura->GetLoopTicks() + delta;
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
    // extract as many packets as possible from the socket's receive buffer and process them
    string*              RecvBuffer         = m_Socket->GetBytes();
    std::vector<uint8_t> Bytes              = CreateByteArray((uint8_t*)RecvBuffer->c_str(), RecvBuffer->size());
    uint32_t             LengthProcessed    = 0;

    // a packet is at least 4 bytes so loop as long as the buffer contains 4 bytes

    while (Bytes.size() >= 4) {
      // bytes 2 and 3 contain the length of the packet
      const uint16_t Length = ByteArrayToUInt16(Bytes, false, 2);
      if (Length < 4) {
        Abort = true;
        break;
      }
      if (Bytes.size() < Length) break;
      const std::vector<uint8_t> Data = std::vector<uint8_t>(begin(Bytes), begin(Bytes) + Length);

      switch (Bytes[0]) {
        case GameProtocol::Magic::W3GS_HEADER:
          if (Bytes[1] == GameProtocol::Magic::REQJOIN) {
            CIncomingJoinRequest joinRequest = GameProtocol::RECEIVE_W3GS_REQJOIN(Data);
            if (!joinRequest.GetIsValid()) {
              // TODO: kTrace2
              PRINT_IF(LogLevel::kDebug, "[AURA] Got invalid REQJOIN <" + ByteArrayToDecString(Bytes) + ">");
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
              DPRINT_IF(LogLevel::kTrace, "[AURA] User name censored: [" + joinRequest.GetOriginalName() + "] -> [" + joinRequest.GetName() + "]");
            }
            const uint8_t joinResult = targetLobby->EventRequestJoin(this, joinRequest);
            if (joinResult == JOIN_RESULT_PLAYER) {
              result = IncomingConnectionStatus::kPromoted;
              m_Type = IncomingConnectionType::kPlayer;
              m_Socket = nullptr;
            } else if (joinResult == JOIN_RESULT_OBSERVER) {
              result = IncomingConnectionStatus::kPromoted;
              m_Type = IncomingConnectionType::kObserver;
            }
            Abort = true;
          } else if (GameProtocol::Magic::SEARCHGAME <= Bytes[1] && Bytes[1] <= GameProtocol::Magic::DECREATEGAME) {
            if (Length > 1024) {
              Abort = true;
              break;
            }
            struct UDPPkt pkt;
            pkt.socket = m_Socket;
            pkt.sender = &(m_Socket->m_RemoteHost);
            memcpy(pkt.buf, Bytes.data(), Length);
            pkt.length = Length;
            m_Aura->m_Net.HandleUDP(&pkt);
          } else {
            Abort = true;
            break;
          }
          break;

        case GPSProtocol::Magic::GPS_HEADER: {
          if (Length >= 13 && Bytes[1] == GPSProtocol::Magic::RECONNECT && m_Type == IncomingConnectionType::kNone && m_Aura->m_Net.m_Config.m_ProxyReconnect > 0) {
            const uint32_t reconnectKey = ByteArrayToUInt32(Bytes, false, 5);
            const uint32_t lastPacket = ByteArrayToUInt32(Bytes, false, 9);
            GameUser::CGameUser* targetUser = nullptr;
            if (Length >= 17) {
              targetUser = m_Aura->m_Net.GetReconnectTargetUser(ByteArrayToUInt32(Bytes, false, 13), Bytes[4]);
            } else {
              targetUser = m_Aura->m_Net.GetReconnectTargetUserLegacy(Bytes[4], reconnectKey);
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
          } else if (Length >= 4 && Bytes[1] == GPSProtocol::Magic::UDPSYN && m_Aura->m_Net.m_Config.m_EnableTCPWrapUDP) {
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
        LengthProcessed += Length;
      }

      if (Abort) {
        // Process no more packets
        break;
      }

      Bytes = std::vector<uint8_t>(begin(Bytes) + Length, end(Bytes));
    }

    if (Abort && result != IncomingConnectionStatus::kPromoted && result != IncomingConnectionStatus::kPromotedPassThrough && result != IncomingConnectionStatus::kReconnected) {
      result = IncomingConnectionStatus::kDestroy;
      RecvBuffer->clear();
    } else if (LengthProcessed > 0) {
      *RecvBuffer = RecvBuffer->substr(LengthProcessed);
    }
  } else if (m_Aura->GetTicksIsAfterDelay(m_Socket->GetLastRecv(), timeout)) {
    return IncomingConnectionStatus::kDestroy;
  }

  if (Abort) {
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
