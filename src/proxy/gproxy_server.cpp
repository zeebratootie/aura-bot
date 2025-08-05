/*

  Copyright [2025] [Leonardo Julca]

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

#include "gproxy_server.h"

#include "../aura.h"
#include "../game.h"
#include "../game_user.h"
#include "../protocol/gps_protocol.h"
#include "../socket.h"
#include "../util.h"

using namespace std;

//
// CGProxyServer
//

CGProxyServer::CGProxyServer(CConnection* nConnection)
  : m_Aura(nConnection->m_Aura),
    m_Connection(ref(*nConnection)),
    m_IsEnabled(false),
    m_IsExtended(false),
    m_SupportsExtended(false),
    m_CheckGameID(false),
    m_UID(0xFF),
    m_Port(0),
    m_Key(GetRandomUInt32()),
    m_Version(0),
    m_BufferSize(0),
    m_TotalRecvPackets(0),
    m_TotalSentPackets(0)
{
}

CGProxyServer::~CGProxyServer()
{
}

CStreamIOSocket& CGProxyServer::GetSocket() const
{
  return *(m_Connection.get().GetSocket());
}

void CGProxyServer::CheckSendAck()
{
  if (!GetIsEnabled()) return;
  if (!m_Aura->GetTicksIsFirstOrAfterDelay(m_LastAckTicks, GPS_ACK_PERIOD)) return;
  GetSocket().PutBytes(GPSProtocol::SEND_GPSS_ACK((uint32_t)m_TotalRecvPackets));
  m_LastAckTicks = m_Aura->GetLoopTicks();
}

void CGProxyServer::Init(uint8_t UID, uint32_t version, uint16_t port, uint8_t emptyActions, bool supportsExtended, int64_t extendedWaitTicks, int64_t gameID)
{
  m_UID = UID;
  m_IsEnabled = true;
  m_Version = version;
  m_Port = port;

  UpdateEmptyActions(emptyActions);
  if (m_Version >= 2 && supportsExtended) {
    m_SupportsExtended = supportsExtended;
    StartExtendedHandShake(extendedWaitTicks, signed_cast_lossy<uint32_t>(gameID));
  }
}

void CGProxyServer::Disable()
{
  m_IsEnabled = false;
  m_IsExtended = false;
  m_SupportsExtended = false;
  while (!m_Buffer.empty()) {
    m_Buffer.pop();
  }
  /*
  m_CheckGameID = false;
  m_UID = 0xFF;
  m_Port = 0;
  m_Key = 0;
  m_Version = 0;
  m_BufferSize = 0;
  m_TotalRecvPackets = 0;
  m_TotalSentPackets = 0;
  m_LastAckTicks.reset();
  */
}

void CGProxyServer::UpdateEmptyActions(uint8_t emptyActions) const
{
  GetSocket().PutBytes(GPSProtocol::SEND_GPSS_INIT(m_Port, m_UID, m_Key, emptyActions));
}

void CGProxyServer::StartExtendedHandShake(int64_t waitTicks, uint32_t gameID) const
{
  GetSocket().PutBytes(GPSProtocol::SEND_GPSS_SUPPORT_EXTENDED(waitTicks, gameID));
}

GProxyExtendedClientResult CGProxyServer::ConfirmExtended(const string_view data)
{
  if (!m_SupportsExtended) return GProxyExtendedClientResult::kInvalid;
  if (m_IsExtended) return GProxyExtendedClientResult::kAlready;
  m_IsExtended = true;
  if (data.size() >= 12) {
    m_CheckGameID = true;
    return GProxyExtendedClientResult::kCheckGameID;
  } else {
    return GProxyExtendedClientResult::kNormal;
  }
}

void CGProxyServer::EventGameStart()
{
  m_SupportsExtended = m_IsExtended;
}

void CGProxyServer::EventSendData(const vector<uint8_t>& data, bool isLoaded)
{
  if (isLoaded && !m_IsEnabled) return;

  // must start counting packet total from beginning of connection
  // accepting fragmented packets should not make an observable difference,
  // but it's the safest behavior, just in case something weird is going on in the caller side.
  size_t count = GameProtocol::GetPacketCount<GameProtocol::FragmentPolicy::kAccept>(data);
  m_TotalSentPackets += count;

  // we can avoid buffering packets until we know the client is using GProxy++ since that'll be determined before the game starts
  // this prevents us from buffering packets for non-reconnectable clients
  if (isLoaded) {
    m_Buffer.push(GameProtocol::PacketWrapper(data, count));
    m_BufferSize += count;
  }
}

void CGProxyServer::EventSendData(const GameProtocol::PacketWrapper& data, bool isLoaded)
{
  if (isLoaded && !m_IsEnabled) return;

  // must start counting packet total from beginning of connection
  // accepting fragmented packets should not make an observable difference,
  // but it's the safest behavior, just in case something weird is going on in the caller side.
  m_TotalSentPackets += data.count;

  // we can avoid buffering packets until we know the client is using GProxy++ since that'll be determined before the game starts
  // this prevents us from buffering packets for non-reconnectable clients
  if (isLoaded) {
    m_Buffer.push(data);
    m_BufferSize += data.count;
  }
}

bool CGProxyServer::UnqueuePackets(const size_t lastPacket)
{
  const size_t alreadyUnqueued = GetUnqueuedPacketsCount();
  if (lastPacket <= alreadyUnqueued) {
    // The client is likely caught up to the server.
    // Or it's doing some incorrect/rogue stuff:
    // - ACK sent before the game starts (not a big deal, but we can only ignore it, since no packets are buffered yet)
    //   * If load-in-game is enabled, and not everyone has loaded yet, then this behavior is legitimate, but we don't have any buffered packets anyway.
    // - lastPacket zero (ACK sent even before the server accepts the join request)
    // - Out-of-order ACKs
    return lastPacket == alreadyUnqueued;
  }

  size_t pendingUnqueue = min(m_BufferSize, lastPacket - alreadyUnqueued);
  size_t thisUnqueue = 0;
  size_t frontCount = 0;
  while (pendingUnqueue > 0) {
    GameProtocol::PacketWrapper& frontPackets = m_Buffer.front();
    frontCount = frontPackets.count;
    thisUnqueue = min(frontCount, pendingUnqueue);
    if (thisUnqueue == frontCount) {
      m_Buffer.pop();
    } else {
      frontPackets.Remove(thisUnqueue);
    }
    pendingUnqueue -= thisUnqueue;
    m_BufferSize -= thisUnqueue;
  }

  return true;
}

void CGProxyServer::SynchronizeReconnectKeyFromClient(const uint32_t key)
{
  m_Key = key;
}

void CGProxyServer::RotateReconnectKey() const
{
  GetSocket().PutBytes(GPSProtocol::SEND_GPSS_CHANGE_KEY(GetRandomUInt32()));
}

void CGProxyServer::SynchronizeFromBuffer()
{
  // send remaining packets from buffer,
  // but preserve buffer in case the client disconnects again

  CStreamIOSocket& socket = GetSocket();
  queue<GameProtocol::PacketWrapper> tempBuffer;
  while (!m_Buffer.empty()) {
    socket.PutBytes(m_Buffer.front().data);
    tempBuffer.push(move(m_Buffer.front()));
    m_Buffer.pop();
  }
  m_Buffer.swap(tempBuffer);
}

bool CGProxyServer::ValidateReconnect(const uint32_t reconnectKey, const uint32_t lastPacket) const
{
  if (GetReconnectKey() != reconnectKey) return false;
  if (GetUnqueuedPacketsCount() > lastPacket) return false;
  return true;
}
