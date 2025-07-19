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
  m_TimeoutTicks = m_Aura->GetLoopTicks() + delta;
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
          if (m_Type != IncomingConnectionType::kUDPTunnel || !m_Aura->m_Net.m_Config.m_EnableTCPWrapUDP) {
            Abort = true;
            break;
          }
          if (Bytes[1] == GameProtocol::Magic::REQJOIN) {
            CIncomingJoinRequest joinRequest = GameProtocol::RECEIVE_W3GS_REQJOIN(Data);
            if (!joinRequest.GetIsValid()) {
              Abort = true;
              break;
            }
            shared_ptr<CGame> targetLobby = m_Aura->GetLobbyOrObservableByHostCounter(joinRequest.GetHostCounter());
            if (!targetLobby || targetLobby->GetIsMirror() || targetLobby->GetLobbyLoading() || (targetLobby->GetIsLobbyStrict() && targetLobby->GetExiting())) {
              break;
            }
            joinRequest.UpdateCensored(targetLobby->m_Config.m_UnsafeNameHandler, targetLobby->m_Config.m_PipeConsideredHarmful);
            if (targetLobby->EventRequestJoin(this, joinRequest)) {
              result = GameSeekerStatus::kPromoted;
              m_Type = IncomingConnectionType::kPlayer;
              m_Socket = nullptr;
            }
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

        case VLANProtocol::Magic::VLAN_HEADER: {
          if (m_Type != IncomingConnectionType::kVLAN || !m_Aura->m_Net.m_Config.m_VLANEnabled) {
            Abort = true;
            break;
          }
          if (Bytes[1] == VLANProtocol::Magic::SEARCHGAME) {
            CIncomingVLanSearchGame vlanSearch = VLANProtocol::RECEIVE_VLAN_SEARCHGAME(Data);
            if (vlanSearch.isValid) {
              m_GameVersion = GAMEVER(1, vlanSearch.gameVersion);
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

      LengthProcessed += Length;

      if (Abort) {
        // Process no more packets
        break;
      }

      Bytes = std::vector<uint8_t>(begin(Bytes) + Length, end(Bytes));
    }

    if (Abort && result != GameSeekerStatus::kPromoted) {
      result = GameSeekerStatus::kDestroy;
      RecvBuffer->clear();
    } else if (LengthProcessed > 0) {
      *RecvBuffer = RecvBuffer->substr(LengthProcessed);
    }
  } else if (m_Aura->GetTicksIsAfterDelay(m_Socket->GetLastRecv(), timeout)) {
    PRINT_IF(LogLevel::kDebug, "Game seeker timed out after " + to_string(timeout) + " ms");
    return GameSeekerStatus::kDestroy;
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
