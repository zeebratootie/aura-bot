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

#include "game_seeker.h"

#include "aura.h"
#include "config/config_bot.h"
#include "game.h"
#include "protocol/game_protocol.h"
#include "game_user.h"
#include "protocol/gps_protocol.h"
#include "map.h"
#include "net.h"
#include "realm.h"
#include "socket.h"
#include "protocol/vlan_protocol.h"

using namespace std;

//
// CGameSeeker
//

CGameSeeker::CGameSeeker(CAura* nAura, uint16_t nPort, IncomingConnectionType nType, CStreamIOSocket* nSocket)
  : CConnection(nAura, nPort, nSocket)
{
  m_Type = nType;
}

CGameSeeker::CGameSeeker(CConnection* nConnection, IncomingConnectionType nType)
  : CConnection(*nConnection)
{
  m_Type = nType;
}

CGameSeeker::~CGameSeeker()
{  
}

void CGameSeeker::SetTimeout(const int64_t delta)
{
  m_TimeoutTicks = m_Aura->GetClockTicks() + delta;
}

void CGameSeeker::SetTimeoutAtLatest(const int64_t atLatestTicks)
{
  if (!m_TimeoutTicks.has_value() || atLatestTicks < m_TimeoutTicks.value()) {
    m_TimeoutTicks = atLatestTicks;
  }
}

bool CGameSeeker::CloseConnection()
{
  if (!m_Socket->GetConnected()) return false;
  m_Socket->Close();
  return true;
}

void CGameSeeker::Init()
{
  switch (m_Type) {
    case IncomingConnectionType::kUDPTunnel: {
      vector<uint8_t> packet = {GPSProtocol::Magic::GPS_HEADER, GPSProtocol::Magic::UDPACK, 4, 0};
      m_Socket->PutBytes(packet);
      break;
    }
    case IncomingConnectionType::kVLAN: {
      // do nothing - client should send VLAN_SEARCHGAME
      break;
    }
    case IncomingConnectionType::kNone:
    case IncomingConnectionType::kPlayer:
    case IncomingConnectionType::kKickedPlayer:
    case IncomingConnectionType::kObserver: {
      UNREACHABLE();
      break;
    }
    IGNORE_ENUM_LAST(IncomingConnectionType)
  }
}

GameSeekerStatus CGameSeeker::Update(fd_set* fd, fd_set* send_fd, int64_t timeout)
{
  if (m_DeleteMe || !m_Socket || m_Socket->HasError()) {
    return GameSeekerStatus::kDestroy;
  }

  if (m_TimeoutTicks.has_value() && m_Aura->GetTicksIsAfter(m_TimeoutTicks.value())) {
    return GameSeekerStatus::kDestroy;
  }

  GameSeekerStatus result = GameSeekerStatus::kOk;
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
          if (m_Type != IncomingConnectionType::kUDPTunnel || !m_Aura->m_Net.m_Config.m_EnableTCPWrapUDP) {
            Abort = true;
            break;
          }
          if (packetType == GameProtocol::Magic::REQJOIN) {
            CIncomingJoinRequest joinRequest = GameProtocol::RECEIVE_W3GS_REQJOIN(packet);
            if (!joinRequest.GetIsValid()) {
              DPRINT_IF(LogLevel::kTrace2, "[AURA] Got invalid REQJOIN <" + GetStringBytesHex(packet) + ">");
              if (joinRequest.GetError() == JoinRequestError::kCannotParse) {
                Abort = true;
              } else {
                Send(GameProtocol::SENDWRAP_W3GS_GHOST_LOBBY_ERROR(GameProtocol::JoinRequestErrorToString(joinRequest.GetError())));
                SetTimeoutAtLatest(m_Aura->GetClockTicks() + 8000);
              }
              break;
            }
            shared_ptr<CGame> targetLobby = m_Aura->GetLobbyOrObservableByHostCounter(joinRequest.GetHostCounter());
            if (!targetLobby || targetLobby->GetIsMirror() || targetLobby->GetLobbyLoading() || (targetLobby->GetIsLobbyStrict() && targetLobby->GetExiting())) {
              break;
            }
            joinRequest.UpdateCensored(targetLobby->m_Config.m_UnsafeNameHandler, targetLobby->m_Config.m_PipeConsideredHarmful);
            JoinRequestResult joinResult = targetLobby->EventRequestJoin(this, joinRequest);
            if (joinResult != JoinRequestResult::kFail && joinResult != JoinRequestResult::kFailDelayed) {
              result = GameSeekerStatus::kPromoted;
              m_Type = IncomingConnectionType::kPlayer;
              m_Socket = nullptr;
            }
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

        case VLANProtocol::Magic::VLAN_HEADER: {
          if (m_Type != IncomingConnectionType::kVLAN || !m_Aura->m_Net.m_Config.m_VLANEnabled) {
            Abort = true;
            break;
          }
          if (packetType == VLANProtocol::Magic::SEARCHGAME) {
            CIncomingVLanSearchGame vlanSearch = VLANProtocol::RECEIVE_VLAN_SEARCHGAME(packet);
            if (vlanSearch.isValid) {
              m_GameVersion = vlanSearch.gameVersion;
              for (const auto& game : m_Aura->GetJoinableGames()) {
                if (game->GetIsStageAcceptingJoins()) {
                  game->SendGameDiscoveryInfoVLAN(this);
                }
              }
            }
          }
          break;
        }

         default:
          Abort = true;
      }

      //if (result != GameSeekerStatus::kPromotedPassThrough) {
      data.remove_prefix(packetSize);
      //}

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
    PRINT_IF(LogLevel::kDebug, "Game seeker timed out after " + to_string(timeout) + " ms");
    return GameSeekerStatus::kDestroy;
  }

  if (Abort) {
    m_DeleteMe = true;
  }

  // At this point, m_Socket may have been transferred to GameUser::CGameUser
  if (m_DeleteMe || !m_Socket->GetConnected() || m_Socket->HasError() || m_Socket->HasFin()) {
    return GameSeekerStatus::kDestroy;
  }

  m_Socket->DoSend(send_fd);

  return result;
}

void CGameSeeker::Send(const std::vector<uint8_t>& data)
{
  if (m_Socket && !m_Socket->HasError()) {
    m_Socket->PutBytes(data);
  }
}

void CGameSeeker::Send(const GameProtocol::PacketWrapper& data)
{
  if (m_Socket && !m_Socket->HasError()) {
    m_Socket->PutBytes(data.data);
  }
}

